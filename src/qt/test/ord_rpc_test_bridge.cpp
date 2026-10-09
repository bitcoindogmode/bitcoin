// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordrpcgate.h>
#include <util/translation.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTextStream>

#include <stdexcept>

const TranslateFn G_TRANSLATION_FUN{nullptr};

/** Test-only adapter: exercise the production gate against a real regtest node. */
int main(int argc, char* argv[])
{
    QCoreApplication app{argc, argv};
    if (argc < 4) return 1;
    QFile cookie{app.arguments().at(2)};
    if (!cookie.open(QIODevice::ReadOnly)) return 2;
    const QByteArray auth{"Basic " + cookie.readAll().trimmed().toBase64()};
    const QUrl upstream{app.arguments().at(1) + "/wallet/ord"};
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    QTemporaryDir dir;
    QSet<QString> allowed;
    for (const QString& output : app.arguments().sliced(3)) allowed.insert(output);
    OrdRpcGate gate{[&](const std::string& method, const UniValue& params) {
        UniValue request{UniValue::VOBJ};
        request.pushKV("id", 1);
        request.pushKV("method", method);
        request.pushKV("params", params);
        QNetworkRequest http{upstream};
        http.setRawHeader("Authorization", auth);
        http.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        http.setTransferTimeout(10000);
        auto* reply{network.post(http, QByteArray::fromStdString(request.write()))};
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        UniValue response;
        const bool parsed{response.read(reply->readAll().toStdString())};
        reply->deleteLater();
        if (!parsed || !response["error"].isNull()) throw std::runtime_error{"upstream failed"};
        return response["result"];
    }, allowed, dir.path()};
    if (!gate.isReady()) return 3;
    UniValue endpoint{UniValue::VOBJ};
    endpoint.pushKV("url", gate.url().toStdString());
    endpoint.pushKV("cookie", gate.cookiePath().toStdString());
    QTextStream out{stdout};
    out << QString::fromStdString(endpoint.write()) << '\n';
    out.flush();
    return app.exec();
}
