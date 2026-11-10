// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: incremental linear regression (OLS) over the inputs — cumulative or
// moving window. Designed for econometrics: connect the series (CSV, RNG, scripts)
// and read the coefficients; the headless trace gives the final estimate on the last line.
//
// Inputs (sockets): 0 = y (dependent), 1..k = x1..xk (regressors).
// Parameters:
//   k          1..5 regressors
//   constante  1 = with intercept (b0) | 0 = without
//   janela     0 = cumulative (all samples) | N = last N samples
//   taxa       samples per SIMULATION SECOND (0 = one per step), as in
//              CSV/RNG/Statistics — to match a CSV use the same `rate` as the CSV
//   inferencia 1 = adds sigma (residual deviation) and standard errors ep0..epk
//   modo       0 = stream (accumulates sample by sample) | 1 = BATCH: receives ONE
//              matrix on socket 0 and regresses the y column at once
//   ycol       batch: y column in the matrix (0 = first)
//   xcols      batch: regressor columns — "0,2", "1-3" or empty (= all
//              except y); at most MAXK columns are used
// Outputs (count controlled by the plugin): b0, b1..bk, R², n [and, with inference,
// sigma, ep0..epk]. In batch mode there are always 5: b (list), ep (list), R², n, σ —
// the coefficients and standard errors travel as lists on the wire. Standard errors
// use the classic formula: Cov(b) = sigma²·(X'X)^-1, with intercept via the
// centered form (1/n + x̄'·Sxx^-1·x̄).
//
// State is accumulated in sums (Σy, Σx, Σxx, Σxy, Σyy) => the blob is small in the
// cumulative window; with a moving window it stores the last N samples (to remove them).
// Everything goes into save/load, so "Restart" (F5) reproduces the same estimate.
//
// Build: make plugins | Usage: Add menu -> Math -> OLS regression
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

const int MAXK = 5; // with inference: 2k+5 outputs (b0..bk, R², n, sigma, ep0..epk) <= 15

struct OLS {
    double k = 1, constant = 1, window = 0, rate = 0, inference = 0;
    double mode = 0, ycol = 0; // 0 = stream; 1 = batch (matrix on socket 0)
    std::string xcols;         // batch: regressor columns ("" = all except y)
    double key = -1e308;   // sample (t*taxa) of the last record
    long n = 0;              // number of samples in the window
    double sy = 0, syy = 0;  // sums of y and y²
    double sx[8] = {0}, sxy[8] = {0}, sxx[8][8] = {{0}};
    std::vector<double> hist; // moving window: flattened rows (y, x1..xk)
    double coef[8] = {0}, ep[8] = {0}, r2 = 0, sigma = 0;
    // batch: published coefficients/errors + change detector for the wire value
    std::vector<double> bL, epL;
    const void* cacheSrc = nullptr;
    int cacheRows = 0, cacheCols = 0, cacheY = -1;
    bool cacheCons = false;
    std::string cacheXcols;
    bool hasBatch = false, batchNew = false;
    long batchCount = 0;
};

int kOf(const OLS* d) {
    int kk = (int)(d->k + 0.5);
    if (kk < 1) kk = 1;
    if (kk > MAXK) kk = MAXK;
    return kk;
}

void zero(OLS* d) {
    d->n = 0;
    d->sy = d->syy = 0;
    for (int j = 0; j < 8; ++j) {
        d->sx[j] = d->sxy[j] = 0;
        for (int l = 0; l < 8; ++l) d->sxx[j][l] = 0;
        d->coef[j] = 0;
        d->ep[j] = 0;
    }
    d->r2 = 0;
    d->sigma = 0;
    d->hist.clear();
    d->key = -1e308;
    d->hasBatch = false; // the batch is redone on the next matrix
}

// adds (sinal = +1) or subtracts (-1) the row v[0]=y, v[1..k]=x in the sums
void accumulate(OLS* d, const double* v, int signal) {
    int kk = kOf(d);
    double y = v[0];
    d->sy += signal * y;
    d->syy += signal * y * y;
    for (int j = 1; j <= kk; ++j) {
        d->sx[j] += signal * v[j];
        d->sxy[j] += signal * v[j] * y;
        for (int l = 1; l <= kk; ++l) d->sxx[j][l] += signal * v[j] * v[l];
    }
}

void sample(OLS* d, const NsContext* c) {
    int kk = kOf(d);
    double v[9] = {0};
    v[0] = sockValue(c, 0);
    for (int j = 1; j <= kk; ++j) v[j] = sockValue(c, j);
    ++d->n;
    accumulate(d, v, +1);
    d->window = d->window < 0 ? 0 : (d->window > 500 ? 500 : d->window);
    if (d->window > 0) { // moving window: store the row and remove the oldest
        for (int j = 0; j <= kk; ++j) d->hist.push_back(v[j]);
        size_t dim = (size_t)kk + 1;
        while (d->hist.size() / dim > (size_t)d->window) {
            double old[9] = {0};
            for (size_t j = 0; j < dim; ++j) old[j] = d->hist[j];
            d->hist.erase(d->hist.begin(), d->hist.begin() + (long)dim);
            --d->n;
            accumulate(d, old, -1);
        }
    }
}

// solves S*x = b (S k×k, row-major; destroys S and b). Zero pivots (constant/
// collinear regressor) get coefficient 0 and the column is dropped.
bool resolveSystem(double* S, int k, double* b, double* x) {
    double scale = 0;
    for (int i = 0; i < k * k; ++i) scale = std::max(scale, std::fabs(S[i]));
    double tol = 1e-12 * std::max(1.0, scale);
    bool exact = true;
    for (int col = 0; col < k; ++col) {
        int piv = col;
        for (int r = col + 1; r < k; ++r)
            if (std::fabs(S[r * k + col]) > std::fabs(S[piv * k + col])) piv = r;
        if (std::fabs(S[piv * k + col]) <= tol) { // degenerate direction
            for (int r = 0; r < k; ++r) S[r * k + col] = 0;
            b[col] = 0;
            exact = false;
            continue;
        }
        if (piv != col) {
            for (int c = 0; c < k; ++c) std::swap(S[piv * k + c], S[col * k + c]);
            std::swap(b[piv], b[col]);
        }
        double d = S[col * k + col];
        for (int c = col; c < k; ++c) S[col * k + c] /= d;
        b[col] /= d;
        for (int r = 0; r < k; ++r) {
            if (r == col) continue;
            double f = S[r * k + col];
            if (f == 0) continue;
            for (int c = col; c < k; ++c) S[r * k + c] -= f * S[col * k + c];
            b[r] -= f * b[col];
        }
    }
    for (int i = 0; i < k; ++i) x[i] = b[i];
    return exact;
}

// inverse of a k×k matrix (Gauss-Jordan with partial pivoting); zero pivot => row/column 0
void invert(const double* A, int k, double* inv) {
    double M[64];
    for (int i = 0; i < k * k; ++i) M[i] = A[i];
    for (int i = 0; i < k * k; ++i) inv[i] = 0;
    for (int i = 0; i < k; ++i) inv[i * k + i] = 1;
    double scale = 0;
    for (int i = 0; i < k * k; ++i) scale = std::max(scale, std::fabs(M[i]));
    double tol = 1e-12 * std::max(1.0, scale);
    for (int col = 0; col < k; ++col) {
        int piv = col;
        for (int r = col + 1; r < k; ++r)
            if (std::fabs(M[r * k + col]) > std::fabs(M[piv * k + col])) piv = r;
        if (std::fabs(M[piv * k + col]) <= tol) { // degenerate direction
            for (int c = 0; c < k; ++c) { M[col * k + c] = 0; inv[col * k + c] = 0; }
            for (int r = 0; r < k; ++r) { M[r * k + col] = 0; inv[r * k + col] = 0; }
            continue;
        }
        if (piv != col) {
            for (int c = 0; c < k; ++c) { std::swap(M[piv * k + c], M[col * k + c]); std::swap(inv[piv * k + c], inv[col * k + c]); }
        }
        double d = M[col * k + col];
        for (int c = 0; c < k; ++c) { M[col * k + c] /= d; inv[col * k + c] /= d; }
        for (int r = 0; r < k; ++r) {
            if (r == col) continue;
            double f = M[r * k + col];
            if (f == 0) continue;
            for (int c = 0; c < k; ++c) { M[r * k + c] -= f * M[col * k + c]; inv[r * k + c] -= f * inv[col * k + c]; }
        }
    }
}

// estimates b0..bk, R² and (optionally) sigma + standard errors from the sums.
// kk = active dimension (stream: parameter k; batch: number of chosen columns).
void computeK(OLS* d, int kk, bool inf) {
    if (kk < 0) kk = 0;
    if (kk > MAXK) kk = MAXK;
    for (int j = 0; j <= kk; ++j) { d->coef[j] = 0; d->ep[j] = 0; }
    d->r2 = 0;
    d->sigma = 0;
    if (d->n < 1) return;
    double n = (double)d->n, my = d->sy / n;
    double mx[8] = {0};
    for (int j = 1; j <= kk; ++j) mx[j] = d->sx[j] / n;
    bool withC = d->constant > 0.5;

    double S[64] = {0}, b[8] = {0}, x[8] = {0};
    for (int j = 1; j <= kk; ++j) {
        for (int l = 1; l <= kk; ++l) {
            double v = d->sxx[j][l];
            if (withC) v -= n * mx[j] * mx[l];
            S[(j - 1) * kk + (l - 1)] = v;
        }
        double u = d->sxy[j];
        if (withC) u -= n * mx[j] * my;
        b[j - 1] = u;
    }
    double Sinv[64] = {0}; // copy for the inverse (S is destroyed by the solve)
    for (int i = 0; i < kk * kk; ++i) Sinv[i] = S[i];
    resolveSystem(S, kk, b, x); // degenerate directions get coefficient 0
    for (int j = 0; j < kk; ++j) d->coef[j + 1] = x[j];

    double sse = 0; // sum of squared residuals
    if (withC) {
        double b0 = my;
        for (int j = 1; j <= kk; ++j) b0 -= d->coef[j] * mx[j];
        d->coef[0] = b0;
        double sst = d->syy - n * my * my;
        if (sst > 1e-300) {
            sse = sst;
            for (int j = 1; j <= kk; ++j) sse -= d->coef[j] * (d->sxy[j] - n * mx[j] * my);
            d->r2 = 1.0 - sse / sst;
        }
    } else {
        d->coef[0] = 0;
        if (d->syy > 1e-300) {
            sse = d->syy;
            for (int j = 1; j <= kk; ++j) sse -= d->coef[j] * d->sxy[j];
            d->r2 = 1.0 - sse / d->syy;
        }
    }
    if (!(sse > 0)) sse = 0; // negative numerical residual
    double dof = n - kk - (withC ? 1.0 : 0.0);

    if (inf && dof > 0) {
        double s2 = sse / dof;
        d->sigma = std::sqrt(s2);
        invert(Sinv, kk, Sinv);
        for (int j = 1; j <= kk; ++j) {
            double v = s2 * Sinv[(j - 1) * kk + (j - 1)];
            d->ep[j] = v > 0 ? std::sqrt(v) : 0;
        }
        if (withC) {
            double q = 1.0 / n; // Var(b0)/sigma² = 1/n + x̄'·Sxx^-1·x̄
            for (int j = 1; j <= kk; ++j)
                for (int l = 1; l <= kk; ++l)
                    q += mx[j] * Sinv[(j - 1) * kk + (l - 1)] * mx[l];
            d->ep[0] = q > 0 ? std::sqrt(s2 * q) : 0;
        }
    }

    if (!std::isfinite(d->r2)) d->r2 = 0;
    if (!std::isfinite(d->sigma)) d->sigma = 0;
    for (int j = 0; j <= kk; ++j) {
        if (!std::isfinite(d->coef[j])) d->coef[j] = 0;
        if (!std::isfinite(d->ep[j])) d->ep[j] = 0;
    }
}

void compute(OLS* d) { computeK(d, kOf(d), d->inference > 0.5); }

// batch regressor columns: "" = all except y; "0,2" or "1-3"
bool readColumns(const std::string& s, int cols, int yc, std::vector<int>& xs) {
    xs.clear();
    if (s.empty()) {
        for (int j = 0; j < cols; ++j)
            if (j != yc) xs.push_back(j);
        if ((int)xs.size() > MAXK) xs.resize(MAXK);
        return true;
    }
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == ',' || s[i] == ';')) ++i;
        if (i >= s.size()) break;
        char* e = nullptr;
        long a = std::strtol(s.c_str() + i, &e, 10);
        if (e == s.c_str() + i) return false;
        i = (size_t)(e - s.c_str());
        long b = a;
        if (i < s.size() && s[i] == '-') {
            ++i;
            char* e2 = nullptr;
            b = std::strtol(s.c_str() + i, &e2, 10);
            if (e2 == s.c_str() + i) return false;
            i = (size_t)(e2 - s.c_str());
        }
        if (a > b) std::swap(a, b);
        for (long j = a; j <= b && j - a < 64; ++j)
            if (j >= 0 && j < cols && j != yc) xs.push_back((int)j);
    }
    if ((int)xs.size() > MAXK) xs.resize(MAXK);
    return true;
}

// batch mode: regresses the y column on the chosen columns of ONE matrix
void batch(OLS* d, const NsContext* c) {
    if (!c || !c->xval || c->nsock < 1) return;
    const NsValue& v = c->xval[0];
    if (!v.data || v.rows <= 0 || v.cols <= 0) return; // no list/matrix: keep
    int cols = v.cols, rows = v.rows;
    int yc = (int)std::lround(d->ycol);
    if (yc < 0) yc = 0;
    if (yc >= cols) yc = cols - 1;
    std::vector<int> xs;
    if (!readColumns(d->xcols, cols, yc, xs)) readColumns("", cols, yc, xs); // invalid: all
    bool cons = d->constant > 0.5;
    // same wire value (the host reuses the vector until another is published): do not redo
    if (d->hasBatch && v.data == d->cacheSrc && rows == d->cacheRows && cols == d->cacheCols &&
        yc == d->cacheY && cons == d->cacheCons && d->xcols == d->cacheXcols)
        return;
    d->cacheSrc = v.data;
    d->cacheRows = rows;
    d->cacheCols = cols;
    d->cacheY = yc;
    d->cacheCons = cons;
    d->cacheXcols = d->xcols;

    int kk = (int)xs.size();
    d->n = 0; // batch sums (the stream dimension k is not touched)
    d->sy = d->syy = 0;
    for (int j = 0; j < 8; ++j) {
        d->sx[j] = d->sxy[j] = 0;
        for (int l = 0; l < 8; ++l) d->sxx[j][l] = 0;
    }
    for (int i = 0; i < rows; ++i) {
        const double* r = v.data + (size_t)i * (size_t)cols;
        bool ok = std::isfinite(r[yc]);
        for (int j = 0; j < kk && ok; ++j) ok = std::isfinite(r[xs[(size_t)j]]);
        if (!ok) continue; // row with a non-finite value is left out
        double y = r[yc];
        ++d->n;
        d->sy += y;
        d->syy += y * y;
        for (int j = 1; j <= kk; ++j) {
            double xj = r[xs[(size_t)j - 1]];
            d->sx[j] += xj;
            d->sxy[j] += xj * y;
            for (int l = 1; l <= kk; ++l) d->sxx[j][l] += xj * r[xs[(size_t)l - 1]];
        }
    }
    computeK(d, kk, true); // in batch mode there is always sigma + standard errors
    d->bL.assign(d->coef, d->coef + kk + 1);
    d->epL.assign(d->ep, d->ep + kk + 1);
    d->batchCount = d->n;
    d->hasBatch = true;
    d->batchNew = true;
}

void update(OLS* d, const NsContext* c) {
    if (!d || !c) return;
    d->batchNew = false;
    if (d->mode > 0.5) { batch(d, c); return; }
    bool newPoint = true;
    if (d->rate > 0) {
        double k = std::floor((double)c->t * d->rate);
        newPoint = (k != d->key);
        if (newPoint) d->key = k;
    }
    if (newPoint) sample(d, c);
    compute(d);
}

void* newValue() { return new (std::nothrow) OLS(); }
void dispose(void* h) { delete (OLS*)h; }

double step(void* h, const NsContext* c) {
    OLS* d = (OLS*)h;
    update(d, c);
    if (!d) return 0.0;
    if (d->mode > 0.5) return d->bL.empty() ? 0.0 : d->bL[0]; // numeric fallback
    return d->coef[0];
}
int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    OLS* d = (OLS*)h;
    if (!d) return 0;
    update(d, c);
    if (d->mode > 0.5) { // batch: b0, ep0, R², n, σ (the lists go through outValue)
        for (int p = 0; p < nout; ++p) {
            if (p == 0) out[p] = d->bL.empty() ? 0.0 : d->bL[0];
            else if (p == 1) out[p] = d->epL.empty() ? 0.0 : d->epL[0];
            else if (p == 2) out[p] = d->r2;
            else if (p == 3) out[p] = d->batchCount;
            else if (p == 4) out[p] = d->sigma;
            else out[p] = 0;
        }
        return 1;
    }
    int kk = kOf(d);
    bool inf = d->inference > 0.5;
    for (int p = 0; p < nout; ++p) {
        if (p <= kk) out[p] = d->coef[p];
        else if (p == kk + 1) out[p] = d->r2;
        else if (p == kk + 2) out[p] = d->n;
        else if (inf && p == kk + 3) out[p] = d->sigma;
        else if (inf && p >= kk + 4 && p <= 2 * kk + 4) out[p] = d->ep[p - (kk + 4)];
        else out[p] = 0;
    }
    return 1;
}

int numOut(void* h) {
    OLS* d = (OLS*)h;
    if (!d) return 4;
    if (d->mode > 0.5) return 5; // batch: b (list), ep (list), R², n, σ
    int kk = kOf(d);
    // b0..bk, R², n [and, with inference, sigma, ep0..epk]
    return kk + 3 + (d->inference > 0.5 ? kk + 2 : 0);
}
char* portLabelIn(void* h, int p) {
    OLS* d = (OLS*)h;
    if (!d) return nullptr;
    std::string s;
    if (d->mode > 0.5) {
        if (p != 0) return nullptr;
        s = "matriz";
    } else if (p == 0) {
        s = "y";
    } else if (p >= 1 && p <= kOf(d)) {
        s = "x" + std::to_string(p);
    } else {
        return nullptr;
    }
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}
char* portLabel(void* h, int p) {
    OLS* d = (OLS*)h;
    std::string s;
    if (d && d->mode > 0.5) { // batch: coefficient and standard-error lists
        if (p < 0 || p > 4) return nullptr;
        if (p == 0) s = "b[" + std::to_string(d->bL.size()) + "]";
        else if (p == 1) s = "ep[" + std::to_string(d->epL.size()) + "]";
        else if (p == 2) s = "R²";
        else if (p == 3) s = "n";
        else s = "σ";
    } else {
        int kk = d ? kOf(d) : 1;
        char b[16];
        if (p == kk + 1) std::snprintf(b, sizeof b, "R²");
        else if (p == kk + 2) std::snprintf(b, sizeof b, "n");
        else if (p >= 0 && p <= kk) std::snprintf(b, sizeof b, "b%d", p);
        else if (p == kk + 3 && d && d->inference > 0.5) std::snprintf(b, sizeof b, "σ");
        else if (d && d->inference > 0.5 && p >= kk + 4 && p <= 2 * kk + 4)
            std::snprintf(b, sizeof b, "ep%d", p - (kk + 4));
        else return nullptr;
        s = b;
    }
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

// publishes the batch lists (0 = nothing new / stream mode)
int outValue(void* h, int port, NsValue* v) {
    OLS* d = (OLS*)h;
    if (!d || !v || d->mode < 0.5 || !d->hasBatch || !d->batchNew) return 0;
    const std::vector<double>* src = nullptr;
    if (port == 0) src = &d->bL;
    else if (port == 1) src = &d->epL;
    else return 0;
    if (src->empty()) return 0;
    v->kind = NS_VAL_LIST;
    v->data = src->data();
    v->rows = 1;
    v->cols = (int)src->size();
    return 1;
}

// param 0=k 1=constante 2=janela 3=taxa 4=inferencia 5=modo 6=ycol
const char* const modeChoices[] = {"stream", "batch"};
NsParam params[] = {
    {"k", 1, MAXK, 1, NS_PARAM_INT, 1, nullptr,
     "number of regressors", 0, nullptr},
    {"constant", 0, 1, 1, NS_PARAM_BOOL, 1, nullptr,
     "include intercept b0", 0, nullptr},
    {"window", 0, 500, 0, NS_PARAM_INT, 1, "samples",
     "moving window (0 = cumulative)", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
    {"inference", 0, 1, 0, NS_PARAM_BOOL, 1, nullptr,
     "publish sigma and standard errors ep0..epk", 0, nullptr},
    {"mode", 0, 1, 0, NS_PARAM_ENUM, 1, nullptr,
     "incremental stream or batch (matrix on socket 0)", 2, modeChoices},
    {"ycol", 0, 15, 0, NS_PARAM_INT, 1, nullptr,
     "y column in batch mode", 0, nullptr},
};
const char* const texts[] = {"xcols"};

double getParam(void* h, int i) {
    OLS* d = (OLS*)h;
    if (!d) return 0;
    switch (i) {
        case 0: return d->k;
        case 1: return d->constant;
        case 2: return d->window;
        case 3: return d->rate;
        case 4: return d->inference;
        case 5: return d->mode;
        default: return d->ycol;
    }
}
int setParam(void* h, int i, double v) {
    OLS* d = (OLS*)h;
    if (!d) return 0;
    if (i == 0) {
        int kk = (int)(v + 0.5);
        if (kk < 1 || kk > MAXK) return 0;
        if (kk != kOf(d)) { d->k = kk; zero(d); } // dimension changed: restart
        return 1;
    }
    if (i == 1) {
        d->constant = v > 0.5 ? 1 : 0;
        d->hasBatch = false; // the batch depends on the intercept
        return 1;
    }
    if (i == 2) {
        double jan = v < 0 ? 0 : (v > 500 ? 500 : v);
        if (jan != d->window) { d->window = jan; zero(d); } // history changed: restart
        return 1;
    }
    if (i == 3) {
        d->rate = v < 0 ? 0 : v;
        return 1;
    }
    if (i == 4) {
        d->inference = v > 0.5 ? 1 : 0;
        if (d->mode < 0.5) compute(d); // in stream mode it reflects immediately; in batch, it does nothing
        return 1;
    }
    if (i == 5) { // mode: stream <-> batch (restarts the accumulation of both)
        double m = v > 0.5 ? 1 : 0;
        if (m != d->mode) { d->mode = m; zero(d); d->hasBatch = false; d->bL.clear(); d->epL.clear(); }
        return 1;
    }
    double yc = v < 0 ? 0 : (v > 15 ? 15 : v);
    if (yc != d->ycol) { d->ycol = yc; d->hasBatch = false; }
    return 1;
}

char* getText(void* h, int i) {
    OLS* d = (OLS*)h;
    if (!d || i != 0) return nullptr;
    char* r = (char*)std::malloc(d->xcols.size() + 1);
    if (r) std::memcpy(r, d->xcols.c_str(), d->xcols.size() + 1);
    return r;
}
int setText(void* h, int i, const char* s) {
    OLS* d = (OLS*)h;
    if (!d || i != 0 || !s) return 0;
    std::string t;
    for (const char* p = s; *p; ++p)
        if (*p != ' ' && *p != '\t') t += *p;
    if (t == d->xcols) return 1;
    d->xcols = t;
    d->hasBatch = false; // columns changed: redo the batch
    return 1;
}

char* save(void* h) {
    OLS* d = (OLS*)h;
    if (!d) return nullptr;
    int kk = kOf(d);
    char b[512];
    std::snprintf(b, sizeof b, "k=%.10g constant=%.10g window=%.10g rate=%.10g inference=%.10g mode=%.10g ycol=%.10g key=%.10g n=%ld sy=%.17g syy=%.17g",
                  d->k, d->constant, d->window, d->rate, d->inference, d->mode, d->ycol, d->key, d->n, d->sy, d->syy);
    std::string s = b;
    if (!d->xcols.empty()) s += " xcols=" + d->xcols;
    char v[64];
    s += " sx=";
    for (int j = 1; j <= kk; ++j) { std::snprintf(v, sizeof v, j > 1 ? ",%.17g" : "%.17g", d->sx[j]); s += v; }
    s += " sxy=";
    for (int j = 1; j <= kk; ++j) { std::snprintf(v, sizeof v, j > 1 ? ",%.17g" : "%.17g", d->sxy[j]); s += v; }
    s += " sxx=";
    for (int j = 1; j <= kk; ++j)
        for (int l = 1; l <= kk; ++l) { std::snprintf(v, sizeof v, (j == 1 && l == 1) ? "%.17g" : ",%.17g", d->sxx[j][l]); s += v; }
    if (d->window > 0 && !d->hist.empty()) { // moving window: store the rows
        s += " hist=";
        size_t dim = (size_t)kk + 1;
        for (size_t r = 0; r + dim <= d->hist.size(); r += dim) {
            if (r) s += ';';
            for (size_t j = 0; j < dim; ++j) {
                std::snprintf(v, sizeof v, j ? ",%.9g" : "%.9g", d->hist[r + j]);
                s += v;
            }
        }
    }
    char* out = (char*)std::malloc(s.size() + 1);
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

int load(void* h, const char* blob) {
    OLS* d = (OLS*)h;
    if (!d || !blob) return 0;
    zero(d);
    double k = d->k, constant = d->constant, window = d->window, rate = d->rate;
    double inference = d->inference, key = -1e308;
    double mode = d->mode, ycol = d->ycol;
    std::string xcols = d->xcols;
    long n = 0;
    double sy = 0, syy = 0;
    double sx[8] = {0}, sxy[8] = {0}, sxx[8][8] = {{0}};
    std::vector<double> hist;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string name(p, e - p);
        p = e + 1;
        if (name == "xcols") { // batch columns: "0,1,2" or "1-3" (no spaces)
            size_t ini = (size_t)(p - blob);
            while (*p && *p != ' ') ++p;
            xcols.assign(blob + ini, (size_t)(p - blob) - ini);
            continue;
        }
        if (name == "hist") { // rows "y,x1,..;y,x1,.."
            while (*p && *p != ' ') {
                while (*p && *p != ' ' && *p != ';') {
                    char* end = nullptr;
                    double v = std::strtod(p, &end);
                    if (end == p) break;
                    hist.push_back(v);
                    p = end;
                    while (*p == ',') ++p;
                }
                if (*p == ';') ++p;
            }
            continue;
        }
        if (name == "sx" || name == "sxy") {
            double* dst = name == "sx" ? sx : sxy;
            for (int j = 1; j <= MAXK && *p && *p != ' '; ++j) {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                dst[j] = v;
                p = end;
                while (*p == ',') ++p;
            }
            continue;
        }
        if (name == "sxx") {
            for (int j = 1; j <= MAXK && *p && *p != ' '; ++j)
                for (int l = 1; l <= MAXK && *p && *p != ' '; ++l) {
                    char* end = nullptr;
                    double v = std::strtod(p, &end);
                    if (end == p) break;
                    sxx[j][l] = v;
                    p = end;
                    while (*p == ',') ++p;
                }
            continue;
        }
        char* end = nullptr;
        if (name == "n") n = std::strtol(p, &end, 10);
        else {
            double v = std::strtod(p, &end);
            if (end == p) break;
            if (name == "k") k = v;
            else if (name == "constant") constant = v;
            else if (name == "window") window = v;
            else if (name == "rate") rate = v;
            else if (name == "inference") inference = v;
            else if (name == "key") key = v;
            else if (name == "sy") sy = v;
            else if (name == "syy") syy = v;
            else if (name == "mode") mode = v;
            else if (name == "ycol") ycol = v;
        }
        if (end == p) break;
        p = end;
    }
    d->k = k;
    int kk = kOf(d);
    d->k = kk;
    d->constant = constant > 0.5 ? 1 : 0;
    d->window = window < 0 ? 0 : (window > 500 ? 500 : window);
    d->rate = rate < 0 ? 0 : rate;
    d->inference = inference > 0.5 ? 1 : 0;
    d->mode = mode > 0.5 ? 1 : 0;
    d->ycol = ycol < 0 ? 0 : (ycol > 15 ? 15 : ycol);
    d->xcols = xcols;
    d->hasBatch = false; // the batch is redone on the first matrix that arrives
    d->key = key;
    d->n = n;
    d->sy = sy;
    d->syy = syy;
    for (int j = 1; j <= MAXK; ++j) {
        d->sx[j] = sx[j];
        d->sxy[j] = sxy[j];
        for (int l = 1; l <= MAXK; ++l) d->sxx[j][l] = sxx[j][l];
    }
    size_t dim = (size_t)kk + 1;
    if (d->window > 0 && hist.size() >= dim) {
        size_t lines = hist.size() / dim;
        if (lines > (size_t)d->window) { // keep the most recent ones
            hist.erase(hist.begin(), hist.begin() + (long)((lines - (size_t)d->window) * dim));
        }
        d->hist = hist;
    }
    compute(d);
    return 1;
}

NsNodeType types[] = {
    {"ols", "OLS regression", {235, 195, 120}, newValue, dispose, step, save, load,
     7, params, getParam, setParam, stepMulti,
     1, texts, getText, setText,
     true, numOut, nullptr, portLabel,
     2, 0, 0, nullptr, NS_SHAPE_BOX, outValue, portLabelIn},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("ols", "1.2", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
