// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: 0/1 memory elements.
//
//   dff / "Flip-flop D"  inputs D, CLK; on the CLK rising edge: Q = D.
//   tff / "Flip-flop T"  inputs T, CLK; on the rising edge: if T, Q = !Q.
//   sr  / "Latch SR"     inputs S, R; S sets, R resets, both at 0 holds.
//                        (S and R set together: reset dominates, Q = 0.)
//
// Port 0 = Q; port 1 (if present) = Qn (complement). The Q state and the
// previous clock level go into save/load; F5/Restart returns to the initial state.
// Build: make plugins | Usage: menu Add -> Logic
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
#include <string>

namespace {

enum Mode { DFF = 0, TFF = 1, SR = 2 };

struct FF {
    bool q = false;
    bool clk = false;
};

void* create() { return new (std::nothrow) FF(); }
void destroy(void* h) { delete (FF*)h; }

double sock(const NsContext* c, int s) {
    if (!c || !c->in || !c->sockOff || s < 0 || s >= c->nsock) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[s]; k < c->sockOff[s + 1]; ++k) v += c->in[k];
    return v;
}

template <int M>
double tick(void* h, const NsContext* c) {
    FF* d = (FF*)h;
    if (!d || !c) return 0.0;
    if (M == SR) {
        const bool s = sock(c, 0) != 0.0;
        const bool r = sock(c, 1) != 0.0;
        if (r) d->q = false;       // reset dominates
        else if (s) d->q = true;
    } else {
        const bool clk = sock(c, 1) != 0.0;
        if (clk && !d->clk) {
            if (M == DFF) d->q = sock(c, 0) != 0.0;
            else if (sock(c, 0) != 0.0) d->q = !d->q; // T
        }
        d->clk = clk;
    }
    return d->q ? 1.0 : 0.0;
}

template <int M>
int tickMulti(void* h, const NsContext* c, double* out, int nout) {
    if (!out || nout < 1) return 0;
    const double q = tick<M>(h, c);
    out[0] = q;
    if (nout > 1) out[1] = q != 0.0 ? 0.0 : 1.0; // Qn
    for (int k = 2; k < nout; ++k) out[k] = 0.0;
    return 1;
}

char* save(void* h) {
    FF* d = (FF*)h;
    if (!d) return nullptr;
    char b[64];
    std::snprintf(b, sizeof b, "q=%d clk=%d", d->q ? 1 : 0, d->clk ? 1 : 0);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    FF* d = (FF*)h;
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
        if (name == "q") d->q = v != 0.0;
        else if (name == "clk") d->clk = v != 0.0;
        else break;
        some = true;
    }
    return some ? 1 : 0;
}

char* labelOut(void*, int p) {
    if (p == 0) return dupStr("Q");
    if (p == 1) return dupStr("Qn");
    return nullptr;
}
char* labelInD(void*, int p) {
    if (p == 0) return dupStr("D");
    if (p == 1) return dupStr("CLK");
    return nullptr;
}
char* labelInT(void*, int p) {
    if (p == 0) return dupStr("T");
    if (p == 1) return dupStr("CLK");
    return nullptr;
}
char* labelInSR(void*, int p) {
    if (p == 0) return dupStr("S");
    if (p == 1) return dupStr("R");
    return nullptr;
}

NsNodeType kind(const char* id, const char* label, float r, float g, float b,
                double (*step)(void*, const NsContext*),
                int (*multi)(void*, const NsContext*, double*, int),
                char* (*inLabel)(void*, int)) {
    NsNodeType t{};
    t.id = id;
    t.label = label;
    t.color[0] = r; t.color[1] = g; t.color[2] = b;
    t.create = create;
    t.destroy = destroy;
    t.step = step;
    t.stepMulti = multi;
    t.save = save;
    t.load = load;
    t.def_in = 2;
    t.portLabel = labelOut;
    t.portLabelIn = inLabel;
    t.capabilities = NS_CAP_STATE | NS_CAP_STEP_MULTI | NS_CAP_INPUT_LABELS;
    return t;
}

NsNodeType types[] = {
    kind("dff", "Flip-flop D", 150, 200, 255, tick<DFF>, tickMulti<DFF>, labelInD),
    kind("tff", "Flip-flop T", 190, 180, 250, tick<TFF>, tickMulti<TFF>, labelInT),
    kind("sr", "Latch SR", 220, 180, 140, tick<SR>, tickMulti<SR>, labelInSR),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("flipflop", "1.0", 3, types, "Logic");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
