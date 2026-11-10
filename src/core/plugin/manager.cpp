// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin manager facade: scans directories and coordinates loader + registry.
#include "manager.h"
#include "loader.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <string>

PluginManager::~PluginManager() {
    types.clear();
    for (auto& l : libs) plugin_loader::close(l.h);
    libs.clear();
}

int PluginManager::scan(const char* dir) {
    if (!dir || !*dir) return 0;
    DIR* d = opendir(dir);
    if (!d) {
        addProblem(dir, "missing directory");
        return 0;
    }
    closedir(d);
    return scanDirs({std::string(dir)});
}

int PluginManager::scanDirs(const std::vector<std::string>& wanted) {
    int n = 0;
    for (const std::string& dir : wanted) {
        if (dir.empty()) continue;
        if (std::find(dirs.begin(), dirs.end(), dir) != dirs.end()) continue;
        DIR* d = opendir(dir.c_str());
        if (!d) continue; // default discovery: a missing folder is normal
        dirs.push_back(dir);
        while (dirent* e = readdir(d)) {
            std::string f = e->d_name;
            if (f.size() < 4 || f.compare(f.size() - 3, 3, ".so") != 0) continue;
            std::string path = dir + "/" + f;
            plugin_loader::LoadedPlugin loaded;
            std::string error;
            if (!plugin_loader::open(path, loaded, error)) {
                addProblem(path, error);
                continue;
            }
            const NsPlugin* p = loaded.plugin;
            const int libIndex = plugin_registry::addLibrary(*this, path, loaded.handle, p);
            plugin_loader::loadResources(*this, libs[(size_t)libIndex]);
            if (!quiet)
                std::printf("plugins: %s v%s (%d tipos)\n", libs[(size_t)libIndex].name.c_str(),
                            libs[(size_t)libIndex].version.c_str(), p->num_types);
            n += plugin_registry::registerTypes(*this, (size_t)libIndex, path, p);
            plugin_loader::loadExec(*this, (size_t)libIndex);
            plugin_loader::loadEvents(*this, (size_t)libIndex);
            plugin_loader::loadUi(*this, (size_t)libIndex);
        }
        closedir(d);
    }
    return n;
}
