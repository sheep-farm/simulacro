// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: stop after a simulation run time.
//
//   parat / "Timed stop"
//     - `tempo` + `unidade` (ms, s, min, h): accumulates SIMULATION time
//       while enabled; on reaching the limit it publishes 1 for one step.
//     - The host pauses (NS_CAP_PAUSE capability). Since the pulse is single, resuming
//       manually does not make the stop fire again; F5/Restart re-arms it.
//     - Optional input 0: with wire at 0, it clears and disarms the counter; with 1
//       (or no wire) it counts normally. This allows arming/re-arming by signal.
//
// The time is simulation time (`dt` per step), not wall-clock time: it is
// deterministic and works the same in headless mode.
// Build: make plugins | Usage: Add menu -> Control -> Timed stop
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

struct Stop {
    double time = 60.0;  // value in the chosen unit
    double unit = 1.0; // 0=ms, 1=s, 2=min, 3=h
    double accumulated = 0; // simulation seconds since arming
    double fired = 0;
};

void* create() { return new (std::nothrow) Stop(); }
void destroy(void* h) { delete (Stop*)h; }

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

double unitFactor(const Stop* d) {
    switch ((int)std::lround(d->unit)) {
        case 0: return 0.001; // ms
        case 2: return 60.0;  // min
        case 3: return 3600.0; // h
        default: return 1.0;   // s
    }
}

double limitSeconds(const Stop* d) {
    double v = d->time * unitFactor(d);
    if (!std::isfinite(v) || v < 0.0) v = 0.0;
    if (v > 1e9) v = 1e9;
    return v;
}

double tick(void* h, const NsContext* c) {
    Stop* d = (Stop*)h;
    if (!d || !c) return 0.0;
    bool hasWire = false;
    const double hab = sock0(c, hasWire);
    if (hasWire && !(hab > 0.0)) { // disabled: clears and re-arms
        d->accumulated = 0.0;
        d->fired = 0.0;
        return 0.0;
    }
    if (d->fired > 0.5) return 0.0; // single pulse; resuming does not repeat
    const double lim = limitSeconds(d);
    const double dt = (c->dt > 0.0 && std::isfinite(c->dt)) ? c->dt : 0.0;
    d->accumulated += dt;
    if (d->accumulated + 1e-12 >= lim) {
        d->fired = 1.0;
        return 1.0;
    }
    return 0.0;
}

char* save(void* h) {
    Stop* d = (Stop*)h;
    if (!d) return nullptr;
    char b[160];
    std::snprintf(b, sizeof b, "time=%.17g unit=%.17g accumulated=%.17g fired=%.17g",
                  d->time, d->unit, d->accumulated, d->fired);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    Stop* d = (Stop*)h;
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
        if (name == "time") d->time = v;
        else if (name == "unit") d->unit = v;
        else if (name == "accumulated") d->accumulated = v;
        else if (name == "fired") d->fired = v;
        else break;
        some = true;
    }
    if (!some) return 0;
    if (!std::isfinite(d->time) || d->time < 0.0) d->time = 0.0;
    if (d->time > 1000000.0) d->time = 1000000.0;
    d->unit = std::max(0.0, std::min(3.0, std::round(d->unit)));
    // Each load/restart re-arms: a "Save current" after the stop must not
    // leave the node permanently triggered (the run time starts over).
    d->accumulated = 0.0;
    d->fired = 0.0;
    return 1;
}

static const char* const kUnits[] = {"ms", "s", "min", "h"};
NsParam params[] = {
    {"time", 0, 1000000, 60, NS_PARAM_NUM, 1, nullptr,
     "simulation time until stop (in the chosen unit)", 0, nullptr},
    {"unit", 0, 3, 1, NS_PARAM_ENUM, 1, nullptr,
     "time unit (ms, s, min or h)", 4, kUnits},
};

double getParam(void* h, int i) {
    Stop* d = (Stop*)h;
    if (!d) return 0;
    return i == 0 ? d->time : d->unit;
}

int setParam(void* h, int i, double v) {
    Stop* d = (Stop*)h;
    if (!d || i < 0 || i > 1) return 0;
    if (i == 0) {
        if (!(v >= 0.0)) v = 0.0;
        if (v > 1000000.0) v = 1000000.0;
        if (v != d->time) { d->accumulated = 0.0; d->fired = 0.0; }
        d->time = v;
    } else {
        const double u = std::max(0.0, std::min(3.0, std::round(v)));
        if (u != d->unit) { d->accumulated = 0.0; d->fired = 0.0; }
        d->unit = u;
    }
    return 1;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("hab") : nullptr; }
char* labelOut(void*, int p) { return p == 0 ? dupStr("stop") : nullptr; }

NsNodeType kind() {
    NsNodeType t{};
    t.id = "timed_stop";
    t.label = "Timed stop";
    t.color[0] = 240; t.color[1] = 120; t.color[2] = 120;
    t.create = create;
    t.destroy = destroy;
    t.step = tick;
    t.save = save;
    t.load = load;
    t.num_params = 2;
    t.params = params;
    t.getParam = getParam;
    t.setParam = setParam;
    t.def_in = 1; // optional enable
    t.portLabel = labelOut;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_STATE | NS_CAP_PARAMS | NS_CAP_INPUT_LABELS | NS_CAP_PAUSE;
    return t;
}
NsNodeType types[] = {kind()};

NsPlugin plugin = NS_PLUGIN_BUILTIN("timed_stop", "1.1", 1, types, "Control");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
