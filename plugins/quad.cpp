// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Example plugin: quadrature oscillator with multi-output.
// Port k = sin(2*pi*f*t + k*pi/2): p0=sin, p1=cos, p2=-sin, p3=-cos, ...
// Structured parameter f (Hz) in [0,20], default 1. nOut chosen in the panel.
// Build: make plugins | Usage: Add menu -> Quadrature (plugin)
#include <plugin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr double PI = 3.141592653589793;

struct Quad {
    double f = 1.0;
};

void* create() { return new (std::nothrow) Quad(); }
void destroy(void* h) { delete (Quad*)h; }

double phase(void* h, const NsContext* c, int k) {
    Quad* d = (Quad*)h;
    if (!d || !c) return 0;
    return std::sin(2.0 * PI * d->f * c->t + k * PI / 2.0);
}

double step(void* h, const NsContext* c) { return phase(h, c, 0); } // port 0

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    if (!h || !c || !out || nout <= 0) return 0;
    for (int k = 0; k < nout; ++k) out[k] = phase(h, c, k);
    return 1;
}

char* save(void* h) {
    Quad* d = (Quad*)h;
    if (!d) return nullptr;
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", d->f);
    char* o = (char*)std::malloc(std::strlen(b) + 1);
    if (o) std::memcpy(o, b, std::strlen(b) + 1);
    return o;
}

int load(void* h, const char* s) {
    Quad* d = (Quad*)h;
    if (!d || !s) return 0;
    char* e = nullptr;
    double v = std::strtod(s, &e);
    while (*e == ' ' || *e == '\t') ++e;
    if (e == s || *e != '\0') return 0;
    d->f = v;
    return 1;
}

double getParam(void* h, int i) {
    Quad* d = (Quad*)h;
    if (!d || i != 0) return 0;
    return d->f;
}

int setParam(void* h, int i, double v) {
    Quad* d = (Quad*)h;
    if (!d || i != 0) return 0;
    d->f = v; // host already clamps to [min,max]
    return 1;
}

NsParam params[] = {
    {"f", 0.0, 20.0, 1.0, NS_PARAM_NUM, 0.1, nullptr,
     "square-wave frequency", 0, nullptr},
};

NsNodeType types[] = {
    {"quad", "Quadrature", {180, 255, 200}, create, destroy, step, save, load,
     1, params, getParam, setParam, stepMulti},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("quad", "1.1", 1, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
