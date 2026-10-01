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

class QTemporaryFile;

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
    void restoreWallet(const QString& mnemonic);
    quint64 previewInscription(const QString& file, const QString& fee_rate, const QString& destination, bool compress);
    bool previewMatches(quint64 request_id, const QString& file, const QString& fee_rate, const QString& destination, bool compress, QString& error) const;
    void invalidatePreview(quint64 request_id);
    void inscribe(quint64 request_id);
    void cancelInscription();
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
    void inscriptionPreview(quint64 request_id, qint64 total_fees, const QString& input_key, const QString& result);
    void inscriptionStarted(quint64 request_id);
    void inscriptionComplete(quint64 request_id, const QString& result);
    void failed(const QString& message);
    void inscriptionFailed(quint64 request_id, const QString& message);

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
    enum class InscriptionOperation {
        NONE,
        PREVIEW,
        REVALIDATE,
        BROADCAST,
    };

    bool verifyExecutable(const QString& path, QString& error) const;
    QStringList baseArguments() const;
    QStringList inscriptionArguments(const QString& file, const QString& fee_rate, const QString& destination, bool compress, bool dry_run) const;
    void startWalletOperation(WalletOperation operation, const QStringList& arguments, SecureString input = {});
    bool validateInscriptionSettings(const QString& fee_rate, const QString& destination, QString& error) const;
    bool createInscriptionSnapshot(const QString& file, const QString& fee_rate, const QString& destination, bool compress, QString& error);
    QString currentInputKey(const QString& file, const QString& fee_rate, const QString& destination, bool compress, QString& error) const;
    void runInscriptionPreview();
    void runInscriptionBroadcast();
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
    InscriptionOperation m_inscription_operation{InscriptionOperation::NONE};
    quint64 m_next_request_id{1};
    quint64 m_inscription_request_id{0};
    bool m_preview_authorized{false};
    qint64 m_preview_total_fees{0};
    QString m_preview_input_key;
    std::unique_ptr<QTemporaryFile> m_preview_snapshot;
    QString m_preview_file;
    QString m_preview_original_file;
    QString m_preview_fee_rate;
    QString m_preview_destination;
    bool m_preview_compress{false};
    bool m_cancel_requested{false};
    quint16 m_server_port{0};
    QString m_server_url;
    bool m_stopping{false};
};

#endif // BITCOIN_QT_ORDMANAGER_H
