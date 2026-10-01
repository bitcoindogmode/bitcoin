// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordconfiguration.h>
#include <qt/ordinstaller.h>
#include <qt/ordmanager.h>
#include <qt/ordprocess.h>
#include <qt/test/ordtests.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
QString HelperPath()
{
    return QCoreApplication::applicationDirPath() + QDir::separator() +
#ifdef Q_OS_WIN
           QStringLiteral("ord_test_helper.exe");
#else
           QStringLiteral("ord_test_helper");
#endif
}

OrdArtifact ArtifactFor(const QByteArray& bytes)
{
    return {
        QStringLiteral("test"),
        QStringLiteral("0.29.0"),
        QStringLiteral("https://example.invalid/ord"),
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256),
    };
}

QString WriteFile(const QString& path, const QByteArray& contents)
{
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) return {};
    file.close();
    return path;
}
} // namespace

void OrdTests::configurationDisabledLeavesSettingsAlone()
{
    const OrdNodeOptions current{
        .prune = {true, OrdSettingSource::COMMAND_LINE},
        .txindex = {false, OrdSettingSource::PERSISTENT},
        .server = {false, OrdSettingSource::DEFAULT},
        .rest = {false, OrdSettingSource::DEFAULT},
    };
    const auto result{ResolveOrdConfiguration(false, current)};
    QVERIFY(result.ok());
    QVERIFY(result.options.prune.value);
    QVERIFY(!result.options.txindex.value);
}

void OrdTests::configurationEnablesRequirements()
{
    const auto result{ResolveOrdConfiguration(true, {})};
    QVERIFY(result.ok());
    QVERIFY(!result.options.prune.value);
    QVERIFY(result.options.txindex.value);
    QVERIFY(result.options.server.value);
    QVERIFY(result.options.rest.value);

    OrdNodeOptions persistent_conflicts;
    persistent_conflicts.prune = {true, OrdSettingSource::PERSISTENT};
    persistent_conflicts.txindex = {false, OrdSettingSource::PERSISTENT};
    const auto overridden{ResolveOrdConfiguration(true, persistent_conflicts)};
    QVERIFY(overridden.ok());
    QVERIFY(!overridden.options.prune.value);
    QVERIFY(overridden.options.txindex.value);
}

void OrdTests::configurationRejectsCommandLineConflicts()
{
    for (const int conflict : {0, 1, 2, 3}) {
        OrdNodeOptions current;
        if (conflict == 0) current.prune = {true, OrdSettingSource::COMMAND_LINE};
        if (conflict == 1) current.txindex = {false, OrdSettingSource::COMMAND_LINE};
        if (conflict == 2) current.server = {false, OrdSettingSource::COMMAND_LINE};
        if (conflict == 3) current.rest = {false, OrdSettingSource::COMMAND_LINE};
        const auto result{ResolveOrdConfiguration(true, current)};
        QVERIFY2(!result.ok(), qPrintable(QStringLiteral("conflict %1 was accepted").arg(conflict)));
    }

    OrdNodeOptions compatible;
    compatible.prune = {false, OrdSettingSource::COMMAND_LINE};
    compatible.txindex = {true, OrdSettingSource::COMMAND_LINE};
    compatible.server = {true, OrdSettingSource::COMMAND_LINE};
    compatible.rest = {true, OrdSettingSource::COMMAND_LINE};
    QVERIFY(ResolveOrdConfiguration(true, compatible).ok());
}

void OrdTests::installerAcceptsPinnedArtifact()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray contents{"fake ord binary"};
    const QString source{WriteFile(dir.filePath("download"), contents)};
    QVERIFY(!source.isEmpty());
    const OrdArtifact artifact{ArtifactFor(contents)};

    QString manifest_error;
    const auto pinned{OrdInstaller::PinnedArtifact("linux-x86_64", manifest_error)};
    QVERIFY2(manifest_error.isEmpty(), qPrintable(manifest_error));
    QCOMPARE(pinned.version, QStringLiteral("0.29.0"));
    QCOMPARE(pinned.sha256.toHex(), QByteArray{"f65c758d71549954470aa7fe23b197478688fb4f910e84c2956cf9144078a94e"});
    const auto unsupported{OrdInstaller::PinnedArtifact("linux-arm64", manifest_error)};
    QVERIFY(unsupported.version.isEmpty());
    QVERIFY(manifest_error.contains("not available"));

    QString error;
    QVERIFY2(OrdInstaller::VerifyFile(source, artifact, error), qPrintable(error));
    QVERIFY2(OrdInstaller::VerifyVersionOutput("ord 0.29.0\n", artifact, error), qPrintable(error));

    const QString destination{dir.filePath("installed/ord")};
    QVERIFY(QDir{}.mkpath(QFileInfo{destination}.absolutePath()));
    QVERIFY2(OrdInstaller::InstallExecutable(source, destination, error), qPrintable(error));
    QFile installed{destination};
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.readAll(), contents);
}

void OrdTests::installerRejectsCorruptAndWrongVersionArtifacts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path{WriteFile(dir.filePath("download"), "truncated")};
    QVERIFY(!path.isEmpty());
    const OrdArtifact artifact{ArtifactFor("expected")};

    QString error;
    QVERIFY(!OrdInstaller::VerifyFile(path, artifact, error));
    QVERIFY(error.contains("checksum mismatch"));
    error.clear();
    QVERIFY(!OrdInstaller::VerifyVersionOutput("ord 0.28.0", artifact, error));
    QVERIFY(error.contains("Unexpected Ord version"));

    // A missing source must leave an existing installation untouched.
    const QString installed{WriteFile(dir.filePath("installed"), "known good")};
    QVERIFY(!installed.isEmpty());
    error.clear();
    QVERIFY(!OrdInstaller::InstallExecutable(dir.filePath("missing"), installed, error));
    QFile preserved{installed};
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray{"known good"});
}

void OrdTests::installerRejectsSymlinkSource()
{
#ifdef Q_OS_WIN
    QSKIP("Creating symlinks requires elevated privileges on many Windows hosts.");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray contents{"fake ord binary"};
    const QString target{WriteFile(dir.filePath("target"), contents)};
    const QString link{dir.filePath("link")};
    QVERIFY(QFile::link(target, link));
    QString error;
    QVERIFY(!OrdInstaller::VerifyFile(link, ArtifactFor(contents), error));
    QVERIFY(error.contains("symbolic link"));
#endif
}

void OrdTests::installerExtractsAndInstallsExecutable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString package_dir{dir.filePath(QStringLiteral("package/ord-0.29.0"))};
    QVERIFY(QDir{}.mkpath(package_dir));
    const QString packaged{WriteFile(QDir{package_dir}.filePath(OrdInstaller::ExecutableName()), "ord fixture")};
    QVERIFY(!packaged.isEmpty());

    const QString archive{dir.filePath(QStringLiteral("ord.tar.gz"))};
    QProcess tar;
    tar.start(QStringLiteral("tar"), {
        QStringLiteral("-czf"), archive,
        QStringLiteral("-C"), dir.filePath(QStringLiteral("package")),
        QStringLiteral("ord-0.29.0"),
    });
    QVERIFY(tar.waitForFinished(30000));
    QCOMPARE(tar.exitCode(), 0);

    QString error;
    const OrdArtifact artifact{ArtifactFor([&] {
        QFile file{archive};
        if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
        return file.readAll();
    }())};
    const QString extracted{OrdInstaller::ExtractExecutable(archive, dir.filePath(QStringLiteral("extract")), artifact, error)};
    QVERIFY2(!extracted.isEmpty(), qPrintable(error));
    const QString installed{dir.filePath(QStringLiteral("installed/bin/") + OrdInstaller::ExecutableName())};
    QVERIFY2(OrdInstaller::InstallExecutable(extracted, installed, error), qPrintable(error));
    QFile result{installed};
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray{"ord fixture"});
    QVERIFY(QFileInfo{installed}.isExecutable());
}

void OrdTests::managerRequestsAndInstallsArtifact()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString package_dir{dir.filePath(QStringLiteral("package/ord-0.29.0"))};
    QVERIFY(QDir{}.mkpath(package_dir));
    const QString packaged{QDir{package_dir}.filePath(OrdInstaller::ExecutableName())};
    QVERIFY(QFile::copy(HelperPath(), packaged));

    const QString archive{dir.filePath(QStringLiteral("ord.tar.gz"))};
    QProcess tar;
    tar.start(QStringLiteral("tar"), {
        QStringLiteral("-czf"), archive,
        QStringLiteral("-C"), dir.filePath(QStringLiteral("package")),
        QStringLiteral("ord-0.29.0"),
    });
    QVERIFY(tar.waitForFinished(30000));
    QCOMPARE(tar.exitCode(), 0);

    QString error;
    const QByteArray checksum{OrdInstaller::FileSha256(archive, error)};
    QVERIFY2(!checksum.isEmpty(), qPrintable(error));
    const OrdArtifact artifact{
        QStringLiteral("test"),
        QStringLiteral("0.29.0"),
        QStringLiteral("https://example.invalid/ord.tar.gz"),
        checksum,
    };
    OrdManager manager{dir.path(), dir.filePath(QStringLiteral("regtest")), QStringLiteral("regtest"), nullptr, artifact};
    QString requested_version;
    QString requested_url;
    QString requested_sha256;
    connect(&manager, &OrdManager::installationRequired, &manager, [&](const QString& version, const QString& url, const QString& sha256) {
        requested_version = version;
        requested_url = url;
        requested_sha256 = sha256;
    });
    QSignalSpy ready{&manager, &OrdManager::ready};
    QSignalSpy failed{&manager, &OrdManager::failed};
    manager.start();
    QCOMPARE(requested_version, artifact.version);
    QCOMPARE(requested_url, artifact.url);
    QCOMPARE(requested_sha256, QString::fromLatin1(artifact.sha256.toHex()));
    manager.installArchive(archive);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(failed.size(), 0);
    QVERIFY(QFileInfo{manager.executablePath()}.isExecutable());
    QVERIFY(manager.executablePath().contains(QStringLiteral("ord/bin/0.29.0")));

    qint64 cardinal_balance{-1};
    qint64 total_balance{-1};
    QString funding_address;
    connect(&manager, &OrdManager::walletDetails, &manager, [&](qint64 cardinal, qint64 total, const QString& address) {
        cardinal_balance = cardinal;
        total_balance = total;
        funding_address = address;
    });
    QSignalSpy wallet_unavailable{&manager, &OrdManager::walletUnavailable};
    manager.refreshWallet();
    QTRY_VERIFY_WITH_TIMEOUT(cardinal_balance >= 0 || wallet_unavailable.size() == 1, 5000);
    QVERIFY2(wallet_unavailable.isEmpty(), qPrintable(wallet_unavailable.isEmpty() ? QString{} : wallet_unavailable.takeFirst().at(0).toString()));
    QCOMPARE(cardinal_balance, 75000);
    QCOMPARE(total_balance, 85000);
    QCOMPARE(funding_address, QStringLiteral("bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9"));

    QSignalSpy wallet_created{&manager, &OrdManager::walletCreated};
    manager.createWallet();
    QTRY_COMPARE(wallet_created.size(), 1);
    QVERIFY(wallet_created.takeFirst().at(0).toString().startsWith(QStringLiteral("abandon abandon")));

    QSignalSpy wallet_restored{&manager, &OrdManager::walletRestored};
    manager.restoreWallet(QStringLiteral("test recovery words"));
    QTRY_COMPARE(wallet_restored.size(), 1);

    const QString inscription_file{WriteFile(dir.filePath(QStringLiteral("inscription.png")), "image")};
    const QString second_file{WriteFile(dir.filePath(QStringLiteral("second.png")), "second")};
    QSignalSpy inscription_started{&manager, &OrdManager::inscriptionStarted};
    qint64 preview_fees{-1};
    connect(&manager, &OrdManager::inscriptionPreview, &manager, [&](quint64, qint64 fees, const QString&, const QString&) { preview_fees = fees; });
    QSignalSpy inscription_complete{&manager, &OrdManager::inscriptionComplete};
    QSignalSpy inscription_failed{&manager, &OrdManager::inscriptionFailed};

    const quint64 invalid_fee_id{manager.previewInscription(inscription_file, QStringLiteral("0"), {}, true)};
    QTRY_COMPARE(inscription_failed.size(), 1);
    QCOMPARE(inscription_failed.takeFirst().at(0).toULongLong(), invalid_fee_id);
    const quint64 wrong_network_id{manager.previewInscription(inscription_file, QStringLiteral("7.5"), QStringLiteral("bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl"), true)};
    QTRY_COMPARE(inscription_failed.size(), 1);
    QCOMPARE(inscription_failed.takeFirst().at(0).toULongLong(), wrong_network_id);

    const quint64 preview_id{manager.previewInscription(inscription_file, QStringLiteral("7.5"), {}, true)};
    QTRY_COMPARE(preview_fees, 1234);
    QString preview_error;
    QVERIFY2(manager.previewMatches(preview_id, inscription_file, QStringLiteral("7.5"), {}, true, preview_error), qPrintable(preview_error));

    preview_fees = -1;
    const quint64 second_preview_id{manager.previewInscription(second_file, QStringLiteral("7.5"), {}, true)};
    QTRY_COMPARE(preview_fees, 1234);
    QVERIFY(!manager.previewMatches(preview_id, inscription_file, QStringLiteral("7.5"), {}, true, preview_error));
    preview_error.clear();
    QVERIFY2(manager.previewMatches(second_preview_id, second_file, QStringLiteral("7.5"), {}, true, preview_error), qPrintable(preview_error));

    QVERIFY(!WriteFile(second_file, "changed bytes").isEmpty());
    preview_error.clear();
    QVERIFY(!manager.previewMatches(second_preview_id, second_file, QStringLiteral("7.5"), {}, true, preview_error));
    manager.invalidatePreview(second_preview_id);

    preview_fees = -1;
    const quint64 final_preview_id{manager.previewInscription(second_file, QStringLiteral("7.5"), {}, true)};
    QTRY_COMPARE(preview_fees, 1234);
    preview_error.clear();
    QVERIFY2(manager.previewMatches(final_preview_id, second_file, QStringLiteral("7.5"), {}, true, preview_error), qPrintable(preview_error));
    manager.inscribe(final_preview_id);
    QCOMPARE(inscription_started.size(), 4);
    QTRY_COMPARE(inscription_complete.size(), 1);
    QCOMPARE(inscription_failed.size(), 0);
    const QString command{inscription_complete.takeFirst().at(1).toString()};
    QVERIFY(command.contains(QStringLiteral("\"--chain\",\"regtest\"")));
    QVERIFY(command.contains(QStringLiteral("\"--index-runes\"")));
    QVERIFY(command.contains(QStringLiteral("\"--index-sats\"")));
    QVERIFY(command.contains(QStringLiteral("\"wallet\",\"--server-url\",\"http://127.0.0.1:")));
    QVERIFY(command.contains(QStringLiteral("\",\"--name\",\"ord\",\"inscribe\"")));
    QVERIFY(command.contains(QStringLiteral("\"--fee-rate\",\"7.5\"")));
    QVERIFY(command.contains(QStringLiteral("\"--file\",\"")));
    QVERIFY(command.contains(QStringLiteral("ord/tmp/inscription-")));
    QVERIFY(!command.contains(QStringLiteral("\"--destination\"")));
    QVERIFY(command.contains(QStringLiteral("\"--compress\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--dry-run\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--no-limit\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--reinscribe\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--parent\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--delegate\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--metadata\"")));
    QVERIFY(!command.contains(QStringLiteral("\"--sat\"")));
}

void OrdTests::processReportsSuccessAndFailure()
{
    QVERIFY2(QFileInfo::exists(HelperPath()), qPrintable(HelperPath()));

    OrdProcess process;
    QSignalSpy completed{&process, &OrdProcess::completed};
    QSignalSpy failed{&process, &OrdProcess::failed};
    process.start(HelperPath(), {"success"});
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.takeFirst().at(0).toInt(), 0);
    QVERIFY(process.output().contains("\"height\":1"));

    process.start(HelperPath(), {"fail"});
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(completed.takeFirst().at(0).toInt(), 23);
    QVERIFY(process.output().contains("simulated failure"));
    QCOMPARE(failed.size(), 0);

    process.start(QStringLiteral("this-program-does-not-exist"), {});
    QTRY_COMPARE(failed.size(), 1);

    failed.clear();
    process.start(HelperPath(), {"crash"});
    QTRY_COMPARE(failed.size(), 1);

    process.start(HelperPath(), {"malformed"});
    QTRY_COMPARE(completed.size(), 1);
    QVERIFY(process.output().contains("not-json"));
}

void OrdTests::processBoundsOutputAndStops()
{
    OrdProcess process;
    QSignalSpy completed{&process, &OrdProcess::completed};
    process.start(HelperPath(), {"large"});
    QTRY_COMPARE(completed.size(), 1);
    QCOMPARE(process.output().size(), 1024 * 1024);

    process.start(HelperPath(), {"hang"});
    QTRY_VERIFY(process.isRunning());
    process.stop();
    QTRY_VERIFY(!process.isRunning());
}
