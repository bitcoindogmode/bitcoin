// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordrpcgate.h>

#include <core_io.h>
#include <primitives/transaction.h>
#include <random.h>
#include <support/cleanse.h>
#include <util/strencodings.h>

#include <QDir>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace {
constexpr qsizetype MAX_RPC_BYTES{1024 * 1024};

QString Outpoint(const COutPoint& output)
{
    return QString::fromStdString(output.hash.ToString()) + ':' + QString::number(output.n);
}

void Cleanse(QByteArray& value)
{
    if (!value.isEmpty()) memory_cleanse(value.data(), value.size());
    value.clear();
}
} // namespace

OrdRpcGate::OrdRpcGate(OrdRpcExecutor execute, QSet<QString> allowed_inputs, const QString& directory, QObject* parent)
    : QObject{parent}, m_execute{std::move(execute)}, m_allowed_inputs{std::move(allowed_inputs)},
      m_server{this}, m_cookie{QDir{directory}.filePath(QStringLiteral("rpc-cookie-XXXXXX"))}
{
    if (!m_execute || !m_cookie.open()) return;
    QByteArray cookie{"ord:" + QByteArray::fromStdString(GetRandHash().ToString())};
    m_authorization = "Basic " + cookie.toBase64();
    const bool written{m_cookie.write(cookie) == cookie.size() && m_cookie.flush()};
    Cleanse(cookie);
    if (!written || !m_server.listen(QHostAddress::LocalHost, 0)) return;

    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            if (m_connections >= 8) { socket->abort(); socket->deleteLater(); continue; }
            ++m_connections;
            socket->setReadBufferSize(MAX_RPC_BYTES + 8192);
            auto bytes{std::make_shared<QByteArray>()};
            connect(socket, &QTcpSocket::disconnected, socket, [this, socket, bytes] {
                --m_connections;
                Cleanse(*bytes);
                socket->deleteLater();
            });
            QTimer::singleShot(10000, socket, [socket] { socket->abort(); });
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                bytes->append(socket->readAll());
                const auto deny{[&] { Cleanse(*bytes); socket->abort(); }};
                if (bytes->size() > MAX_RPC_BYTES + 8192) { deny(); return; }
                const qsizetype end{bytes->indexOf("\r\n\r\n")};
                if (end < 0) { if (bytes->size() > 8192) deny(); return; }
                if (end > 8192) { deny(); return; }
                const QList<QByteArray> headers{bytes->first(end).split('\n')};
                if (headers.isEmpty() || (headers.first().trimmed() != "POST /wallet/ord HTTP/1.1" && headers.first().trimmed() != "POST /wallet/ord HTTP/1.0")) { deny(); return; }
                qlonglong length{-1};
                bool authenticated{false};
                bool has_length{false};
                bool has_authorization{false};
                for (const QByteArray& line : headers.sliced(1)) {
                    const qsizetype colon{line.indexOf(':')};
                    if (colon < 1) { deny(); return; }
                    const QByteArray name{line.first(colon).trimmed().toLower()};
                    const QByteArray value{line.sliced(colon + 1).trimmed()};
                    if (name == "authorization") {
                        if (has_authorization) { deny(); return; }
                        has_authorization = true;
                        authenticated = TimingResistantEqual(std::string_view{value.constData(), static_cast<size_t>(value.size())},
                                                             std::string_view{m_authorization.constData(), static_cast<size_t>(m_authorization.size())});
                    }
                    if (name == "transfer-encoding") { deny(); return; }
                    if (name == "content-length") {
                        bool ok{false};
                        length = value.toLongLong(&ok);
                        if (!ok || has_length || length <= 0 || length > MAX_RPC_BYTES) { deny(); return; }
                        has_length = true;
                    }
                }
                if (!authenticated || !has_length) { deny(); return; }
                if (bytes->size() < end + 4 + length) return;
                if (bytes->size() != end + 4 + length) { deny(); return; }
                UniValue request;
                const bool parsed{request.read(std::string_view{bytes->constData() + end + 4, static_cast<size_t>(length)})};
                Cleanse(*bytes);
                if (!parsed || !request.isObject()) { deny(); return; }
                UniValue reply{UniValue::VOBJ};
                reply.pushKV("id", request["id"]);
                try {
                    if (!request["method"].isStr() || !request["params"].isArray()) throw std::runtime_error{"invalid RPC request"};
                    reply.pushKV("result", dispatch(request["method"].get_str(), request["params"]));
                    reply.pushKV("error", UniValue{});
                } catch (...) {
                    // Never echo RPC exceptions or input: importdescriptors can contain secrets.
                    UniValue error{UniValue::VOBJ};
                    error.pushKV("code", -32000);
                    error.pushKV("message", "Ord RPC safety gate rejected the request or the wallet command failed");
                    reply.pushKV("result", UniValue{});
                    reply.pushKV("error", error);
                }
                QByteArray body{QByteArray::fromStdString(reply.write())};
                if (body.size() > MAX_RPC_BYTES) { Cleanse(body); deny(); return; }
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                Cleanse(body);
                socket->disconnectFromHost();
            });
        }
    });
}

OrdRpcGate::~OrdRpcGate()
{
    Cleanse(m_authorization);
}

bool OrdRpcGate::isReady() const { return m_execute && m_cookie.isOpen() && m_server.isListening(); }
QString OrdRpcGate::url() const { return QStringLiteral("127.0.0.1:%1").arg(m_server.serverPort()); }
QString OrdRpcGate::cookiePath() const { return m_cookie.fileName(); }

UniValue OrdRpcGate::dispatch(const std::string& method, const UniValue& params)
{
    static const QSet<QString> METHODS{
        "getblockchaininfo", "getnetworkinfo", "listwallets", "loadwallet", "getwalletinfo", "listdescriptors",
        "getblockcount", "listunspent", "listlockunspent", "gettxout", "getrawchangeaddress",
        "walletprocesspsbt", "signrawtransactionwithwallet", "getdescriptorinfo", "importdescriptors", "sendrawtransaction",
    };
    if (!m_execute || !params.isArray() || !METHODS.contains(QString::fromStdString(method))) throw std::runtime_error{"RPC method not allowed"};
    if (method == "loadwallet" && (params.empty() || !params[0].isStr() || params[0].get_str() != "ord")) throw std::runtime_error{"wrong wallet"};
    if (method == "walletprocesspsbt" && (params.size() < 2 || !params[1].isBool() || params[1].get_bool())) throw std::runtime_error{"dry-run signing forbidden"};

    // Check the actual transaction at the last boundary before it reaches Core.
    // Only commits funded entirely by the frozen safe set, and their reveals, pass.
    if (method == "sendrawtransaction") {
        CMutableTransaction tx;
        if (params.empty() || !params[0].isStr() || !DecodeHexTx(tx, params[0].get_str()) || tx.vin.empty()) throw std::runtime_error{"invalid transaction"};
        const bool commit{std::all_of(tx.vin.begin(), tx.vin.end(), [this](const CTxIn& in) { return m_allowed_inputs.contains(Outpoint(in.prevout)); })};
        const bool reveal{tx.vin.size() == 1 && m_commit_outputs.contains(Outpoint(tx.vin.front().prevout))};
        if (!commit && !reveal) throw std::runtime_error{"unapproved funding input"};
        UniValue result{m_execute(method, params)};
        if (commit) {
            for (size_t index = 0; index < tx.vout.size(); ++index) {
                m_commit_outputs.insert(QString::fromStdString(tx.GetHash().ToString()) + ':' + QString::number(index));
            }
        }
        return result;
    }
    UniValue result{m_execute(method, params)};
    if (method == "listunspent" || method == "listlockunspent") {
        if (!result.isArray()) throw std::runtime_error{"invalid unspent response"};
        UniValue filtered{UniValue::VARR};
        for (const UniValue& coin : result.getValues()) {
            if (!coin["txid"].isStr() || !coin["vout"].isNum()) throw std::runtime_error{"invalid outpoint"};
            const QString output{QString::fromStdString(coin["txid"].get_str()) + ':' + QString::number(coin["vout"].getInt<uint32_t>())};
            if (m_allowed_inputs.contains(output)) filtered.push_back(coin);
        }
        return filtered;
    }
    return result;
}
