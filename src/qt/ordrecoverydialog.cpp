// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordrecoverydialog.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

OrdRecoveryDialog::OrdRecoveryDialog(const QString& mnemonic, QWidget* parent) : QDialog{parent}, m_words{new QLabel{mnemonic, this}}
{
    setWindowTitle(tr("Back up Ord wallet recovery words"));
    setWindowFlag(Qt::WindowCloseButtonHint, false);
    setWindowModality(Qt::ApplicationModal);
    auto* warning{new QLabel{tr("Write these recovery words down in order and store them offline. Anyone with these words can spend the Ord wallet. They will not be shown again."), this}};
    warning->setWordWrap(true);
    m_words->setObjectName(QStringLiteral("recoveryWords"));
    m_words->setTextFormat(Qt::PlainText);
    m_words->setWordWrap(true);
    m_words->setTextInteractionFlags(Qt::NoTextInteraction);
    m_words->setContextMenuPolicy(Qt::NoContextMenu);
    m_words->setFocusPolicy(Qt::NoFocus);
    auto* acknowledged{new QCheckBox{tr("I have recorded and safely stored these recovery words."), this}};
    acknowledged->setObjectName(QStringLiteral("backupAcknowledged"));
    auto* done{new QPushButton{tr("Continue"), this}};
    done->setObjectName(QStringLiteral("backupContinue"));
    done->setEnabled(false);
    connect(acknowledged, &QCheckBox::toggled, done, &QPushButton::setEnabled);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    auto* layout{new QVBoxLayout{this}};
    layout->addWidget(warning);
    layout->addWidget(m_words);
    layout->addWidget(acknowledged);
    layout->addWidget(done);
}

OrdRecoveryDialog::~OrdRecoveryDialog()
{
    m_words->setText(QString(m_words->text().size(), QChar{'\0'}));
    m_words->clear();
}

void OrdRecoveryDialog::closeEvent(QCloseEvent* event)
{
    event->ignore();
}
