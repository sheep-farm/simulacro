// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: signal filters with persistent state.
//
//   movavg     moving average of N samples (includes the current sample)
//   median     moving median of N samples (odd N)
//   savgol     centered Savitzky-Golay, odd window and polynomial order
//   butter_lp  2nd-order Butterworth low-pass (fc in Hz, Q)
//   butter_hp  2nd-order Butterworth high-pass
//   butter_bp  2nd-order Butterworth band-pass
//
// The Butterworth sample rate comes from NsContext::dt (fs = 1/dt). The
// filter recomputes the coefficients when dt changes and preserves the state
// x1/x2/y1/y2; the frequency is limited to 0.99·Nyquist. Savitzky-Golay is
// CENTERED: the output corresponds to the center of the window, so it has a
// delay of (N−1)/2 samples and only starts after the window fills. Non-finite
// inputs are ignored and do not poison the state; the host already counts them
// in `nan=…`.
//
// The buffer and the state go into the save/load blob; F5 reproduces the same series.
//
// Build: make plugins | Usage: menu Add -> Signal -> Filters
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
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace {

const double PI = 3.141592653589793238462643383279502884;

struct Filters {
    double window = 20.0;
    double order = 2.0;
    double fc = 10.0;
    double q = 0.7071067811865476;

    std::deque<double> buf;
    double sum = 0.0;       // movavg
    std::vector<double> sg;  // Savitzky-Golay coefficients
    int sgN = 0;

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    double fs = 0.0;         // rate used in the current coefficients
    bool coefOk = false;
    double last = 0.0;
};

void* create() { return new (std::nothrow) Filters(); }
void destroy(void* h) { delete (Filters*)h; }

double sock0(const NsContext* c) {
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) v += c->in[k];
    return v;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("x") : nullptr; }

int nInt(const Filters* d) {
    int n = (int)std::llround(d ? d->window : 20.0);
    return n;
}

int orderInt(const Filters* d) {
    int o = (int)std::llround(d ? d->order : 2.0);
    if (o < 2) o = 2;
    if (o > 5) o = 5;
    return o;
}

// Normalize janela/ordem for type M. The window is always odd in the filters
// that require it; in Savitzky-Golay it must also be > ordem.
void normalize(Filters* d, int M) {
    if (!d) return;
    int n = nInt(d);
    if (M == 0) { // moving average
        if (n < 2) n = 2;
        if (n > 4096) n = 4096;
    } else if (M == 1) { // median
        if (n < 3) n = 3;
        if (n > 999) n = 999;
        if ((n & 1) == 0) ++n;
    } else if (M == 2) { // Savitzky-Golay
        if (n < 5) n = 5;
        if (n > 101) n = 101;
        int o = orderInt(d);
        if (n <= o) n = o + 1;
        if ((n & 1) == 0) ++n;
        if (n > 101) n = 101;
    } else { // Butterworth
        if (!(d->fc > 0.0)) d->fc = 0.001;
        if (d->fc > 1e5) d->fc = 1e5;
        if (!(d->q > 0.0)) d->q = 0.1;
        if (d->q < 0.1) d->q = 0.1;
        if (d->q > 20.0) d->q = 20.0;
    }
    d->window = (double)n;
    while ((int)d->buf.size() > n) {
        if (M == 0) d->sum -= d->buf.front();
        d->buf.pop_front();
    }
    if (M == 0) {
        d->sum = 0.0;
        for (double v : d->buf) d->sum += v;
    }
}

// Solve a small dense system by Gauss-Jordan with partial pivoting. Returns
// false if a direction is degenerate.
bool resolve(std::vector<double>& A, int n, std::vector<double>& b, std::vector<double>& x) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (std::fabs(A[r * n + col]) > std::fabs(A[piv * n + col])) piv = r;
        if (std::fabs(A[piv * n + col]) < 1e-14) return false;
        if (piv != col) {
            for (int c = 0; c < n; ++c) std::swap(A[piv * n + c], A[col * n + c]);
            std::swap(b[piv], b[col]);
        }
        const double div = A[col * n + col];
        for (int c = col; c < n; ++c) A[col * n + c] /= div;
        b[col] /= div;
        for (int r = 0; r < n; ++r) {
            if (r == col) continue;
            const double f = A[r * n + col];
            if (f == 0.0) continue;
            for (int c = col; c < n; ++c) A[r * n + c] -= f * A[col * n + c];
            b[r] -= f * b[col];
        }
    }
    x = b;
    return true;
}

// Smoothing coefficients at the CENTER of the window: y0 = Σ w_j·x_j.
// Matrix A is the Vandermonde in x_j = j−m, j=0..n−1, and w = A·(AᵀA)⁻¹·e0.
void computeSg(Filters* d) {
    if (!d) return;
    const int n = nInt(d);
    const int p = orderInt(d);
    d->sg.assign((size_t)n, 0.0);
    d->sgN = n;
    if (n < 3 || p < 1 || p >= n) return;

    const int k = p + 1;
    std::vector<double> M((size_t)k * k, 0.0);
    for (int i = 0; i < n; ++i) {
        const double x = (double)(i - (n - 1) / 2);
        double pot = 1.0;
        double row[8] = {0};
        for (int j = 0; j < k; ++j) {
            row[j] = pot;
            pot *= x;
        }
        for (int a = 0; a < k; ++a)
            for (int b = 0; b < k; ++b) M[(size_t)a * k + b] += row[a] * row[b];
    }
    std::vector<double> rhs((size_t)k, 0.0);
    rhs[0] = 1.0;
    std::vector<double> z;
    if (!resolve(M, k, rhs, z)) return;
    for (int i = 0; i < n; ++i) {
        const double x = (double)(i - (n - 1) / 2);
        double pot = 1.0, w = 0.0;
        for (int j = 0; j < k; ++j) {
            w += z[(size_t)j] * pot;
            pot *= x;
        }
        d->sg[(size_t)i] = w;
    }
}

template <int M>
void computeButter(Filters* d, double fs) {
    d->coefOk = false;
    d->b0 = 1.0; d->b1 = 0.0; d->b2 = 0.0; d->a1 = 0.0; d->a2 = 0.0;
    if (!d || !(fs > 0.0) || !std::isfinite(fs)) return;
    double fc = d->fc;
    const double nyq = 0.5 * fs;
    if (fc <= 0.0) fc = 0.001;
    if (fc > 0.99 * nyq) fc = 0.99 * nyq;
    if (!(fc > 0.0)) return;
    const double w0 = 2.0 * PI * fc / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double q = d->q > 0.0 ? d->q : 0.7071067811865476;
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;

    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    if (M == 3) { // low-pass
        b0 = (1.0 - cw) * 0.5;
        b1 = 1.0 - cw;
        b2 = b0;
    } else if (M == 4) { // high-pass
        b0 = (1.0 + cw) * 0.5;
        b1 = -(1.0 + cw);
        b2 = b0;
    } else { // band-pass
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
    }
    if (!std::isfinite(b0) || !std::isfinite(b1) || !std::isfinite(b2) ||
        !std::isfinite(a0) || !(std::fabs(a0) > 1e-15))
        return;

    d->b0 = b0 / a0;
    d->b1 = b1 / a0;
    d->b2 = b2 / a0;
    d->a1 = -2.0 * cw / a0;
    d->a2 = (1.0 - alpha) / a0;
    d->fs = fs;
    d->coefOk = true;
}

template <int M>
double tick(void* h, const NsContext* c) {
    Filters* d = (Filters*)h;
    if (!d || !c) return 0.0;
    // The blob does not know which type it is; each step ensures the window for
    // its type (odd in median/Savitzky-Golay, > order in Savitzky-Golay).
    if (M == 1 || M == 2) normalize(d, M);
    else if (M >= 3) normalize(d, 3);
    const double x = sock0(c);
    if (!std::isfinite(x)) return d->last;

    double y = 0.0;
    if (M == 0) { // moving average
        d->buf.push_back(x);
        d->sum += x;
        const int n = nInt(d);
        while ((int)d->buf.size() > n) {
            d->sum -= d->buf.front();
            d->buf.pop_front();
        }
        y = d->buf.empty() ? x : d->sum / (double)d->buf.size();
    } else if (M == 1) { // median
        d->buf.push_back(x);
        const int n = nInt(d);
        while ((int)d->buf.size() > n) d->buf.pop_front();
        std::vector<double> v(d->buf.begin(), d->buf.end());
        const size_t middle = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + (long)middle, v.end());
        y = v[middle];
    } else if (M == 2) { // centered Savitzky-Golay
        d->buf.push_back(x);
        const int n = nInt(d);
        while ((int)d->buf.size() > n) d->buf.pop_front();
        if (d->sgN != n) computeSg(d);
        if ((int)d->buf.size() == n && (int)d->sg.size() == n) {
            double sum = 0.0;
            for (int i = 0; i < n; ++i) sum += d->sg[(size_t)i] * d->buf[(size_t)i];
            y = sum;
        } else {
            y = 0.0;
        }
    } else { // Butterworth
        const double fs = c->dt > 0.0 ? 1.0 / c->dt : 0.0;
        if (!(fs > 0.0) || !std::isfinite(fs)) {
            d->last = x;
            return x;
        }
        if (!d->coefOk || std::fabs(fs - d->fs) > 1e-12 * std::fmax(1.0, fs))
            computeButter<M>(d, fs);
        if (!d->coefOk) {
            d->last = x;
            return x;
        }
        y = d->b0 * x + d->b1 * d->x1 + d->b2 * d->x2 - d->a1 * d->y1 - d->a2 * d->y2;
        if (!std::isfinite(y)) y = 0.0;
        d->x2 = d->x1;
        d->x1 = x;
        d->y2 = d->y1;
        d->y1 = y;
    }
    if (!std::isfinite(y)) y = d->last;
    d->last = y;
    return y;
}

// ---- parameters ----
NsParam pMov[] = {
    {"window", 2, 4096, 20, NS_PARAM_INT, 1, "samples",
     "number of samples in the moving average", 0, nullptr},
};
NsParam pMed[] = {
    {"window", 3, 999, 5, NS_PARAM_INT, 2, "samples",
     "odd number of samples in the median", 0, nullptr},
};
NsParam pSg[] = {
    {"window", 5, 101, 11, NS_PARAM_INT, 2, "samples",
     "odd Savitzky-Golay window", 0, nullptr},
    {"order", 2, 5, 2, NS_PARAM_INT, 1, nullptr,
     "local polynomial order", 0, nullptr},
};
NsParam pButter[] = {
    {"fc", 0.001, 100000, 10, NS_PARAM_NUM, 0.1, "Hz",
     "cutoff frequency (−3 dB)", 0, nullptr},
    {"q", 0.1, 20, 0.7071067811865476, NS_PARAM_NUM, 0.01, nullptr,
     "fator de qualidade (0,707 = Butterworth)", 0, nullptr},
};

enum Field { C_WINDOW, C_ORDER, C_FC, C_Q };

const NsParam* table(int m) {
    switch (m) {
        case 0: return pMov;
        case 1: return pMed;
        case 2: return pSg;
        default: return pButter;
    }
}
int nParams(int m) {
    switch (m) {
        case 0: return (int)(sizeof pMov / sizeof pMov[0]);
        case 1: return (int)(sizeof pMed / sizeof pMed[0]);
        case 2: return (int)(sizeof pSg / sizeof pSg[0]);
        default: return (int)(sizeof pButter / sizeof pButter[0]);
    }
}
const Field* fields(int m) {
    static const Field fMov[] = {C_WINDOW};
    static const Field fMed[] = {C_WINDOW};
    static const Field fSg[] = {C_WINDOW, C_ORDER};
    static const Field fButter[] = {C_FC, C_Q};
    switch (m) {
        case 0: return fMov;
        case 1: return fMed;
        case 2: return fSg;
        default: return fButter;
    }
}

double* field(Filters* d, Field c) {
    switch (c) {
        case C_WINDOW: return &d->window;
        case C_ORDER: return &d->order;
        case C_FC: return &d->fc;
        case C_Q: return &d->q;
    }
    return &d->last;
}

template <int M>
double getParam(void* h, int i) {
    Filters* d = (Filters*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0.0;
    return *field(d, fields(M)[i]);
}

template <int M>
int setParam(void* h, int i, double v) {
    Filters* d = (Filters*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0;
    const NsParam& p = table(M)[i];
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    const Field c = fields(M)[i];
    *field(d, c) = v;
    normalize(d, M);
    if (M == 2) computeSg(d);
    if (M >= 3) d->coefOk = false;
    return 1;
}

// ---- state ----
char* save(void* h) {
    Filters* d = (Filters*)h;
    if (!d) return nullptr;
    char b[768];
    std::snprintf(b, sizeof b,
                  "window=%.17g order=%.17g fc=%.17g q=%.17g "
                  "x1=%.17g x2=%.17g y1=%.17g y2=%.17g last=%.17g buf=",
                  d->window, d->order, d->fc, d->q, d->x1, d->x2, d->y1, d->y2,
                  d->last);
    std::string s = b;
    for (size_t i = 0; i < d->buf.size(); ++i) {
        std::snprintf(b, sizeof b, i ? ",%.17g" : "%.17g", d->buf[i]);
        s += b;
    }
    return dupStr(s.c_str());
}

int load(void* h, const char* blob) {
    Filters* d = (Filters*)h;
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
        if (name == "buf") {
            std::deque<double> buf;
            while (*p) {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                buf.push_back(v);
                p = end;
                if (*p != ',') break;
                ++p;
            }
            d->buf.swap(buf);
            some = true;
            continue;
        }
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "window") d->window = v;
        else if (name == "order") d->order = v;
        else if (name == "fc") d->fc = v;
        else if (name == "q") d->q = v;
        else if (name == "x1") d->x1 = v;
        else if (name == "x2") d->x2 = v;
        else if (name == "y1") d->y1 = v;
        else if (name == "y2") d->y2 = v;
        else if (name == "last") d->last = v;
        else break;
        some = true;
    }
    if (!some) return 0;
    // Determining the type from the node id is not possible here; normalize
    // conservatively and let each step adjust the window when needed.
    if (d->window < 2) d->window = 2;
    if (d->window > 4096) d->window = 4096;
    if (d->order < 2) d->order = 2;
    if (d->order > 5) d->order = 5;
    if (!(d->fc > 0)) d->fc = 0.001;
    if (!(d->q > 0)) d->q = 0.1;
    d->coefOk = false;
    computeSg(d);
    d->sum = 0.0;
    for (double v : d->buf) d->sum += v;
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
    kind("movavg", "Moving average", 120, 200, 255, tick<0>, nParams(0), table(0),
         getParam<0>, setParam<0>),
    kind("median", "Median", 150, 210, 235, tick<1>, nParams(1), table(1),
         getParam<1>, setParam<1>),
    kind("savgol", "Savitzky-Golay", 180, 220, 190, tick<2>, nParams(2),
         table(2), getParam<2>, setParam<2>),
    kind("butter_lp", "Butterworth LP", 120, 170, 255, tick<3>, nParams(3),
         table(3), getParam<3>, setParam<3>),
    kind("butter_hp", "Butterworth HP", 110, 150, 245, tick<4>, nParams(4),
         table(4), getParam<4>, setParam<4>),
    kind("butter_bp", "Butterworth BP", 140, 160, 245, tick<5>, nParams(5),
         table(5), getParam<5>, setParam<5>),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("filters", "1.0", 6, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
