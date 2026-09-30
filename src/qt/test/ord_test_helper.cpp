// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <QCoreApplication>
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

    if (app.arguments().contains(QStringLiteral("inscribe"))) {
        out << app.arguments().join('|') << '\n';
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
    if (mode == "crash") std::abort();

    err << "unknown mode\n";
    return 2;
}
