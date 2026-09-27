// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_OPTIONTESTS_H
#define BITCOIN_QT_TEST_OPTIONTESTS_H

#include <common/settings.h>
#include <qt/optionsmodel.h>
#include <univalue.h>

#include <QObject>

class BitcoinApplication;

class OptionTests : public QObject
{
    Q_OBJECT
public:
    explicit OptionTests(BitcoinApplication& app);

private Q_SLOTS:
    void init(); // called before each test function execution.
    void migrateSettings();
    void integerGetArgBug();
    void parametersInteraction();
    void ordSettingsPersisted();
    void ordCompatibleCommandLineSettings();
    void extractFilter();

private:
    interfaces::Node& m_node;
    BitcoinApplication& m_app;
    common::Settings m_previous_settings;
};

#endif // BITCOIN_QT_TEST_OPTIONTESTS_H
