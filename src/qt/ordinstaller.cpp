// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordinstaller.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>

QString OrdInstaller::PlatformId()
{
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("linux-x86_64");
#elif defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("windows-x86_64");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("macos-x86_64");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("macos-arm64");
#else
    return QStringLiteral("unsupported");
#endif
}

QString OrdInstaller::ExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("ord.exe");
#else
    return QStringLiteral("ord");
#endif
}

OrdArtifact OrdInstaller::PinnedArtifact(const QString& platform, QString& error)
{
    static const QString version{QStringLiteral("0.29.0")};
    static const QString base_url{QStringLiteral("https://github.com/ordinals/ord/releases/download/0.29.0/")};
    const auto artifact = [&](const QString& filename, const char* sha256) {
        return OrdArtifact{platform, version, base_url + filename, QByteArray::fromHex(sha256)};
    };

    if (platform == "linux-x86_64") {
        return artifact(QStringLiteral("ord-0.29.0-x86_64-unknown-linux-gnu.tar.gz"), "f65c758d71549954470aa7fe23b197478688fb4f910e84c2956cf9144078a94e");
    }
    if (platform == "windows-x86_64") {
        return artifact(QStringLiteral("ord-0.29.0-x86_64-pc-windows-msvc.zip"), "93de82db792ccc37ae385c49646c0f649d38049f4e959499c6e7c5d1a81bf2ad");
    }
    if (platform == "macos-x86_64") {
        return artifact(QStringLiteral("ord-0.29.0-x86_64-apple-darwin.tar.gz"), "a0085f296057563a31258402437c1182fc13bb9559826d1f5490feb4be6dbb75");
    }
    if (platform == "macos-arm64") {
        return artifact(QStringLiteral("ord-0.29.0-aarch64-apple-darwin.tar.gz"), "9360e97054a1d96624190634882c187126b02647a889b344cb601627ed1bd80c");
    }

    error = QStringLiteral("Ord %1 is not available for platform %2.").arg(version, platform);
    return {};
}

QByteArray OrdInstaller::FileSha256(const QString& path, QString& error)
{
    QFileInfo info{path};
    if (info.isSymLink()) {
        error = QStringLiteral("Refusing to verify a symbolic link: %1").arg(path);
        return {};
    }

    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return {};
    }

    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&file)) {
        error = file.errorString();
        return {};
    }
    return hash.result();
}

bool OrdInstaller::VerifyFile(const QString& path, const OrdArtifact& artifact, QString& error)
{
    const QByteArray actual{FileSha256(path, error)};
    if (!error.isEmpty()) return false;
    if (actual != artifact.sha256) {
        error = QStringLiteral("Ord artifact checksum mismatch (expected %1, got %2).")
                    .arg(QString::fromLatin1(artifact.sha256.toHex()), QString::fromLatin1(actual.toHex()));
        return false;
    }
    return true;
}

bool OrdInstaller::VerifyVersionOutput(const QByteArray& output, const OrdArtifact& artifact, QString& error)
{
    QByteArray expected{"ord "};
    expected += artifact.version.toUtf8();
    if (output.trimmed() != expected) {
        error = QStringLiteral("Unexpected Ord version output: %1").arg(QString::fromUtf8(output.trimmed()));
        return false;
    }
    return true;
}

QString OrdInstaller::ExtractExecutable(const QString& archive, const QString& destination, const OrdArtifact& artifact, QString& error)
{
    if (!VerifyFile(archive, artifact, error)) return {};
    if (!QDir{}.mkpath(destination)) {
        error = QStringLiteral("Could not create extraction directory: %1").arg(destination);
        return {};
    }

    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("-xf"), archive, QStringLiteral("-C"), destination});
    if (!tar.waitForStarted(10000) || !tar.waitForFinished(120000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        error = QStringLiteral("Could not extract Ord archive with tar: %1").arg(QString::fromUtf8(tar.readAll()));
        return {};
    }

    QDirIterator files{destination, {ExecutableName()}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories};
    if (!files.hasNext()) {
        error = QStringLiteral("The verified Ord archive did not contain %1.").arg(ExecutableName());
        return {};
    }
    const QString executable{files.next()};
    if (files.hasNext()) {
        error = QStringLiteral("The verified Ord archive contained multiple executables.");
        return {};
    }
    return executable;
}

bool OrdInstaller::InstallExecutable(const QString& source, const QString& destination, QString& error)
{
    const QFileInfo source_info{source};
    const QFileInfo destination_info{destination};
    if (!source_info.isFile() || source_info.isSymLink() || destination_info.isSymLink() || QFileInfo{destination_info.absolutePath()}.isSymLink()) {
        error = QStringLiteral("Refusing unsafe Ord installation path.");
        return false;
    }
    if (!QDir{}.mkpath(destination_info.absolutePath())) {
        error = QStringLiteral("Could not create Ord installation directory.");
        return false;
    }

    QFile input{source};
    if (!input.open(QIODevice::ReadOnly)) {
        error = input.errorString();
        return false;
    }
    QSaveFile output{destination};
    if (!output.open(QIODevice::WriteOnly)) {
        error = output.errorString();
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray chunk{input.read(1024 * 1024)};
        if (chunk.isEmpty() && input.error() != QFile::NoError) {
            error = input.errorString();
            output.cancelWriting();
            return false;
        }
        if (output.write(chunk) != chunk.size()) {
            error = output.errorString();
            output.cancelWriting();
            return false;
        }
    }
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    if (!output.commit()) {
        error = output.errorString();
        return false;
    }
    return true;
}
