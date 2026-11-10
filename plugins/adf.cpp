// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: augmented Dickey-Fuller (ADF) unit root test.
//
// Input: socket 0 = y (the series). Parameters:
//   modelo     0 = no constant, 1 = constant (drift), 2 = constant + trend
//   defasagens p (0 = automatic by AIC; 1..12 = fixed)
//   janela     samples used (0 = all, up to 2000)
//   taxa       samples/s (0 = each step)
//
// The regression is
//   Δy_t = α + β·t + γ·y_{t-1} + Σ_{i=1..p} δ_i·Δy_{t-i} + ε_t
// and the test is τ = γ̂ / se(γ̂). The outputs are τ, the asymptotic critical
// values from MacKinnon (2010) at 1/5/10%, n, p used and γ̂. A τ more negative
// than the critical value rejects the unit root (stationarity); τ near zero is
// the signature of a random walk.
//
// Numerical details: y is divided by the standard deviation (and centered when
// there is a constant/trend) and t is normalized to [0,1]; τ and γ̂ are
// invariant to these changes and the conditioning of the normal equations
// improves a lot. OLS via normal equations + Gauss-Jordan (p+2 ≤ 14 columns).
// AIC = m·log(SSR/m) + 2k. The critical values are the asymptotic ones
// (n ≥ 100 is the recommended regime); there is no finite-sample correction and
// no p-value.
//
// With insufficient data (n < 24) or a degenerate series, the outputs stay at
// zero and n shows the sample size (check n). Non-finite samples are ignored.
// The window is saved in the blob (continuous save/load/undo).
// Build: make plugins | Usage: menu Add -> Math -> ADF
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

const int kMaxP = 12;
const int kMaxN = 2000;
const int kMaxCol = 16;

struct Adf {
    double model = 1, lags = 0, window = 0, rate = 0;
    double key = -1e308;
    std::deque<double> ys;
    // step outputs
    double oTau = 0, oCv1 = 0, oCv5 = 0, oCv10 = 0, oN = 0, oP = 0, oGamma = 0;
};

int windowCap(const Adf* d) {
    int cap = d->window < 0 ? 0 : (int)d->window;
    if (cap <= 0 || cap > kMaxN) cap = kMaxN;
    return cap;
}

void pruneWindow(Adf* d) {
    const int cap = windowCap(d);
    while ((int)d->ys.size() > cap) d->ys.pop_front();
}

void criticalValues(int model, double& c1, double& c5, double& c10) {
    // MacKinnon (2010), asymptotic: "n" (no constant), "c", "ct"
    if (model <= 0) { c1 = -2.5658; c5 = -1.9410; c10 = -1.6168; }
    else if (model >= 2) { c1 = -3.9588; c5 = -3.4105; c10 = -3.1270; }
    else { c1 = -3.4304; c5 = -2.8615; c10 = -2.5668; }
}

struct Adjustment {
    bool ok = false;
    double tau = 0, gamma = 0, ep = 0, ssr = 0;
    int m = 0, k = 0;
};

// OLS of Δz_t on [const? trend? z_{t-1} Δz_{t-1} .. Δz_{t-p}].
// Returns τ, γ̂, se, SSR, m and k (number of columns).
Adjustment regression(const std::vector<double>& z, const std::vector<double>& dif, int p, int model) {
    Adjustment r;
    const int N = (int)z.size();
    const int nc = model >= 1 ? 1 : 0;
    const int nt = model >= 2 ? 1 : 0;
    const int k = nc + nt + 1 + p;
    if (p < 0 || k > kMaxCol) return r;
    const int t0 = p + 1, t1 = N - 1;
    const int m = t1 - t0 + 1;
    if (m < k + 3) return r; // degrees of freedom for σ²
    const int gi = nc + nt;  // γ column (z_{t-1})

    double A[kMaxCol][kMaxCol] = {};
    double b[kMaxCol] = {};
    for (int t = t0; t <= t1; ++t) {
        double x[kMaxCol] = {};
        int c = 0;
        if (nc) x[c++] = 1.0;
        if (nt) x[c++] = (double)(t - t0) / (double)(t1 - t0);
        x[c++] = z[t - 1];
        for (int i = 1; i <= p; ++i) x[c++] = dif[t - i];
        for (int a = 0; a < k; ++a) {
            for (int bb = 0; bb <= a; ++bb) A[a][bb] += x[a] * x[bb];
            b[a] += x[a] * dif[t];
        }
    }
    for (int a = 0; a < k; ++a)
        for (int bb = a + 1; bb < k; ++bb) A[a][bb] = A[bb][a];

    // inverse of A (Gauss-Jordan with partial pivoting) + solution b
    double inv[kMaxCol][kMaxCol] = {};
    for (int i = 0; i < k; ++i) inv[i][i] = 1.0;
    for (int col = 0; col < k; ++col) {
        int piv = col;
        for (int rr = col + 1; rr < k; ++rr)
            if (std::fabs(A[rr][col]) > std::fabs(A[piv][col])) piv = rr;
        if (!(std::fabs(A[piv][col]) > 1e-12)) return r; // collinear/degenerate
        if (piv != col) {
            for (int j = 0; j < k; ++j) std::swap(A[col][j], A[piv][j]);
            for (int j = 0; j < k; ++j) std::swap(inv[col][j], inv[piv][j]);
        }
        const double d = A[col][col];
        for (int j = 0; j < k; ++j) { A[col][j] /= d; inv[col][j] /= d; }
        for (int rr = 0; rr < k; ++rr) {
            if (rr == col) continue;
            const double f = A[rr][col];
            if (f == 0) continue;
            for (int j = 0; j < k; ++j) { A[rr][j] -= f * A[col][j]; inv[rr][j] -= f * inv[col][j]; }
        }
    }
    double coef[kMaxCol] = {};
    for (int a = 0; a < k; ++a)
        for (int j = 0; j < k; ++j) coef[a] += inv[a][j] * b[j];

    double ssr = 0;
    for (int t = t0; t <= t1; ++t) {
        double x[kMaxCol] = {};
        int c = 0;
        if (nc) x[c++] = 1.0;
        if (nt) x[c++] = (double)(t - t0) / (double)(t1 - t0);
        x[c++] = z[t - 1];
        for (int i = 1; i <= p; ++i) x[c++] = dif[t - i];
        double fit = 0;
        for (int a = 0; a < k; ++a) fit += x[a] * coef[a];
        const double e = dif[t] - fit;
        ssr += e * e;
    }
    const int gl = m - k;
    if (gl <= 0) return r;
    const double s2 = ssr / gl;
    const double varG = inv[gi][gi] * s2;
    if (!(varG > 0) || !std::isfinite(varG)) return r;
    r.gamma = coef[gi];
    r.ep = std::sqrt(varG);
    if (!(r.ep > 0)) return r;
    r.tau = r.gamma / r.ep;
    if (!std::isfinite(r.tau)) return r;
    r.ssr = ssr;
    r.m = m;
    r.k = k;
    r.ok = true;
    return r;
}

void compute(Adf* d) {
    const int N = (int)d->ys.size();
    d->oN = N;
    d->oP = 0;
    d->oTau = 0;
    d->oGamma = 0;
    criticalValues((int)std::lround(d->model), d->oCv1, d->oCv5, d->oCv10);
    if (N < 24) return;

    double mean = 0;
    for (double y : d->ys) mean += y;
    mean /= N;
    double var = 0;
    for (double y : d->ys) var += (y - mean) * (y - mean);
    var /= N;
    const double sd = std::sqrt(var);
    if (!(sd > 1e-300) || !std::isfinite(sd)) return;

    const bool center = d->model >= 0.5;
    std::vector<double> z((size_t)N);
    for (int i = 0; i < N; ++i) z[(size_t)i] = (center ? d->ys[(size_t)i] - mean : d->ys[(size_t)i]) / sd;
    std::vector<double> dif((size_t)N, 0.0);
    for (int i = 1; i < N; ++i) dif[(size_t)i] = z[(size_t)i] - z[(size_t)i - 1];

    int pmax = (int)std::floor(12.0 * std::pow((double)N / 100.0, 0.25));
    if (pmax > kMaxP) pmax = kMaxP;
    if (pmax > N - 26) pmax = N - 26; // leave degrees of freedom
    if (pmax < 0) pmax = 0;
    const int fixed = (int)std::lround(d->lags);
    Adjustment chosen;
    if (fixed > 0) {
        const int p = fixed > pmax ? pmax : fixed;
        chosen = regression(z, dif, p, (int)std::lround(d->model));
        if (chosen.ok) d->oP = p;
    } else { // AIC: smallest m·log(SSR/m) + 2k (ties go to the lower order)
        double bestAic = 1e308;
        for (int p = 0; p <= pmax; ++p) {
            const Adjustment a = regression(z, dif, p, (int)std::lround(d->model));
            if (!a.ok || !(a.ssr > 0)) continue; // perfect fit = degenerate
            const double aic = a.m * std::log(a.ssr / a.m) + 2.0 * a.k;
            if (aic < bestAic) { // increasing p: ties go to the smaller
                bestAic = aic;
                chosen = a;
                d->oP = p;
            }
        }
    }
    if (!chosen.ok) { d->oP = 0; return; }
    d->oTau = chosen.tau;
    d->oGamma = chosen.gamma;
}

void update(Adf* d, const NsContext* c) {
    if (!d || !c) return;
    bool newValue = true;
    if (d->rate > 0) {
        const double k = std::floor((double)c->t * d->rate);
        newValue = (k != d->key);
        if (newValue) d->key = k;
    }
    if (!newValue) return;
    const double y = sockValue(c, 0);
    if (!std::isfinite(y)) return; // keep the last estimate
    d->ys.push_back(y);
    pruneWindow(d);
    compute(d);
}

void* newValue() { return new (std::nothrow) Adf(); }
void dispose(void* h) { delete (Adf*)h; }

double step(void* h, const NsContext* c) {
    Adf* d = (Adf*)h;
    update(d, c);
    return d ? d->oTau : 0.0;
}

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Adf* d = (Adf*)h;
    if (!d) return 0;
    update(d, c);
    const double v[7] = {d->oTau, d->oCv1, d->oCv5, d->oCv10, d->oN, d->oP, d->oGamma};
    for (int p = 0; p < nout && p < 7; ++p) out[p] = v[p];
    for (int p = 7; p < nout; ++p) out[p] = 0;
    return 1;
}

int numOut(void* h) {
    (void)h;
    return 7;
}

char* portLabel(void* h, int p) {
    (void)h;
    static const char* names[7] = {"τ", "cv1", "cv5", "cv10", "n", "p", "γ"};
    if (p < 0 || p >= 7) return nullptr;
    const char* s = names[p];
    char* r = (char*)std::malloc(std::strlen(s) + 1);
    if (r) std::memcpy(r, s, std::strlen(s) + 1);
    return r;
}

// param 0=modelo 1=defasagens 2=janela 3=taxa
const char* const modelChoices[] = {"no constant", "constant", "constant + trend"};
NsParam params[] = {
    {"model", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr,
     "deterministic terms of the ADF regression", 3, modelChoices},
    {"lags", 0, 12, 0, NS_PARAM_INT, 1, nullptr,
     "fixed lags (0 = chosen by AIC)", 0, nullptr},
    {"window", 0, 2000, 0, NS_PARAM_INT, 1, "samples",
     "samples used (0 = all, up to 2000)", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
};
double getParam(void* h, int i) {
    Adf* d = (Adf*)h;
    if (!d) return 0;
    switch (i) {
        case 0: return d->model;
        case 1: return d->lags;
        case 2: return d->window;
        default: return d->rate;
    }
}
int setParam(void* h, int i, double v) {
    Adf* d = (Adf*)h;
    if (!d) return 0;
    switch (i) {
        case 0: {
            int m = (int)std::lround(v);
            if (m < 0) m = 0;
            if (m > 2) m = 2;
            d->model = m;
            break;
        }
        case 1: {
            int p = (int)std::lround(v);
            d->lags = p < 0 ? 0 : (p > kMaxP ? kMaxP : p);
            break;
        }
        case 2:
            d->window = v < 0 ? 0 : (v > kMaxN ? kMaxN : v);
            pruneWindow(d);
            break;
        default: d->rate = v < 0 ? 0 : v; break;
    }
    compute(d); // reflect the change immediately (even with the simulation stopped)
    return 1;
}

char* save(void* h) {
    Adf* d = (Adf*)h;
    if (!d) return nullptr;
    char b[320];
    std::snprintf(b, sizeof b, "model=%.10g lags=%.10g window=%.10g rate=%.10g key=%.10g",
                  d->model, d->lags, d->window, d->rate, d->key);
    std::string out(b);
    out += " ys=";
    char v[48];
    for (size_t i = 0; i < d->ys.size(); ++i) {
        std::snprintf(v, sizeof v, "%s%.17g", i ? "," : "", d->ys[i]);
        out += v;
    }
    char* r = (char*)std::malloc(out.size() + 1);
    if (r) std::memcpy(r, out.c_str(), out.size() + 1);
    return r;
}

int load(void* h, const char* blob) {
    Adf* d = (Adf*)h;
    if (!d || !blob) return 0;
    double model = d->model, lags = d->lags, window = d->window, rate = d->rate;
    double key = -1e308;
    std::deque<double> ys;
    bool hasWindow = false;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string name(p, e - p);
        p = e + 1;
        if (name == "ys") {
            while (*p) {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                if (std::isfinite(v)) ys.push_back(v);
                p = end;
                if (*p == ',') ++p;
                else break;
            }
            hasWindow = true;
            continue;
        }
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) break;
        if (name == "model") model = v;
        else if (name == "lags") lags = v;
        else if (name == "window") window = v;
        else if (name == "rate") rate = v;
        else if (name == "key") key = v;
        p = end;
    }
    int m = (int)std::lround(model);
    d->model = m < 0 ? 0 : (m > 2 ? 2 : m);
    int pf = (int)std::lround(lags);
    d->lags = pf < 0 ? 0 : (pf > kMaxP ? kMaxP : pf);
    d->window = window < 0 ? 0 : (window > kMaxN ? kMaxN : window);
    d->rate = rate < 0 ? 0 : rate;
    d->key = key;
    if (hasWindow) d->ys = std::move(ys);
    pruneWindow(d);
    compute(d); // outputs consistent with the restored state
    return 1;
}

NsNodeType types[] = {
    {"adf", "ADF (unit root)", {205, 170, 110}, newValue, dispose, step, save, load,
     4, params, getParam, setParam, stepMulti,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, portLabel,
     1, 0, 0, nullptr},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("adf", "1.0", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
