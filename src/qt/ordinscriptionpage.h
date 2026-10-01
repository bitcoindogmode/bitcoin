// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDINSCRIPTIONPAGE_H
#define BITCOIN_QT_ORDINSCRIPTIONPAGE_H

#include <QWidget>

class OrdManager;

QT_BEGIN_NAMESPACE
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QDragEnterEvent;
class QDropEvent;
QT_END_NAMESPACE

class OrdInscriptionPage : public QWidget
{
    Q_OBJECT

public:
    explicit OrdInscriptionPage(QWidget* parent = nullptr);
    void setOrdManager(OrdManager* manager);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void chooseFile();
    void setFile(const QString& path);
    void createWallet();
    void restoreWallet();
    void showRecoveryWords(const QString& mnemonic);
    void previewInscription();
    void createInscription();
    void invalidatePreview();
    QString inputKey() const;
    void setBusy(bool busy);

    OrdManager* m_manager{nullptr};
    QString m_file;
    QLabel* m_drop_label;
    QLabel* m_preview;
    QLabel* m_file_details;
    QLabel* m_wallet_status;
    QLabel* m_balance;
    QLabel* m_funding_address;
    QLineEdit* m_fee_rate;
    QLineEdit* m_destination;
    QCheckBox* m_compress;
    QPushButton* m_choose_button;
    QPushButton* m_refresh_wallet_button;
    QPushButton* m_create_wallet_button;
    QPushButton* m_restore_wallet_button;
    QPushButton* m_copy_address_button;
    QPushButton* m_preview_button;
    QPushButton* m_inscribe_button;
    QTextEdit* m_result;
    QString m_preview_key;
    qint64 m_preview_fees{0};
    qint64 m_cardinal_balance{0};
    bool m_wallet_available{false};
    bool m_preview_pending{false};
};

#endif // BITCOIN_QT_ORDINSCRIPTIONPAGE_H
