// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: autocorrelation (ACF) and partial autocorrelation (PACF) of a series,
// over a moving window of samples.
//
// Input: socket 0 = x. Outputs: ports 1..k = ρ(1..k) (ACF) or φ_jj (PACF),
// with k up to 16 (the number of ports follows the `k` parameter). Parameters:
//   k      = number of published lags (1..16, default 8)
//   janela = number of SAMPLES in the window (8..600, default 120)
//   taxa   = samples per simulation SECOND (0 = one per step)
//
// The ACF uses the classical sample estimator (mean subtracted, denominator
// Σ(x−x̄)², i.e. r0 = 1) and the PACF comes from the Durbin-Levinson recursion
// on those r_j — no table and no state beyond the buffer. The buffer goes into
// the save/load blob (17 digits) => F5 reproduces exactly the same curve.
// Non-finite samples are ignored (the host already counts the port in `nan=…`).
//
// Build: make plugins | Usage: menu Add -> Math -> ACF / PACF
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
#include <vector>

namespace {

const int MAXK = 16;

struct Acf {
    double k = 8, window = 120, rate = 0;
    double key = -1e308; // sample (t·taxa) of the last record
    std::vector<double> xs;
};

void* newValue() { return new (std::nothrow) Acf(); }
void dispose(void* h) { delete (Acf*)h; }

int kOf(const Acf* d) {
    int k = d ? (int)std::llround(d->k) : 1;
    if (k < 1) k = 1;
    if (k > MAXK) k = MAXK;
    return k;
}

size_t capacityOf(const Acf* d) {
    double j = d ? std::llround(d->window) : 120.0;
    if (j < 8) j = 8;
    if (j > 600) j = 600;
    return (size_t)j;
}

void adjustBuffer(Acf* d) {
    size_t cap = capacityOf(d);
    if (d->xs.size() > cap) d->xs.erase(d->xs.begin(), d->xs.end() - (long)cap);
}

// M = 0 -> ACF, M = 1 -> PACF
template <int M>
int compute(void* h, const NsContext* c, double* out, int nout) {
    Acf* d = (Acf*)h;
    if (!d || !out || nout < 1) return 0;
    const double x = sockValue(c, 0);
    bool newValue = true;
    if (d->rate > 0) {
        double mark = std::floor((double)c->t * d->rate);
        newValue = (mark != d->key);
        if (newValue) d->key = mark;
    }
    if (newValue && std::isfinite(x)) {
        d->xs.push_back(x);
        adjustBuffer(d);
    }
    for (int p = 0; p < nout; ++p) out[p] = 0.0; // warm-up/short

    const int n = (int)d->xs.size();
    const int K = std::min(kOf(d), nout);
    if (n < 3 || K < 1) return 1;

    double mean = 0.0;
    for (double v : d->xs) mean += v;
    mean /= n;
    double s2 = 0.0;
    for (double v : d->xs) {
        double dx = v - mean;
        s2 += dx * dx;
    }
    if (!(s2 > 0.0)) return 1; // constant series

    std::vector<double> r((size_t)K + 1, 0.0);
    for (int j = 1; j <= K; ++j) {
        double s = 0.0;
        for (int t = j; t < n; ++t) s += (d->xs[(size_t)t] - mean) * (d->xs[(size_t)(t - j)] - mean);
        r[(size_t)j] = s / s2;
    }
    if (M == 0) {
        for (int j = 1; j <= K; ++j) {
            double v = r[(size_t)j];
            if (v > 1.0) v = 1.0;
            if (v < -1.0) v = -1.0;
            out[j - 1] = v;
        }
        return 1;
    }
    // Durbin-Levinson: φ_mm from r_1..r_m
    std::vector<double> prev((size_t)K + 1, 0.0), phi((size_t)K + 1, 0.0);
    for (int m = 1; m <= K; ++m) {
        double num = r[(size_t)m], den = 1.0;
        for (int j = 1; j < m; ++j) {
            num -= prev[(size_t)j] * r[(size_t)(m - j)];
            den -= prev[(size_t)j] * r[(size_t)j];
        }
        double p = (std::fabs(den) > 1e-300) ? num / den : 0.0;
        if (!std::isfinite(p)) p = 0.0;
        if (p > 1.0) p = 1.0;
        if (p < -1.0) p = -1.0;
        phi[(size_t)m] = p;
        for (int j = 1; j < m; ++j) phi[(size_t)j] = prev[(size_t)j] - p * prev[(size_t)(m - j)];
        prev = phi;
        out[m - 1] = p;
    }
    return 1;
}

template <int M>
double tick(void* h, const NsContext* c) { // case nOut == 1 (k=1)
    double buf[MAXK] = {0};
    return compute<M>(h, c, buf, 1) ? buf[0] : 0.0;
}

int numOut(void* h) { return kOf((Acf*)h); }

char* label(const char* prefix, int p, bool repeated) {
    std::string s = prefix;
    if (repeated) {
        s += std::to_string(p + 1);
        s += std::to_string(p + 1);
    } else {
        s += std::to_string(p + 1);
    }
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}
char* labelAcf(void*, int p) { return label("\xCF\x81", p, false); }  // ρ1, ρ2, …
char* labelPacf(void*, int p) { return label("\xCF\x86", p, true); } // φ11, φ22, …

// param 0=k 1=janela 2=taxa
NsParam params[] = {
    {"k", 1, MAXK, 8, NS_PARAM_INT, 1, nullptr,
     "number of lags (ports)", 0, nullptr},
    {"window", 8, 600, 120, NS_PARAM_INT, 1, "samples",
     "samples used in the correlation", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
};
double getParam(void* h, int i) {
    Acf* d = (Acf*)h;
    if (!d) return 0;
    if (i == 0) return d->k;
    if (i == 1) return d->window;
    return d->rate;
}
int setParam(void* h, int i, double v) {
    Acf* d = (Acf*)h;
    if (!d) return 0;
    if (i == 0) {
        int k = (int)std::llround(v);
        if (k < 1) k = 1;
        if (k > MAXK) k = MAXK;
        d->k = k;
        return 1;
    }
    if (i == 1) {
        double j = std::llround(v);
        if (j < 8) j = 8;
        if (j > 600) j = 600;
        d->window = j;
        adjustBuffer(d);
        return 1;
    }
    d->rate = v < 0 ? 0 : v;
    return 1;
}

char* save(void* h) {
    Acf* d = (Acf*)h;
    if (!d) return nullptr;
    char b[128];
    std::snprintf(b, sizeof b, "k=%.10g window=%.10g rate=%.10g key=%.17g xs=", d->k, d->window,
                  d->rate, d->key);
    std::string s = b;
    for (size_t i = 0; i < d->xs.size(); ++i) {
        std::snprintf(b, sizeof b, i ? ",%.17g" : "%.17g", d->xs[i]);
        s += b;
    }
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

int load(void* h, const char* blob) {
    Acf* d = (Acf*)h;
    if (!d || !blob) return 0;
    double k = d->k, window = d->window, rate = d->rate, key = d->key;
    std::vector<double> xs;
    bool some = false; // requires at least one recognized key
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        const std::string name(p, e - p);
        p = e + 1;
        if (name == "xs") {
            some = true;
            while (*p) {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                xs.push_back(v);
                p = end;
                if (*p != ',') break;
                ++p;
            }
            continue;
        }
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "k") k = v;
        else if (name == "window") window = v;
        else if (name == "rate") rate = v;
        else if (name == "key") key = v;
        some = true;
    }
    if (!some) return 0;
    int kk = (int)std::llround(k);
    if (kk < 1) kk = 1;
    if (kk > MAXK) kk = MAXK;
    double jj = std::llround(window);
    if (jj < 8) jj = 8;
    if (jj > 600) jj = 600;
    d->k = kk;
    d->window = jj;
    d->rate = rate < 0 ? 0 : rate;
    d->key = key;
    size_t cap = (size_t)jj;
    if (xs.size() > cap) xs.erase(xs.begin(), xs.end() - (long)cap);
    d->xs.swap(xs);
    return 1;
}

NsNodeType types[] = {
    {"acf", "ACF (autocorrelation)", {150, 200, 235}, newValue, dispose, tick<0>, save, load,
     3, params, getParam, setParam, compute<0>,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, labelAcf,
     1, 0, 0, nullptr},
    {"pacf", "PACF (partial)", {140, 185, 225}, newValue, dispose, tick<1>, save, load,
     3, params, getParam, setParam, compute<1>,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, labelPacf,
     1, 0, 0, nullptr},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("acf", "1.0", 2, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
