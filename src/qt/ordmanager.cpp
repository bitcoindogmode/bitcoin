// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordmanager.h>
#include <qt/guiutil.h>

#include <addresstype.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <key_io.h>
#include <support/cleanse.h>
#include <util/fs_helpers.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTimer>

#include <cmath>
#include <limits>
#include <utility>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr qint64 MAX_INSCRIPTION_FILE_BYTES{10 * 1024 * 1024};
constexpr auto ASSET_INDEX_DIRECTORY{"ord/data-runes-sats-v1"};

QJsonObject ParseObject(const QByteArray& output)
{
    const qsizetype first{output.indexOf('{')};
    const qsizetype last{output.lastIndexOf('}')};
    if (first < 0 || last < first) return {};
    return QJsonDocument::fromJson(output.sliced(first, last - first + 1)).object();
}

void Cleanse(QByteArray& value)
{
    if (!value.isEmpty()) memory_cleanse(value.data(), static_cast<size_t>(value.size()));
    value.clear();
}

bool OpenRegularFileNoFollow(const QString& path, QFile& file, QString& error)
{
#ifdef Q_OS_UNIX
    const QByteArray encoded{QFile::encodeName(path)};
    const int fd{::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
    if (fd < 0) {
        error = QObject::tr("Could not open the inscription file without following links.");
        return false;
    }
    struct stat status;
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode)) {
        ::close(fd);
        error = QObject::tr("The inscription source must be a regular file, not a link or device.");
        return false;
    }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        error = QObject::tr("Could not read the inscription file.");
        return false;
    }
#else
    const QFileInfo before{path};
    if (before.isSymLink() || !before.isFile() || !before.isReadable() || !file.open(QIODevice::ReadOnly)) {
        error = QObject::tr("The inscription source must be a readable regular file, not a link or device.");
        return false;
    }
    const QFileInfo after{path};
    if (after.isSymLink() || before.canonicalFilePath() != after.canonicalFilePath()) {
        file.close();
        error = QObject::tr("The inscription file changed while it was being opened.");
        return false;
    }
#endif
    return true;
}

QString MakeInputKey(const QString& file, const QByteArray& file_hash, qint64 size, const QString& fee_rate, const QString& destination, bool compress)
{
    const QByteArray value{file.toUtf8() + '\0' + file_hash + '\0' + QByteArray::number(size) + '\0' + fee_rate.toUtf8() + '\0' + destination.toUtf8() + '\0' + QByteArray::number(compress)};
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

std::optional<qint64> JsonAmount(const QJsonObject& object, const QString& name)
{
    const QJsonValue value{object.value(name)};
    if (!value.isDouble()) return std::nullopt;
    const double amount{value.toDouble()};
    if (!std::isfinite(amount) || amount < 0 || amount > static_cast<double>(MAX_MONEY) || std::floor(amount) != amount) return std::nullopt;
    return static_cast<qint64>(amount);
}

bool OrdServerReady(quint16 port, const QString& chain)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(50)) return false;
    socket.write("GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept: application/json\r\nConnection: close\r\n\r\n");
    if (!socket.waitForBytesWritten(50)) return false;

    QByteArray response;
    for (int attempt = 0; attempt < 4 && response.size() <= 4096; ++attempt) {
        if (!socket.waitForReadyRead(50)) break;
        response.append(socket.readAll());
    }
    response.append(socket.readAll());
    if (response.size() > 4096) return false;
    const qsizetype status_end{response.indexOf("\r\n")};
    const qsizetype header_end{response.indexOf("\r\n\r\n")};
    if (status_end < 0 || header_end < status_end) return false;
    const QByteArray status{response.first(status_end)};
    if (!status.startsWith("HTTP/1.1 200 ") && !status.startsWith("HTTP/1.0 200 ")) return false;
    const QJsonObject status_object{QJsonDocument::fromJson(response.sliced(header_end + 4)).object()};
    return status_object.value("chain").toString() == chain && status_object.value("inscription_index").toBool() &&
           status_object.value("rune_index").toBool() && status_object.value("sat_index").toBool() &&
           status_object.value("json_api").toBool() && !status_object.value("unrecoverably_reorged").toBool(true);
}

QString ExtractMnemonic(const QByteArray& output)
{
    const QByteArray key{QByteArrayLiteral("\"mnemonic\"")};
    qsizetype position{output.indexOf(key)};
    if (position < 0) return {};
    position += key.size();
    while (position < output.size() && (output.at(position) == ' ' || output.at(position) == '\t' || output.at(position) == '\r' || output.at(position) == '\n')) ++position;
    if (position >= output.size() || output.at(position++) != ':') return {};
    while (position < output.size() && (output.at(position) == ' ' || output.at(position) == '\t' || output.at(position) == '\r' || output.at(position) == '\n')) ++position;
    if (position >= output.size() || output.at(position++) != '"') return {};

    QString mnemonic;
    mnemonic.reserve(256);
    int words{1};
    bool previous_space{false};
    for (; position < output.size(); ++position) {
        const char value{output.at(position)};
        if (value == '"') {
            if (mnemonic.isEmpty() || previous_space || (words != 12 && words != 15 && words != 18 && words != 21 && words != 24)) {
                mnemonic.fill(QChar{'\0'});
                return {};
            }
            return mnemonic;
        }
        if (value == ' ') {
            if (mnemonic.isEmpty() || previous_space) {
                mnemonic.fill(QChar{'\0'});
                return {};
            }
            previous_space = true;
            ++words;
        } else if (value >= 'a' && value <= 'z') {
            previous_space = false;
        } else {
            mnemonic.fill(QChar{'\0'});
            return {};
        }
        mnemonic.append(QChar::fromLatin1(value));
    }
    mnemonic.fill(QChar{'\0'});
    return {};
}
} // namespace

OrdManager::OrdManager(QString bitcoin_data_dir, QString bitcoin_network_dir, QString chain, QObject* parent, std::optional<OrdArtifact> artifact, OrdRpcExecutor execute_rpc, quint16 rpc_port)
    : QObject{parent},
      m_bitcoin_data_dir{std::move(bitcoin_data_dir)},
      m_bitcoin_network_dir{std::move(bitcoin_network_dir)},
      m_chain{std::move(chain)},
      m_artifact_override{std::move(artifact)},
      m_index{this},
      m_server{this},
      m_wallet{this},
      m_inscription{this},
      m_execute_rpc{std::move(execute_rpc)},
      m_rpc_port{rpc_port}
{
    connect(&m_index, &OrdProcess::failed, this, [this](const QString& error) {
        if (m_stopping) return;
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(tr("Ord indexing could not be started: %1").arg(error));
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) failPendingInscription(tr("Ord indexing could not be started: %1").arg(error));
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
            if (pending == AfterIndex::PREVIEW_INSCRIPTION) failPendingInscription(error);
            Q_EMIT failed(error);
        }
    });
    connect(&m_server, &OrdProcess::failed, this, [this](const QString& error) {
        if (m_stopping) return;
        m_server_url.clear();
        m_server_port = 0;
        m_preview_authorized = false;
        m_rpc_gate.reset();
        m_inscription.stop();
        const QString message{tr("The local Ord server could not be started: %1").arg(error)};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) failPendingInscription(message);
        Q_EMIT failed(message);
    });
    connect(&m_server, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        m_server_url.clear();
        m_server_port = 0;
        m_preview_authorized = false;
        m_rpc_gate.reset();
        m_inscription.stop();
        const QString message{tr("The local Ord server exited with code %1.\n\n%2").arg(exit_code).arg(QString::fromUtf8(m_server.output()).trimmed())};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) failPendingInscription(message);
        Q_EMIT failed(message);
    });
    connect(&m_wallet, &OrdProcess::failed, this, [this](const QString& error) {
        m_wallet_operation = WalletOperation::NONE;
        QByteArray output{m_wallet.takeOutput()};
        Cleanse(output);
        if (!m_stopping) Q_EMIT walletUnavailable(tr("Ord wallet command could not be started: %1").arg(error));
    });
    connect(&m_wallet, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        const WalletOperation operation{std::exchange(m_wallet_operation, WalletOperation::NONE)};
        QByteArray output{m_wallet.takeOutput()};
        if (exit_code != 0) {
            if (operation == WalletOperation::CREATE || operation == WalletOperation::RESTORE) {
                Q_EMIT walletUnavailable(tr("Ord wallet creation or restoration exited with code %1. Its output was hidden because it may contain recovery words.").arg(exit_code));
            } else {
                Q_EMIT walletUnavailable(tr("Ord wallet command exited with code %1.\n\n%2").arg(exit_code).arg(QString::fromUtf8(output).trimmed()));
            }
            Cleanse(output);
            return;
        }

        if (operation == WalletOperation::BALANCE) {
            const QJsonObject object{ParseObject(output)};
            const auto cardinal{JsonAmount(object, QStringLiteral("cardinal"))};
            const auto total{JsonAmount(object, QStringLiteral("total"))};
            if (!cardinal || !total) {
                Q_EMIT walletUnavailable(tr("Ord returned an invalid wallet balance response."));
                Cleanse(output);
                return;
            }
            m_cardinal_balance = *cardinal;
            m_total_balance = *total;
            if (!m_funding_address.isEmpty()) {
                Q_EMIT walletDetails(m_cardinal_balance, m_total_balance, m_funding_address);
            } else {
                QTimer::singleShot(0, this, [this] {
                    startWalletOperation(WalletOperation::RECEIVE, {QStringLiteral("receive")});
                });
            }
        } else if (operation == WalletOperation::RECEIVE) {
            const QJsonObject object{ParseObject(output)};
            const QJsonArray addresses = object.value(QStringLiteral("addresses")).toArray();
            if (addresses.isEmpty() || !addresses.at(0).isString()) {
                Q_EMIT walletUnavailable(tr("Ord returned an invalid funding address response.\n\n%1").arg(QString::fromUtf8(output).trimmed()));
                Cleanse(output);
                return;
            }
            const QString funding_address{addresses.at(0).toString()};
            if (!IsValidDestinationString(funding_address.toStdString(), Params())) {
                Q_EMIT walletUnavailable(tr("Ord returned a funding address for the wrong network or an invalid address."));
                Cleanse(output);
                return;
            }
            m_funding_address = funding_address;
            Q_EMIT walletDetails(m_cardinal_balance, m_total_balance, m_funding_address);
        } else if (operation == WalletOperation::CREATE) {
            m_creating_wallet = false;
            QString mnemonic{ExtractMnemonic(output)};
            Cleanse(output);
            if (mnemonic.isEmpty()) {
                Q_EMIT walletUnavailable(tr("Ord created the wallet but did not return recovery words."));
                Cleanse(output);
                return;
            }
            m_funding_address.clear();
            m_backup_pending = true;
            Q_EMIT walletCreated(mnemonic);
            mnemonic.fill(QChar{'\0'});
        } else if (operation == WalletOperation::RESTORE) {
            if (QFileInfo::exists(backupMarker()) && !QFile::remove(backupMarker())) {
                Q_EMIT walletUnavailable(tr("The wallet was restored, but its backup-required marker could not be cleared."));
                Cleanse(output);
                return;
            }
            m_creating_wallet = false;
            m_backup_pending = false;
            m_funding_address.clear();
            Q_EMIT walletRestored();
        }
        Cleanse(output);
    });
    connect(&m_inscription, &OrdProcess::failed, this, [this](const QString& error) {
        const quint64 request_id{m_inscription_request_id};
        m_inscription_operation = InscriptionOperation::NONE;
        m_broadcast_pending = false;
        m_preview_authorized = false;
        m_preview_snapshot.reset();
        m_rpc_gate.reset();
        const bool cancelled{std::exchange(m_cancel_requested, false)};
        if (!m_stopping) {
            Q_EMIT inscriptionFailed(request_id, cancelled ? tr("The inscription operation was cancelled. If transaction creation had started, inspect the Ord wallet for a pending commit before retrying.") : tr("Ord inscription could not be started: %1").arg(error));
        }
    });
    connect(&m_inscription, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        QByteArray raw_output{m_inscription.takeOutput()};
        const QString output{QString::fromUtf8(raw_output).trimmed()};
        const InscriptionOperation operation{std::exchange(m_inscription_operation, InscriptionOperation::NONE)};
        const quint64 request_id{m_inscription_request_id};

        if (m_cancel_requested) {
            m_cancel_requested = false;
            m_preview_authorized = false;
            m_preview_snapshot.reset();
            m_rpc_gate.reset();
            Cleanse(raw_output);
            Q_EMIT inscriptionFailed(request_id, tr("The inscription operation was cancelled. If transaction creation had started, inspect the Ord wallet for a pending commit before retrying."));
            return;
        }
        if (exit_code != 0) {
            m_preview_authorized = false;
            m_preview_snapshot.reset();
            m_rpc_gate.reset();
            Cleanse(raw_output);
            Q_EMIT inscriptionFailed(request_id, tr("Ord inscription exited with code %1.\n\n%2").arg(exit_code).arg(output));
            return;
        }

        if (operation == InscriptionOperation::CARDINALS || operation == InscriptionOperation::RARE_SATS) {
            const QJsonDocument document{QJsonDocument::fromJson(raw_output)};
            bool valid{document.isArray()};
            static const QRegularExpression outpoint{QStringLiteral("^[0-9a-f]{64}:[0-9]{1,10}$")};
            for (const QJsonValue& value : document.array()) {
                const QString coin{value.toObject().value("output").toString()};
                if (!outpoint.match(coin).hasMatch() || coin.section(':', 1).toULongLong() > std::numeric_limits<uint32_t>::max()) { valid = false; break; }
                if (operation == InscriptionOperation::CARDINALS) m_safe_inputs.insert(coin);
                else m_safe_inputs.remove(coin);
            }
            Cleanse(raw_output);
            if (!valid || (operation == InscriptionOperation::RARE_SATS && m_safe_inputs.isEmpty())) {
                m_preview_snapshot.reset();
                Q_EMIT inscriptionFailed(request_id, tr("Could not identify funding outputs free of inscriptions, runes, and non-common sats. No transaction was broadcast."));
                return;
            }
            if (operation == InscriptionOperation::CARDINALS) {
                m_inscription_operation = InscriptionOperation::RARE_SATS;
                QStringList arguments{baseArguments()};
                arguments << "wallet" << "--server-url" << m_server_url << "--name" << "ord" << "sats";
                m_inscription.start(m_executable, arguments);
            } else {
                m_rpc_gate = std::make_unique<OrdRpcGate>(m_execute_rpc, m_safe_inputs, QDir{m_bitcoin_network_dir}.filePath("ord/tmp"), this);
                if (!m_rpc_gate->isReady()) {
                    m_rpc_gate.reset();
                    m_preview_snapshot.reset();
                    Q_EMIT inscriptionFailed(request_id, tr("Could not start the isolated Ord funding-input gate. No transaction was broadcast."));
                    return;
                }
                m_inscription_operation = InscriptionOperation::PREVIEW;
                m_inscription.start(m_executable, inscriptionArguments(m_preview_file, m_preview_fee_rate, m_preview_destination, m_preview_compress, true));
            }
            return;
        }

        if (operation == InscriptionOperation::PREVIEW || operation == InscriptionOperation::REVALIDATE) {
            const QJsonObject object{ParseObject(raw_output)};
            const auto total_fees{JsonAmount(object, QStringLiteral("total_fees"))};
            if (!total_fees) {
                m_preview_authorized = false;
                m_preview_snapshot.reset();
                Cleanse(raw_output);
                Q_EMIT inscriptionFailed(request_id, tr("Ord returned an invalid inscription cost preview."));
                return;
            }
            if (operation == InscriptionOperation::PREVIEW) {
                m_preview_total_fees = *total_fees;
                m_preview_authorized = true;
                Q_EMIT inscriptionPreview(request_id, *total_fees, m_preview_input_key, output);
            } else if (*total_fees != m_preview_total_fees) {
                m_preview_authorized = false;
                m_preview_snapshot.reset();
                Cleanse(raw_output);
                Q_EMIT inscriptionFailed(request_id, tr("The inscription cost changed from %1 to %2 sats. Preview again before broadcasting.").arg(m_preview_total_fees).arg(*total_fees));
                return;
            } else {
                m_broadcast_pending = true;
                QTimer::singleShot(0, this, [this, request_id] {
                    if (request_id == m_inscription_request_id) runInscriptionBroadcast();
                });
            }
        } else if (operation == InscriptionOperation::BROADCAST) {
            m_preview_authorized = false;
            m_preview_snapshot.reset();
            m_rpc_gate.reset();
            Q_EMIT inscriptionComplete(request_id, output);
        }
        Cleanse(raw_output);
    });
}

OrdManager::~OrdManager()
{
    stop();
}

bool OrdManager::verifyExecutable(const QString& path, QString& error) const
{
    QProcess version;
    version.setProcessEnvironment(OrdProcess::SafeEnvironment());
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
    const QString config_directory{QDir{m_bitcoin_network_dir}.filePath("ord")};
    if (!QDir{}.mkpath(config_directory)) { Q_EMIT failed(tr("Could not create the private Ord configuration directory.")); return; }
    m_config = std::make_unique<QTemporaryFile>(QDir{config_directory}.filePath("managed-config-XXXXXX.yaml"));
    if (!m_config->open() || m_config->write("{}\n") != 3 || !m_config->flush()) {
        m_config.reset();
        Q_EMIT failed(tr("Could not create the private Ord configuration."));
        return;
    }
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
    const QString data_dir{QDir{m_bitcoin_network_dir}.filePath(QString::fromLatin1(ASSET_INDEX_DIRECTORY))};
    QDir{}.mkpath(data_dir);
    QStringList arguments{baseArguments()};
    arguments << "index" << "update";
    m_index.start(m_executable, arguments);
}

void OrdManager::startServer()
{
    if (m_stopping || !isReady()) return;
    if (m_server.isRunning() && !m_server_url.isEmpty()) {
        continueAfterIndex();
        return;
    }
    if (m_server.isRunning()) return;

    m_server_port = 0;
    m_server_url.clear();

    QStringList command{baseArguments()};
    command << QStringLiteral("server") << QStringLiteral("--address") << QStringLiteral("127.0.0.1")
            << QStringLiteral("--http-port") << QStringLiteral("0");
    m_server.start(m_executable, command);
    QTimer::singleShot(100, this, [this] { waitForServer(); });
}

void OrdManager::waitForServer(int attempts_remaining)
{
    if (m_stopping || !m_server.isRunning()) return;
    if (m_server_port == 0) {
        static const QRegularExpression listening{QStringLiteral(R"(Listening on http://127\.0\.0\.1:([0-9]{1,5}))")};
        const QRegularExpressionMatch match{listening.match(QString::fromUtf8(m_server.output()))};
        if (match.hasMatch()) {
            bool ok{false};
            const uint port{match.captured(1).toUInt(&ok)};
            if (ok && port > 0 && port <= std::numeric_limits<quint16>::max()) {
                m_server_port = static_cast<quint16>(port);
                m_server_url = QStringLiteral("http://127.0.0.1:%1").arg(m_server_port);
            }
        }
    }
    if (m_server_port == 0) {
        if (attempts_remaining <= 1) {
            const QString message{tr("The local Ord server did not report its loopback port in time.")};
            const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
            if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
            if (pending == AfterIndex::PREVIEW_INSCRIPTION) failPendingInscription(message);
            Q_EMIT failed(message);
            m_server.stop();
            return;
        }
        QTimer::singleShot(100, this, [this, attempts_remaining] { waitForServer(attempts_remaining - 1); });
        return;
    }
    if (OrdServerReady(m_server_port, m_chain)) {
        continueAfterIndex();
        return;
    }
    if (attempts_remaining <= 1) {
        const QString message{tr("The local Ord server did not become ready in time.")};
        const AfterIndex pending{std::exchange(m_after_index, AfterIndex::NONE)};
        if (pending == AfterIndex::REFRESH_WALLET) Q_EMIT walletUnavailable(message);
        if (pending == AfterIndex::PREVIEW_INSCRIPTION) failPendingInscription(message);
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
        const quint64 request_id{m_inscription_request_id};
        QTimer::singleShot(0, this, [this, request_id] {
            if (request_id == m_inscription_request_id) runInscriptionPreview();
        });
    }
}

void OrdManager::failPendingInscription(const QString& message)
{
    m_inscription_operation = InscriptionOperation::NONE;
    m_broadcast_pending = false;
    m_preview_authorized = false;
    m_preview_snapshot.reset();
    m_rpc_gate.reset();
    Q_EMIT inscriptionFailed(m_inscription_request_id, message);
}

QStringList OrdManager::baseArguments() const
{
    return {
        QStringLiteral("--chain"), m_chain,
        QStringLiteral("--config"), m_config ? m_config->fileName() : QString{},
        QStringLiteral("--index"), QDir{m_bitcoin_network_dir}.filePath(QString::fromLatin1(ASSET_INDEX_DIRECTORY) + "/index.redb"),
        QStringLiteral("--bitcoin-rpc-url"), QStringLiteral("127.0.0.1:%1").arg(m_rpc_port),
        QStringLiteral("--bitcoin-data-dir"), m_bitcoin_data_dir,
        QStringLiteral("--cookie-file"), QDir{m_bitcoin_network_dir}.filePath(QStringLiteral(".cookie")),
        QStringLiteral("--data-dir"), QDir{m_bitcoin_network_dir}.filePath(QString::fromLatin1(ASSET_INDEX_DIRECTORY)),
        QStringLiteral("--index-runes"),
        QStringLiteral("--index-sats"),
    };
}

void OrdManager::startWalletOperation(WalletOperation operation, const QStringList& arguments, SecureString input)
{
    if (!isReady() || m_wallet.isRunning()) return;
    if ((operation == WalletOperation::BALANCE || operation == WalletOperation::RECEIVE) && backupRequired()) {
        Q_EMIT walletUnavailable(tr("Back up the Ord wallet before obtaining a funding address."));
        return;
    }
    m_wallet_operation = operation;
    QStringList command{baseArguments()};
    command << QStringLiteral("wallet") << QStringLiteral("--server-url") << m_server_url
            << QStringLiteral("--name") << QStringLiteral("ord") << arguments;
    m_wallet.start(m_executable, command, std::move(input));
}

void OrdManager::refreshWallet()
{
    if (!isReady()) return;
    if (backupRequired()) { Q_EMIT walletUnavailable(tr("Ord wallet backup has not been acknowledged. Funding and inscriptions are disabled. If creation was interrupted, restore or back up the wallet before using it.")); return; }
    if (m_server.isRunning() && !m_server_url.isEmpty()) {
        startWalletOperation(WalletOperation::BALANCE, {QStringLiteral("balance")});
        return;
    }
    m_after_index = AfterIndex::REFRESH_WALLET;
    if (!m_index.isRunning()) startIndex();
}

void OrdManager::createWallet()
{
    if (!isReady() || m_wallet.isRunning()) return;
    if (backupRequired()) {
        Q_EMIT walletUnavailable(tr("An Ord wallet backup is still required. A new wallet cannot be created until the existing wallet is backed up or restored."));
        return;
    }
    QSaveFile marker{backupMarker()};
    if (!marker.open(QIODevice::WriteOnly) || marker.write("Backup acknowledgment required.\n") < 0 || !marker.commit()) {
        Q_EMIT walletUnavailable(tr("Could not record the required Ord wallet backup. Wallet creation was not started."));
        return;
    }
    // Persist the backup gate before Core can persist a newly generated wallet.
    FILE* marker_file{fsbridge::fopen(GUIUtil::QStringToPath(backupMarker()), "rb+")};
    const bool persisted{marker_file && FileCommit(marker_file)};
    if (marker_file) std::fclose(marker_file);
    if (!persisted) {
        Q_EMIT walletUnavailable(tr("Could not persist the required Ord wallet backup gate. Wallet creation was not started."));
        return;
    }
    DirectoryCommit(GUIUtil::QStringToPath(QFileInfo{backupMarker()}.absolutePath()));
    m_creating_wallet = true;
    startWalletOperation(WalletOperation::CREATE, {QStringLiteral("create")});
}

QString OrdManager::backupMarker() const { return QDir{m_bitcoin_network_dir}.filePath("ord/backup-required"); }
bool OrdManager::backupRequired() const { return m_creating_wallet || QFileInfo::exists(backupMarker()); }

bool OrdManager::acknowledgeWalletBackup()
{
    if (!m_backup_pending || !QFile::remove(backupMarker())) return false;
    m_backup_pending = false;
    return true;
}

void OrdManager::restoreWallet(const QString& mnemonic)
{
    QStringList arguments{QStringLiteral("restore"), QStringLiteral("--from"), QStringLiteral("mnemonic")};
    QString normalized{mnemonic.trimmed()};
    QByteArray encoded{normalized.toUtf8()};
    SecureString input{encoded.constData(), static_cast<size_t>(encoded.size())};
    input.push_back('\n');
    Cleanse(encoded);
    normalized.fill(QChar{'\0'});
    startWalletOperation(WalletOperation::RESTORE, arguments, std::move(input));
}

QStringList OrdManager::inscriptionArguments(const QString& file, const QString& fee_rate, const QString& destination, bool compress, bool dry_run) const
{
    QStringList arguments{baseArguments()};
    if (!m_rpc_gate || !m_rpc_gate->isReady()) return {};
    arguments[arguments.indexOf("--bitcoin-rpc-url") + 1] = m_rpc_gate->url();
    arguments[arguments.indexOf("--cookie-file") + 1] = m_rpc_gate->cookiePath();
    arguments << QStringLiteral("wallet") << QStringLiteral("--server-url") << m_server_url
              << QStringLiteral("--name") << QStringLiteral("ord")
              << QStringLiteral("inscribe") << QStringLiteral("--fee-rate") << fee_rate
              << QStringLiteral("--file") << file;
    if (!destination.isEmpty()) arguments << QStringLiteral("--destination") << destination;
    if (compress) arguments << QStringLiteral("--compress");
    if (dry_run) arguments << QStringLiteral("--dry-run");
    return arguments;
}

bool OrdManager::validateInscriptionSettings(const QString& fee_rate, const QString& destination, QString& error) const
{
    static const QRegularExpression fee_pattern{QStringLiteral(R"(^[0-9]+(?:\.[0-9]{1,2})?$)")};
    bool fee_ok{false};
    const double parsed_fee{QLocale::c().toDouble(fee_rate, &fee_ok)};
    if (!fee_ok || !fee_pattern.match(fee_rate).hasMatch() || parsed_fee < 0.1 || parsed_fee > 1000.0) {
        error = tr("Enter a fee rate from 0.1 through 1,000 sats/vB with at most two decimal places.");
        return false;
    }
    if (!destination.isEmpty() && !IsValidDestinationString(destination.toStdString(), Params())) {
        error = tr("The inscription destination is not a valid address for the active Bitcoin network.");
        return false;
    }
    return true;
}

bool OrdManager::ReadPreviewFile(const QString& path, QByteArray& bytes, QString& error)
{
    QFile source{path};
    bytes.clear();
    if (!OpenRegularFileNoFollow(path, source, error)) return false;
    bytes = source.read(MAX_INSCRIPTION_FILE_BYTES + 1);
    if (source.error() != QFileDevice::NoError || !source.atEnd() || bytes.size() > MAX_INSCRIPTION_FILE_BYTES) {
        bytes.clear();
        error = tr("Could not read a regular inscription file within the 10 MiB limit.");
        return false;
    }
    return true;
}

QString OrdManager::currentInputKey(const QString& file, const QString& fee_rate, const QString& destination, bool compress, QString& error) const
{
    QFile source{file};
    if (!OpenRegularFileNoFollow(file, source, error)) return {};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 total{0};
    while (!source.atEnd()) {
        const QByteArray chunk{source.read(64 * 1024)};
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            error = tr("Could not read the complete inscription file.");
            return {};
        }
        total += chunk.size();
        if (total > MAX_INSCRIPTION_FILE_BYTES) {
            error = tr("The inscription file exceeds the 10 MiB safety limit.");
            return {};
        }
        hash.addData(chunk);
    }
    return MakeInputKey(QFileInfo{file}.absoluteFilePath(), hash.result(), total, fee_rate, destination, compress);
}

bool OrdManager::createInscriptionSnapshot(const QString& file, const QString& fee_rate, const QString& destination, bool compress, QString& error)
{
    if (!validateInscriptionSettings(fee_rate, destination, error)) return false;

    QFile source{file};
    if (!OpenRegularFileNoFollow(file, source, error)) return false;
    const QString snapshot_dir{QDir{m_bitcoin_network_dir}.filePath(QStringLiteral("ord/tmp"))};
    if (!QDir{}.mkpath(snapshot_dir)) {
        error = tr("Could not create the private Ord snapshot directory.");
        return false;
    }
    QString suffix{QFileInfo{file}.suffix()};
    if (!QRegularExpression{QStringLiteral(R"(^[A-Za-z0-9]{1,16}$)")}.match(suffix).hasMatch()) suffix.clear();
    const QString pattern{QDir{snapshot_dir}.filePath(QStringLiteral("inscription-XXXXXX") + (suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix))};
    auto snapshot{std::make_unique<QTemporaryFile>(pattern)};
    snapshot->setAutoRemove(true);
    if (!snapshot->open()) {
        error = tr("Could not create a private snapshot of the inscription file.");
        return false;
    }
    snapshot->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QCryptographicHash hash{QCryptographicHash::Sha256};
    qint64 total{0};
    while (!source.atEnd()) {
        const QByteArray chunk{source.read(64 * 1024)};
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            error = tr("Could not read the complete inscription file.");
            return false;
        }
        total += chunk.size();
        if (total > MAX_INSCRIPTION_FILE_BYTES) {
            error = tr("The inscription file exceeds the 10 MiB safety limit.");
            return false;
        }
        if (snapshot->write(chunk) != chunk.size()) {
            error = tr("Could not write the complete private inscription snapshot.");
            return false;
        }
        hash.addData(chunk);
    }
    if (!snapshot->flush()) {
        error = tr("Could not flush the private inscription snapshot.");
        return false;
    }
    snapshot->close();

    m_preview_original_file = QFileInfo{file}.absoluteFilePath();
    m_preview_file = snapshot->fileName();
    m_preview_fee_rate = fee_rate;
    m_preview_destination = destination;
    m_preview_compress = compress;
    m_preview_input_key = MakeInputKey(m_preview_original_file, hash.result(), total, fee_rate, destination, compress);
    m_preview_snapshot = std::move(snapshot);
    return true;
}

quint64 OrdManager::previewInscription(const QString& file, const QString& fee_rate, const QString& destination, bool compress)
{
    const quint64 request_id{m_next_request_id++};
    if (m_next_request_id == 0) ++m_next_request_id;
    QString error;
    if (!isReady()) error = tr("Ord is not installed yet.");
    else if (backupRequired()) error = tr("Back up the Ord wallet before previewing or broadcasting inscriptions.");
    else if (!m_execute_rpc) error = tr("The wallet-scoped Ord RPC safety gate is unavailable.");
    else if (isInscribing()) error = tr("An Ord inscription operation is already in progress.");
    else {
        m_inscription_request_id = request_id;
        m_preview_authorized = false;
        m_cancel_requested = false;
        m_preview_snapshot.reset();
        if (!createInscriptionSnapshot(file, fee_rate, destination, compress, error) && error.isEmpty()) {
            error = tr("Could not prepare the inscription preview.");
        }
    }
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, request_id, error] { Q_EMIT inscriptionFailed(request_id, error); });
        return request_id;
    }

    m_after_index = AfterIndex::PREVIEW_INSCRIPTION;
    m_inscription_operation = InscriptionOperation::PREVIEW;
    Q_EMIT inscriptionStarted(request_id);
    if (m_server.isRunning() && !m_server_url.isEmpty()) {
        runInscriptionPreview();
    } else if (!m_index.isRunning()) {
        startIndex();
    }
    return request_id;
}

void OrdManager::runInscriptionPreview()
{
    if (m_stopping || m_inscription_operation != InscriptionOperation::PREVIEW || !m_preview_snapshot || m_inscription.isRunning()) return;
    m_after_index = AfterIndex::NONE;
    m_rpc_gate.reset();
    m_safe_inputs.clear();
    m_inscription_operation = InscriptionOperation::CARDINALS;
    QStringList arguments{baseArguments()};
    arguments << "wallet" << "--server-url" << m_server_url << "--name" << "ord" << "cardinals";
    m_inscription.start(m_executable, arguments);
}

bool OrdManager::previewMatches(quint64 request_id, const QString& file, const QString& fee_rate, const QString& destination, bool compress, QString& error) const
{
    if (!m_preview_authorized || request_id == 0 || request_id != m_inscription_request_id || !m_preview_snapshot) {
        error = tr("This inscription does not have a current successful cost preview.");
        return false;
    }
    if (!validateInscriptionSettings(fee_rate, destination, error)) return false;
    const QString current{currentInputKey(file, fee_rate, destination, compress, error)};
    if (current.isEmpty()) return false;
    if (current != m_preview_input_key) {
        error = tr("The file bytes or inscription settings changed after the cost preview.");
        return false;
    }
    return true;
}

void OrdManager::invalidatePreview(quint64 request_id)
{
    if (request_id == 0 || request_id != m_inscription_request_id || isInscribing()) return;
    m_preview_authorized = false;
    m_preview_snapshot.reset();
    m_rpc_gate.reset();
}

void OrdManager::inscribe(quint64 request_id)
{
    if (!isReady() || backupRequired() || m_inscription.isRunning() || !m_preview_authorized || request_id != m_inscription_request_id || !m_preview_snapshot || !m_rpc_gate || !m_rpc_gate->isReady()) {
        Q_EMIT inscriptionFailed(request_id, tr("A matching successful cost preview is required before broadcasting."));
        return;
    }

    m_preview_authorized = false;
    m_inscription_operation = InscriptionOperation::REVALIDATE;
    Q_EMIT inscriptionStarted(request_id);
    m_inscription.start(m_executable, inscriptionArguments(m_preview_file, m_preview_fee_rate, m_preview_destination, m_preview_compress, true));
}

void OrdManager::runInscriptionBroadcast()
{
    if (!std::exchange(m_broadcast_pending, false)) return;
    if (m_stopping) return;
    if (backupRequired() || !m_preview_snapshot || m_inscription.isRunning() || !m_rpc_gate || !m_rpc_gate->isReady()) {
        failPendingInscription(tr("The authorized Ord session is no longer available. Preview again before broadcasting."));
        return;
    }
    m_inscription_operation = InscriptionOperation::BROADCAST;
    m_inscription.start(m_executable, inscriptionArguments(m_preview_file, m_preview_fee_rate, m_preview_destination, m_preview_compress, false));
}

void OrdManager::cancelInscription()
{
    if (!isInscribing()) return;
    m_preview_authorized = false;
    m_rpc_gate.reset();
    m_broadcast_pending = false;
    m_after_index = AfterIndex::NONE;
    if (m_inscription.isRunning()) {
        m_cancel_requested = true;
        m_inscription.stop();
    } else {
        m_inscription_operation = InscriptionOperation::NONE;
        m_preview_snapshot.reset();
        Q_EMIT inscriptionFailed(m_inscription_request_id, tr("The inscription operation was cancelled before transaction creation."));
    }
}

void OrdManager::stop()
{
    m_stopping = true;
    m_broadcast_pending = false;
    m_rpc_gate.reset();
    m_index.stop();
    m_server.stop();
    m_wallet.stop();
    m_inscription.stop();
    m_preview_snapshot.reset();
}
