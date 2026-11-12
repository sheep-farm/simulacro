// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin manager facade: scans directories and coordinates loader + registry.
#include "manager.h"
#include "loader.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
namespace fs = std::filesystem;
#include <string>

PluginManager::~PluginManager() {
    types.clear();
    for (auto& l : libs) plugin_loader::close(l.h);
    libs.clear();
}

int PluginManager::scan(const char* dir) {
    if (!dir || !*dir) return 0;
    std::error_code ec;
    if (!fs::is_directory(fs::path(dir), ec)) {
        addProblem(dir, "missing directory");
        return 0;
    }
    return scanDirs({std::string(dir)});
}

int PluginManager::scanDirs(const std::vector<std::string>& wanted) {
    int n = 0;
    // extensões aceitas por plataforma. No macOS o CMake gera MODULEs como
    // .so (igual ao Linux) e o dlopen abre .dylib também — aceitamos ambos.
#if defined(_WIN32)
    const char* exts[] = {".dll"};
#elif defined(__APPLE__)
    const char* exts[] = {".so", ".dylib"};
#else
    const char* exts[] = {".so"};
#endif
    auto hasLibExt = [&exts](const std::string& f) {
        for (const char* e : exts) {
            const size_t n = std::strlen(e);
            if (f.size() > n && f.compare(f.size() - n, n, e) == 0) return true;
        }
        return false;
    };
    std::set<std::string> seen;
    for (const std::string& dir : wanted) {
        if (dir.empty()) continue;
        if (std::find(dirs.begin(), dirs.end(), dir) != dirs.end()) continue;
        std::error_code ec;
        fs::directory_iterator it(fs::path(dir), ec), end;
        if (ec) continue; // default discovery: a missing folder is normal
        dirs.push_back(dir);
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string f = it->path().filename().string();
            if (!hasLibExt(f)) continue;
            const std::string path = it->path().string();
            if (!seen.insert(path).second) continue;
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
            plugin_loader::loadActs(*this, (size_t)libIndex);
        }
    }
    return n;
}
