// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef BITCOIN_QT_ORDCONFIGURATION_H
#define BITCOIN_QT_ORDCONFIGURATION_H

#include <QString>

enum class OrdSettingSource {
    DEFAULT,
    PERSISTENT,
    COMMAND_LINE,
};

struct OrdBoolSetting {
    bool value{false};
    OrdSettingSource source{OrdSettingSource::DEFAULT};
};

struct OrdNodeOptions {
    OrdBoolSetting prune;
    OrdBoolSetting txindex;
    OrdBoolSetting server;
    OrdBoolSetting rest;
};

struct OrdConfigurationResult {
    OrdNodeOptions options;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

/** Resolve the node settings required by the Ord opt-in.
 *
 * Explicit command-line conflicts are reported instead of silently overridden.
 * Persistent and default values may be replaced by the user's GUI opt-in.
 */
OrdConfigurationResult ResolveOrdConfiguration(bool ord_enabled, const OrdNodeOptions& current);

#endif // BITCOIN_QT_ORDCONFIGURATION_H
