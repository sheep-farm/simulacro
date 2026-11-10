// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: Hodrick-Prescott (HP) filter on a MOVING window. On each new sample,
// it solves min Σ(y−τ)² + λ Σ(Δ²τ)² over the last N samples and publishes the
// trend and the cycle (y − τ) at the most recent point.
//
// Input: socket 0 = y (the series; equally spaced samples).
// Parameters:
//   lambda  smoothness weight (1600 = quarterly data; 129600 = monthly;
//           6.25 = annual — the value is per SAMPLE, see the note in the README)
//   janela  samples used (8…500; default 120)
//   taxa    samples per second (0 = one per step), as in the other plugins
//   modo    0 = moving window (stream) | 1 = BATCH: receives a list/matrix on
//           socket 0 and filters the whole series (retrospective, no endpoint bias)
//   coluna  in batch mode with a matrix: column to filter (0 = first)
// Outputs (fixed count): `tend` (HP trend), `ciclo` (y − trend), `n` (window).
//
// The system is (I + λ·D'D)τ = y with D the 2nd difference: a pentadiagonal
// symmetric positive-definite matrix => banded Cholesky, O(N) per sample. With
// small N the trend is the series itself (no fit).
//
// HP is a two-sided filter: the most recent value is REVISED on each new
// sample (normal HP behavior in real time). The window goes into save/load,
// so "Restart" (F5) reproduces the same trend.
// Build: make plugins | Usage: menu Add -> Math -> HP
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

const int JAN_MIN = 8, JAN_MAX = 500;

struct HP {
    double lambda = 1600, window = 120, rate = 0, key = -1e308;
    double mode = 0, column = 0; // 0 = stream; 1 = batch (list/matrix on socket 0)
    std::vector<double> ys;      // stream: last samples (≤ janela)
    double tend = 0, cycle = 0;  // last result
    // batch: input/result and wire-value change detector
    std::vector<double> inBuf, tendL, cicL;
    const void* cacheSrc = nullptr;
    int cacheRows = 0, cacheCols = 0, cacheCol = -1;
    double cacheLam = -1;
    bool hasBatch = false, batchNew = false;
    long batchCount = 0;
};

// Solve (I + λ·D'D) x = y by banded Cholesky (width 2).
// Pentadiagonal SPD matrix; false if something goes wrong (should not happen).
bool hpResolve(const std::vector<double>& y, double lam, std::vector<double>& x) {
    int n = (int)y.size();
    if (n < 4) return false;
    std::vector<double> a0(n), a1(n, 0.0), a2(n, 0.0);
    for (int i = 0; i < n; ++i) {
        double d = 6.0;                       // interior rows
        if (i == 0 || i == n - 1) d = 1.0;    // ends
        else if (i == 1 || i == n - 2) d = 5.0;
        a0[i] = 1.0 + lam * d;
    }
    for (int i = 0; i < n - 1; ++i) a1[i] = -lam * ((i == 0 || i == n - 2) ? 2.0 : 4.0);
    for (int i = 0; i < n - 2; ++i) a2[i] = lam;

    // L (band 2): l0 diagonal, l1 (i,i-1), l2 (i,i-2)
    std::vector<double> l0(n), l1(n, 0.0), l2(n, 0.0);
    for (int i = 0; i < n; ++i) {
        if (i >= 2) l2[i] = a2[i - 2] / l0[i - 2];
        if (i >= 1) {
            double v = a1[i - 1];
            if (i >= 2) v -= l2[i] * l1[i - 1];
            l1[i] = v / l0[i - 1];
        }
        double s = a0[i] - l1[i] * l1[i] - l2[i] * l2[i];
        if (!(s > 0)) return false;
        l0[i] = std::sqrt(s);
    }
    // L z = y
    std::vector<double> z(n);
    for (int i = 0; i < n; ++i) {
        double v = y[i];
        if (i >= 1) v -= l1[i] * z[i - 1];
        if (i >= 2) v -= l2[i] * z[i - 2];
        z[i] = v / l0[i];
    }
    // L' x = z
    x.assign(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double v = z[i];
        if (i + 1 < n) v -= l1[i + 1] * x[i + 1];
        if (i + 2 < n) v -= l2[i + 2] * x[i + 2];
        x[i] = v / l0[i];
    }
    return true;
}

// compute trend/cycle with the current buffer (without sampling)
void compute(HP* d) {
    if (!d) return;
    int n = (int)d->ys.size();
    if (n < 4) { // short window: trend = series
        d->tend = n ? d->ys.back() : 0;
        d->cycle = 0;
        return;
    }
    std::vector<double> x;
    double lam = d->lambda > 0 ? d->lambda : 0;
    if (!hpResolve(d->ys, lam, x)) { // should not occur; fall back to the raw series
        d->tend = d->ys.back();
        d->cycle = 0;
        return;
    }
    d->tend = x.back();
    d->cycle = d->ys.back() - d->tend;
}

// batch mode: filter the whole series arriving on socket 0 (list or matrix column)
void batch(HP* d, const NsContext* c) {
    if (!c || !c->xval || c->nsock < 1) return;
    const NsValue& v = c->xval[0];
    const double* src = nullptr;
    int n = 0, col = -1;
    if (v.kind == NS_VAL_LIST && v.data && v.cols > 0) {
        src = v.data;
        n = v.cols;
    } else if (v.kind == NS_VAL_MAT && v.data && v.rows > 0 && v.cols > 0) {
        int j = (int)std::lround(d->column);
        if (j < 0) j = 0;
        if (j >= v.cols) j = v.cols - 1;
        col = j;
        d->inBuf.resize((size_t)v.rows);
        for (int i = 0; i < v.rows; ++i) d->inBuf[(size_t)i] = v.data[(size_t)i * v.cols + j];
        src = d->inBuf.data();
        n = v.rows;
    } else {
        return; // no list/matrix: keep the previous result
    }
    // same wire value (the host reuses the vector until it publishes another): do not redo
    if (d->hasBatch && v.data == d->cacheSrc && n == d->cacheRows && v.cols == d->cacheCols &&
        col == d->cacheCol && d->lambda == d->cacheLam)
        return;
    d->cacheSrc = v.data;
    d->cacheRows = n;
    d->cacheCols = v.cols;
    d->cacheCol = col;
    d->cacheLam = d->lambda;
    if (v.kind == NS_VAL_LIST) {
        d->inBuf.assign(src, src + n);
    }
    d->tendL.assign((size_t)n, 0.0);
    d->cicL.assign((size_t)n, 0.0);
    if (n >= 4) {
        std::vector<double> x;
        double lam = d->lambda > 0 ? d->lambda : 0;
        if (hpResolve(d->inBuf, lam, x) && (int)x.size() == n) d->tendL = x;
        else d->tendL = d->inBuf; // fallback: identity
    } else {
        d->tendL = d->inBuf;
    }
    for (int i = 0; i < n; ++i) d->cicL[(size_t)i] = d->inBuf[(size_t)i] - d->tendL[(size_t)i];
    d->tend = n ? d->tendL[(size_t)n - 1] : 0;
    d->cycle = n ? d->cicL[(size_t)n - 1] : 0;
    d->batchCount = n;
    d->hasBatch = true;
    d->batchNew = true;
}

void sample(HP* d, const NsContext* c) {
    d->ys.push_back(sockValue(c, 0));
    int jan = (int)d->window;
    if (jan < JAN_MIN) jan = JAN_MIN;
    if (jan > JAN_MAX) jan = JAN_MAX;
    while ((int)d->ys.size() > jan) d->ys.erase(d->ys.begin());
    compute(d);
}

void update(HP* d, const NsContext* c) {
    if (!d || !c) return;
    d->batchNew = false;
    if (d->mode > 0.5) { batch(d, c); return; }
    bool newItem = true;
    if (d->rate > 0) {
        double k = std::floor((double)c->t * d->rate);
        newItem = (k != d->key);
        if (newItem) d->key = k;
    }
    if (newItem) sample(d, c);
}

void* newValue() { return new (std::nothrow) HP(); }
void dispose(void* h) { delete (HP*)h; }

double step(void* h, const NsContext* c) {
    HP* d = (HP*)h;
    update(d, c);
    return d ? d->tend : 0.0;
}
int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    HP* d = (HP*)h;
    if (!d) return 0;
    update(d, c);
    if (nout > 0) out[0] = d->tend;
    if (nout > 1) out[1] = d->cycle;
    if (nout > 2) out[2] = d->mode > 0.5 ? d->batchCount : (double)(long)d->ys.size();
    for (int p = 3; p < nout; ++p) out[p] = 0;
    return 1;
}
int numOut(void*) { return 3; }
char* portLabel(void* h, int p) {
    HP* d = (HP*)h;
    std::string s;
    long n = d ? (d->mode > 0.5 ? d->batchCount : (long)d->ys.size()) : 0;
    if (p == 0) s = "tend";
    else if (p == 1) s = "cycle";
    else if (p == 2) s = "n";
    else return nullptr;
    if (d && d->mode > 0.5 && p < 2) s += "[" + std::to_string(n) + "]";
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

// publish the batch-mode lists (0 = nothing changed / stream mode)
int outValue(void* h, int port, NsValue* v) {
    HP* d = (HP*)h;
    if (!d || !v || d->mode < 0.5 || !d->hasBatch || !d->batchNew || d->batchCount <= 0) return 0;
    if (port == 0) {
        v->kind = NS_VAL_LIST;
        v->data = d->tendL.data();
        v->rows = 1;
        v->cols = (int)d->tendL.size();
        return 1;
    }
    if (port == 1) {
        v->kind = NS_VAL_LIST;
        v->data = d->cicL.data();
        v->rows = 1;
        v->cols = (int)d->cicL.size();
        return 1;
    }
    return 0;
}

// param 0=lambda 1=janela 2=taxa
const char* const modeChoices[] = {"stream", "batch"};
NsParam params[] = {
    {"lambda", 0, 1e9, 1600, NS_PARAM_NUM, 100, nullptr,
     "trend smoothness (larger = smoother)", 0, nullptr},
    {"window", JAN_MIN, JAN_MAX, 120, NS_PARAM_INT, 1, "samples",
     "samples used in the filter", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
    {"mode", 0, 1, 0, NS_PARAM_ENUM, 1, nullptr,
     "incremental stream or batch (list/matrix on socket 0)", 2, modeChoices},
    {"column", 0, 15, 0, NS_PARAM_INT, 1, nullptr,
     "matrix column in batch mode", 0, nullptr},
};
double getParam(void* h, int i) {
    HP* d = (HP*)h;
    if (!d) return 0;
    if (i == 0) return d->lambda;
    if (i == 1) return d->window;
    if (i == 2) return d->rate;
    return i == 3 ? d->mode : d->column;
}
int setParam(void* h, int i, double v) {
    HP* d = (HP*)h;
    if (!d) return 0;
    if (i == 0) {
        if (v < 0) v = 0;
        d->lambda = v;
        compute(d); // reflect immediately
        return 1;
    }
    if (i == 1) {
        if (v < JAN_MIN || v > JAN_MAX) return 0;
        d->window = v;
        while ((int)d->ys.size() > (int)v) d->ys.erase(d->ys.begin());
        compute(d);
        return 1;
    }
    if (i == 2) {
        d->rate = v < 0 ? 0 : v;
        return 1;
    }
    if (i == 3) { d->mode = v > 0.5 ? 1 : 0; d->hasBatch = false; return 1; }
    if (v < 0 || v > 15) return 0;
    d->column = v;
    d->hasBatch = false;
    return 1;
}

char* save(void* h) {
    HP* d = (HP*)h;
    if (!d) return nullptr;
    char b[256];
    std::snprintf(b, sizeof b, "lambda=%.17g window=%.10g rate=%.10g mode=%.10g column=%.10g key=%.10g ys=",
                  d->lambda, d->window, d->rate, d->mode, d->column, d->key);
    std::string s = b;
    char v[40];
    for (size_t i = 0; i < d->ys.size(); ++i) {
        std::snprintf(v, sizeof v, i ? ",%.9g" : "%.9g", d->ys[i]);
        s += v;
    }
    char* out = (char*)std::malloc(s.size() + 1);
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

int load(void* h, const char* blob) {
    HP* d = (HP*)h;
    if (!d || !blob) return 0;
    double lambda = d->lambda, window = d->window, rate = d->rate, key = -1e308;
    double mode = d->mode, column = d->column;
    std::vector<double> ys;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string name(p, e - p);
        p = e + 1;
        if (name == "ys") {
            while (*p && *p != ' ') {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                ys.push_back(v);
                p = end;
                while (*p == ',') ++p;
            }
            continue;
        }
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "lambda") lambda = v;
        else if (name == "window") window = v;
        else if (name == "rate") rate = v;
        else if (name == "mode") mode = v;
        else if (name == "column") column = v;
        else if (name == "key") key = v;
    }
    d->lambda = lambda < 0 ? 0 : lambda;
    if (window < JAN_MIN) window = JAN_MIN;
    if (window > JAN_MAX) window = JAN_MAX;
    d->window = window;
    d->rate = rate < 0 ? 0 : rate;
    d->mode = mode > 0.5 ? 1 : 0;
    d->column = column < 0 ? 0 : (column > 15 ? 15 : column);
    d->hasBatch = false;
    d->key = key;
    if ((int)ys.size() > (int)d->window) ys.erase(ys.begin(), ys.end() - (long)d->window);
    d->ys = ys;
    compute(d);
    return 1;
}

NsNodeType types[] = {
    {"hp", "Hodrick-Prescott", {200, 175, 235}, newValue, dispose, step, save, load,
     5, params, getParam, setParam, stepMulti,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, portLabel,
     1, 0, 0, nullptr, NS_SHAPE_BOX, outValue},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("hp", "1.1", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
