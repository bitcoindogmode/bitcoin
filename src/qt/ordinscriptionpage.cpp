// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordinscriptionpage.h>

#include <qt/ordmanager.h>

#include <QCheckBox>
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
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

OrdInscriptionPage::OrdInscriptionPage(QWidget* parent)
    : QWidget{parent},
      m_drop_label{new QLabel{tr("Drop an image or data file here"), this}},
      m_preview{new QLabel{this}},
      m_file_details{new QLabel{tr("No file selected"), this}},
      m_fee_rate{new QLineEdit{this}},
      m_destination{new QLineEdit{this}},
      m_compress{new QCheckBox{tr("Use Ord Brotli compression when beneficial"), this}},
      m_choose_button{new QPushButton{tr("Choose File…"), this}},
      m_inscribe_button{new QPushButton{tr("Create Inscription…"), this}},
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
    buttons->addWidget(m_inscribe_button);

    m_result->setReadOnly(true);
    m_result->setPlaceholderText(tr("Ord command results will appear here."));
    m_result->setMinimumHeight(100);

    auto* layout{new QVBoxLayout{this}};
    layout->addWidget(title);
    layout->addWidget(explanation);
    layout->addWidget(m_drop_label);
    layout->addWidget(m_preview);
    layout->addWidget(m_file_details);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_result);
    layout->addStretch();

    connect(m_choose_button, &QPushButton::clicked, this, &OrdInscriptionPage::chooseFile);
    connect(m_inscribe_button, &QPushButton::clicked, this, &OrdInscriptionPage::createInscription);
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
    connect(m_manager, &OrdManager::inscriptionStarted, this, [this] {
        setBusy(true);
        m_result->setPlainText(tr("Creating inscription…"));
    });
    connect(m_manager, &OrdManager::inscriptionComplete, this, [this](const QString& result) {
        setBusy(false);
        m_result->setPlainText(result);
        QMessageBox::information(this, tr("Inscription created"), tr("Ord created and broadcast the inscription transactions. Save the command result shown on this page."));
    });
    connect(m_manager, &OrdManager::inscriptionFailed, this, [this](const QString& error) {
        setBusy(false);
        m_result->setPlainText(error);
        QMessageBox::critical(this, tr("Inscription failed"), error);
    });
    setBusy(m_manager->isInscribing());
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

void OrdInscriptionPage::createInscription()
{
    if (!m_manager || !m_manager->isReady()) {
        QMessageBox::warning(this, tr("Ord unavailable"), tr("Install and synchronize Ord before creating an inscription."));
        return;
    }
    if (m_file.isEmpty() || m_fee_rate->text().isEmpty()) return;

    const QFileInfo info{m_file};
    const QString destination{m_destination->text().trimmed()};
    const QString confirmation{tr("Create an inscription from %1 (%2 bytes) using the dedicated “ord” wallet at %3 sats/vB?\n\nThis creates and broadcasts Bitcoin transactions and pays mining fees. This action cannot be undone.")
                                   .arg(info.fileName()).arg(info.size()).arg(m_fee_rate->text())};
    if (QMessageBox::question(this, tr("Confirm inscription"), confirmation, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;

    m_manager->inscribe(m_file, m_fee_rate->text(), destination, m_compress->isChecked());
}

void OrdInscriptionPage::setBusy(bool busy)
{
    m_choose_button->setEnabled(!busy);
    m_fee_rate->setEnabled(!busy);
    m_destination->setEnabled(!busy);
    m_compress->setEnabled(!busy);
    m_inscribe_button->setEnabled(!busy && !m_file.isEmpty());
}
