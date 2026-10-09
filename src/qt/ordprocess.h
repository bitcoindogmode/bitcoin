// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDPROCESS_H
#define BITCOIN_QT_ORDPROCESS_H

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>

#include <support/allocators/secure.h>

class OrdProcess : public QObject
{
    Q_OBJECT

public:
    explicit OrdProcess(QObject* parent = nullptr);
    ~OrdProcess() override;
    void start(const QString& program, const QStringList& arguments, SecureString input = {});
    void stop();
    bool isRunning() const;
    QByteArray output() const { return m_output; }
    QByteArray takeOutput();
    static QProcessEnvironment SafeEnvironment();

Q_SIGNALS:
    void completed(int exit_code);
    void failed(const QString& message);

private:
    void readOutput();
    void clearInput();

    static constexpr qsizetype MAX_OUTPUT_BYTES{1024 * 1024};
    QProcess m_process;
    QByteArray m_output;
    SecureString m_input;
    quint64 m_invocation{0};
};

#endif // BITCOIN_QT_ORDPROCESS_H
