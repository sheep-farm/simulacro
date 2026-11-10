// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin manager facade: owns the registry, scans directories and creates
// live instances. Public host API kept compatible with the old plugin.h.
#pragma once
#include "instance.h"
#include "registry.h"
#include <memory>
#include <string>
#include <vector>

struct PluginManager : PluginRegistry {
    ~PluginManager();
    int scan(const char* dir);                        // explicit directory; warns if missing
    int scanDirs(const std::vector<std::string>& dirs); // ignores missing directories
    std::shared_ptr<PluginInstance> createInstance(const std::string& id) const;
};
