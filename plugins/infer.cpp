// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: inference on an estimated coefficient.
//
// Inputs (sockets): 0 = b (coefficient), 1 = ep (standard error), 2 = gl
// (degrees of freedom = n − k). Outputs: t = b/ep, two-tailed p, lower and upper
// CI at level `nivel` (default 0.95), using the Student t distribution with `gl`
// degrees of freedom (gl < 1 → normal approximation).
//
// No state between steps (only the `nivel` parameter), so the blob stores just
// the parameter and the port is recomputed on each sample. Typical case: `ols`
// with `inference=1` publishes b0..bk and ep0..epk; wire b_i and ep_i into the
// first two inputs and `n − k` into the third (a SCRIPT resolves it: `in(n)`),
// and the node returns t, p and the CI ready for the scope.
//
// Build: make plugins | Usage: menu Add -> Math -> Inference (t/p/CI)
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

struct Infer {
    double level = 0.95; // CI confidence (0.5..0.9999)
};

void* create() { return new (std::nothrow) Infer(); }
void destroy(void* h) { delete (Infer*)h; }

// ---- regularized incomplete beta (for the Student t CDF) ----
double betacf(double a, double b, double x) {
    const int MAXIT = 300;
    const double EPS = 3e-15, FPMIN = 1e-300;
    const double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < FPMIN) d = FPMIN;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= MAXIT; ++m) {
        const int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return h;
}

double betai(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    const double bt =
        std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) +
                 b * std::log1p(-x));
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}

// P(T ≤ t) with df degrees of freedom
double tCdf(double t, double df) {
    if (df <= 0.0) return t < 0.0 ? 0.0 : (t > 0.0 ? 1.0 : 0.5);
    const double x = df / (df + t * t);
    const double tail = 0.5 * betai(0.5 * df, 0.5, x); // P(T > |t|)
    return t >= 0.0 ? 1.0 - tail : tail;
}

// t quantile (p in (0.5;1)) by bisection — robust and table-free
double tInv(double p, double df) {
    if (p <= 0.5) return 0.0;
    double lo = 0.0, hi = 1.0;
    while (tCdf(hi, df) < p && hi < 1e12) hi *= 2.0;
    for (int i = 0; i < 200; ++i) {
        const double m = 0.5 * (lo + hi);
        if (tCdf(m, df) < p) lo = m;
        else hi = m;
    }
    return 0.5 * (lo + hi);
}

double zInv(double p) { // standard normal quantile (for gl < 1)
    // Acklam, precision ~1e-9 — enough to display the CI
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double pl = 0.02425, ph = 1.0 - pl;
    if (p < pl) {
        const double q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    if (p > ph) {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    const double q = p - 0.5, r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
}

double pTwoTailed(double t, double gl) {
    if (!std::isfinite(t)) return 0.0;
    if (gl < 1.0) return std::erfc(std::fabs(t) / std::sqrt(2.0)); // normal
    const double x = gl / (gl + t * t);
    double p = betai(0.5 * gl, 0.5, x);
    if (!(p >= 0.0)) p = 0.0;
    if (p > 1.0) p = 1.0;
    return p;
}

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Infer* d = (Infer*)h;
    if (!d || !out || nout < 4) return 0;
    const double b = sockValue(c, 0), se = sockValue(c, 1), gl = sockValue(c, 2);
    if (!(se > 0.0) || !std::isfinite(b) || !std::isfinite(se)) {
        out[0] = out[1] = out[2] = out[3] = 0.0;
        return 1;
    }
    const double t = b / se;
    double level = d->level;
    if (!(level > 0.5)) level = 0.5;
    if (level > 0.9999) level = 0.9999;
    const double alfa2 = 0.5 * (1.0 + level);
    const double q = (gl >= 1.0) ? tInv(alfa2, gl) : zInv(alfa2);
    out[0] = t;
    out[1] = pTwoTailed(t, gl);
    out[2] = b - q * se;
    out[3] = b + q * se;
    return 1;
}

// fallback for when the node ends up with nOut=1 (the host calls `step`): same
// computation, port 0 = t.
double tick(void* h, const NsContext* c) {
    double out[4] = {0, 0, 0, 0};
    return stepMulti(h, c, out, 4) ? out[0] : 0.0;
}

int numOut(void*) { return 4; }

char* dupLabel(const char* s) {
    if (!s) return nullptr;
    char* o = (char*)std::malloc(std::strlen(s) + 1);
    if (o) std::memcpy(o, s, std::strlen(s) + 1);
    return o;
}
char* portLabel(void*, int p) {
    const char* names[] = {"t", "p", "ic_lo", "ic_hi"};
    if (p < 0 || p > 3) return nullptr;
    return dupLabel(names[p]);
}
char* portLabelIn(void*, int p) {
    const char* names[] = {"b", "ep", "gl"};
    return (p >= 0 && p < 3) ? dupLabel(names[p]) : nullptr;
}

NsParam params[] = {
    {"level", 0.5, 0.9999, 0.95, NS_PARAM_NUM, 0.001, nullptr,
     "confidence level of the interval (0.5 to 0.9999)", 0, nullptr},
};

double getParam(void* h, int i) {
    Infer* d = (Infer*)h;
    return (d && i == 0) ? d->level : 0.0;
}
int setParam(void* h, int i, double v) {
    Infer* d = (Infer*)h;
    if (!d || i != 0) return 0;
    if (!(v > 0.5)) v = 0.5;
    if (v > 0.9999) v = 0.9999;
    d->level = v;
    return 1;
}

char* save(void* h) {
    Infer* d = (Infer*)h;
    if (!d) return nullptr;
    char b[64];
    std::snprintf(b, sizeof b, "level=%.17g", d->level);
    char* o = (char*)std::malloc(std::strlen(b) + 1);
    if (o) std::memcpy(o, b, std::strlen(b) + 1);
    return o;
}

int load(void* h, const char* s) {
    Infer* d = (Infer*)h;
    if (!d || !s) return 0;
    double level = d->level;
    if (std::sscanf(s, "level=%lf", &level) != 1) return 0;
    if (!(level > 0.5)) level = 0.5;
    if (level > 0.9999) level = 0.9999;
    d->level = level;
    return 1;
}

NsNodeType types[] = {
    {"infer", "Inference (t/p/CI)", {225, 185, 130}, create, destroy, tick, save, load,
     1, params, getParam, setParam, stepMulti,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, portLabel,
     3, 0, 0, nullptr, NS_SHAPE_BOX, nullptr, portLabelIn},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("infer", "1.0", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
