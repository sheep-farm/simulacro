// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Example plugin: 30-step delay line.
// Shows what a script cannot do: an internal buffer with serialized state.
// Build: make plugins | Usage: menu Add -> Delay 30 (plugin)
#include <plugin.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
const int N = 30;

struct Delay {
    std::vector<double> buf;
    size_t pos = 0;
    Delay() : buf(N, 0.0) {}
};

void* create() { return new (std::nothrow) Delay(); }
void destroy(void* h) { delete (Delay*)h; }

double step(void* h, const NsContext* c) {
    Delay* d = (Delay*)h;
    if (!d || !c) return 0;
    double out = d->buf[d->pos];
    d->buf[d->pos] = (c->nin > 0 && c->in) ? c->in[0] : 0.0;
    d->pos = (d->pos + 1) % d->buf.size();
    return out;
}

char* save(void* h) {
    Delay* d = (Delay*)h;
    if (!d) return nullptr;
    std::string s = std::to_string(d->pos) + ";";
    for (double v : d->buf) { char b[48]; std::snprintf(b, sizeof b, "%.17g,", v); s += b; }
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int load(void* h, const char* s) {
    Delay* d = (Delay*)h;
    if (!d || !s) return 0;
    size_t pos = 0;
    if (std::sscanf(s, "%zu;", &pos) != 1 || pos >= d->buf.size()) return 0;
    const char* p = std::strchr(s, ';');
    if (!p) return 0;
    ++p;
    std::vector<double> buf;
    while (*p) {
        char* e = nullptr;
        double v = std::strtod(p, &e);
        if (e == p) break;
        buf.push_back(v);
        p = e;
        if (*p == ',') ++p;
    }
    if (buf.size() != d->buf.size()) return 0;
    d->buf = buf;
    d->pos = pos;
    return 1;
}

NsNodeType types[] = {
    {"delay30", "Delay 30", {140, 220, 255}, create, destroy, step, save, load},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("delay", "1.1", 1, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
