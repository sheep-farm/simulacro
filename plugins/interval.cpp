// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: interval membership — output 1 if in(0) satisfies it, otherwise 0.
// The configuration is the notation itself: [0;1] ]0;1] [0;1[ ]1.34;3.7[
// ([ / ] closed/open at each end; editable in the properties panel)
// Build: make plugins | Usage: menu Add -> Interval (plugin)
#include <plugin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
struct Interval {
    double lo = 0, hi = 1;
    bool loOpen = false, hiOpen = false;
};

void* create() { return new (std::nothrow) Interval(); }
void destroy(void* h) { delete (Interval*)h; }

// "[0;1]" | "]0;1]" | "[0;1[" | "]1.34;3.7[" (leading/trailing spaces ok)
bool parse(const char* s, Interval& o) {
    while (*s == ' ' || *s == '\t') ++s;
    bool loOpen, hiOpen;
    if (*s == '[') loOpen = false;
    else if (*s == ']') loOpen = true;
    else return false;
    ++s;
    char* e1 = nullptr;
    double lo = std::strtod(s, &e1);
    if (e1 == s || !std::isfinite(lo)) return false;
    s = e1;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s != ';') return false;
    ++s;
    while (*s == ' ' || *s == '\t') ++s;
    char* e2 = nullptr;
    double hi = std::strtod(s, &e2);
    if (e2 == s || !std::isfinite(hi)) return false;
    s = e2;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == ']') hiOpen = false;
    else if (*s == '[') hiOpen = true;
    else return false;
    ++s;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s != '\0' || lo > hi) return false;
    o.lo = lo; o.hi = hi; o.loOpen = loOpen; o.hiOpen = hiOpen;
    return true;
}

std::string canon(const Interval& o) {
    char b[64];
    std::snprintf(b, sizeof b, "%c%.6g;%.6g%c", o.loOpen ? ']' : '[', o.lo, o.hi, o.hiOpen ? '[' : ']');
    return b;
}

double step(void* h, const NsContext* c) {
    Interval* d = (Interval*)h;
    if (!d || !c) return 0;
    double v = (c->nin > 0 && c->in) ? c->in[0] : 0.0;
    bool okLo = d->loOpen ? (v > d->lo) : (v >= d->lo);
    bool okHi = d->hiOpen ? (v < d->hi) : (v <= d->hi);
    return (okLo && okHi) ? 1.0 : 0.0;
}

char* save(void* h) {
    Interval* d = (Interval*)h;
    if (!d) return nullptr;
    std::string s = canon(*d);
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int load(void* h, const char* s) {
    Interval* d = (Interval*)h;
    if (!d || !s) return 0;
    Interval o;
    if (!parse(s, o)) return 0;
    *d = o;
    return 1;
}

// ---- text parameter: the notation itself (e.g. "[0;1]") ----
const char* textNames[] = {"interval"};

char* getText(void* h, int i) {
    Interval* d = (Interval*)h;
    if (!d || i != 0) return nullptr;
    std::string s = canon(*d);
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int setText(void* h, int i, const char* s) { return i == 0 ? load(h, s) : 0; }

NsNodeType types[] = {
    {"interval", "Interval", {255, 210, 120}, create, destroy, step, save, load,
     0, nullptr, nullptr, nullptr, nullptr,
     1, textNames, getText, setText},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("interval", "1.0", 1, types, "Logic");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
