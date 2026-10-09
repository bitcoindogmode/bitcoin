// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QThread>

#include <cstdlib>

int main(int argc, char* argv[])
{
    QCoreApplication app{argc, argv};
    const QString mode{app.arguments().value(1)};
    QTextStream out{stdout};
    QTextStream err{stderr};

    if (mode == "--version") {
        out << "ord 0.29.0\n";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("index")) && app.arguments().contains(QStringLiteral("update"))) {
        out << "{}\n";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("server"))) {
        const qsizetype port_option{app.arguments().indexOf(QStringLiteral("--http-port"))};
        if (port_option < 0 || port_option + 1 >= app.arguments().size()) return 25;
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, app.arguments().at(port_option + 1).toUShort())) return 26;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    const QByteArray request{socket->readAll()};
                    const QByteArray body{request.contains("GET /status ") ? QByteArray{"{\"chain\":\"regtest\",\"inscription_index\":true,\"rune_index\":true,\"sat_index\":true,\"json_api\":true,\"unrecoverably_reorged\":false}"} : QByteArray{"1"}};
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        err << "Listening on http://127.0.0.1:" << server.serverPort() << '\n';
        err.flush();
        return app.exec();
    }

    if (app.arguments().contains(QStringLiteral("balance"))) {
        out << "{\"cardinal\":75000,\"ordinal\":10000,\"total\":85000}\n";
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("receive"))) {
        out << "{\"addresses\":[\"bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9\"]}\n";
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("create"))) {
        out << "{\"mnemonic\":\"abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about\",\"passphrase\":\"\"}\n";
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("restore"))) {
        QTextStream input{stdin};
        return input.readLine().isEmpty() ? 24 : 0;
    }
    if (app.arguments().contains(QStringLiteral("cardinals"))) {
        out << "[{\"output\":\"" << QString(64, '1') << ":0\",\"amount\":75000},{\"output\":\"" << QString(64, '2') << ":0\",\"amount\":5000}]\n";
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("sats"))) {
        out << "[{\"output\":\"" << QString(64, '2') << ":0\",\"rarity\":\"uncommon\"}]\n";
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("inscribe"))) {
        QJsonObject result{
            {QStringLiteral("arguments"), QJsonArray::fromStringList(app.arguments())},
            {QStringLiteral("commit"), QStringLiteral("0000")},
            {QStringLiteral("inscriptions"), QJsonArray{}},
            {QStringLiteral("reveal"), QStringLiteral("1111")},
            {QStringLiteral("reveal_broadcast"), !app.arguments().contains(QStringLiteral("--dry-run"))},
            {QStringLiteral("total_fees"), 1234},
        };
        out << QJsonDocument{result}.toJson(QJsonDocument::Compact) << '\n';
        return 0;
    }

    if (mode == "success") {
        out << "{\"height\":1}\n";
        return 0;
    }
    if (mode == "malformed") {
        out << "not-json\n";
        return 0;
    }
    if (mode == "fail") {
        err << "simulated failure\n";
        return 23;
    }
    if (mode == "large") {
        out << QString(2 * 1024 * 1024, 'x');
        return 0;
    }
    if (mode == "hang") {
        out << "started\n";
        out.flush();
        QThread::sleep(30);
        return 0;
    }
    if (mode == "environment") {
        out << (qEnvironmentVariableIsSet("ORD_NO_INDEX_INSCRIPTIONS") || qEnvironmentVariableIsSet("ORD_INDEX") || qEnvironmentVariableIsSet("ORD_CONFIG") ? "unsafe" : "clean");
        return 0;
    }
    if (mode == "crash") std::abort();

    err << "unknown mode\n";
    return 2;
}
