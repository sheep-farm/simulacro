// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: periodic binary pulse generator.
//
//   pulsems / "Pulse generator (ms)"
//     - output 1 during `largura` ms at the start of each `periodo` ms;
//     - `fase` shifts the pulse start within the period;
//     - optional input 0: with wire at 0 the output stays at 0; with no wire (or 1)
//       it generates freely.
//
// The time is simulation time (`t`): deterministic and the same in headless mode.
// Example: period=1000, width=100 -> 1 in [0,0.1)s, 0 in [0.1,1)s, ...
// Build: make plugins | Usage: Add menu -> Signal -> Pulse generator (ms)
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

struct Pulse {
    double period = 1000.0; // ms
    double width = 100.0;  // ms
    double phase = 0.0;       // ms
};

void* create() { return new (std::nothrow) Pulse(); }
void destroy(void* h) { delete (Pulse*)h; }

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
    Pulse* d = (Pulse*)h;
    if (!d || !c) return 0.0;
    bool hasWire = false;
    const double hab = sock0(c, hasWire);
    if (hasWire && !(hab > 0.0)) return 0.0;
    const double period = d->period > 0.0 ? d->period : 1.0;
    double width = d->width;
    if (!(width >= 0.0)) width = 0.0;
    if (width > period) width = period;
    if (width <= 0.0) return 0.0;
    double phase = std::fmod(c->t * 1000.0 + d->phase, period);
    if (phase < 0.0) phase += period;
    return phase < width ? 1.0 : 0.0;
}

char* save(void* h) {
    Pulse* d = (Pulse*)h;
    if (!d) return nullptr;
    char b[128];
    std::snprintf(b, sizeof b, "period=%.17g width=%.17g phase=%.17g", d->period,
                  d->width, d->phase);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    Pulse* d = (Pulse*)h;
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
        if (name == "period") d->period = v;
        else if (name == "width") d->width = v;
        else if (name == "phase") d->phase = v;
        else break;
        some = true;
    }
    if (!some) return 0;
    if (!(d->period >= 1.0) || !std::isfinite(d->period)) d->period = 1.0;
    if (d->period > 3600000.0) d->period = 3600000.0;
    if (!(d->width >= 0.0) || !std::isfinite(d->width)) d->width = 0.0;
    if (d->width > d->period) d->width = d->period;
    if (!std::isfinite(d->phase)) d->phase = 0.0;
    return 1;
}

NsParam params[] = {
    {"period", 1, 3600000, 1000, NS_PARAM_NUM, 10, "ms",
     "interval between pulse starts", 0, nullptr},
    {"width", 0, 3600000, 100, NS_PARAM_NUM, 10, "ms",
     "pulse width (limited to the period)", 0, nullptr},
    {"phase", 0, 3600000, 0, NS_PARAM_NUM, 10, "ms",
     "pulse offset within the period", 0, nullptr},
};

double getParam(void* h, int i) {
    Pulse* d = (Pulse*)h;
    if (!d) return 0;
    if (i == 0) return d->period;
    if (i == 1) return d->width;
    return d->phase;
}

int setParam(void* h, int i, double v) {
    Pulse* d = (Pulse*)h;
    if (!d || i < 0 || i > 2) return 0;
    if (i == 0) {
        if (!(v >= 1.0)) v = 1.0;
        if (v > 3600000.0) v = 3600000.0;
        d->period = v;
        if (d->width > d->period) d->width = d->period;
    } else if (i == 1) {
        if (!(v >= 0.0)) v = 0.0;
        if (v > d->period) v = d->period;
        d->width = v;
    } else {
        if (!std::isfinite(v)) v = 0.0;
        d->phase = v;
    }
    return 1;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("hab") : nullptr; }
char* labelOut(void*, int p) { return p == 0 ? dupStr("0/1") : nullptr; }

NsNodeType kind() {
    NsNodeType t{};
    t.id = "pulse_ms";
    t.label = "Pulse generator (ms)";
    t.color[0] = 235; t.color[1] = 190; t.color[2] = 90;
    t.create = create;
    t.destroy = destroy;
    t.step = tick;
    t.save = save;
    t.load = load;
    t.num_params = 3;
    t.params = params;
    t.getParam = getParam;
    t.setParam = setParam;
    t.def_in = 1;
    t.portLabel = labelOut;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_PARAMS | NS_CAP_INPUT_LABELS;
    return t;
}
NsNodeType types[] = {kind()};

NsPlugin plugin = NS_PLUGIN_BUILTIN("pulse", "1.0", 1, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
