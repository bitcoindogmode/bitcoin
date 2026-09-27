// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qt/ordconfiguration.h>

#include <utility>

namespace {
bool IsCommandLineConflict(const OrdBoolSetting& setting, bool required)
{
    return setting.source == OrdSettingSource::COMMAND_LINE && setting.value != required;
}
} // namespace

OrdConfigurationResult ResolveOrdConfiguration(bool ord_enabled, const OrdNodeOptions& current)
{
    OrdConfigurationResult result{current, {}};
    if (!ord_enabled) return result;

    if (IsCommandLineConflict(current.prune, false)) {
        result.error = QStringLiteral("Ord requires pruning to be disabled, but -prune was set on the command line.");
        return result;
    }

    for (const auto& [name, setting] : {
             std::pair{"txindex", current.txindex},
             std::pair{"server", current.server},
             std::pair{"rest", current.rest},
         }) {
        if (IsCommandLineConflict(setting, true)) {
            result.error = QStringLiteral("Ord requires -%1=1, but it was disabled on the command line.").arg(QString::fromLatin1(name));
            return result;
        }
    }

    result.options.prune = {false, OrdSettingSource::PERSISTENT};
    result.options.txindex = {true, OrdSettingSource::PERSISTENT};
    result.options.server = {true, OrdSettingSource::PERSISTENT};
    result.options.rest = {true, OrdSettingSource::PERSISTENT};
    return result;
}
