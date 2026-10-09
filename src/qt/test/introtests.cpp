// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/intro.h>
#include <qt/test/introtests.h>

#include <QCheckBox>
#include <QLabel>
#include <QSpinBox>
#include <QTest>

void IntroTests::ordDefaultsOff()
{
    Intro intro{nullptr, 700, 15};
    auto* ord{intro.findChild<QCheckBox*>("enableOrd")};
    auto* warning{intro.findChild<QLabel*>("ordWarningLabel")};
    QVERIFY(ord);
    QVERIFY(warning);
    QVERIFY(!ord->isChecked());
    QVERIFY(!warning->isVisible());
    QVERIFY(!intro.getOrdEnabled());
    intro.setOrdEnabled(true);
    QVERIFY(intro.getOrdEnabled());
}

void IntroTests::ordDisablesPruning()
{
    Intro intro{nullptr, 700, 15};
    auto* ord{intro.findChild<QCheckBox*>("enableOrd")};
    auto* prune{intro.findChild<QCheckBox*>("prune")};
    auto* prune_size{intro.findChild<QSpinBox*>("pruneGB")};
    auto* warning{intro.findChild<QLabel*>("ordWarningLabel")};
    QVERIFY(ord);
    QVERIFY(prune);
    QVERIFY(prune_size);

    prune->setChecked(true);
    ord->setChecked(true);
    QVERIFY(intro.getOrdEnabled());
    QVERIFY(!prune->isChecked());
    QVERIFY(!prune->isEnabled());
    QVERIFY(!prune_size->isEnabled());
    QVERIFY(warning->isVisibleTo(&intro));
    QCOMPARE(intro.getPruneMiB(), 0);

    ord->setChecked(false);
    QVERIFY(prune->isEnabled());
}
