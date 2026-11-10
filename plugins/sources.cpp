// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: deterministic signal sources for research and identification.
//
// Types (all with optional port 0 = enable; wire <= 0 freezes the output):
//   degrau      d + A·(t >= t0)
//   impulso     d + A in [t0, t0+largura)
//   rampa       d + slope·(t−t0) for t >= t0
//   seno        d + A·sin(2π·f·t + phase)
//   quadrada    d + A·sign(2π·f·t + phase), with duty cycle
//   triangular  d + A·tri(2π·f·t + phase)  (0 at start, +1 at 1/4, −1 at 3/4)
//   chirp       linear sweep f0→f1 during `duracao`; afterwards holds f1
//   prbs        ±1 binary sequence with a 16-bit LFSR and seed
//
// Time comes from NsContext::t, not from an internal counter: restarting (F5)
// reproduces exactly the same signal. PRBS stores the LFSR in the save/load blob and,
// with `taxa` = 0, advances one bit per step; with taxa > 0, one bit every 1/taxa s.
// Non-finite inputs and the enable port are handled before the computation;
// the host already counts each non-finite port in `nan=…`.
//
// Build: make plugins | Usage: Add menu -> Signal -> Sources
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

const double PI = 3.141592653589793238462643383279502884;

// User-configurable fields. Only the ones appearing in each type's table
// are exposed in the editor.
enum Field {
    C_AMPL, C_OFFSET, C_T0, C_PHASE, C_FREQ, C_CYCLE, C_INCL, C_LARG,
    C_F0, C_F1, C_DUR, C_RATE, C_SEED
};

struct Sources {
    double ampl = 1.0;
    double offset = 0.0;
    double t0 = 0.0;
    double phase = 0.0;     // degrees, for the editor
    double freq = 1.0;     // Hz
    double cycle = 0.5;    // 0,01..0,99
    double incl = 1.0;     // units/s
    double larg = 0.1;     // s
    double f0 = 1.0, f1 = 10.0, dur = 1.0;
    double rate = 0.0;     // PRBS bits/s (0 = one per step)
    double seed = 1.0;  // integer 1..65535

    uint32_t state = 1;   // 16-bit LFSR
    double bit = 1.0;      // last PRBS bit (+1/−1)
    double nextValue = 0.0;     // time of the next PRBS bit
    bool started = false;
    double last = 0.0;   // value held when port 0 freezes
};

void* create() { return new (std::nothrow) Sources(); }
void destroy(void* h) { delete (Sources*)h; }

double sock0(const NsContext* c) {
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) v += c->in[k];
    return v;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("hab") : nullptr; }

double* field(Sources* d, Field c) {
    switch (c) {
        case C_AMPL: return &d->ampl;
        case C_OFFSET: return &d->offset;
        case C_T0: return &d->t0;
        case C_PHASE: return &d->phase;
        case C_FREQ: return &d->freq;
        case C_CYCLE: return &d->cycle;
        case C_INCL: return &d->incl;
        case C_LARG: return &d->larg;
        case C_F0: return &d->f0;
        case C_F1: return &d->f1;
        case C_DUR: return &d->dur;
        case C_RATE: return &d->rate;
        case C_SEED: return &d->seed;
    }
    return &d->last;
}

void normalize(Sources* d) {
    if (!d) return;
    if (!(d->freq > 0.0)) d->freq = 0.001;
    if (d->freq > 1e6) d->freq = 1e6;
    if (d->cycle < 0.01) d->cycle = 0.01;
    if (d->cycle > 0.99) d->cycle = 0.99;
    if (!(d->larg > 0.0)) d->larg = 0.001;
    if (!(d->dur > 0.0)) d->dur = 0.001;
    if (d->f0 < 0.0) d->f0 = 0.0;
    if (d->f1 < 0.0) d->f1 = 0.0;
    if (!(d->rate > 0.0)) d->rate = 0.0;
    int s = (int)std::llround(d->seed);
    if (s < 1) s = 1;
    if (s > 65535) s = 65535;
    d->seed = (double)s;
    d->state &= 0xffffu;
    if (d->state == 0) d->state = 1;
    if (!std::isfinite(d->nextValue)) d->nextValue = 0.0;
    if (!std::isfinite(d->last)) d->last = 0.0;
}

void resetPrbs(Sources* d) {
    if (!d) return;
    int s = (int)std::llround(d->seed);
    if (s < 1) s = 1;
    if (s > 65535) s = 65535;
    d->state = (uint32_t)s;
    d->bit = (d->state & 1u) ? 1.0 : -1.0;
    d->nextValue = 0.0;
    d->started = false;
}

// 16-bit LFSR, polynomial x^16 + x^14 + x^13 + x^11 + 1 (period 65535).
void advancePrbs(Sources* d) {
    uint32_t x = d->state & 0xffffu;
    uint32_t b = ((x >> 0) ^ (x >> 2) ^ (x >> 3) ^ (x >> 5)) & 1u;
    x = (x >> 1) | (b << 15);
    d->state = x & 0xffffu;
    d->bit = (x & 1u) ? 1.0 : -1.0;
}

double tri(double phase) { // phase in turns; 0 → 0, 0.25 → +1, 0.5 → 0, 0.75 → −1
    double f = phase - std::floor(phase);
    if (f < 0.25) return 4.0 * f;
    if (f < 0.75) return 2.0 - 4.0 * f;
    return 4.0 * f - 4.0;
}

template <int M>
double tick(void* h, const NsContext* c) {
    Sources* d = (Sources*)h;
    if (!d || !c) return 0.0;

    // Optional port 0: with a wire, value <= 0 freezes the output (and the PRBS).
    const bool hasPort = c->nin > 0 && c->in && c->sockOff && c->nsock >= 1;
    if (hasPort && sock0(c) <= 0.0) return d->last;

    const double t = c->t;
    double r = 0.0;

    if (M == 0) { // step
        r = d->offset + (t >= d->t0 ? d->ampl : 0.0);
    } else if (M == 1) { // impulse
        r = d->offset + ((t >= d->t0 && t < d->t0 + d->larg) ? d->ampl : 0.0);
    } else if (M == 2) { // ramp
        const double u = t > d->t0 ? t - d->t0 : 0.0;
        r = d->offset + d->incl * u;
    } else if (M == 3 || M == 4 || M == 5) { // sine/square/triangle
        double f = d->freq * t + d->phase / 360.0;
        if (M == 3) r = d->offset + d->ampl * std::sin(2.0 * PI * f);
        else if (M == 4) {
            double frac = f - std::floor(f);
            r = d->offset + d->ampl * (frac < d->cycle ? 1.0 : -1.0);
        } else r = d->offset + d->ampl * tri(f);
    } else if (M == 6) { // linear chirp
        const double phase0 = d->phase / 360.0;
        double loops;
        if (t <= 0.0) loops = phase0;
        else if (t <= d->dur)
            loops = phase0 + d->f0 * t + 0.5 * (d->f1 - d->f0) * t * t / d->dur;
        else
            loops = phase0 + 0.5 * (d->f0 + d->f1) * d->dur + d->f1 * (t - d->dur);
        r = d->offset + d->ampl * std::sin(2.0 * PI * loops);
    } else { // PRBS
        if (!d->started) {
            d->state = (uint32_t)std::llround(d->seed);
            if (d->state == 0) d->state = 1;
            d->nextValue = t;
            d->started = true;
        }
        if (d->rate > 0.0) {
            const double tick = 1.0 / d->rate;
            int save = 0;
            while (t >= d->nextValue && save < 100000) {
                advancePrbs(d);
                d->nextValue += tick;
                ++save;
            }
            if (t >= d->nextValue) d->nextValue = t + tick; // absurd jump: resynchronizes
        } else {
            advancePrbs(d);
        }
        r = d->offset + d->ampl * d->bit;
    }

    if (!std::isfinite(r)) r = d->last;
    d->last = r;
    return r;
}

// ---- parameter tables per type ----
NsParam pStep[] = {
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "step amplitude", 0, nullptr},
    {"t0", 0, 1e9, 0, NS_PARAM_NUM, 0.1, "s",
     "time when the step starts", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "base value before/after the step", 0, nullptr},
};
NsParam pImpulse[] = {
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "impulse amplitude", 0, nullptr},
    {"t0", 0, 1e9, 1, NS_PARAM_NUM, 0.1, "s",
     "impulse start", 0, nullptr},
    {"width", 0.001, 1e6, 0.1, NS_PARAM_NUM, 0.01, "s",
     "impulse duration", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "base value", 0, nullptr},
};
NsParam pRamp[] = {
    {"slope", -1e9, 1e9, 1, NS_PARAM_NUM, 0.1, "/s",
     "ramp rate", 0, nullptr},
    {"t0", 0, 1e9, 0, NS_PARAM_NUM, 0.1, "s",
     "time when the ramp starts", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "base value", 0, nullptr},
};
NsParam pSine[] = {
    {"freq", 0.001, 1e6, 1, NS_PARAM_NUM, 0.1, "Hz",
     "sine frequency", 0, nullptr},
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "amplitude", 0, nullptr},
    {"phase", -360, 360, 0, NS_PARAM_NUM, 1, "°",
     "initial phase in degrees", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "mean value", 0, nullptr},
};
NsParam pSquare[] = {
    {"freq", 0.001, 1e6, 1, NS_PARAM_NUM, 0.1, "Hz",
     "square-wave frequency", 0, nullptr},
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "amplitude", 0, nullptr},
    {"phase", -360, 360, 0, NS_PARAM_NUM, 1, "°",
     "initial phase in degrees", 0, nullptr},
    {"cycle", 0.01, 0.99, 0.5, NS_PARAM_NUM, 0.01, nullptr,
     "fraction of the period at high level", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "mean value", 0, nullptr},
};
NsParam pTriangular[] = {
    {"freq", 0.001, 1e6, 1, NS_PARAM_NUM, 0.1, "Hz",
     "triangle frequency", 0, nullptr},
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "amplitude", 0, nullptr},
    {"phase", -360, 360, 0, NS_PARAM_NUM, 1, "°",
     "initial phase in degrees", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "mean value", 0, nullptr},
};
NsParam pChirp[] = {
    {"f0", 0, 1e6, 1, NS_PARAM_NUM, 0.1, "Hz",
     "start frequency", 0, nullptr},
    {"f1", 0, 1e6, 10, NS_PARAM_NUM, 0.1, "Hz",
     "end frequency", 0, nullptr},
    {"duration", 0.001, 1e6, 1, NS_PARAM_NUM, 0.1, "s",
     "sweep duration", 0, nullptr},
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "amplitude", 0, nullptr},
    {"phase", -360, 360, 0, NS_PARAM_NUM, 1, "°",
     "initial phase in degrees", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "mean value", 0, nullptr},
};
NsParam pPrbs[] = {
    {"seed", 1, 65535, 1, NS_PARAM_INT, 1, nullptr,
     "LFSR seed (1..65535)", 0, nullptr},
    {"rate", 0, 1e6, 0, NS_PARAM_NUM, 1, "Hz",
     "bits per second (0 = one per step)", 0, nullptr},
    {"ampl", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "amplitude", 0, nullptr},
    {"offset", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "mean value", 0, nullptr},
};

const Field fStep[] = {C_AMPL, C_T0, C_OFFSET};
const Field fImpulse[] = {C_AMPL, C_T0, C_LARG, C_OFFSET};
const Field fRamp[] = {C_INCL, C_T0, C_OFFSET};
const Field fSine[] = {C_FREQ, C_AMPL, C_PHASE, C_OFFSET};
const Field fSquare[] = {C_FREQ, C_AMPL, C_PHASE, C_CYCLE, C_OFFSET};
const Field fTriangular[] = {C_FREQ, C_AMPL, C_PHASE, C_OFFSET};
const Field fChirp[] = {C_F0, C_F1, C_DUR, C_AMPL, C_PHASE, C_OFFSET};
const Field fPrbs[] = {C_SEED, C_RATE, C_AMPL, C_OFFSET};

const NsParam* table(int m) {
    switch (m) {
        case 0: return pStep;
        case 1: return pImpulse;
        case 2: return pRamp;
        case 3: return pSine;
        case 4: return pSquare;
        case 5: return pTriangular;
        case 6: return pChirp;
        default: return pPrbs;
    }
}
int nParams(int m) {
    switch (m) {
        case 0: return (int)(sizeof pStep / sizeof pStep[0]);
        case 1: return (int)(sizeof pImpulse / sizeof pImpulse[0]);
        case 2: return (int)(sizeof pRamp / sizeof pRamp[0]);
        case 3: return (int)(sizeof pSine / sizeof pSine[0]);
        case 4: return (int)(sizeof pSquare / sizeof pSquare[0]);
        case 5: return (int)(sizeof pTriangular / sizeof pTriangular[0]);
        case 6: return (int)(sizeof pChirp / sizeof pChirp[0]);
        default: return (int)(sizeof pPrbs / sizeof pPrbs[0]);
    }
}
const Field* fields(int m) {
    switch (m) {
        case 0: return fStep;
        case 1: return fImpulse;
        case 2: return fRamp;
        case 3: return fSine;
        case 4: return fSquare;
        case 5: return fTriangular;
        case 6: return fChirp;
        default: return fPrbs;
    }
}

template <int M>
double getParam(void* h, int i) {
    Sources* d = (Sources*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0.0;
    return *field(d, fields(M)[i]);
}

template <int M>
int setParam(void* h, int i, double v) {
    Sources* d = (Sources*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0;
    const NsParam& p = table(M)[i];
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    const Field c = fields(M)[i];
    if (c == C_SEED) {
        int s = (int)std::llround(v);
        if (s < 1) s = 1;
        if (s > 65535) s = 65535;
        d->seed = (double)s;
        resetPrbs(d);
        return 1;
    }
    *field(d, c) = v;
    if (c == C_RATE) {
        d->started = false;
        d->nextValue = 0.0;
    }
    normalize(d);
    return 1;
}

// ---- state (blob) ----
char* save(void* h) {
    Sources* d = (Sources*)h;
    if (!d) return nullptr;
    char b[640];
    std::snprintf(
        b, sizeof b,
        "ampl=%.17g offset=%.17g t0=%.17g phase=%.17g freq=%.17g cycle=%.17g "
        "slope=%.17g width=%.17g f0=%.17g f1=%.17g duration=%.17g rate=%.17g "
        "seed=%.17g state=%u bit=%.17g started=%d next=%.17g last=%.17g",
        d->ampl, d->offset, d->t0, d->phase, d->freq, d->cycle, d->incl, d->larg, d->f0,
        d->f1, d->dur, d->rate, d->seed, (unsigned)d->state, d->bit,
        d->started ? 1 : 0, d->nextValue, d->last);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    Sources* d = (Sources*)h;
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
        double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "ampl") d->ampl = v;
        else if (name == "offset") d->offset = v;
        else if (name == "t0") d->t0 = v;
        else if (name == "phase") d->phase = v;
        else if (name == "freq") d->freq = v;
        else if (name == "cycle") d->cycle = v;
        else if (name == "slope") d->incl = v;
        else if (name == "width") d->larg = v;
        else if (name == "f0") d->f0 = v;
        else if (name == "f1") d->f1 = v;
        else if (name == "duration") d->dur = v;
        else if (name == "rate") d->rate = v;
        else if (name == "seed") d->seed = v;
        else if (name == "state") d->state = (uint32_t)std::llround(v) & 0xffffu;
        else if (name == "bit") d->bit = v;
        else if (name == "started") d->started = v != 0.0;
        else if (name == "next") d->nextValue = v;
        else if (name == "last") d->last = v;
        else break; // unknown key: blob from another version
        some = true;
    }
    if (!some) return 0;
    normalize(d);
    return 1;
}

// ---- type table ----
using StepFn = double (*)(void*, const NsContext*);
using GetFn = double (*)(void*, int);
using SetFn = int (*)(void*, int, double);

NsNodeType kind(const char* id, const char* label, float r, float g, float b,
                StepFn step, int np, const NsParam* params, GetFn get, SetFn set) {
    NsNodeType t{};
    t.id = id;
    t.label = label;
    t.color[0] = r;
    t.color[1] = g;
    t.color[2] = b;
    t.create = create;
    t.destroy = destroy;
    t.step = step;
    t.save = save;
    t.load = load;
    t.num_params = np;
    t.params = params;
    t.getParam = get;
    t.setParam = set;
    t.def_in = 1;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_STATE | NS_CAP_PARAMS | NS_CAP_INPUT_LABELS;
    return t;
}

NsNodeType types[] = {
    kind("step", "Step", 120, 210, 180, tick<0>, nParams(0), table(0),
         getParam<0>, setParam<0>),
    kind("impulse", "Impulse", 120, 190, 210, tick<1>, nParams(1), table(1),
         getParam<1>, setParam<1>),
    kind("ramp", "Ramp", 140, 200, 160, tick<2>, nParams(2), table(2),
         getParam<2>, setParam<2>),
    kind("sine", "Sine", 100, 180, 255, tick<3>, nParams(3), table(3),
         getParam<3>, setParam<3>),
    kind("square", "Square", 255, 210, 120, tick<4>, nParams(4), table(4),
         getParam<4>, setParam<4>),
    kind("triangle", "Triangle", 255, 170, 120, tick<5>, nParams(5),
         table(5), getParam<5>, setParam<5>),
    kind("chirp", "Chirp", 210, 160, 255, tick<6>, nParams(6), table(6),
         getParam<6>, setParam<6>),
    kind("prbs", "PRBS", 190, 190, 220, tick<7>, nParams(7), table(7),
         getParam<7>, setParam<7>),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("sources", "1.0", 8, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
