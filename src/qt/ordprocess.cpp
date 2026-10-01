// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordprocess.h>

#include <support/cleanse.h>

#include <algorithm>
#include <utility>

#include <QProcessEnvironment>
#include <QTimer>

namespace {
void AddLoopbackNoProxy(QProcessEnvironment& environment, const QString& name)
{
    QStringList entries{environment.value(name).split(',', Qt::SkipEmptyParts)};
    if (!entries.contains(QStringLiteral("127.0.0.1"))) entries.append(QStringLiteral("127.0.0.1"));
    if (!entries.contains(QStringLiteral("localhost"))) entries.append(QStringLiteral("localhost"));
    environment.insert(name, entries.join(','));
}
} // namespace

OrdProcess::OrdProcess(QObject* parent) : QObject{parent}
{
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
    AddLoopbackNoProxy(environment, QStringLiteral("NO_PROXY"));
    AddLoopbackNoProxy(environment, QStringLiteral("no_proxy"));
    m_process.setProcessEnvironment(environment);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &OrdProcess::readOutput);
    connect(&m_process, &QProcess::started, this, [this] {
        if (!m_input.empty()) m_process.write(m_input.data(), static_cast<qint64>(m_input.size()));
        m_process.closeWriteChannel();
        clearInput();
    });
    connect(&m_process, &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus status) {
        readOutput();
        clearInput();
        if (status == QProcess::CrashExit) {
            Q_EMIT failed(tr("Ord process crashed."));
        } else {
            Q_EMIT completed(exit_code);
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        clearInput();
        if (error != QProcess::Crashed) Q_EMIT failed(m_process.errorString());
    });
}

OrdProcess::~OrdProcess()
{
    if (isRunning()) {
        m_process.terminate();
        if (!m_process.waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(3000);
        }
    }
    clearInput();
    if (!m_output.isEmpty()) m_output.fill('\0');
}

void OrdProcess::start(const QString& program, const QStringList& arguments, SecureString input)
{
    if (isRunning()) {
        Q_EMIT failed(tr("Ord process is already running."));
        return;
    }
    if (!m_output.isEmpty()) m_output.fill('\0');
    m_output.clear();
    m_input = std::move(input);
    m_process.start(program, arguments);
}

QByteArray OrdProcess::takeOutput()
{
    return std::exchange(m_output, {});
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
    QByteArray incoming{m_process.readAll()};
    const qsizetype available{MAX_OUTPUT_BYTES - m_output.size()};
    if (available > 0) m_output.append(incoming.first(std::min(available, incoming.size())));
    if (!incoming.isEmpty()) memory_cleanse(incoming.data(), static_cast<size_t>(incoming.size()));
}

void OrdProcess::clearInput()
{
    SecureString{}.swap(m_input);
}
