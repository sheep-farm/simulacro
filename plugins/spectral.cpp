// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: spectral analysis of a moving window.
//
//   fft       one-sided magnitude spectrum (list of N/2 numbers on the wire)
//   psd       power spectral density by Welch (average of segments)
//   domfreq   dominant frequency in Hz (number on port 0)
//
// Common parameters: `janela` (N, power of 2 between 16 and 4096) and `salto`
// (samples between recalculations). PSD adds `media` (1..16 segments with
// overlap of `salto` samples). The Hann window is applied in all; the
// frequency uses fs = 1/dt of the step and, if dt is invalid, fs = 1.
//
// `fft` and `psd` publish a LIST on the wire (ABI 10) and also leave the dominant
// frequency as the numeric value of the port; `domfreq` only returns the number in Hz.
// The spectrum is recomputed every `salto` samples; between recalculations the previous
// value is held. The list is valid until the next step (the host copies it).
//
// The buffer goes into the save/load blob; F5 reproduces the same window and the same
// spectrum. Non-finite inputs are ignored and counted by the host in `nan=…`.
//
// Build: make plugins | Usage: Add menu -> Signal -> FFT / PSD / Frequency
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace {

const double PI = 3.141592653589793238462643383279502884;

struct Spectral {
    double window = 256.0;
    double hop = 128.0;
    double mean = 4.0;

    std::deque<double> buf;
    std::vector<double> spec; // published magnitude/PSD
    double domfreq = 0.0;
    long n = 0;               // samples since the last recalculation
    double last = 0.0;
};

void* create() { return new (std::nothrow) Spectral(); }
void destroy(void* h) { delete (Spectral*)h; }

double sock0(const NsContext* c) {
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) v += c->in[k];
    return v;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("x") : nullptr; }

int pot2(double v) {
    int j = (int)std::llround(v);
    if (j < 16) j = 16;
    if (j > 4096) j = 4096;
    int p = 16;
    while (p * 2 <= j) p *= 2;
    return p;
}

int nInt(const Spectral* d) { return pot2(d ? d->window : 256.0); }
int hopInt(const Spectral* d) {
    int s = (int)std::llround(d ? d->hop : 128.0);
    const int n = nInt(d);
    if (s < 1) s = 1;
    if (s > n) s = n;
    return s;
}
int meanInt(const Spectral* d) {
    int m = (int)std::llround(d ? d->mean : 4.0);
    if (m < 1) m = 1;
    if (m > 16) m = 16;
    return m;
}

size_t capBuffer(const Spectral* d, int M) {
    const size_t n = (size_t)nInt(d);
    if (M != 1) return n;
    return n + (size_t)(meanInt(d) - 1) * (size_t)hopInt(d);
}

void normalize(Spectral* d, int M) {
    if (!d) return;
    d->window = (double)nInt(d);
    d->hop = (double)hopInt(d);
    d->mean = (double)meanInt(d);
    const size_t cap = capBuffer(d, M);
    while (d->buf.size() > cap) d->buf.pop_front();
}

// Iterative radix-2 FFT, in-place. The size must be a power of 2.
void fft(std::vector<std::complex<double>>& a) {
    const int n = (int)a.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[(size_t)i], a[(size_t)j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[(size_t)(i + j)];
                const std::complex<double> v = a[(size_t)(i + j + len / 2)] * w;
                a[(size_t)(i + j)] = u + v;
                a[(size_t)(i + j + len / 2)] = u - v;
                w *= wl;
            }
        }
    }
}

// Fills re/im with the Hann window and returns the sum of the weights. The periodic
// window (N in the denominator) is the usual choice for FFT/PSD.
void hannWindow(std::vector<std::complex<double>>& a, const std::deque<double>& buf,
                size_t start, int n) {
    a.assign((size_t)n, std::complex<double>(0.0, 0.0));
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * PI * (double)i / (double)n);
        a[(size_t)i] = std::complex<double>(buf[start + (size_t)i] * w, 0.0);
    }
}

double windowSum2(int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * PI * (double)i / (double)n);
        s += w * w;
    }
    return s;
}

double windowSum(int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += 0.5 - 0.5 * std::cos(2.0 * PI * (double)i / (double)n);
    return s;
}

template <int M>
void computeSpectrum(Spectral* d, double dt) {
    const int n = nInt(d);
    if (n < 16 || (int)d->buf.size() < n) return;
    const double fs = dt > 0.0 && std::isfinite(dt) ? 1.0 / dt : 1.0;
    const double sumW = windowSum(n);
    const double sumW2 = windowSum2(n);
    if (!(sumW > 0.0) || !(sumW2 > 0.0)) return;

    if (M == 0) { // FFT: one window, one-sided magnitude
        std::vector<std::complex<double>> a;
        hannWindow(a, d->buf, d->buf.size() - (size_t)n, n);
        fft(a);
        d->spec.assign((size_t)(n / 2), 0.0);
        int peak = 1;
        double maxv = -1.0;
        for (int k = 0; k < n / 2; ++k) {
            double mag = std::abs(a[(size_t)k]);
            if (k > 0) mag *= 2.0;
            mag /= sumW;
            d->spec[(size_t)k] = mag;
            if (k >= 1 && mag > maxv) { maxv = mag; peak = k; }
        }
        d->domfreq = (double)peak * fs / (double)n;
        return;
    }

    if (M == 1) { // Welch: average of N-length segments with step `salto`
        const int hop = hopInt(d);
        const int mean = meanInt(d);
        std::vector<double> acc((size_t)(n / 2), 0.0);
        int used = 0;
        for (int s = 0; s < mean; ++s) {
            const size_t start = (size_t)s * (size_t)hop;
            if (start + (size_t)n > d->buf.size()) break;
            std::vector<std::complex<double>> a;
            hannWindow(a, d->buf, start, n);
            fft(a);
            for (int k = 0; k < n / 2; ++k) {
                double p = std::norm(a[(size_t)k]) / (fs * sumW2);
                if (k > 0) p *= 2.0;
                acc[(size_t)k] += p;
            }
            ++used;
        }
        if (used == 0) return;
        for (double& v : acc) v /= (double)used;
        d->spec.swap(acc);
        int peak = 1;
        double maxv = -1.0;
        for (int k = 1; k < n / 2; ++k)
            if (d->spec[(size_t)k] > maxv) { maxv = d->spec[(size_t)k]; peak = k; }
        d->domfreq = (double)peak * fs / (double)n;
        return;
    }

    // domfreq: only the peak position, without publishing the list
    std::vector<std::complex<double>> a;
    hannWindow(a, d->buf, d->buf.size() - (size_t)n, n);
    fft(a);
    int peak = 1;
    double maxv = -1.0;
    for (int k = 1; k < n / 2; ++k) {
        const double mag = std::abs(a[(size_t)k]);
        if (mag > maxv) { maxv = mag; peak = k; }
    }
    d->domfreq = (double)peak * fs / (double)n;
    d->spec.clear();
}

template <int M>
double tick(void* h, const NsContext* c) {
    Spectral* d = (Spectral*)h;
    if (!d || !c) return 0.0;
    normalize(d, M);
    const double x = sock0(c);
    if (std::isfinite(x)) {
        d->buf.push_back(x);
        const size_t cap = capBuffer(d, M);
        while (d->buf.size() > cap) d->buf.pop_front();
        ++d->n;
    }
    if ((int)d->buf.size() >= nInt(d) && d->n >= hopInt(d)) {
        computeSpectrum<M>(d, c->dt);
        d->n = 0;
    }
    d->last = d->domfreq;
    return d->domfreq;
}

template <int M>
int outValue(void* h, int port, NsValue* v) {
    Spectral* d = (Spectral*)h;
    if (!d || !v || port != 0 || d->spec.empty()) return 0;
    v->kind = NS_VAL_LIST;
    v->num = d->domfreq;
    v->txt = nullptr;
    v->data = d->spec.data();
    v->rows = 1;
    v->cols = (int)d->spec.size();
    return 1;
}

int numOut(void*) { return 1; }

char* portLabel(void*, int p) {
    if (p != 0) return nullptr;
    return dupStr("spectrum");
}

char* portLabelHz(void*, int p) {
    if (p != 0) return nullptr;
    return dupStr("Hz");
}

// ---- parameters ----
NsParam pFft[] = {
    {"window", 16, 4096, 256, NS_PARAM_INT, 16, "samples",
     "FFT size (power of 2)", 0, nullptr},
    {"hop", 1, 4096, 128, NS_PARAM_INT, 1, "samples",
     "samples between recalculations", 0, nullptr},
};
NsParam pPsd[] = {
    {"window", 16, 4096, 256, NS_PARAM_INT, 16, "samples",
     "segment size (power of 2)", 0, nullptr},
    {"hop", 1, 4096, 128, NS_PARAM_INT, 1, "samples",
     "step between segments", 0, nullptr},
    {"mean", 1, 16, 4, NS_PARAM_INT, 1, "segmentos",
     "number of Welch segments", 0, nullptr},
};
NsParam pDom[] = {
    {"window", 16, 4096, 256, NS_PARAM_INT, 16, "samples",
     "FFT size (power of 2)", 0, nullptr},
    {"hop", 1, 4096, 64, NS_PARAM_INT, 1, "samples",
     "samples between recalculations", 0, nullptr},
};

enum Field { C_WINDOW, C_HOP, C_MEAN };

const NsParam* table(int m) {
    if (m == 0) return pFft;
    if (m == 1) return pPsd;
    return pDom;
}
int nParams(int m) {
    if (m == 0) return (int)(sizeof pFft / sizeof pFft[0]);
    if (m == 1) return (int)(sizeof pPsd / sizeof pPsd[0]);
    return (int)(sizeof pDom / sizeof pDom[0]);
}
const Field* fields(int m) {
    static const Field fFft[] = {C_WINDOW, C_HOP};
    static const Field fPsd[] = {C_WINDOW, C_HOP, C_MEAN};
    static const Field fDom[] = {C_WINDOW, C_HOP};
    if (m == 0) return fFft;
    if (m == 1) return fPsd;
    return fDom;
}
double* field(Spectral* d, Field c) {
    if (c == C_WINDOW) return &d->window;
    if (c == C_HOP) return &d->hop;
    return &d->mean;
}

template <int M>
double getParam(void* h, int i) {
    Spectral* d = (Spectral*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0.0;
    return *field(d, fields(M)[i]);
}

template <int M>
int setParam(void* h, int i, double v) {
    Spectral* d = (Spectral*)h;
    if (!d || i < 0 || i >= nParams(M)) return 0;
    const NsParam& p = table(M)[i];
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    *field(d, fields(M)[i]) = v;
    normalize(d, M);
    d->spec.clear();
    return 1;
}

// ---- state ----
char* save(void* h) {
    Spectral* d = (Spectral*)h;
    if (!d) return nullptr;
    char b[512];
    std::snprintf(b, sizeof b,
                  "window=%.17g shigh=%.17g mean=%.17g dominant_freq=%.17g n=%ld "
                  "last=%.17g buf=",
                  d->window, d->hop, d->mean, d->domfreq, d->n, d->last);
    std::string s = b;
    for (size_t i = 0; i < d->buf.size(); ++i) {
        std::snprintf(b, sizeof b, i ? ",%.17g" : "%.17g", d->buf[i]);
        s += b;
    }
    return dupStr(s.c_str());
}

int load(void* h, const char* blob) {
    Spectral* d = (Spectral*)h;
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
        else if (name == "hop") d->hop = v;
        else if (name == "mean") d->mean = v;
        else if (name == "dominant_freq") d->domfreq = v;
        else if (name == "n") d->n = (long)std::llround(v);
        else if (name == "last") d->last = v;
        else break;
        some = true;
    }
    if (!some) return 0;
    d->window = (double)pot2(d->window);
    int s = (int)std::llround(d->hop);
    if (s < 1) s = 1;
    if (s > nInt(d)) s = nInt(d);
    d->hop = (double)s;
    int m = (int)std::llround(d->mean);
    if (m < 1) m = 1;
    if (m > 16) m = 16;
    d->mean = (double)m;
    if (d->n < 0) d->n = 0;
    if (!std::isfinite(d->domfreq)) d->domfreq = 0.0;
    if (!std::isfinite(d->last)) d->last = 0.0;
    d->spec.clear();
    return 1;
}

// ---- type table ----
using StepFn = double (*)(void*, const NsContext*);
using GetFn = double (*)(void*, int);
using SetFn = int (*)(void*, int, double);
using ValueFn = int (*)(void*, int, NsValue*);

NsNodeType kind(const char* id, const char* label, float r, float g, float b,
                StepFn step, int np, const NsParam* params, GetFn get, SetFn set,
                bool list, ValueFn value) {
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
    t.self_out = list ? 1 : 0;
    t.numOut = list ? numOut : nullptr;
    t.outValue = value;
    t.portLabel = list ? portLabel : portLabelHz;
    t.capabilities = NS_CAP_STATE | NS_CAP_PARAMS | NS_CAP_INPUT_LABELS |
                     (list ? (NS_CAP_VALUE_OUT | NS_CAP_DYNAMIC_OUT) : 0);
    return t;
}

NsNodeType types[] = {
    kind("fft", "FFT", 110, 190, 255, tick<0>, nParams(0), table(0), getParam<0>,
         setParam<0>, true, outValue<0>),
    kind("psd", "PSD (Welch)", 90, 170, 240, tick<1>, nParams(1), table(1),
         getParam<1>, setParam<1>, true, outValue<1>),
    kind("dominant_freq", "Dominant frequency", 130, 210, 220, tick<2>, nParams(2),
         table(2), getParam<2>, setParam<2>, false, nullptr),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("spectral", "1.0", 3, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
