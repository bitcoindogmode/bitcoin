// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDMANAGER_H
#define BITCOIN_QT_ORDMANAGER_H

#include <qt/ordinstaller.h>
#include <qt/ordprocess.h>

#include <QObject>
#include <QTemporaryDir>

#include <memory>
#include <optional>

class OrdManager : public QObject
{
    Q_OBJECT

public:
    explicit OrdManager(QString bitcoin_data_dir, QString bitcoin_network_dir, QString chain, QObject* parent = nullptr, std::optional<OrdArtifact> artifact = std::nullopt);
    ~OrdManager() override;

    void start();
    void installArchive(const QString& archive);
    void startIndex();
    void stop();
    bool isReady() const { return !m_executable.isEmpty(); }
    QString executablePath() const { return m_executable; }

Q_SIGNALS:
    void progress(const QString& title, int percentage);
    void installationRequired(const QString& version, const QString& url, const QString& sha256);
    void ready();
    void indexComplete();
    void failed(const QString& message);

private:
    bool verifyExecutable(const QString& path, QString& error) const;

    QString m_bitcoin_data_dir;
    QString m_bitcoin_network_dir;
    QString m_chain;
    QString m_executable;
    OrdArtifact m_artifact;
    std::optional<OrdArtifact> m_artifact_override;
    std::unique_ptr<QTemporaryDir> m_temporary_dir;
    OrdProcess m_index;
    bool m_stopping{false};
};

#endif // BITCOIN_QT_ORDMANAGER_H
