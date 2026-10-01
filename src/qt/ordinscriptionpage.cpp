// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordinscriptionpage.h>

#include <qt/ordmanager.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace {
class RecoveryDialog final : public QDialog
{
public:
    using QDialog::QDialog;

    void reject() override {}

protected:
    void closeEvent(QCloseEvent* event) override { event->ignore(); }
};
} // namespace

OrdInscriptionPage::OrdInscriptionPage(QWidget* parent)
    : QWidget{parent},
      m_drop_label{new QLabel{tr("Drop an image or data file here"), this}},
      m_preview{new QLabel{this}},
      m_file_details{new QLabel{tr("No file selected"), this}},
      m_wallet_status{new QLabel{tr("Waiting for Ord wallet status…"), this}},
      m_balance{new QLabel{tr("Balance unavailable"), this}},
      m_funding_address{new QLabel{this}},
      m_fee_rate{new QLineEdit{this}},
      m_destination{new QLineEdit{this}},
      m_compress{new QCheckBox{tr("Use Ord Brotli compression when beneficial"), this}},
      m_choose_button{new QPushButton{tr("Choose File…"), this}},
      m_refresh_wallet_button{new QPushButton{tr("Refresh"), this}},
      m_create_wallet_button{new QPushButton{tr("Create Ord Wallet…"), this}},
      m_restore_wallet_button{new QPushButton{tr("Restore Ord Wallet…"), this}},
      m_copy_address_button{new QPushButton{tr("Copy Address"), this}},
      m_preview_button{new QPushButton{tr("Preview Cost"), this}},
      m_inscribe_button{new QPushButton{tr("Broadcast Inscription…"), this}},
      m_result{new QTextEdit{this}}
{
    setAcceptDrops(true);

    auto* title{new QLabel{tr("Create an Ordinal inscription"), this}};
    QFont title_font{title->font()};
    title_font.setPointSize(title_font.pointSize() + 4);
    title_font.setBold(true);
    title->setFont(title_font);

    auto* explanation{new QLabel{tr("This uses the dedicated Bitcoin Core wallet named “ord”. It does not spend from the wallet selected in the toolbar. Confirm that the Ord wallet is backed up and funded before continuing."), this}};
    explanation->setWordWrap(true);

    m_wallet_status->setWordWrap(true);
    m_funding_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_funding_address->setWordWrap(true);
    m_copy_address_button->setEnabled(false);
    auto* wallet_buttons{new QHBoxLayout};
    wallet_buttons->addWidget(m_refresh_wallet_button);
    wallet_buttons->addWidget(m_create_wallet_button);
    wallet_buttons->addWidget(m_restore_wallet_button);
    wallet_buttons->addStretch();
    wallet_buttons->addWidget(m_copy_address_button);
    auto* wallet_layout{new QVBoxLayout};
    wallet_layout->addWidget(m_wallet_status);
    wallet_layout->addWidget(m_balance);
    wallet_layout->addWidget(m_funding_address);
    wallet_layout->addLayout(wallet_buttons);
    auto* wallet_group{new QGroupBox{tr("Dedicated Ord wallet"), this}};
    wallet_group->setLayout(wallet_layout);

    m_drop_label->setAlignment(Qt::AlignCenter);
    m_drop_label->setMinimumHeight(90);
    m_drop_label->setFrameShape(QFrame::StyledPanel);
    m_drop_label->setStyleSheet(QStringLiteral("QLabel { border: 2px dashed palette(mid); padding: 20px; }"));

    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(120);
    m_preview->setVisible(false);
    m_file_details->setWordWrap(true);

    auto* fee_validator{new QDoubleValidator{0.1, 1000000.0, 2, m_fee_rate}};
    fee_validator->setLocale(QLocale::c());
    m_fee_rate->setValidator(fee_validator);
    m_fee_rate->setText(QStringLiteral("5"));
    m_fee_rate->setPlaceholderText(tr("sats/vB"));
    m_destination->setPlaceholderText(tr("Optional inscription destination address"));
    m_compress->setChecked(true);

    auto* form{new QFormLayout};
    form->addRow(tr("Fee rate:"), m_fee_rate);
    form->addRow(tr("Destination:"), m_destination);
    form->addRow({}, m_compress);

    auto* buttons{new QHBoxLayout};
    buttons->addWidget(m_choose_button);
    buttons->addStretch();
    buttons->addWidget(m_preview_button);
    buttons->addWidget(m_inscribe_button);

    m_result->setReadOnly(true);
    m_result->setPlaceholderText(tr("Ord command results will appear here."));
    m_result->setMinimumHeight(100);

    auto* layout{new QVBoxLayout{this}};
    layout->addWidget(title);
    layout->addWidget(explanation);
    layout->addWidget(wallet_group);
    layout->addWidget(m_drop_label);
    layout->addWidget(m_preview);
    layout->addWidget(m_file_details);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_result);
    layout->addStretch();

    connect(m_choose_button, &QPushButton::clicked, this, &OrdInscriptionPage::chooseFile);
    connect(m_refresh_wallet_button, &QPushButton::clicked, this, [this] {
        if (!m_manager) return;
        setBusy(true);
        m_manager->refreshWallet();
    });
    connect(m_create_wallet_button, &QPushButton::clicked, this, &OrdInscriptionPage::createWallet);
    connect(m_restore_wallet_button, &QPushButton::clicked, this, &OrdInscriptionPage::restoreWallet);
    connect(m_copy_address_button, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_funding_address->text());
    });
    connect(m_preview_button, &QPushButton::clicked, this, &OrdInscriptionPage::previewInscription);
    connect(m_inscribe_button, &QPushButton::clicked, this, &OrdInscriptionPage::createInscription);
    connect(m_fee_rate, &QLineEdit::textChanged, this, &OrdInscriptionPage::invalidatePreview);
    connect(m_destination, &QLineEdit::textChanged, this, &OrdInscriptionPage::invalidatePreview);
    connect(m_compress, &QCheckBox::toggled, this, &OrdInscriptionPage::invalidatePreview);
    setBusy(false);
}

void OrdInscriptionPage::setOrdManager(OrdManager* manager)
{
    if (m_manager == manager) return;
    if (m_manager) disconnect(m_manager, nullptr, this, nullptr);
    m_manager = manager;
    if (!m_manager) {
        setBusy(false);
        return;
    }
    connect(m_manager, &OrdManager::walletUnavailable, this, [this](const QString& error) {
        m_wallet_available = false;
        m_wallet_status->setText(tr("The dedicated Ord wallet is not ready. Create a new wallet or restore an existing one.\n\n%1").arg(error));
        m_balance->setText(tr("Balance unavailable"));
        m_funding_address->clear();
        m_copy_address_button->setEnabled(false);
        setBusy(false);
    });
    connect(m_manager, &OrdManager::walletCreated, this, [this](const QString& mnemonic) {
        showRecoveryWords(mnemonic);
        setBusy(true);
        m_manager->refreshWallet();
    });
    connect(m_manager, &OrdManager::walletRestored, this, [this] {
        QMessageBox::information(this, tr("Ord wallet restored"), tr("The dedicated Ord wallet was restored. DogMode will now refresh its address and balance."));
        setBusy(true);
        m_manager->refreshWallet();
    });
    connect(m_manager, &OrdManager::walletDetails, this, [this](qint64 cardinal, qint64 total, const QString& address) {
        m_wallet_available = true;
        m_cardinal_balance = cardinal;
        m_wallet_status->setText(tr("The dedicated Ord wallet is ready."));
        m_balance->setText(tr("Spendable cardinal balance: %1 sats    Total wallet balance: %2 sats").arg(cardinal).arg(total));
        m_funding_address->setText(address);
        m_copy_address_button->setEnabled(true);
        m_create_wallet_button->setEnabled(false);
        m_restore_wallet_button->setEnabled(false);
        setBusy(false);
    });
    connect(m_manager, &OrdManager::inscriptionStarted, this, [this] {
        setBusy(true);
        m_result->setPlainText(m_preview_pending ? tr("Calculating inscription cost without broadcasting…") : tr("Creating and broadcasting inscription…"));
    });
    connect(m_manager, &OrdManager::inscriptionPreview, this, [this](qint64 total_fees, const QString&) {
        m_preview_pending = false;
        m_preview_fees = total_fees;
        m_preview_key = inputKey();
        const qint64 required{total_fees + 10000};
        m_result->setPlainText(tr("Cost preview\n\nMining fees: %1 sats\nDefault inscription postage: 10,000 sats\nApproximate required cardinal balance: %2 sats\nAvailable cardinal balance: %3 sats")
                                   .arg(total_fees).arg(required).arg(m_cardinal_balance));
        setBusy(false);
        if (m_cardinal_balance < required) {
            QMessageBox::warning(this, tr("Insufficient Ord wallet balance"), tr("The Ord wallet needs approximately %1 sats but currently has %2 spendable cardinal sats. Fund the address shown above, confirm the transaction, then refresh the wallet.").arg(required).arg(m_cardinal_balance));
        }
    });
    connect(m_manager, &OrdManager::inscriptionComplete, this, [this](const QString& result) {
        m_preview_pending = false;
        m_result->setPlainText(result);
        invalidatePreview();
        setBusy(true);
        m_manager->refreshWallet();
        QMessageBox::information(this, tr("Inscription created"), tr("Ord created and broadcast the inscription transactions. Save the command result shown on this page."));
    });
    connect(m_manager, &OrdManager::inscriptionFailed, this, [this](const QString& error) {
        m_preview_pending = false;
        setBusy(false);
        m_result->setPlainText(error);
        QMessageBox::critical(this, tr("Inscription failed"), error);
    });
    setBusy(m_manager->isInscribing());
    if (m_manager->isReady()) {
        setBusy(true);
        m_manager->refreshWallet();
    }
}

void OrdInscriptionPage::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().front().isLocalFile()) {
        event->acceptProposedAction();
    }
}

void OrdInscriptionPage::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls() || event->mimeData()->urls().size() != 1 || !event->mimeData()->urls().front().isLocalFile()) return;
    setFile(event->mimeData()->urls().front().toLocalFile());
    event->acceptProposedAction();
}

void OrdInscriptionPage::chooseFile()
{
    const QString path{QFileDialog::getOpenFileName(this, tr("Select inscription file"))};
    if (!path.isEmpty()) setFile(path);
}

void OrdInscriptionPage::setFile(const QString& path)
{
    const QFileInfo info{path};
    if (!info.isFile() || !info.isReadable()) {
        QMessageBox::warning(this, tr("Invalid inscription file"), tr("Select a readable regular file."));
        return;
    }
    m_file = info.absoluteFilePath();
    invalidatePreview();
    m_file_details->setText(tr("%1 — %2 bytes").arg(info.fileName()).arg(info.size()));
    m_drop_label->setText(info.fileName());

    const QPixmap image{m_file};
    if (!image.isNull()) {
        m_preview->setPixmap(image.scaled(420, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_preview->setVisible(true);
    } else {
        m_preview->clear();
        m_preview->setVisible(false);
    }
    setBusy(m_manager && m_manager->isInscribing());
}

void OrdInscriptionPage::createWallet()
{
    if (!m_manager) return;
    const QString warning{tr("Create a new dedicated Ord wallet?\n\nDogMode will show the recovery words exactly once. The wallet cannot be recovered if those words are lost.")};
    if (QMessageBox::warning(this, tr("Create Ord wallet"), warning, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes) {
        setBusy(true);
        m_manager->createWallet();
    }
}

void OrdInscriptionPage::restoreWallet()
{
    if (!m_manager) return;
    QDialog dialog{this};
    dialog.setWindowTitle(tr("Restore Ord wallet"));
    auto* mnemonic{new QPlainTextEdit{&dialog}};
    mnemonic->setPlaceholderText(tr("Enter the recovery words in order"));
    auto* passphrase{new QLineEdit{&dialog}};
    passphrase->setEchoMode(QLineEdit::Password);
    passphrase->setPlaceholderText(tr("Optional BIP39 passphrase"));
    auto* form{new QFormLayout};
    form->addRow(tr("Recovery words:"), mnemonic);
    form->addRow(tr("Passphrase:"), passphrase);
    auto* buttons{new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog}};
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Restore Wallet"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout{new QVBoxLayout{&dialog}};
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || mnemonic->toPlainText().trimmed().isEmpty()) return;
    setBusy(true);
    m_manager->restoreWallet(mnemonic->toPlainText(), passphrase->text());
    mnemonic->clear();
    passphrase->clear();
}

void OrdInscriptionPage::showRecoveryWords(const QString& mnemonic)
{
    RecoveryDialog dialog{this};
    dialog.setWindowTitle(tr("Back up Ord wallet recovery words"));
    dialog.setWindowFlag(Qt::WindowCloseButtonHint, false);
    auto* warning{new QLabel{tr("Write these recovery words down in order and store them offline. Anyone with these words can spend the Ord wallet. They will not be shown again."), &dialog}};
    warning->setWordWrap(true);
    auto* words{new QPlainTextEdit{mnemonic, &dialog}};
    words->setReadOnly(true);
    auto* copy{new QPushButton{tr("Copy Recovery Words"), &dialog}};
    auto* acknowledged{new QCheckBox{tr("I have recorded and safely stored these recovery words."), &dialog}};
    auto* done{new QPushButton{tr("Continue"), &dialog}};
    done->setEnabled(false);
    connect(copy, &QPushButton::clicked, &dialog, [mnemonic] { QApplication::clipboard()->setText(mnemonic); });
    connect(acknowledged, &QCheckBox::toggled, done, &QPushButton::setEnabled);
    connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto* layout{new QVBoxLayout{&dialog}};
    layout->addWidget(warning);
    layout->addWidget(words);
    layout->addWidget(copy);
    layout->addWidget(acknowledged);
    layout->addWidget(done);
    dialog.exec();
    if (QApplication::clipboard()->text() == mnemonic) QApplication::clipboard()->clear();
    words->clear();
}

void OrdInscriptionPage::previewInscription()
{
    if (!m_manager || !m_wallet_available || m_file.isEmpty() || m_fee_rate->text().isEmpty()) return;
    invalidatePreview();
    m_preview_pending = true;
    m_manager->previewInscription(m_file, m_fee_rate->text(), m_destination->text().trimmed(), m_compress->isChecked());
}

void OrdInscriptionPage::createInscription()
{
    if (!m_manager || !m_manager->isReady()) {
        QMessageBox::warning(this, tr("Ord unavailable"), tr("Install and synchronize Ord before creating an inscription."));
        return;
    }
    if (m_file.isEmpty() || m_fee_rate->text().isEmpty()) return;
    if (m_preview_key != inputKey()) {
        QMessageBox::warning(this, tr("Cost preview required"), tr("Preview the current inscription cost before broadcasting."));
        return;
    }

    const QFileInfo info{m_file};
    const QString destination{m_destination->text().trimmed()};
    const QString confirmation{tr("Create an inscription from %1 (%2 bytes) using the dedicated “ord” wallet at %3 sats/vB?\n\nEstimated mining fees: %4 sats\nDefault postage: 10,000 sats\n\nThis creates and broadcasts Bitcoin transactions. This action cannot be undone.")
                                   .arg(info.fileName()).arg(info.size()).arg(m_fee_rate->text()).arg(m_preview_fees)};
    if (QMessageBox::question(this, tr("Confirm inscription"), confirmation, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;

    m_manager->inscribe(m_file, m_fee_rate->text(), destination, m_compress->isChecked());
}

void OrdInscriptionPage::invalidatePreview()
{
    m_preview_key.clear();
    m_preview_fees = 0;
    m_inscribe_button->setEnabled(false);
}

QString OrdInscriptionPage::inputKey() const
{
    const QFileInfo info{m_file};
    QFile file{m_file};
    QCryptographicHash file_hash{QCryptographicHash::Sha256};
    if (!file.open(QIODevice::ReadOnly) || !file_hash.addData(&file)) return {};
    const QByteArray value{m_file.toUtf8() + '\0' + file_hash.result() + '\0' + QByteArray::number(info.size()) + '\0' + m_fee_rate->text().toUtf8() + '\0' + m_destination->text().trimmed().toUtf8() + '\0' + QByteArray::number(m_compress->isChecked())};
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

void OrdInscriptionPage::setBusy(bool busy)
{
    m_choose_button->setEnabled(!busy);
    m_fee_rate->setEnabled(!busy);
    m_destination->setEnabled(!busy);
    m_compress->setEnabled(!busy);
    m_refresh_wallet_button->setEnabled(!busy && m_manager && m_manager->isReady());
    m_create_wallet_button->setEnabled(!busy && !m_wallet_available);
    m_restore_wallet_button->setEnabled(!busy && !m_wallet_available);
    m_preview_button->setEnabled(!busy && m_wallet_available && !m_file.isEmpty() && !m_fee_rate->text().isEmpty());
    m_inscribe_button->setEnabled(!busy && m_wallet_available && !m_preview_key.isEmpty() && m_preview_key == inputKey());
}
