// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDRPCGATE_H
#define BITCOIN_QT_ORDRPCGATE_H

#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTemporaryFile>
#include <univalue.h>

#include <functional>

/** Executor is always scoped to /wallet/ord, never a Qt-selected wallet. */
using OrdRpcExecutor = std::function<UniValue(const std::string&, const UniValue&)>;

/** Per-preview authenticated RPC gate. The input allowlist never expands. */
class OrdRpcGate final : public QObject
{
public:
    OrdRpcGate(OrdRpcExecutor execute, QSet<QString> allowed_inputs, const QString& directory, QObject* parent = nullptr);
    ~OrdRpcGate() override;
    bool isReady() const;
    QString url() const;
    QString cookiePath() const;
    UniValue dispatch(const std::string& method, const UniValue& params);

private:
    OrdRpcExecutor m_execute;
    const QSet<QString> m_allowed_inputs;
    QSet<QString> m_commit_outputs;
    QTcpServer m_server;
    QTemporaryFile m_cookie;
    QByteArray m_authorization;
    int m_connections{0};
};

#endif // BITCOIN_QT_ORDRPCGATE_H
