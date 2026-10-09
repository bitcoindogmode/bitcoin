// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDRECOVERYDIALOG_H
#define BITCOIN_QT_ORDRECOVERYDIALOG_H

#include <QDialog>

class QLabel;

/** Application-owned backup prompt. No text selection or clipboard actions. */
class OrdRecoveryDialog final : public QDialog
{
public:
    explicit OrdRecoveryDialog(const QString& mnemonic, QWidget* parent = nullptr);
    ~OrdRecoveryDialog() override;
    void reject() override {}

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QLabel* m_words;
};

#endif // BITCOIN_QT_ORDRECOVERYDIALOG_H
