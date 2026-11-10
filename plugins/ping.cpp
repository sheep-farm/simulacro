// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: square-wave generator 0/1 per interval in milliseconds.
//
//   pingms / "Square wave (ms)"
//     - output 0 alternates 1/0 every `ms` (simulation time);
//     - `inicio`: 1 = starts at 1; 0 = starts at 0;
//     - input 0 is optional: with a wire, it works as an enable
//       (0 turns it off; 1 or no wire generates normally).
//
// It does not use wall-clock time: the result is deterministic and reproducible in
// headless mode. The full period is 2 × ms.
// Build: make plugins | Usage: Add menu -> Signal -> Square wave (ms)
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

struct Ping {
    double ms = 500.0;
    double start = 1.0;
};

void* create() { return new (std::nothrow) Ping(); }
void destroy(void* h) { delete (Ping*)h; }

double sock0(const NsContext* c, bool& hasWire) {
    hasWire = false;
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) {
        v += c->in[k];
        hasWire = true;
    }
    return v;
}

double tick(void* h, const NsContext* c) {
    Ping* d = (Ping*)h;
    if (!d || !c) return 0.0;
    bool hasWire = false;
    const double hab = sock0(c, hasWire);
    if (hasWire && !(hab > 0.0)) return 0.0; // input wired to 0 = off
    const double ms = d->ms > 0.0 ? d->ms : 1.0;
    const double half = ms / 1000.0; // seconds of each state
    const double period = 2.0 * half;
    double phase = std::fmod(c->t, period);
    if (phase < 0.0) phase += period;
    const bool high = d->start > 0.5 ? (phase < half) : (phase >= half);
    return high ? 1.0 : 0.0;
}

char* save(void* h) {
    Ping* d = (Ping*)h;
    if (!d) return nullptr;
    char b[96];
    std::snprintf(b, sizeof b, "ms=%.17g start=%.17g", d->ms, d->start);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    Ping* d = (Ping*)h;
    if (!d || !blob) return 0;
    bool some = false;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (*e != '=') break;
        const std::string name(p, e - p);
        p = e + 1;
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "ms") d->ms = v;
        else if (name == "start") d->start = v;
        else break;
        some = true;
    }
    if (!some) return 0;
    if (!(d->ms >= 1.0)) d->ms = 1.0;
    if (d->ms > 3600000.0) d->ms = 3600000.0;
    d->start = d->start > 0.5 ? 1.0 : 0.0;
    return 1;
}

NsParam params[] = {
    {"ms", 1, 3600000, 500, NS_PARAM_NUM, 10, "ms",
     "duration of each state (0 and 1) in milliseconds", 0, nullptr},
    {"start", 0, 1, 1, NS_PARAM_BOOL, 1, nullptr,
     "1 = starts at 1; 0 = starts at 0", 0, nullptr},
};

double getParam(void* h, int i) {
    Ping* d = (Ping*)h;
    if (!d) return 0;
    return i == 0 ? d->ms : d->start;
}

int setParam(void* h, int i, double v) {
    Ping* d = (Ping*)h;
    if (!d || i < 0 || i > 1) return 0;
    if (i == 0) {
        if (!(v >= 1.0)) v = 1.0;
        if (v > 3600000.0) v = 3600000.0;
        d->ms = v;
    } else {
        d->start = v > 0.5 ? 1.0 : 0.0;
    }
    return 1;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("hab") : nullptr; }
char* labelOut(void*, int p) { return p == 0 ? dupStr("0/1") : nullptr; }

NsNodeType kind() {
    NsNodeType t{};
    t.id = "ping_ms";
    t.label = "Square wave (ms)";
    t.color[0] = 120; t.color[1] = 220; t.color[2] = 180;
    t.create = create;
    t.destroy = destroy;
    t.step = tick;
    t.save = save;
    t.load = load;
    t.num_params = 2;
    t.params = params;
    t.getParam = getParam;
    t.setParam = setParam;
    t.def_in = 1; // optional enable (no wire = generates freely)
    t.portLabel = labelOut;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_STATE | NS_CAP_PARAMS | NS_CAP_INPUT_LABELS;
    return t;
}
NsNodeType types[] = {kind()};

NsPlugin plugin = NS_PLUGIN_BUILTIN("ping", "1.0", 1, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
