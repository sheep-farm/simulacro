// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Example plugin: gain with a structured parameter.
// Shows declared params (the host generates the editor): k in [-10,10], default 1.
// Build: make plugins | Usage: Add menu -> Gain K (plugin)
#include <plugin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
struct Gain {
    double k = 1;
};

void* create() { return new (std::nothrow) Gain(); }
void destroy(void* h) { delete (Gain*)h; }

double step(void* h, const NsContext* c) {
    Gain* d = (Gain*)h;
    if (!d || !c) return 0;
    double in = (c->nin > 0 && c->in) ? c->in[0] : 0.0;
    return d->k * in;
}

char* save(void* h) {
    Gain* d = (Gain*)h;
    if (!d) return nullptr;
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", d->k);
    char* o = (char*)std::malloc(std::strlen(b) + 1);
    if (o) std::memcpy(o, b, std::strlen(b) + 1);
    return o;
}

int load(void* h, const char* s) {
    Gain* d = (Gain*)h;
    if (!d || !s) return 0;
    char* e = nullptr;
    double v = std::strtod(s, &e);
    while (*e == ' ' || *e == '\t') ++e;
    if (e == s || *e != '\0') return 0;
    d->k = v;
    return 1;
}

double getParam(void* h, int i) {
    Gain* d = (Gain*)h;
    if (!d || i != 0) return 0;
    return d->k;
}

int setParam(void* h, int i, double v) {
    Gain* d = (Gain*)h;
    if (!d || i != 0) return 0;
    d->k = v; // host already clamps to [min,max]
    return 1;
}

NsParam params[] = {
    {"k", -10.0, 10.0, 1.0, NS_PARAM_NUM, 0.1, nullptr,
     "multiplicative gain", 0, nullptr},
};

NsNodeType types[] = {
    {"pgain", "Gain K", {255, 180, 100}, create, destroy, step, save, load,
     1, params, getParam, setParam},
};

// Declarative in-node UI (additive ABI 14 extension): a single slider bound to
// param 0. The plugin still only knows about params/save/load; the *host* draws
// it. Third parties without the symbol keep the generic editor.
static const NsUiItem kItensK[] = {{NS_UI_SLIDER, "", 0, 0, 0, 0, 0}};
static const NsUiSpec kSpecsGanho[] = {{"pgain", 1, kItensK}};
NS_PLUGIN_UI(kSpecsGanho, 1);

NsPlugin plugin = NS_PLUGIN_BUILTIN("pgain", "1.0", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
