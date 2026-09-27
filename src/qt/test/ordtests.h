// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_TEST_ORDTESTS_H
#define BITCOIN_QT_TEST_ORDTESTS_H

#include <QObject>

class OrdTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void configurationDisabledLeavesSettingsAlone();
    void configurationEnablesRequirements();
    void configurationRejectsCommandLineConflicts();
    void installerAcceptsPinnedArtifact();
    void installerRejectsCorruptAndWrongVersionArtifacts();
    void installerRejectsSymlinkSource();
    void installerExtractsAndInstallsExecutable();
    void managerRequestsAndInstallsArtifact();
    void processReportsSuccessAndFailure();
    void processBoundsOutputAndStops();
};

#endif // BITCOIN_QT_TEST_ORDTESTS_H
