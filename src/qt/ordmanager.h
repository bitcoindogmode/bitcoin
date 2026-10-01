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
    void refreshWallet();
    void createWallet();
    void restoreWallet(const QString& mnemonic, const QString& passphrase);
    void previewInscription(const QString& file, const QString& fee_rate, const QString& destination, bool compress);
    void inscribe(const QString& file, const QString& fee_rate, const QString& destination, bool compress);
    void stop();
    bool isReady() const { return !m_executable.isEmpty(); }
    bool isInscribing() const { return m_inscription.isRunning(); }
    QString executablePath() const { return m_executable; }

Q_SIGNALS:
    void progress(const QString& title, int percentage);
    void installationRequired(const QString& version, const QString& url, const QString& sha256);
    void ready();
    void indexComplete();
    void walletUnavailable(const QString& message);
    void walletCreated(const QString& mnemonic);
    void walletRestored();
    void walletDetails(qint64 cardinal, qint64 total, const QString& address);
    void inscriptionPreview(qint64 total_fees, const QString& result);
    void inscriptionStarted();
    void inscriptionComplete(const QString& result);
    void failed(const QString& message);
    void inscriptionFailed(const QString& message);

private:
    enum class WalletOperation {
        NONE,
        BALANCE,
        RECEIVE,
        CREATE,
        RESTORE,
    };
    enum class AfterIndex {
        NONE,
        REFRESH_WALLET,
        PREVIEW_INSCRIPTION,
    };

    bool verifyExecutable(const QString& path, QString& error) const;
    QStringList baseArguments() const;
    QStringList inscriptionArguments(const QString& file, const QString& fee_rate, const QString& destination, bool compress, bool dry_run) const;
    void startWalletOperation(WalletOperation operation, const QStringList& arguments, const QByteArray& input = {});
    void runInscriptionPreview();
    void startServer();
    void waitForServer(int attempts_remaining = 100);
    void continueAfterIndex();

    QString m_bitcoin_data_dir;
    QString m_bitcoin_network_dir;
    QString m_chain;
    QString m_executable;
    OrdArtifact m_artifact;
    std::optional<OrdArtifact> m_artifact_override;
    std::unique_ptr<QTemporaryDir> m_temporary_dir;
    OrdProcess m_index;
    OrdProcess m_server;
    OrdProcess m_wallet;
    OrdProcess m_inscription;
    WalletOperation m_wallet_operation{WalletOperation::NONE};
    AfterIndex m_after_index{AfterIndex::NONE};
    qint64 m_cardinal_balance{0};
    qint64 m_total_balance{0};
    QString m_funding_address;
    bool m_inscription_preview{false};
    QString m_preview_file;
    QString m_preview_fee_rate;
    QString m_preview_destination;
    bool m_preview_compress{false};
    quint16 m_server_port{0};
    QString m_server_url;
    bool m_stopping{false};
};

#endif // BITCOIN_QT_ORDMANAGER_H
