// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordmanager.h>

#include <QDir>
#include <QProcess>

#include <utility>

OrdManager::OrdManager(QString bitcoin_data_dir, QString bitcoin_network_dir, QString chain, QObject* parent, std::optional<OrdArtifact> artifact)
    : QObject{parent},
      m_bitcoin_data_dir{std::move(bitcoin_data_dir)},
      m_bitcoin_network_dir{std::move(bitcoin_network_dir)},
      m_chain{std::move(chain)},
      m_artifact_override{std::move(artifact)},
      m_index{this}
{
    connect(&m_index, &OrdProcess::failed, this, [this](const QString& error) {
        if (m_stopping) return;
        Q_EMIT failed(tr("Ord indexing could not be started: %1").arg(error));
    });
    connect(&m_index, &OrdProcess::completed, this, [this](int exit_code) {
        if (m_stopping) return;
        if (exit_code == 0) {
            Q_EMIT indexComplete();
        } else {
            Q_EMIT failed(tr("Ord indexing exited with code %1.\n\n%2").arg(exit_code).arg(QString::fromUtf8(m_index.output())));
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
    if (!isReady() || m_index.isRunning()) return;
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

void OrdManager::stop()
{
    m_stopping = true;
    m_index.stop();
}
