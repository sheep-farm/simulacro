// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin registry: type metadata, parameters, resources and load problems.
// Loading (dlopen/dlsym) lives in loader.h; live handles in instance.h.
#pragma once
#include "../../sdk/nodesim_plugin.h"
#include "instance.h"
#include <string>
#include <vector>

struct PluginRegistry {
    struct Param {
        std::string name;
        double min = 0, max = 0, def = 0;
        int kind = NS_PARAM_NUM;
        double step = 0;
        std::string unit, help;
        std::vector<std::string> choices;
    };
    struct RegType {
        std::string id, label;
        float color[3] = {200, 200, 200};
        void* (*create)() = nullptr;
        void (*destroy)(void*) = nullptr;
        double (*step)(void*, const NsContext*) = nullptr;
        int (*stepMulti)(void*, const NsContext*, double*, int) = nullptr;
        char* (*save)(void*) = nullptr;
        int (*load)(void*, const char*) = nullptr;
        std::vector<Param> params;
        double (*getParam)(void*, int) = nullptr;
        int (*setParam)(void*, int, double) = nullptr;
        std::vector<std::string> texts; // text parameters (name)
        char* (*getText)(void*, int) = nullptr;
        int (*setText)(void*, int, const char*) = nullptr;
        bool selfOut = false;             // plugin controls the number of outputs
        int (*numOut)(void*) = nullptr;   // desired number of ports (0 = host controls)
        char* (*outText)(void*, int) = nullptr; // text per port (null = numeric)
        char* (*portLabel)(void*, int) = nullptr; // name of port k (null = "p<k>")
        char* (*portLabelIn)(void*, int) = nullptr; // name of input socket k (null = none)
        std::string group;                // category (empty = "General")
        int defIn = 1;                    // default inputs on creation (1..8)
        int prefW = 0, prefH = 0;         // preferred node size (0 = host computes)
        void (*draw)(void*, const NsDraw*, const NsRect*) = nullptr; // body drawing
        int shape = NS_SHAPE_BOX;         // node shape (box = default)
        int (*outValue)(void*, int, NsValue*) = nullptr; // value per port (ABI 10)
        int capabilities = 0;             // effective NsCaps (inferred + declared)
        int lib = -1;                     // index in libs (catalog/--tipos)
        int substeps = 1;                 // internal calls per step (1..1024)
        std::vector<EventDecl> events;     // sources declared by nodesim_plugin_events
        void (*onEvent)(void*, const NsEvent*) = nullptr; // optional sink
        // Additive ABI 14 extension: declarative in-node UI (nodesim_plugin_ui).
        struct UiItem {
            int kind = NS_UI_END;
            std::string name;             // custom label / caption (empty = param name)
            int index = -1;               // params[] or texts[] index
            double min = 0, max = 0, step = 0; // optional overrides
            int flags = 0;
        };
        std::vector<UiItem> ui;           // per-type layout (ordered)
        // Additive ABI 14 extension: canvas click actions (nodesim_plugin_acts)
        struct ActDecl { int kind = NS_ACT_HOLD; int param = 0; int inset = 0; };
        std::vector<ActDecl> acts;
    };
    struct Lib {
        // Resource declared by a plugin (optional symbol
        // `nodesim_plugin_resources`). `resolved` is the absolute path for
        // FILE/DIR (relative to the .so directory) or the variable name for
        // ENV. An ENV value is NEVER read/displayed, only its presence.
        struct Resource {
            std::string id;
            int kind = NS_RES_FILE;
            std::string path;        // declared path
            std::string resolved;    // absolute path or variable name
            bool required = false;
            std::string description;
            std::string sha256;      // declared expected hash ("" = not required)
            bool present = false;
            long long bytes = -1;    // file: size; dir/env: -1
            std::string error;       // reason for absence/nonconformity
        };
        void* h = nullptr;
        std::string file, name, version;
        int api = 0;          // API declared by the .so
        int structSize = 0;   // NsPlugin struct_size (0 = old initializer)
        int capabilities = 0; // reserved global capabilities
        // ABI 14: identity/provenance (strings may be empty).
        std::string author, license, homepage, description, build;
        // Additive ABI 14 extension: declared external resources.
        std::vector<Resource> resources;
    };
    struct Problem { std::string file, message; };

    std::vector<Lib> libs;
    std::vector<RegType> types;
    std::vector<std::string> dirs;    // directories actually scanned
    std::vector<Problem> problems;    // rejections/warnings for --check-plugins
    bool quiet = false; // --types: does not mix scan log with JSON on stdout

    void addProblem(const std::string& file, const std::string& message);
    const RegType* find(const std::string& id) const;
    bool colorOf(const std::string& id, float rgb[3]) const;
};

// Resource type name for catalog/diagnostics/manifest.
inline const char* resourceKindName(int kind) {
    switch (kind) {
        case NS_RES_FILE: return "file";
        case NS_RES_DIR: return "directory";
        case NS_RES_ENV: return "env";
        default: return "unknown";
    }
}

// Adds one library's identity/provenance metadata (ABI 13/14). `handle` and
// `p` must already be validated by the loader. Returns the new lib index.
namespace plugin_registry {
int addLibrary(PluginRegistry& reg, const std::string& path, void* handle,
               const NsPlugin* p);
// Registers the types and parameter/text metadata of libIndex.
// Returns the number of accepted types.
int registerTypes(PluginRegistry& reg, size_t libIndex, const std::string& path,
                  const NsPlugin* p);
} // namespace plugin_registry
