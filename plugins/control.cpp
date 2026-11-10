// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: control and signal-conditioning blocks.
//
//   pid         PID on the error (socket 0), with filtered derivative and
//               anti-windup; optional ports: u, P, I, D
//   integ         y += ganho·x·dt, with optional symmetric limit
//   deriv         filtered derivative of x (time constant tf)
//   sat           clamps x to [min, max]
//   deadband      dead zone: |x| <= zona → 0; outside, subtract the zone
//   ratelimit     limits the rate of change of y (rise/fall per second)
//   pausa         input 0/1: publishes 1 while it asks to pause the simulation
//                 (the host recognizes it via the NS_CAP_PAUSE capability). By
//                 default it is level-based; edge=1 requests only on the 0->1
//                 transition.
//   pausams       input 0/1: publishes the DURATION in ms of a timed pause
//                 (NS_CAP_PAUSE_MS); the host pauses and resumes on its own.
//
// The step uses NsContext::dt. In the PID, `limite` = 0 disables saturation;
// with anti-windup, the integrator does not grow while the output is saturated
// in the same direction as the error. The derivative is filtered by
// y += dt/(tf+dt)·(dx/dt − y); tf = 0 uses the raw derivative. All non-finite
// inputs are ignored; the host already counts them in `nan=…`.
//
// The state (integrator, previous error, filtered derivative and rate limiter
// output) goes into the save/load blob; F5 reproduces the same response.
//
// Build: make plugins | Usage: menu Add -> Control
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

struct Control {
    double kp = 1.0, ki = 0.0, kd = 0.0, tf = 0.05;
    double limit = 0.0, antiwindup = 1.0;
    double gain = 1.0;
    double minv = -1.0, maxv = 1.0;
    double zone = 0.1;
    double rise = 1.0, fall = 1.0;
    double threshold = 0.5;  // pausa: minimum input value to request a stop
    double edge = 0.0;   // pausa: 0 = level (default); 1 = only on the 0->1 transition
    double ms = 500.0;    // pausams: duration of the timed pause
    bool high = false;    // pausa: previous state of the input

    double i = 0.0;      // integral term of the PID / integrator
    double ep = 0.0;     // previous error of the PID
    double df = 0.0;     // filtered derivative of the PID/derivative
    double y = 0.0;      // rate limiter output
    double last = 0.0;
    bool hasEp = false;
    bool hasY = false;
};

void* create() { return new (std::nothrow) Control(); }
void destroy(void* h) { delete (Control*)h; }

double sock0(const NsContext* c) {
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) v += c->in[k];
    return v;
}

char* labelInPid(void*, int p) { return p == 0 ? dupStr("e") : nullptr; }
char* labelIn(void*, int p) { return p == 0 ? dupStr("x") : nullptr; }

char* labelOutPid(void*, int p) {
    static const char* names[] = {"u", "P", "I", "D"};
    return (p >= 0 && p < 4) ? dupStr(names[p]) : nullptr;
}
char* labelOut(void*, int p) { return p == 0 ? dupStr("y") : nullptr; }
char* inPauseLabel(void*, int p) { return p == 0 ? dupStr("0/1") : nullptr; }
char* outPauseLabel(void*, int p) { return p == 0 ? dupStr("stop") : nullptr; }
char* outPauseMsLabel(void*, int p) { return p == 0 ? dupStr("ms") : nullptr; }

void normalize(Control* d) {
    if (!d) return;
    if (!(d->kp >= 0.0)) d->kp = 0.0;
    if (!(d->ki >= 0.0)) d->ki = 0.0;
    if (!(d->kd >= 0.0)) d->kd = 0.0;
    if (!(d->gain >= 0.0)) d->gain = 0.0;
    if (!(d->tf >= 0.0)) d->tf = 0.0;
    if (d->tf > 1000.0) d->tf = 1000.0;
    if (!(d->limit >= 0.0)) d->limit = 0.0;
    if (d->limit > 1e12) d->limit = 1e12;
    if (d->minv > d->maxv) std::swap(d->minv, d->maxv);
    if (!(d->zone >= 0.0)) d->zone = 0.0;
    if (!(d->rise >= 0.0)) d->rise = 0.0;
    if (!(d->fall >= 0.0)) d->fall = 0.0;
    if (!(d->threshold >= 0.0)) d->threshold = 0.0;
    d->edge = d->edge > 0.5 ? 1.0 : 0.0;
    if (!(d->ms >= 0.0)) d->ms = 0.0;
    if (d->ms > 3600000.0) d->ms = 3600000.0;
    if (!std::isfinite(d->i)) d->i = 0.0;
    if (!std::isfinite(d->ep)) d->ep = 0.0;
    if (!std::isfinite(d->df)) d->df = 0.0;
    if (!std::isfinite(d->y)) d->y = 0.0;
    d->antiwindup = d->antiwindup > 0.5 ? 1.0 : 0.0;
}

struct PidOut { double u, p, i, d; };

PidOut pidCompute(Control* d, const NsContext* c) {
    const double e = sock0(c);
    const double dt = c->dt > 0.0 && std::isfinite(c->dt) ? c->dt : 0.0;
    if (!d->hasEp) {
        d->ep = e;
        d->df = 0.0;
        d->hasEp = true;
    }
    const double p = d->kp * e;
    double alpha = 1.0;
    double d_raw = 0.0;
    if (dt > 0.0) {
        d_raw = (e - d->ep) / dt;
        alpha = d->tf > 0.0 ? dt / (d->tf + dt) : 1.0;
    } else {
        alpha = 0.0;
    }
    d->df += alpha * (d_raw - d->df);
    const double dterm = d->kd * d->df;
    const double iOld = d->i;
    double iNew = iOld + d->ki * e * dt;
    double u = p + iNew + dterm;
    if (d->limit > 0.0) {
        double uc = u;
        if (uc > d->limit) uc = d->limit;
        if (uc < -d->limit) uc = -d->limit;
        // Conditional anti-windup: undo the increment only when the error
        // pushes the output beyond the limit.
        if (d->antiwindup > 0.5 && uc != u &&
            ((u > d->limit && e > 0.0) || (u < -d->limit && e < 0.0))) {
            iNew = iOld;
            u = p + iNew + dterm;
            uc = u;
            if (uc > d->limit) uc = d->limit;
            if (uc < -d->limit) uc = -d->limit;
        }
        u = uc;
    }
    d->i = iNew;
    d->ep = e;
    d->last = u;
    return {u, p, iNew, dterm};
}

double pidTick(void* h, const NsContext* c) {
    Control* d = (Control*)h;
    if (!d || !c) return 0.0;
    return pidCompute(d, c).u;
}

int pidMultiTick(void* h, const NsContext* c, double* out, int nout) {
    Control* d = (Control*)h;
    if (!d || !c || !out || nout < 1) return 0;
    PidOut r = pidCompute(d, c);
    out[0] = r.u;
    if (nout > 1) out[1] = r.p;
    if (nout > 2) out[2] = r.i;
    if (nout > 3) out[3] = r.d;
    for (int k = 4; k < nout; ++k) out[k] = 0.0;
    return 1;
}

template <int M>
double tick(void* h, const NsContext* c) {
    Control* d = (Control*)h;
    if (!d || !c) return 0.0;
    if (M == 0) return pidTick(h, c);
    const double x = sock0(c);
    if (!std::isfinite(x)) return d->last;
    const double dt = c->dt > 0.0 && std::isfinite(c->dt) ? c->dt : 0.0;
    double y = 0.0;

    if (M == 1) { // integrator
        d->i += d->gain * x * dt;
        if (d->limit > 0.0) {
            if (d->i > d->limit) d->i = d->limit;
            if (d->i < -d->limit) d->i = -d->limit;
        }
        y = d->i;
    } else if (M == 2) { // filtered derivative
        if (!d->hasEp) {
            d->ep = x;
            d->df = 0.0;
            d->hasEp = true;
        }
        double raw = 0.0, alpha = 0.0;
        if (dt > 0.0) {
            raw = (x - d->ep) / dt;
            alpha = d->tf > 0.0 ? dt / (d->tf + dt) : 1.0;
        }
        d->df += alpha * (raw - d->df);
        y = d->gain * d->df;
        d->ep = x;
    } else if (M == 3) { // saturation
        y = x;
        if (y < d->minv) y = d->minv;
        if (y > d->maxv) y = d->maxv;
    } else if (M == 4) { // dead zone
        if (std::fabs(x) <= d->zone) y = 0.0;
        else y = x > 0.0 ? x - d->zone : x + d->zone;
    } else if (M == 5) { // rate limiter
        if (!d->hasY) {
            d->y = x;
            d->hasY = true;
        } else {
            const double maxD = (x > d->y ? d->rise : d->fall) * dt;
            if (maxD <= 0.0) {
                // invalid dt or zero gain: hold the output
            } else {
                const double delta = x - d->y;
                d->y += delta > maxD ? maxD : (delta < -maxD ? -maxD : delta);
            }
        }
        y = d->y;
    } else if (M == 6) { // pausa: 0/1 -> stop request (edge pulse or level)
        const bool now = x >= d->threshold;
        const bool request = d->edge > 0.5 ? (now && !d->high) : now;
        d->high = now;
        y = request ? 1.0 : 0.0;
    } else { // pausams: 0/1 -> duration (ms) of a timed pause
        const bool now = x >= d->threshold;
        const bool request = d->edge > 0.5 ? (now && !d->high) : now;
        d->high = now;
        y = request ? d->ms : 0.0;
    }
    if (!std::isfinite(y)) y = d->last;
    d->last = y;
    return y;
}

// ---- parameters ----
NsParam pPid[] = {
    {"kp", 0, 1e12, 1, NS_PARAM_NUM, 0.01, nullptr,
     "proportional gain", 0, nullptr},
    {"ki", 0, 1e12, 0, NS_PARAM_NUM, 0.01, "1/s",
     "integral gain", 0, nullptr},
    {"kd", 0, 1e12, 0, NS_PARAM_NUM, 0.01, "s",
     "derivative gain", 0, nullptr},
    {"tf", 0, 1000, 0.05, NS_PARAM_NUM, 0.01, "s",
     "derivative filter time constant (0 = no filter)", 0, nullptr},
    {"limit", 0, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "symmetric output limit (0 = no saturation)", 0, nullptr},
    {"antiwindup", 0, 1, 1, NS_PARAM_BOOL, 1, nullptr,
     "impede o integrador de crescer saturado", 0, nullptr},
};
NsParam pInt[] = {
    {"gain", 0, 1e12, 1, NS_PARAM_NUM, 0.01, nullptr,
     "integration gain", 0, nullptr},
    {"limit", 0, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr,
     "symmetric integrator limit (0 = no limit)", 0, nullptr},
};
NsParam pDer[] = {
    {"tf", 0, 1000, 0.05, NS_PARAM_NUM, 0.01, "s",
     "filter time constant (0 = raw derivative)", 0, nullptr},
    {"gain", 0, 1e12, 1, NS_PARAM_NUM, 0.01, nullptr,
     "derivative gain", 0, nullptr},
};
NsParam pSat[] = {
    {"min", -1e12, 1e12, -1, NS_PARAM_NUM, 0.1, nullptr,
     "lower limit", 0, nullptr},
    {"max", -1e12, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr,
     "upper limit", 0, nullptr},
};
NsParam pDead[] = {
    {"zone", 0, 1e12, 0.1, NS_PARAM_NUM, 0.01, nullptr,
     "dead-zone half-width", 0, nullptr},
};
NsParam pRate[] = {
    {"rise", 0, 1e12, 1, NS_PARAM_NUM, 0.1, "/s",
     "maximum rise rate", 0, nullptr},
    {"fall", 0, 1e12, 1, NS_PARAM_NUM, 0.1, "/s",
     "maximum fall rate", 0, nullptr},
};
NsParam pauseParams[] = {
    {"threshold", 0, 1e12, 0.5, NS_PARAM_NUM, 0.01, nullptr,
     "minimum input value to request stop", 0, nullptr},
    {"edge", 0, 1, 0, NS_PARAM_BOOL, 1, nullptr,
     "0 = pause while the input is high; 1 = only on the 0→1 transition", 0, nullptr},
};
NsParam pauseMsParams[] = {
    {"ms", 0, 3600000, 500, NS_PARAM_NUM, 10, "ms",
     "pause duration in milliseconds", 0, nullptr},
    {"threshold", 0, 1e12, 0.5, NS_PARAM_NUM, 0.01, nullptr,
     "minimum input value to request pause", 0, nullptr},
    {"edge", 0, 1, 1, NS_PARAM_BOOL, 1, nullptr,
     "1 = pause only on the 0→1 transition; 0 = while the input is high", 0, nullptr},
};

enum Field { C_KP, C_KI, C_KD, C_TF, C_LIM, C_AW, C_GAIN, C_MIN, C_MAX, C_ZONE,
             C_SUB, C_DESC, C_THRESHOLD, C_EDGE, C_MS };

const NsParam* table(int m) {
    switch (m) {
        case 0: return pPid;
        case 1: return pInt;
        case 2: return pDer;
        case 3: return pSat;
        case 4: return pDead;
        case 5: return pRate;
        case 6: return pauseParams;
        default: return pauseMsParams;
    }
}
int nParams(int m) {
    switch (m) {
        case 0: return (int)(sizeof pPid / sizeof pPid[0]);
        case 1: return (int)(sizeof pInt / sizeof pInt[0]);
        case 2: return (int)(sizeof pDer / sizeof pDer[0]);
        case 3: return (int)(sizeof pSat / sizeof pSat[0]);
        case 4: return (int)(sizeof pDead / sizeof pDead[0]);
        case 5: return (int)(sizeof pRate / sizeof pRate[0]);
        case 6: return (int)(sizeof pauseParams / sizeof pauseParams[0]);
        default: return (int)(sizeof pauseMsParams / sizeof pauseMsParams[0]);
    }
}
const Field* fields(int m) {
    static const Field fPid[] = {C_KP, C_KI, C_KD, C_TF, C_LIM, C_AW};
    static const Field fInt[] = {C_GAIN, C_LIM};
    static const Field fDer[] = {C_TF, C_GAIN};
    static const Field fSat[] = {C_MIN, C_MAX};
    static const Field fDead[] = {C_ZONE};
    static const Field fRate[] = {C_SUB, C_DESC};
    static const Field pauseFields[] = {C_THRESHOLD, C_EDGE};
    static const Field pauseMsFields[] = {C_MS, C_THRESHOLD, C_EDGE};
    switch (m) {
        case 0: return fPid;
        case 1: return fInt;
        case 2: return fDer;
        case 3: return fSat;
        case 4: return fDead;
        case 5: return fRate;
        case 6: return pauseFields;
        default: return pauseMsFields;
    }
}
double* field(Control* d, Field c) {
    switch (c) {
        case C_KP: return &d->kp;
        case C_KI: return &d->ki;
        case C_KD: return &d->kd;
        case C_TF: return &d->tf;
        case C_LIM: return &d->limit;
        case C_AW: return &d->antiwindup;
        case C_GAIN: return &d->gain;
        case C_MIN: return &d->minv;
        case C_MAX: return &d->maxv;
        case C_ZONE: return &d->zone;
        case C_SUB: return &d->rise;
        case C_DESC: return &d->fall;
        case C_THRESHOLD: return &d->threshold;
        case C_EDGE: return &d->edge;
        case C_MS: return &d->ms;
    }
    return &d->last;
}

template <int M>
double getParam(void* h, int i) {
    Control* d = (Control*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0.0;
    return *field(d, fields(M)[i]);
}

template <int M>
int setParam(void* h, int i, double v) {
    Control* d = (Control*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0;
    const NsParam& p = table(M)[i];
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    const Field c = fields(M)[i];
    *field(d, c) = v;
    if (c == C_AW) d->antiwindup = v > 0.5 ? 1.0 : 0.0;
    if (c == C_MIN && d->minv > d->maxv) d->maxv = d->minv;
    if (c == C_MAX && d->maxv < d->minv) d->minv = d->maxv;
    normalize(d);
    return 1;
}

// ---- state ----
char* save(void* h) {
    Control* d = (Control*)h;
    if (!d) return nullptr;
    char b[768];
    std::snprintf(b, sizeof b,
                  "kp=%.17g ki=%.17g kd=%.17g tf=%.17g limit=%.17g antiwindup=%.17g "
                  "gain=%.17g min=%.17g max=%.17g zone=%.17g rise=%.17g "
                  "fall=%.17g threshold=%.17g edge=%.17g high=%d ms=%.17g "
                  "i=%.17g ep=%.17g df=%.17g y=%.17g hasEp=%d hasY=%d "
                  "last=%.17g",
                  d->kp, d->ki, d->kd, d->tf, d->limit, d->antiwindup, d->gain,
                  d->minv, d->maxv, d->zone, d->rise, d->fall, d->threshold,
                  d->edge, d->high ? 1 : 0, d->ms, d->i, d->ep,
                  d->df, d->y, d->hasEp ? 1 : 0, d->hasY ? 1 : 0, d->last);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    Control* d = (Control*)h;
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
        if (name == "kp") d->kp = v;
        else if (name == "ki") d->ki = v;
        else if (name == "kd") d->kd = v;
        else if (name == "tf") d->tf = v;
        else if (name == "limit") d->limit = v;
        else if (name == "antiwindup") d->antiwindup = v;
        else if (name == "gain") d->gain = v;
        else if (name == "min") d->minv = v;
        else if (name == "max") d->maxv = v;
        else if (name == "zone") d->zone = v;
        else if (name == "rise") d->rise = v;
        else if (name == "fall") d->fall = v;
        else if (name == "threshold") d->threshold = v;
        else if (name == "edge") d->edge = v;
        else if (name == "high") d->high = v != 0.0;
        else if (name == "ms") d->ms = v;
        else if (name == "i") d->i = v;
        else if (name == "ep") d->ep = v;
        else if (name == "df") d->df = v;
        else if (name == "y") d->y = v;
        else if (name == "hasEp") d->hasEp = v != 0.0;
        else if (name == "hasY") d->hasY = v != 0.0;
        else if (name == "last") d->last = v;
        else break;
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
                StepFn step, int np, const NsParam* params, GetFn get, SetFn set,
                bool pid) {
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
    t.stepMulti = pid ? pidMultiTick : nullptr;
    t.portLabel = pid ? labelOutPid : labelOut;
    t.portLabelIn = pid ? labelInPid : labelIn;
    t.capabilities = NS_CAP_STATE | NS_CAP_PARAMS | NS_CAP_INPUT_LABELS |
                     (pid ? NS_CAP_STEP_MULTI : 0);
    return t;
}

NsNodeType types[] = {
    kind("pid", "PID", 110, 210, 170, tick<0>, nParams(0), table(0), getParam<0>,
         setParam<0>, true),
    kind("integrator", "Integrator", 110, 190, 220, tick<1>, nParams(1), table(1),
         getParam<1>, setParam<1>, false),
    kind("derivative", "Derivative", 130, 190, 240, tick<2>, nParams(2), table(2),
         getParam<2>, setParam<2>, false),
    kind("sat", "Saturation", 220, 190, 120, tick<3>, nParams(3), table(3),
         getParam<3>, setParam<3>, false),
    kind("deadband", "Dead zone", 220, 170, 140, tick<4>, nParams(4), table(4),
         getParam<4>, setParam<4>, false),
    kind("ratelimit", "Rate limiter", 190, 180, 220, tick<5>, nParams(5),
         table(5), getParam<5>, setParam<5>, false),
    kind("stop", "Stop", 235, 120, 120, tick<6>, nParams(6), table(6),
         getParam<6>, setParam<6>, false),
    kind("pause_ms", "Pause (ms)", 235, 165, 95, tick<7>, nParams(7), table(7),
         getParam<7>, setParam<7>, false),
};
// The host pauses the simulation when output 0 of this type is != 0.
struct PauseAdjuster {
    PauseAdjuster() {
        types[6].capabilities |= NS_CAP_PAUSE;
        types[6].portLabel = outPauseLabel;
        types[6].portLabelIn = inPauseLabel;
        types[7].capabilities |= NS_CAP_PAUSE_MS;
        types[7].portLabel = outPauseMsLabel;
        types[7].portLabelIn = inPauseLabel;
    }
} pauseAdjuster;

NsPlugin plugin = NS_PLUGIN_BUILTIN("control", "1.2", 8, types, "Control");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
