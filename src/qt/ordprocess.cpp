// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordprocess.h>

#include <algorithm>

#include <QTimer>

OrdProcess::OrdProcess(QObject* parent) : QObject{parent}
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &OrdProcess::readOutput);
    connect(&m_process, &QProcess::started, this, [this] {
        if (!m_input.isEmpty()) m_process.write(m_input);
        m_process.closeWriteChannel();
        m_input.clear();
    });
    connect(&m_process, &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus status) {
        readOutput();
        if (status == QProcess::CrashExit) {
            Q_EMIT failed(tr("Ord process crashed."));
        } else {
            Q_EMIT completed(exit_code);
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::Crashed) Q_EMIT failed(m_process.errorString());
    });
}

OrdProcess::~OrdProcess()
{
    if (!isRunning()) return;
    m_process.terminate();
    if (!m_process.waitForFinished(3000)) {
        m_process.kill();
        m_process.waitForFinished();
    }
}

void OrdProcess::start(const QString& program, const QStringList& arguments, const QByteArray& input)
{
    if (isRunning()) {
        Q_EMIT failed(tr("Ord process is already running."));
        return;
    }
    m_output.clear();
    m_input = input;
    m_process.start(program, arguments);
}

void OrdProcess::stop()
{
    if (!isRunning()) return;
    m_process.terminate();
    QTimer::singleShot(3000, &m_process, [this] {
        if (isRunning()) m_process.kill();
    });
}

bool OrdProcess::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void OrdProcess::readOutput()
{
    const QByteArray incoming{m_process.readAll()};
    const qsizetype available{MAX_OUTPUT_BYTES - m_output.size()};
    if (available > 0) m_output.append(incoming.first(std::min(available, incoming.size())));
    Q_EMIT outputChanged(m_output);
}
