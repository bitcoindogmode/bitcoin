// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDINSTALLER_H
#define BITCOIN_QT_ORDINSTALLER_H

#include <QByteArray>
#include <QString>

struct OrdArtifact {
    QString platform;
    QString version;
    QString url;
    QByteArray sha256;
};

class OrdInstaller
{
public:
    static QString PlatformId();
    static QString ExecutableName();
    static OrdArtifact PinnedArtifact(const QString& platform, QString& error);
    static QByteArray FileSha256(const QString& path, QString& error);
    static bool VerifyFile(const QString& path, const OrdArtifact& artifact, QString& error);
    static bool VerifyVersionOutput(const QByteArray& output, const OrdArtifact& artifact, QString& error);
    static QString ExtractExecutable(const QString& archive, const QString& destination, const OrdArtifact& artifact, QString& error);
    static bool InstallExecutable(const QString& source, const QString& destination, QString& error);
};

#endif // BITCOIN_QT_ORDINSTALLER_H
