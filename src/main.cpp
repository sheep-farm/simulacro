// simulacro — entry point (GUI + headless runner)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include <wx/wx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "engine.h"
#include "i18n.h"
#include "version.h"

// Read by MainFrame (app.cpp) to open a file on startup.
std::string g_startFile;
bool g_openScopes = false;

namespace {
constexpr const char* kVersion = SIMULACRO_VERSION; // src/core/version.h

void usage(FILE* out) {
    std::fprintf(out,
        "simulacro %s — node graph editor and simulator\n\n"
        "Usage:\n"
        "  simulacro [FILE]              open the graphical editor\n"
        "  simulacro --run FILE [opts]   run headless and print a CSV trace\n\n"
        "Options:\n"
        "  -h, --help                    this help\n"
        "  -V, --version                 version information\n"
        "  --lang en|pt-BR               interface language (GUI)\n"
        "  --run FILE                    headless execution\n"
        "  --steps N                     number of steps (default 600)\n"
        "  --rate HZ                     samples per simulated second (default 60)\n"
        "  --plugin-dir DIR              plugin directory (default discovery)\n"
        "  --summary                     print only the final values\n"
        "  --open-scopes                 open every Scope page on startup (GUI)\n",
        kVersion);
}

std::string headerName(const Node& n, int id) {
    std::string s = std::to_string(id) + ":";
    s += n.title.empty() ? std::string(nodeTypeName(n.type)) : n.title;
    return s;
}

int runHeadless(const std::string& file, int steps, int rate, const std::string& pluginDir,
                bool summary) {
    Engine e;
    // quiet por padrão; NODESIM_VERBOSE_PLUGINS=1 imprime "plugins: nome v…"
    // (diagnóstico e asserção de carga nos smoke tests do CI)
    e.plugins.quiet = std::getenv("NODESIM_VERBOSE_PLUGINS") == nullptr;
    if (!pluginDir.empty()) e.scanPlugins(pluginDir);
    else e.scanPlugins();
    if (!e.load(file)) {
        std::fprintf(stderr, "simulacro: could not load %s\n", file.c_str());
        return 1;
    }
    e.dt = 1.0 / (rate > 0 ? rate : 60);
    e.running = false;

    struct Col {
        int id;
        int ch;
        std::string name;
    };
    std::vector<Col> cols;
    for (const auto& n : e.g.nodes) {
        if (n.type != NodeType::SCOPE) continue;
        const ScopeCfg cfg = parseScopeCfg(n.params);
        if (cfg.type == ScopeType::MAP) continue;
        const int nin = n.nIn < 1 ? 1 : n.nIn;
        for (int k = 0; k < nin; ++k) {
            std::string name = headerName(n, n.id) + ":ch" + std::to_string(k);
            const auto it = e.scopeData.find(n.id);
            if (it != e.scopeData.end() && k < (int)it->second.names.size() &&
                !it->second.names[k].empty())
                name = headerName(n, n.id) + ":" + it->second.names[k];
            cols.push_back({n.id, k, name});
        }
    }

    if (!summary) {
        std::cout << "t";
        for (const auto& c : cols) std::cout << "," << c.name;
        std::cout << "\n";
    }

    for (int i = 0; i < steps; ++i) {
        e.stepOnce();
        if (summary) continue;
        std::cout << std::fixed << std::setprecision(6) << e.t;
        for (const auto& c : cols) {
            double v = 0;
            const auto it = e.scopeData.find(c.id);
            if (it != e.scopeData.end() && c.ch < (int)it->second.ch.size() &&
                !it->second.ch[c.ch].empty())
                v = it->second.ch[c.ch].back();
            std::cout << "," << v;
        }
        std::cout << "\n";
    }

    if (summary) {
        std::cout << std::fixed << std::setprecision(6) << "t," << e.t << "\n";
        for (const auto& c : cols) {
            double v = 0;
            const auto it = e.scopeData.find(c.id);
            if (it != e.scopeData.end() && c.ch < (int)it->second.ch.size() &&
                !it->second.ch[c.ch].empty())
                v = it->second.ch[c.ch].back();
            std::cout << c.name << "," << v << "\n";
        }
    }
    if (!e.lastError.empty()) std::fprintf(stderr, "simulacro: %s\n", e.lastError.c_str());
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    std::string startFile;
    std::string runFile;
    std::string pluginDir;
    std::string lang;
    int steps = 600;
    int rate = 60;
    bool summary = false;
    bool headless = false;
    bool openScopes = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i] ? argv[i] : "";
        auto value = [&](const char* opt) -> std::string {
            const std::string o = opt;
            if (a.rfind(o + "=", 0) == 0) return a.substr(o.size() + 1);
            if (i + 1 < argc) return argv[++i];
            return {};
        };
        if (a == "-h" || a == "--help") { usage(stdout); return 0; }
        if (a == "-V" || a == "--version") {
            std::printf("simulacro %s\n%s\n", kVersion, nodesimVersion().c_str());
            return 0;
        }
        if (a == "--run" || a.rfind("--run=", 0) == 0) {
            headless = true;
            runFile = value("--run");
            if (runFile.empty()) { std::fprintf(stderr, "simulacro: --run expects a file\n"); return 2; }
        } else if (a == "--steps" || a.rfind("--steps=", 0) == 0) {
            steps = std::max(0, std::atoi(value("--steps").c_str()));
        } else if (a == "--rate" || a.rfind("--rate=", 0) == 0) {
            rate = std::max(1, std::atoi(value("--rate").c_str()));
        } else if (a == "--plugin-dir" || a.rfind("--plugin-dir=", 0) == 0) {
            pluginDir = value("--plugin-dir");
        } else if (a == "--lang" || a.rfind("--lang=", 0) == 0) {
            lang = value("--lang");
        } else if (a == "--summary") {
            summary = true;
        } else if (a == "--open-scopes") {
            openScopes = true;
        } else if (a.size() > 1 && a[0] == '-') {
            std::fprintf(stderr, "simulacro: unknown option '%s'\n", a.c_str());
            usage(stderr);
            return 2;
        } else if (startFile.empty()) {
            startFile = a;
        }
    }

    if (!lang.empty()) {
#if defined(_WIN32)
        _putenv_s("NODESIM_LANG", lang.c_str()); // setenv é POSIX
#else
        setenv("NODESIM_LANG", lang.c_str(), 1);
#endif
    }

    if (headless) return runHeadless(runFile, steps, rate, pluginDir, summary);

    g_startFile = startFile;
    g_openScopes = openScopes;
    // Our own parser already consumed the options; keep wxWidgets from
    // rejecting the file/arguments in wxApp::OnInit().
    char* wxargv[] = {argv[0], nullptr};
    int wxargc = 1;
    return wxEntry(wxargc, wxargv);}
