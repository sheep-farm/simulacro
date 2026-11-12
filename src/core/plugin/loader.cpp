// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Directory discovery, dlopen/dlsym and the optional plugin tables (resources,
// execution policy and event sources/sinks). The registry policy lives in
// registry.cpp and is orchestrated by manager.cpp.
#include "loader.h"
#include "../sha256.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <sys/stat.h>

// ---- shim de dlopen: Linux/macOS usam dlfcn; Windows usa LoadLibrary ----
// As macros mantêm os pontos de chamada idênticos nas três plataformas.
#if defined(_WIN32)
#include <windows.h>
#define NS_DL_OPEN(p)    ((void*)LoadLibraryA(p))
#define NS_DL_SYM(h, n)  ((void*)GetProcAddress((HMODULE)(h), (n)))
#define NS_DL_CLOSE(h)   FreeLibrary((HMODULE)(h))
#define NS_DL_ERR()      std::string("LoadLibrary/GetProcAddress failed (code ") + \
                         std::to_string((unsigned long)GetLastError()) + ")"
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#define NS_DL_OPEN(p)    dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define NS_DL_SYM(h, n)  dlsym((h), (n))
#define NS_DL_CLOSE(h)   dlclose((h))
inline std::string ns_dlerr() { // dlerror CONSOME o estado: uma única chamada
    const char* e = dlerror();
    return std::string(e ? e : "dlopen failed");
}
#define NS_DL_ERR() ns_dlerr()
#else
#include <dlfcn.h>
#include <unistd.h>
#define NS_DL_OPEN(p)    dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define NS_DL_SYM(h, n)  dlsym((h), (n))
#define NS_DL_CLOSE(h)   dlclose((h))
inline std::string ns_dlerr() { // dlerror CONSOME o estado: uma única chamada
    const char* e = dlerror();
    return std::string(e ? e : "dlopen failed");
}
#define NS_DL_ERR() ns_dlerr()
#endif

namespace {

void addUnique(std::vector<std::string>& out, const std::string& dir) {
    if (dir.empty()) return;
    if (std::find(out.begin(), out.end(), dir) == out.end()) out.push_back(dir);
}

void splitDirs(const char* value, std::vector<std::string>& out) {
    if (!value || !*value) return;
    std::string s = value;
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find(':', p);
        std::string d = s.substr(p, q == std::string::npos ? std::string::npos : q - p);
        addUnique(out, d);
        if (q == std::string::npos) break;
        p = q + 1;
    }
}

// Executable directory in absolute form. On Linux, /proc/self/exe resolves
// symlinks; if it does not exist, returns empty and discovery continues without this step.
std::string executableDir() {
    // diretório do executável, por plataforma (descoberta de plugins
    // relativa a <prefix>); vazio = descoberta relativa apenas
#if defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (!n) return "";
    buf[n] = '\0';
    std::string exe(buf);
    const size_t p = exe.rfind('\\');
    if (p == std::string::npos) return "";
    return p == 2 ? exe.substr(0, 3) : exe.substr(0, p); // C:\ raiz
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    std::string exe(buf);
    const size_t p = exe.rfind('/');
    if (p == std::string::npos) return "";
    return p == 0 ? "/" : exe.substr(0, p);
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string exe(buf);
    const size_t p = exe.rfind('/');
    if (p == std::string::npos) return "";
    return p == 0 ? "/" : exe.substr(0, p);
#endif
}

// Normalizes a path that already exists; if it does not exist, keeps it as is
// (scanDirs ignores missing directories).
std::string normalizePath(const std::string& p) {
#if !defined(_WIN32)                  // Windows: mantém como está (scanDirs
    char buf[PATH_MAX];               // ignora pastas ausentes de qualquer forma)
    if (realpath(p.c_str(), buf)) return buf;
#endif
    return p;
}

} // namespace

std::vector<std::string> defaultPluginDirs() {
    std::vector<std::string> out;
    // 1) explicit path (tests, non-standard installations, and development)
    splitDirs(std::getenv("NODESIM_PLUGIN_PATH"), out);
    // 2) project local folder: keeps `make plugins` working without installing
    addUnique(out, "plugins");
    // 3) installation relative to the executable: $PREFIX/bin/nodesim finds
    //    $PREFIX/share/nodesim/plugins (and the lib/ variant) without depending on
    //    XDG_DATA_DIRS or on PREFIX being /usr or /usr/local.
    const std::string exeDir = executableDir();
    if (!exeDir.empty()) {
        addUnique(out, normalizePath(exeDir + "/../share/nodesim/plugins"));
        addUnique(out, normalizePath(exeDir + "/../lib/nodesim/plugins"));
    }
    // 4) user data (XDG)
    const char* xdgHome = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string dataHome;
    if (xdgHome && *xdgHome) dataHome = xdgHome;
    else dataHome = std::string(home && *home ? home : ".") + "/.local/share";
    addUnique(out, dataHome + "/nodesim/plugins");
    // 5) system data (XDG)
    const char* xdgDirs = std::getenv("XDG_DATA_DIRS");
    std::vector<std::string> systemDirs;
    splitDirs(xdgDirs && *xdgDirs ? xdgDirs : "/usr/local/share:/usr/share", systemDirs);
    for (const auto& d : systemDirs) addUnique(out, d + "/nodesim/plugins");
    return out;
}

namespace plugin_loader {

bool open(const std::string& path, LoadedPlugin& out, std::string& error) {
    out = LoadedPlugin{};
    void* h = NS_DL_OPEN(path.c_str());
    if (!h) {
        error = NS_DL_ERR();
        return false;
    }
    (void)NS_DL_ERR();
    auto sym = (const NsPlugin* (*)())NS_DL_SYM(h, "nodesim_plugin");
    if (const char* err = dlerror()) {
        error = std::string("missing nodesim_plugin (") + err + ")";
        NS_DL_CLOSE(h);
        return false;
    }
    const NsPlugin* p = sym ? sym() : nullptr;
    if (!p) {
        error = "nodesim_plugin returned null";
        NS_DL_CLOSE(h);
        return false;
    }
    if (p->api != NODESIM_PLUGIN_API) {
        error = "ABI " + std::to_string(p->api) + " (expected " +
                std::to_string(NODESIM_PLUGIN_API) + ")";
        NS_DL_CLOSE(h);
        return false;
    }
    // struct_size == 0 is accepted for old manual initializers,
    // but the ABI 14 fields are not read in that case. If declared,
    // it must cover the minimal struct the host knows.
    if (p->struct_size != 0 && p->struct_size < (int)sizeof(NsPlugin)) {
        error = "struct_size " + std::to_string(p->struct_size) +
                " smaller than NsPlugin (" + std::to_string(sizeof(NsPlugin)) + ")";
        NS_DL_CLOSE(h);
        return false;
    }
    if (p->num_types <= 0 || p->num_types > 1024 || !p->types) {
        error = "invalid type table";
        NS_DL_CLOSE(h);
        return false;
    }
    out.handle = h;
    out.plugin = p;
    return true;
}

void close(void* handle) {
    if (handle) NS_DL_CLOSE(handle);
}


namespace {

std::string lowercase(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool validHash(const std::string& h) {
    if (h.size() != 64) return false;
    for (unsigned char c : h)
        if (!std::isxdigit(c)) return false;
    return true;
}

std::string fileDir(const std::string& file) {
    size_t p = file.rfind('/');
    std::string dir = p == std::string::npos ? "." : (p == 0 ? "/" : file.substr(0, p));
    char buf[PATH_MAX];
    if (realpath(dir.c_str(), buf)) return buf;
    return dir;
}

} // namespace

void loadResources(PluginRegistry& pm, PluginRegistry::Lib& lib) {
    (void)NS_DL_ERR();
    auto sym = (const NsResources* (*)())NS_DL_SYM(lib.h, "nodesim_plugin_resources");
    if (!sym) {
        (void)NS_DL_ERR(); // absence is normal: clears the pending error
        return;
    }
    const NsResources* table = sym();
    if (!table) {
        pm.addProblem(lib.file, "nodesim_plugin_resources returned null");
        return;
    }
    if (table->api != NODESIM_PLUGIN_API) {
        pm.addProblem(lib.file, "resource table with ABI " + std::to_string(table->api) +
                                    " (expected " + std::to_string(NODESIM_PLUGIN_API) + ")");
        return;
    }
    if (table->struct_size != 0 && table->struct_size < (int)sizeof(NsResources)) {
        pm.addProblem(lib.file, "resource struct_size " +
                                    std::to_string(table->struct_size) + " smaller than NsResources (" +
                                    std::to_string(sizeof(NsResources)) + ")");
        return;
    }
    if (table->num_resources < 0 || table->num_resources > 1024) {
        pm.addProblem(lib.file, "invalid num_resources: " +
                                    std::to_string(table->num_resources));
        return;
    }
    if (table->num_resources > 0 && !table->resources) {
        pm.addProblem(lib.file, "num_resources > 0 without a resource table");
        return;
    }

    const std::string base = fileDir(lib.file);
    for (int i = 0; i < table->num_resources; ++i) {
        const NsResource& nr = table->resources[i];
        PluginRegistry::Lib::Resource r;
        r.id = nr.id ? nr.id : "";
        r.kind = nr.kind;
        r.path = nr.path ? nr.path : "";
        r.required = nr.required != 0;
        r.description = nr.description ? nr.description : "";
        r.sha256 = nr.sha256 ? lowercase(nr.sha256) : "";

        if (r.id.empty()) {
            pm.addProblem(lib.file, "resource without id (ignored)");
            continue;
        }
        if (std::any_of(lib.resources.begin(), lib.resources.end(),
                        [&](const PluginRegistry::Lib::Resource& x) { return x.id == r.id; })) {
            pm.addProblem(lib.file, "duplicate resource id ignored: " + r.id);
            continue;
        }
        if (r.kind != NS_RES_FILE && r.kind != NS_RES_DIR && r.kind != NS_RES_ENV) {
            pm.addProblem(lib.file, "resource '" + r.id + "' with invalid kind: " +
                                        std::to_string(r.kind));
            continue;
        }
        if (r.path.empty()) {
            r.error = "empty path";
            pm.addProblem(lib.file, "resource '" + r.id + "' without path");
            lib.resources.push_back(std::move(r));
            continue;
        }
        if (r.kind != NS_RES_FILE) {
            r.sha256.clear(); // hash only applies to a file
        } else if (!r.sha256.empty() && !validHash(r.sha256)) {
            pm.addProblem(lib.file, "resource '" + r.id + "' with invalid sha256 (ignored)");
            r.sha256.clear();
        }

        if (r.kind == NS_RES_ENV) {
            r.resolved = r.path; // variable name; the value is never read
            r.present = std::getenv(r.path.c_str()) != nullptr;
            if (!r.present) r.error = "missing variable";
        } else {
            r.resolved = (!r.path.empty() && r.path[0] == '/') ? r.path : base + "/" + r.path;
            struct stat st;
            if (stat(r.resolved.c_str(), &st) != 0) {
                r.present = false;
                r.error = "missing";
            } else if (r.kind == NS_RES_FILE && !S_ISREG(st.st_mode)) {
                r.error = "not a regular file";
            } else if (r.kind == NS_RES_DIR && !S_ISDIR(st.st_mode)) {
                r.error = "not a directory";
            } else {
                r.present = true;
                r.bytes = (long long)st.st_size;
                if (!r.sha256.empty()) {
                    std::string obtained = lowercase(sha256File(r.resolved));
                    if (obtained.empty()) {
                        r.error = "could not read to check sha256";
                        pm.addProblem(lib.file, "resource '" + r.id + "': " + r.error);
                    } else if (obtained != r.sha256) {
                        r.error = "sha256 mismatch (expected " + r.sha256 + ", got " +
                                  obtained + ")";
                        pm.addProblem(lib.file, "resource '" + r.id + "': " + r.error);
                    }
                }
            }
        }

        if (!r.present && r.required) {
            const bool missing = r.error.empty() || r.error == "missing" ||
                                  r.error == "missing variable";
            pm.addProblem(lib.file, std::string("required resource ") +
                                        (missing ? "missing" : "invalid") + ": '" + r.id +
                                        "' (" + r.path + ")" +
                                        (missing ? "" : ": " + r.error));
        }
        lib.resources.push_back(std::move(r));
    }
}

int loadExec(PluginRegistry& pm, size_t libIndex) {
    if (libIndex >= pm.libs.size()) return 0;
    PluginRegistry::Lib& lib = pm.libs[libIndex];
    (void)NS_DL_ERR();
    auto sym = (const NsExecTable* (*)())NS_DL_SYM(lib.h, "nodesim_plugin_exec");
    if (!sym) {
        (void)NS_DL_ERR();
        return 0;
    }
    const NsExecTable* table = sym();
    if (!table) {
        pm.addProblem(lib.file, "nodesim_plugin_exec returned null");
        return 0;
    }
    if (table->api != NODESIM_PLUGIN_API) {
        pm.addProblem(lib.file, "execution table with ABI " + std::to_string(table->api) +
                                    " (expected " + std::to_string(NODESIM_PLUGIN_API) + ")");
        return 0;
    }
    if (table->struct_size != 0 && table->struct_size < (int)sizeof(NsExecTable)) {
        pm.addProblem(lib.file, "execution struct_size " +
                                    std::to_string(table->struct_size) +
                                    " smaller than NsExecTable (" +
                                    std::to_string(sizeof(NsExecTable)) + ")");
        return 0;
    }
    if (table->num_entries < 0 || table->num_entries > 1024) {
        pm.addProblem(lib.file, "invalid execution num_entries: " +
                                    std::to_string(table->num_entries));
        return 0;
    }
    if (table->num_entries > 0 && !table->entries) {
        pm.addProblem(lib.file, "num_entries > 0 without an execution table");
        return 0;
    }

    int applied = 0;
    std::vector<std::string> seen;
    for (int i = 0; i < table->num_entries; ++i) {
        const NsExec& ne = table->entries[i];
        const std::string id = ne.type ? ne.type : "";
        if (id.empty()) {
            pm.addProblem(lib.file, "execution policy without type (ignored)");
            continue;
        }
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            pm.addProblem(lib.file, "duplicate execution policy ignored: " + id);
            continue;
        }
        seen.push_back(id);
        if (ne.substeps < 0 || ne.substeps > 1024) {
            pm.addProblem(lib.file, "invalid substeps for '" + id + "': " +
                                        " (expected 1..1024)");
            continue;
        }
        const int sub = ne.substeps == 0 ? 1 : ne.substeps;
        auto it = std::find_if(pm.types.begin(), pm.types.end(),
                               [&](const PluginRegistry::RegType& t) {
                                   return t.lib == (int)libIndex && t.id == id;
                               });
        if (it == pm.types.end()) {
            pm.addProblem(lib.file, "execution policy for unknown type: " + id);
            continue;
        }
        it->substeps = sub;
        ++applied;
    }
    return applied;
}

int loadEvents(PluginRegistry& pm, size_t libIndex) {
    if (libIndex >= pm.libs.size()) return 0;
    PluginRegistry::Lib& lib = pm.libs[libIndex];
    (void)NS_DL_ERR();
    auto sym = (const NsEvents* (*)())NS_DL_SYM(lib.h, "nodesim_plugin_events");
    if (!sym) {
        (void)NS_DL_ERR();
        return 0;
    }
    const NsEvents* table = sym();
    if (!table) {
        pm.addProblem(lib.file, "nodesim_plugin_events returned null");
        return 0;
    }
    if (table->api != NODESIM_PLUGIN_API) {
        pm.addProblem(lib.file, "event table with ABI " + std::to_string(table->api) +
                                    " (expected " + std::to_string(NODESIM_PLUGIN_API) + ")");
        return 0;
    }
    if (table->struct_size != 0 && table->struct_size < (int)sizeof(NsEvents)) {
        pm.addProblem(lib.file, "event struct_size " +
                                    std::to_string(table->struct_size) +
                                    " smaller than NsEvents (" + std::to_string(sizeof(NsEvents)) + ")");
        return 0;
    }
    if (table->num_sources < 0 || table->num_sources > 1024 ||
        table->num_sinks < 0 || table->num_sinks > 1024) {
        pm.addProblem(lib.file, "invalid count in event table");
        return 0;
    }
    if (table->num_sources > 0 && !table->sources) {
        pm.addProblem(lib.file, "num_sources > 0 without an event source table");
        return 0;
    }
    if (table->num_sinks > 0 && !table->sinks) {
        pm.addProblem(lib.file, "num_sinks > 0 without an event sink table");
        return 0;
    }

    auto pluginKind = [&](const std::string& id) -> PluginRegistry::RegType* {
        for (auto& t : pm.types)
            if (t.lib == (int)libIndex && t.id == id) return &t;
        return nullptr;
    };

    int applied = 0;
    for (int i = 0; i < table->num_sources; ++i) {
        const NsEventSource& f = table->sources[i];
        const std::string id = f.type ? f.type : "";
        if (id.empty()) {
            pm.addProblem(lib.file, "event source without type (ignored)");
            continue;
        }
        PluginRegistry::RegType* t = pluginKind(id);
        if (!t) {
            pm.addProblem(lib.file, "event source for unknown type: " + id);
            continue;
        }
        if (f.port < 0 || f.port >= NS_MAX_PORTS) {
            pm.addProblem(lib.file, "invalid port in event source of '" + id + "': " +
                                        std::to_string(f.port));
            continue;
        }
        if (f.kind != NS_EVENT_RISE && f.kind != NS_EVENT_FALL && f.kind != NS_EVENT_CHANGE) {
            pm.addProblem(lib.file, "invalid edge in event source of '" + id + "'");
            continue;
        }
        if (!std::isfinite(f.threshold)) {
            pm.addProblem(lib.file, "non-finite threshold in event source of '" + id + "'");
            continue;
        }
        if (f.kind == NS_EVENT_CHANGE && f.threshold != 0.0) {
            pm.addProblem(lib.file, "change with a non-zero threshold in '" + id + "'");
            continue;
        }
        bool duplicate = false;
        for (const auto& e : t->events) if (e.port == f.port) duplicate = true;
        if (duplicate) {
            pm.addProblem(lib.file, "duplicate event source in '" + id + "' port " +
                                        std::to_string(f.port));
            continue;
        }
        t->events.push_back({f.port, f.kind, f.threshold});
        ++applied;
    }
    for (int i = 0; i < table->num_sinks; ++i) {
        const NsEventSink& s = table->sinks[i];
        const std::string id = s.type ? s.type : "";
        if (id.empty()) {
            pm.addProblem(lib.file, "event sink without type (ignored)");
            continue;
        }
        PluginRegistry::RegType* t = pluginKind(id);
        if (!t) {
            pm.addProblem(lib.file, "event sink for unknown type: " + id);
            continue;
        }
        if (!s.on_event) {
            pm.addProblem(lib.file, "event sink of '" + id + "' without callback");
            continue;
        }
        if (t->onEvent) {
            pm.addProblem(lib.file, "duplicate event sink in '" + id + "'");
            continue;
        }
        t->onEvent = s.on_event;
        ++applied;
    }
    return applied;
}



// ---- In-node UI (additive ABI 14 extension: nodesim_plugin_ui) ----
int loadUi(PluginRegistry& pm, size_t libIndex) {
    if (libIndex >= pm.libs.size()) return 0;
    PluginRegistry::Lib& lib = pm.libs[libIndex];
    (void)NS_DL_ERR();
    auto sym = (const NsUiTable* (*)())NS_DL_SYM(lib.h, "nodesim_plugin_ui");
    if (!sym) {
        (void)NS_DL_ERR(); // absence is normal (generic parameter editor remains)
        return 0;
    }
    const NsUiTable* table = sym();
    if (!table) {
        pm.addProblem(lib.file, "nodesim_plugin_ui returned null");
        return 0;
    }
    if (table->api != NODESIM_PLUGIN_API) {
        pm.addProblem(lib.file, "ui table with ABI " + std::to_string(table->api) +
                                    " (expected " + std::to_string(NODESIM_PLUGIN_API) + ")");
        return 0;
    }
    if (table->struct_size != 0 && table->struct_size < (int)sizeof(NsUiTable)) {
        pm.addProblem(lib.file, "ui struct_size " +
                                    std::to_string(table->struct_size) +
                                    " smaller than NsUiTable (" +
                                    std::to_string(sizeof(NsUiTable)) + ")");
        return 0;
    }
    if (table->num_specs < 0 || table->num_specs > 1024) {
        pm.addProblem(lib.file, "invalid num_specs in the ui table");
        return 0;
    }
    if (table->num_specs > 0 && !table->specs) {
        pm.addProblem(lib.file, "num_specs > 0 without a spec table");
        return 0;
    }

    auto pluginKind = [&](const std::string& id) -> PluginRegistry::RegType* {
        for (auto& t : pm.types)
            if (t.lib == (int)libIndex && t.id == id) return &t;
        return nullptr;
    };

    int applied = 0;
    for (int i = 0; i < table->num_specs; ++i) {
        const NsUiSpec& sp = table->specs[i];
        const std::string id = sp.type ? sp.type : "";
        if (id.empty()) {
            pm.addProblem(lib.file, "ui spec without type (ignored)");
            continue;
        }
        PluginRegistry::RegType* t = pluginKind(id);
        if (!t) {
            pm.addProblem(lib.file, "ui spec for unknown type: " + id);
            continue;
        }
        if (sp.num_items < 0 || sp.num_items > 512) {
            pm.addProblem(lib.file, "invalid num_items in the ui spec of '" + id + "'");
            continue;
        }
        if (sp.num_items > 0 && !sp.items) {
            pm.addProblem(lib.file, "num_items > 0 without an item table in '" + id + "'");
            continue;
        }
        t->ui.clear();
        int appliedType = 0;
        for (int k = 0; k < sp.num_items; ++k) {
            const NsUiItem& it = sp.items[k];
            PluginRegistry::RegType::UiItem d;
            d.kind = it.kind;
            d.name = it.name ? it.name : "";
            d.index = it.index;
            d.min = it.min, d.max = it.max, d.step = it.step;
            d.flags = it.flags;
            const bool binds = d.kind == NS_UI_SLIDER || d.kind == NS_UI_INT ||
                               d.kind == NS_UI_SWITCH || d.kind == NS_UI_COMBO ||
                               d.kind == NS_UI_LED;
            if (d.kind == NS_UI_END) break;
            const bool textKind = d.kind == NS_UI_TEXT;
            const bool labelKind = d.kind == NS_UI_LABEL || d.kind == NS_UI_VSPACE;
            if (binds) {
                if (d.index < 0 ||
                     d.index >= (textKind ? (int)t->texts.size() : (int)t->params.size())) {
                    pm.addProblem(lib.file,
                                  "ui item with an index out of bounds in '" + id + "'");
                    continue;
                }
                // A switch bound to an INT/NUM still works: >= 0.5 turns on.
                // A slider bound to an ENUM/BOOL would write outside the domain;
                // keep the host permissive (clamp on draw) but warn.
                if (!textKind) {
                    const PluginRegistry::Param& sr = t->params[(size_t)d.index];
                    const bool okKind =
                        (d.kind == NS_UI_SLIDER && sr.kind == NS_PARAM_NUM) ||
                        (d.kind == NS_UI_INT && sr.kind == NS_PARAM_INT) ||
                        (d.kind == NS_UI_SWITCH && sr.kind == NS_PARAM_BOOL) ||
                        (d.kind == NS_UI_COMBO && sr.kind == NS_PARAM_ENUM) ||
                        (d.kind == NS_UI_LED);
                    if (!okKind)
                        pm.addProblem(lib.file, "ui item '" + (d.name.empty() ? sr.name : d.name) +
                                                   "' in '" + id +
                                                   "': kind does not match the parameter");
                }
            } else if (!labelKind) {
                pm.addProblem(lib.file, "ui item without valid binding in '" + id + "'");
                continue;
            }
            t->ui.push_back(d);
            ++appliedType;
        }
        if (appliedType > 0) ++applied;
    }
    return applied;
}



// ---- Canvas CLICK actions (additive ABI 14 extension: nodesim_plugin_acts) --
int loadActs(PluginRegistry& pm, size_t libIndex) {
    if (libIndex >= pm.libs.size()) return 0;
    PluginRegistry::Lib& lib = pm.libs[libIndex];
    (void)NS_DL_ERR();
    auto sym = (const NsActTable* (*)())NS_DL_SYM(lib.h, "nodesim_plugin_acts");
    if (!sym) {
        (void)NS_DL_ERR(); // absence is normal
        return 0;
    }
    const NsActTable* table = sym();
    if (!table) {
        pm.addProblem(lib.file, "nodesim_plugin_acts returned null");
        return 0;
    }
    if (table->api != NODESIM_PLUGIN_API) {
        pm.addProblem(lib.file, "acts table with ABI " + std::to_string(table->api) +
                                    " (expected " + std::to_string(NODESIM_PLUGIN_API) + ")");
        return 0;
    }
    if (table->struct_size != 0 && table->struct_size < (int)sizeof(NsActTable)) {
        pm.addProblem(lib.file, "acts struct_size " +
                                    std::to_string(table->struct_size) +
                                    " smaller than NsActTable (" +
                                    std::to_string(sizeof(NsActTable)) + ")");
        return 0;
    }
    if (table->num_entries < 0 || table->num_entries > 1024) {
        pm.addProblem(lib.file, "invalid num_entries in the acts table");
        return 0;
    }
    if (table->num_entries > 0 && !table->entries) {
        pm.addProblem(lib.file, "num_entries > 0 without an acts table");
        return 0;
    }

    auto pluginKind = [&](const std::string& id) -> PluginRegistry::RegType* {
        for (auto& t : pm.types)
            if (t.lib == (int)libIndex && t.id == id) return &t;
        return nullptr;
    };

    int applied = 0;
    for (int i = 0; i < table->num_entries; ++i) {
        const NsAct& a = table->entries[i];
        const std::string id = a.type ? a.type : "";
        if (id.empty()) {
            pm.addProblem(lib.file, "act without type (ignored)");
            continue;
        }
        PluginRegistry::RegType* t = pluginKind(id);
        if (!t) {
            pm.addProblem(lib.file, "act for unknown type: " + id);
            continue;
        }
        if (a.kind != NS_ACT_HOLD && a.kind != NS_ACT_TOGGLE) {
            pm.addProblem(lib.file, "invalid kind in act of '" + id + "'");
            continue;
        }
        if (a.param < 0 || a.param >= (int)t->params.size()) {
            pm.addProblem(lib.file, "act of '" + id + "' with an out-of-range param index");
            continue;
        }
        bool dup = false;
        for (const auto& d : t->acts)
            if (d.param == a.param) dup = true;
        if (dup) {
            pm.addProblem(lib.file, "duplicate act on param " +
                                        std::to_string(a.param) + " of '" + id + "'");
            continue;
        }
        const int inset = table->struct_size >= (int)sizeof(NsActTable)
                              ? table->inset
                              : 0; // old tables: no margin behavior
        t->acts.push_back({a.kind, a.param, inset});
        ++applied;
    }
    return applied;
}

} // namespace plugin_loader
