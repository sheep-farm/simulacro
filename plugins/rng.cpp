// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: pseudo-random number generator with a SEED (reproducible series).
//
// Used for stochastic shocks in dynamic models (AR(1), VAR, Monte Carlo):
//   - fixed `semente` => the same sequence on every run and after "Restart" (F5),
//     because the generator state goes into the node's save/load blob;
//   - `dist`   = 0 uniform [0,1) | 1 normal(0,1) | 2 sign ±1;
//   - `ampl`/`desloc` => value = desloc + ampl * draw (default 1 / 0);
//   - `taxa`   = draws per SIMULATION SECOND (0 = one per step), the same
//     way the CSV plugin reads rows: the value is held between draws
//     (e.g. taxa = 12 for a "monthly" shock on a 60 Hz clock);
//   - `saídas` (host field, 1..8): each port is an INDEPENDENT draw.
//
// Socket 0 = enable (optional): with a wire, value <= 0 FREEZES the series (holds the
// last value and does not advance the generator); with no wire at all, the series runs freely.
//
// Build: make plugins | Usage: Add menu -> Math -> Random (RNG)
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

struct Rng {
    double seed = 12345, dist = 0, ampl = 1, offset = 0, rate = 0;
    uint64_t st = 0;        // generator state (xorshift64*)
    double key = -1e308;  // sample (from t*taxa, or the step number) of the last draw
    double tick = 0;       // step counter (taxa = 0)
    double vals[8] = {0};   // last draw per port
};

uint64_t next(uint64_t& s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ULL;
}
uint64_t seed(double seed) { // splitmix64: spreads the seed through the state
    uint64_t z = (uint64_t)(int64_t)seed + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
double uniform(uint64_t& s) { return (double)(next(s) >> 11) * (1.0 / 9007199254740992.0); } // [0,1)
double normal(uint64_t& s) { // Box-Muller
    double u1 = uniform(s), u2 = uniform(s);
    if (u1 < 1e-300) u1 = 1e-300;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}
double drawRandom(Rng* d) {
    switch ((int)(d->dist + 0.5)) {
        case 1: return normal(d->st);
        case 2: return uniform(d->st) < 0.5 ? -1.0 : 1.0;
        default: return uniform(d->st);
    }
}
void reSeed(Rng* d) {
    d->st = seed(d->seed);
    d->key = -1e308;
    d->tick = 0;
}
void* newValue() { Rng* d = new (std::nothrow) Rng(); if (d) reSeed(d); return d; }
void dispose(void* h) { delete (Rng*)h; }

// value of socket s = sum of the wires of that socket (see NsContext::sockOff)
// advances (or freezes) the series and returns the values of the 'nout' ports in d->vals
void advance(Rng* d, const NsContext* c, int nout) {
    if (!d || !c) return;
    if (nout < 1) nout = 1;
    if (nout > 8) nout = 8;
    if (c->nin > 0 && sockValue(c, 0) <= 0) return; // enable: freezes
    double key = d->rate > 0 ? std::floor((double)c->t * d->rate) : (d->tick + 1.0);
    if (key == d->key) return; // already drew in this sample
    d->key = key;
    ++d->tick;
    for (int k = 0; k < nout; ++k) d->vals[k] = d->offset + d->ampl * drawRandom(d);
}

double step(void* h, const NsContext* c) {
    Rng* d = (Rng*)h;
    advance(d, c, 1);
    return d ? d->vals[0] : 0.0;
}
int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Rng* d = (Rng*)h;
    advance(d, c, nout);
    if (!d) return 0;
    for (int k = 0; k < nout; ++k) out[k] = d->vals[k];
    return 1;
}

// param 0=seed 1=dist 2=ampl 3=desloc 4=taxa
const char* const distChoices[] = {"uniform", "normal", "sign"};
NsParam params[] = {
    {"seed", 0, 2000000000, 12345, NS_PARAM_INT, 1, nullptr,
     "fixed seed: same sequence on every F5", 0, nullptr},
    {"dist", 0, 2, 0, NS_PARAM_ENUM, 1, nullptr,
     "draw distribution", 3, distChoices},
    {"ampl", 0, 1000, 1, NS_PARAM_NUM, 0.1, nullptr,
     "multiplicative draw amplitude", 0, nullptr},
    {"offset", -1000, 1000, 0, NS_PARAM_NUM, 0.1, nullptr,
     "offset added to the draw", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "draws per second (0 = one per step)", 0, nullptr},
};
double getParam(void* h, int i) {
    Rng* d = (Rng*)h;
    if (!d) return 0;
    switch (i) {
        case 0: return d->seed;
        case 1: return d->dist;
        case 2: return d->ampl;
        case 3: return d->offset;
        default: return d->rate;
    }
}
int setParam(void* h, int i, double v) {
    Rng* d = (Rng*)h;
    if (!d) return 0;
    if (i == 0) { d->seed = std::floor(v + 0.5); reSeed(d); return 1; }
    if (i == 1) { d->dist = std::floor(v + 0.5); return 1; }
    if (i == 2) { d->ampl = v; return 1; }
    if (i == 3) { d->offset = v; return 1; }
    d->rate = v;
    return 1;
}

char* save(void* h) {
    Rng* d = (Rng*)h;
    if (!d) return nullptr;
    char b[512];
    std::snprintf(b, sizeof b,
                  "seed=%.10g dist=%.10g ampl=%.10g offset=%.10g rate=%.10g st=%llu key=%.10g step=%.10g vals=",
                  d->seed, d->dist, d->ampl, d->offset, d->rate,
                  (unsigned long long)d->st, d->key, d->tick);
    std::string s = b;
    for (int k = 0; k < 8; ++k) {
        char v[40];
        std::snprintf(v, sizeof v, k ? ",%.10g" : "%.10g", d->vals[k]);
        s += v;
    }
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}
int load(void* h, const char* blob) {
    Rng* d = (Rng*)h;
    if (!d || !blob) return 0;
    double seed = d->seed, dist = d->dist, ampl = d->ampl, offset = d->offset, rate = d->rate;
    double key = d->key, tick = d->tick, vals[8] = {0};
    unsigned long long st = 0;
    bool hasSt = false, hasVals = false;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') { while (*e) ++e; p = e; continue; }
        std::string k(p, e - p);
        p = e + 1;
        if (k == "vals") {
            for (int i = 0; i < 8 && *p; ++i) {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                vals[i] = v;
                p = end;
                while (*p == ',') ++p;
            }
            hasVals = true;
            continue;
        }
        char* end = nullptr;
        if (k == "st") { st = std::strtoull(p, &end, 10); hasSt = true; }
        else {
            double v = std::strtod(p, &end);
            if (end == p) break;
            if (k == "seed") seed = v;
            else if (k == "dist") dist = v;
            else if (k == "ampl") ampl = v;
            else if (k == "offset") offset = v;
            else if (k == "rate") rate = v;
            else if (k == "key") key = v;
            else if (k == "step") tick = v;
        }
        p = end;
        while (*p && *p != ' ') ++p;
    }
    d->seed = seed; d->dist = dist; d->ampl = ampl; d->offset = offset; d->rate = rate;
    if (hasSt) {
        d->st = (uint64_t)st;
        d->key = key;
        d->tick = tick;
    } else reSeed(d); // blob with parameters only: starts the series from the seed
    if (hasVals) for (int k = 0; k < 8; ++k) d->vals[k] = vals[k];
    return 1;
}

NsNodeType types[] = {
    {"rng", "Random (RNG)", {140, 220, 170}, newValue, dispose, step, save, load,
     5, params, getParam, setParam, stepMulti,
     0, nullptr, nullptr, nullptr,
     false, nullptr, nullptr, nullptr,
     1, 0, 0, nullptr},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("rng", "1.1", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
