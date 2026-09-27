// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_TEST_INTROTESTS_H
#define BITCOIN_QT_TEST_INTROTESTS_H

#include <QObject>

class IntroTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ordDefaultsOff();
    void ordDisablesPruning();
};

#endif // BITCOIN_QT_TEST_INTROTESTS_H
