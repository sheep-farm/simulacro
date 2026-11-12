// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Program version, plugin ABI and file format version.
// Kept in a single place so --version, the window and the manifest don't diverge.
#pragma once
#include <string>
#include "plugin.h"

#define NODESIM_VERSION "1.4.0"
static constexpr int NODESIM_ABI = NODESIM_PLUGIN_API;
static constexpr int NODESIM_FORMAT = 10; // 1st line of the .sim file: "nodesim 10"

inline std::string nodesimVersion() {
    return std::string("nodesim ") + NODESIM_VERSION + " (ABI " + std::to_string(NODESIM_ABI) +
           ", format " + std::to_string(NODESIM_FORMAT) + ")";
}
