// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordmanager.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace {
QJsonObject ParseObject(const QByteArray& output)
{
    const qsizetype first{output.indexOf('{')};
    const qsizetype last{output.lastIndexOf('}')};
    if (first < 0 || last < first) return {};
    return QJsonDocument::fromJson(output.sliced(first, last - first + 1)).object();
}
} // namespace

OrdManager::OrdManager(QString bitcoin_data_dir, QString bitcoin_network_dir, QString chain, QObject* parent, std::optional<OrdArtifact> artifact)
    : QObject{parent},
      m_bitcoin_data_dir{std::move(bitcoin_data_dir)},
      m_bitcoin_network_dir{std::move(bitcoin_network_dir)},
      m_chain{std::move(chain)},
      m_artifact_override{std::move(artifact)},
      m_index{this},
      m_server{this},
      m_wallet{this},
      m_inscription{this}
{
    connect(&m_index, &OrdProcess::failed, this, [this](const QString& error) {
        if (m_stopping) return;
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(tr("Ord indexing could not be started: %1").arg(error));
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) Q_EMIT inscriptionFailed(tr("Ord indexing could not be started: %1").arg(error));
        Q_EMIT failed(tr("Ord indexing could not be started: %1").arg(error));
    });
    connect(&m_index, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        if (exit_code == 0) {
            Q_EMIT indexComplete();
            QTimer::singleShot(0, this, &OrdManager::startServer);
        } else {
            const QString error{tr("Ord indexing exited with code %1.\n\n%2").arg(exit_code).arg(QString::fromUtf8(m_index.output()))};
            const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
            if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(error);
            if (pending == AfterIndex::PREVIEW_INSCRIPTION) Q_EMIT inscriptionFailed(error);
            Q_EMIT failed(error);
        }
    });
    connect(&m_server, &OrdProcess::failed, this, [this](const QString& error) {
        if (m_stopping) return;
        const QString message{tr("The local Ord server could not be started: %1").arg(error)};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) Q_EMIT inscriptionFailed(message);
        Q_EMIT failed(message);
    });
    connect(&m_server, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        if (m_server_url.isEmpty()) return;
        m_server_url.clear();
        const QString message{tr("The local Ord server exited with code %1.\n\n%2").arg(exit_code).arg(QString::fromUtf8(m_server.output()).trimmed())};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) Q_EMIT inscriptionFailed(message);
        Q_EMIT failed(message);
    });
    connect(&m_wallet, &OrdProcess::failed, this, [this](const QString& error) {
        m_wallet_operation = WalletOperation::NONE;
        if (!m_stopping) Q_EMIT walletUnavailable(tr("Ord wallet command could not be started: %1").arg(error));
    });
    connect(&m_wallet, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        const WalletOperation operation{std::exchange(m_wallet_operation, WalletOperation::NONE)};
        const QByteArray output{m_wallet.output()};
        if (exit_code != 0) {
            Q_EMIT walletUnavailable(tr("Ord wallet command exited with code %1.\n\n%2").arg(exit_code).arg(QString::fromUtf8(output).trimmed()));
            return;
        }

        const QJsonObject object{ParseObject(output)};
        if (operation == WalletOperation::BALANCE) {
            if (!object.contains(QStringLiteral("cardinal")) || !object.contains(QStringLiteral("total"))) {
                Q_EMIT walletUnavailable(tr("Ord returned an invalid wallet balance response."));
                return;
            }
            m_cardinal_balance = static_cast<qint64>(object.value(QStringLiteral("cardinal")).toDouble());
            m_total_balance = static_cast<qint64>(object.value(QStringLiteral("total")).toDouble());
            if (!m_funding_address.isEmpty()) {
                Q_EMIT walletDetails(m_cardinal_balance, m_total_balance, m_funding_address);
            } else {
                QTimer::singleShot(0, this, [this] {
                    startWalletOperation(WalletOperation::RECEIVE, {QStringLiteral("receive")});
                });
            }
        } else if (operation == WalletOperation::RECEIVE) {
            const QJsonArray addresses = object.value(QStringLiteral("addresses")).toArray();
            if (addresses.isEmpty() || !addresses.at(0).isString()) {
                Q_EMIT walletUnavailable(tr("Ord returned an invalid funding address response.\n\n%1").arg(QString::fromUtf8(output).trimmed()));
                return;
            }
            m_funding_address = addresses.at(0).toString();
            Q_EMIT walletDetails(m_cardinal_balance, m_total_balance, m_funding_address);
        } else if (operation == WalletOperation::CREATE) {
            const QString mnemonic{object.value(QStringLiteral("mnemonic")).toString()};
            if (mnemonic.isEmpty()) {
                Q_EMIT walletUnavailable(tr("Ord created the wallet but did not return recovery words."));
                return;
            }
            m_funding_address.clear();
            Q_EMIT walletCreated(mnemonic);
        } else if (operation == WalletOperation::RESTORE) {
            m_funding_address.clear();
            Q_EMIT walletRestored();
        }
    });
    connect(&m_inscription, &OrdProcess::failed, this, [this](const QString& error) {
        m_inscription_preview = false;
        if (!m_stopping) Q_EMIT inscriptionFailed(tr("Ord inscription could not be started: %1").arg(error));
    });
    connect(&m_inscription, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        const QString output{QString::fromUtf8(m_inscription.output()).trimmed()};
        const bool preview{std::exchange(m_inscription_preview, false)};
        if (exit_code == 0 && preview) {
            const QJsonObject object{ParseObject(m_inscription.output())};
            if (!object.contains(QStringLiteral("total_fees"))) {
                Q_EMIT inscriptionFailed(tr("Ord returned an invalid inscription cost preview."));
                return;
            }
            Q_EMIT inscriptionPreview(static_cast<qint64>(object.value(QStringLiteral("total_fees")).toDouble()), output);
        } else if (exit_code == 0) {
            Q_EMIT inscriptionComplete(output);
        } else {
            Q_EMIT inscriptionFailed(tr("Ord inscription exited with code %1.\n\n%2").arg(exit_code).arg(output));
        }
    });
}

OrdManager::~OrdManager()
{
    stop();
}

bool OrdManager::verifyExecutable(const QString& path, QString& error) const
{
    QProcess version;
    version.start(path, {QStringLiteral("--version")});
    if (!version.waitForStarted(10000) || !version.waitForFinished(10000) || version.exitCode() != 0) {
        error = tr("Could not run the installed Ord executable: %1").arg(QString::fromUtf8(version.readAll()));
        return false;
    }
    return OrdInstaller::VerifyVersionOutput(version.readAllStandardOutput(), m_artifact, error);
}

void OrdManager::start()
{
    QString error;
    m_artifact = m_artifact_override ? *m_artifact_override : OrdInstaller::PinnedArtifact(OrdInstaller::PlatformId(), error);
    if (!error.isEmpty()) {
        Q_EMIT failed(error);
        return;
    }

    const QString root{QDir{m_bitcoin_network_dir}.filePath(QStringLiteral("ord/bin"))};
    m_executable = QDir{root}.filePath(QStringLiteral("%1/%2").arg(m_artifact.version, OrdInstaller::ExecutableName()));
    if (QFileInfo{m_executable}.isExecutable() && verifyExecutable(m_executable, error)) {
        Q_EMIT ready();
        return;
    }
    m_executable.clear();
    Q_EMIT installationRequired(m_artifact.version, m_artifact.url, QString::fromLatin1(m_artifact.sha256.toHex()));
}

void OrdManager::installArchive(const QString& archive)
{
    if (m_stopping || archive.isEmpty()) return;
    m_temporary_dir = std::make_unique<QTemporaryDir>();
    if (!m_temporary_dir->isValid()) {
        Q_EMIT failed(tr("Could not create a temporary directory for Ord."));
        return;
    }

    Q_EMIT progress(tr("Verifying Ord %1").arg(m_artifact.version), 0);
    QString error;
    const QString extracted{OrdInstaller::ExtractExecutable(archive, m_temporary_dir->filePath(QStringLiteral("extracted")), m_artifact, error)};
    if (extracted.isEmpty() || !verifyExecutable(extracted, error)) {
        Q_EMIT progress({}, 100);
        Q_EMIT failed(error);
        return;
    }
    const QString root{QDir{m_bitcoin_network_dir}.filePath(QStringLiteral("ord/bin"))};
    const QString destination{QDir{root}.filePath(QStringLiteral("%1/%2").arg(m_artifact.version, OrdInstaller::ExecutableName()))};
    if (!OrdInstaller::InstallExecutable(extracted, destination, error) || !verifyExecutable(destination, error)) {
        Q_EMIT progress({}, 100);
        Q_EMIT failed(error);
        return;
    }
    m_executable = destination;
    m_temporary_dir.reset();
    Q_EMIT progress({}, 100);
    Q_EMIT ready();
}

void OrdManager::startIndex()
{
    if (!isReady() || m_index.isRunning() || m_server.isRunning()) return;
    const QString data_dir{QDir{m_bitcoin_network_dir}.filePath(QStringLiteral("ord/data"))};
    QDir{}.mkpath(data_dir);
    m_index.start(m_executable, {
        QStringLiteral("--chain"), m_chain,
        QStringLiteral("--bitcoin-data-dir"), m_bitcoin_data_dir,
        QStringLiteral("--cookie-file"), QDir{m_bitcoin_network_dir}.filePath(QStringLiteral(".cookie")),
        QStringLiteral("--data-dir"), data_dir,
        QStringLiteral("index"), QStringLiteral("update"),
    });
}

void OrdManager::startServer()
{
    if (m_stopping || !isReady()) return;
    if (m_server.isRunning()) {
        continueAfterIndex();
        return;
    }

    QTcpServer port_reservation;
    if (!port_reservation.listen(QHostAddress::LocalHost, 0)) {
        const QString message{tr("Could not reserve a local port for the Ord server: %1").arg(port_reservation.errorString())};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) Q_EMIT inscriptionFailed(message);
        Q_EMIT failed(message);
        return;
    }
    m_server_port = port_reservation.serverPort();
    port_reservation.close();
    m_server_url = QStringLiteral("http://127.0.0.1:%1").arg(m_server_port);

    QStringList command{baseArguments()};
    command << QStringLiteral("server") << QStringLiteral("--address") << QStringLiteral("127.0.0.1")
            << QStringLiteral("--http-port") << QString::number(m_server_port);
    m_server.start(m_executable, command);
    QTimer::singleShot(100, this, [this] { waitForServer(); });
}

void OrdManager::waitForServer(int attempts_remaining)
{
    if (m_stopping || !m_server.isRunning()) return;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, m_server_port);
    if (socket.waitForConnected(50)) {
        continueAfterIndex();
        return;
    }
    if (attempts_remaining <= 1) {
        const QString message{tr("The local Ord server did not become ready in time.")};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) Q_EMIT inscriptionFailed(message);
        Q_EMIT failed(message);
        m_server_url.clear();
        m_server.stop();
        return;
    }
    QTimer::singleShot(100, this, [this, attempts_remaining] { waitForServer(attempts_remaining - 1); });
}

void OrdManager::continueAfterIndex()
{
    const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
    if (pending == AfterIndex::REFRESH_WALLET) {
        QTimer::singleShot(0, this, [this] { startWalletOperation(WalletOperation::BALANCE, {QStringLiteral("balance")}); });
    } else if (pending == AfterIndex::PREVIEW_INSCRIPTION) {
        QTimer::singleShot(0, this, &OrdManager::runInscriptionPreview);
    }
}

QStringList OrdManager::baseArguments() const
{
    return {
        QStringLiteral("--chain"), m_chain,
        QStringLiteral("--bitcoin-data-dir"), m_bitcoin_data_dir,
        QStringLiteral("--cookie-file"), QDir{m_bitcoin_network_dir}.filePath(QStringLiteral(".cookie")),
        QStringLiteral("--data-dir"), QDir{m_bitcoin_network_dir}.filePath(QStringLiteral("ord/data")),
    };
}

void OrdManager::startWalletOperation(WalletOperation operation, const QStringList& arguments, const QByteArray& input)
{
    if (!isReady() || m_wallet.isRunning()) return;
    m_wallet_operation = operation;
    QStringList command{baseArguments()};
    command << QStringLiteral("wallet") << QStringLiteral("--server-url") << m_server_url
            << QStringLiteral("--name") << QStringLiteral("ord") << arguments;
    m_wallet.start(m_executable, command, input);
}

void OrdManager::refreshWallet()
{
    if (!isReady()) return;
    if (m_server.isRunning() && !m_server_url.isEmpty()) {
        startWalletOperation(WalletOperation::BALANCE, {QStringLiteral("balance")});
        return;
    }
    m_after_index = AfterIndex::REFRESH_WALLET;
    if (!m_index.isRunning()) startIndex();
}

void OrdManager::createWallet()
{
    startWalletOperation(WalletOperation::CREATE, {QStringLiteral("create")});
}

void OrdManager::restoreWallet(const QString& mnemonic, const QString& passphrase)
{
    QStringList arguments{QStringLiteral("restore"), QStringLiteral("--from"), QStringLiteral("mnemonic")};
    if (!passphrase.isEmpty()) arguments << QStringLiteral("--passphrase") << passphrase;
    startWalletOperation(WalletOperation::RESTORE, arguments, mnemonic.trimmed().toUtf8() + '\n');
}

QStringList OrdManager::inscriptionArguments(const QString& file, const QString& fee_rate, const QString& destination, bool compress, bool dry_run) const
{
    QStringList arguments{baseArguments()};
    arguments << QStringLiteral("wallet") << QStringLiteral("--server-url") << m_server_url
              << QStringLiteral("--name") << QStringLiteral("ord")
              << QStringLiteral("inscribe") << QStringLiteral("--fee-rate") << fee_rate
              << QStringLiteral("--file") << file;
    if (!destination.isEmpty()) arguments << QStringLiteral("--destination") << destination;
    if (compress) arguments << QStringLiteral("--compress");
    if (dry_run) arguments << QStringLiteral("--dry-run");
    return arguments;
}

void OrdManager::previewInscription(const QString& file, const QString& fee_rate, const QString& destination, bool compress)
{
    if (!isReady() || m_inscription.isRunning()) return;
    m_preview_file = file;
    m_preview_fee_rate = fee_rate;
    m_preview_destination = destination;
    m_preview_compress = compress;
    m_after_index = AfterIndex::PREVIEW_INSCRIPTION;
    m_inscription_preview = true;
    Q_EMIT inscriptionStarted();
    if (m_server.isRunning() && !m_server_url.isEmpty()) {
        runInscriptionPreview();
    } else if (!m_index.isRunning()) {
        startIndex();
    }
}

void OrdManager::runInscriptionPreview()
{
    m_inscription.start(m_executable, inscriptionArguments(m_preview_file, m_preview_fee_rate, m_preview_destination, m_preview_compress, true));
}

void OrdManager::inscribe(const QString& file, const QString& fee_rate, const QString& destination, bool compress)
{
    if (!isReady()) {
        Q_EMIT inscriptionFailed(tr("Ord is not installed yet."));
        return;
    }
    if (m_inscription.isRunning()) {
        Q_EMIT inscriptionFailed(tr("An Ord inscription is already in progress."));
        return;
    }

    m_inscription_preview = false;
    Q_EMIT inscriptionStarted();
    m_inscription.start(m_executable, inscriptionArguments(file, fee_rate, destination, compress, false));
}

void OrdManager::stop()
{
    m_stopping = true;
    m_index.stop();
    m_server.stop();
    m_wallet.stop();
    m_inscription.stop();
}
