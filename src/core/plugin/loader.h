// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin loader: directory discovery, dlopen/dlsym and the optional
// resource/execution/event tables. No registry policy lives here.
#pragma once
#include "registry.h"
#include <string>
#include <vector>

// Default plugin directories, in precedence order:
//   1. $NODESIM_PLUGIN_PATH (separated by ':')
//   2. ./plugins (development)
//   3. <executable dir>/../share/nodesim/plugins and ../lib/nodesim/plugins
//      (installation with any PREFIX)
//   4. $XDG_DATA_HOME/nodesim/plugins (or ~/.local/share/nodesim/plugins)
//   5. each $XDG_DATA_DIRS + /nodesim/plugins (or /usr/local/share and /usr/share)
// The list is deduplicated; nonexistent directories are ignored by scanDirs().
std::vector<std::string> defaultPluginDirs();

namespace plugin_loader {

struct LoadedPlugin {
    void* handle = nullptr;
    const NsPlugin* plugin = nullptr;
};

// Opens and validates one .so. On failure returns false and fills `error`.
bool open(const std::string& path, LoadedPlugin& out, std::string& error);
void close(void* handle);

void loadResources(PluginRegistry& pm, PluginRegistry::Lib& lib);
int loadExec(PluginRegistry& pm, size_t libIndex);
int loadEvents(PluginRegistry& pm, size_t libIndex);
int loadUi(PluginRegistry& pm, size_t libIndex);

} // namespace plugin_loader
