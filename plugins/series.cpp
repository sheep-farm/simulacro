// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: series transformations (one sample per step).
//
//   Δ (difference)  x[t] − x[t−1]                    (first sample = 0)
//   Log-return      log(x[t] / x[t−1])               (x≤0 or previous≤0 = 0)
//   EMA             y += alpha·(x − y)               (1st sample: y = x)
//   Delay (lag)     x[t−n]                           (warms up with zeros)
//
// Each type has its own state and the save/load blob stores everything (including
// `alpha`/`n`): the trace is reproducible from the file. Non-finite inputs
// are IGNORED (they do not poison the state); the host already counts each non-finite
// port in `nan=…`.
//
// Build: make plugins | Usage: Add menu -> Signal -> Δ / Log-return / EMA / Delay
#include <plugin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <string>

namespace {

const int MAX_LAG = 256;

struct Series {
    // Δ and log-return
    double prev = 0;
    bool hasPrev = false;
    // EMA
    double y = 0;
    bool hasY = false;
    double alpha = 0.1;
    // lag
    int n = 1;
    std::deque<double> buf; // front = oldest sample
};

void* create() { return new (std::nothrow) Series(); }
void destroy(void* h) { delete (Series*)h; }

// value of socket 0 (sum of the wires of that socket, as in the host)
double sock0(const NsContext* c) {
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) v += c->in[k];
    return v;
}

int nOf(const Series* d) {
    int n = d ? d->n : 1;
    if (n < 1) n = 1;
    if (n > MAX_LAG) n = MAX_LAG;
    return n;
}

// keeps the lag buffer of size n (warms up with zeros when it grows)
void adjustBuffer(Series* d) {
    int n = nOf(d);
    while ((int)d->buf.size() < n) d->buf.push_front(0.0);
    while ((int)d->buf.size() > n) d->buf.pop_front();
}

template <int M>
double tick(void* h, const NsContext* c) {
    Series* d = (Series*)h;
    if (!d) return 0.0;
    const double x = sock0(c);
    const bool finite = std::isfinite(x);

    if (M == 0) { // difference
        if (!finite) return 0.0;
        double r = d->hasPrev ? x - d->prev : 0.0;
        d->prev = x;
        d->hasPrev = true;
        return r;
    }
    if (M == 1) { // log-return
        if (!finite) return 0.0;
        double r = (d->hasPrev && x > 0.0 && d->prev > 0.0) ? std::log(x / d->prev) : 0.0;
        d->prev = x;
        d->hasPrev = true;
        return r;
    }
    if (M == 2) { // EMA
        double a = d->alpha;
        if (!(a > 0.0)) a = 0.001;
        if (a > 1.0) a = 1.0;
        if (finite) {
            if (!d->hasY) { d->y = x; d->hasY = true; }
            else d->y += a * (x - d->y);
        }
        return d->y;
    }
    // lag
    adjustBuffer(d);
    double r = d->buf.empty() ? 0.0 : d->buf.front();
    if (finite) {
        d->buf.push_back(x);
        while ((int)d->buf.size() > nOf(d)) d->buf.pop_front();
    }
    return r;
}

// ---- parameters ----
NsParam pEma[] = {
    {"alpha", 0.001, 1.0, 0.1, NS_PARAM_NUM, 0.01, nullptr,
     "new-sample weight (0.001 to 1)", 0, nullptr},
};
NsParam pLag[] = {
    {"n", 1, MAX_LAG, 1, NS_PARAM_INT, 1, "samples",
     "delay in samples", 0, nullptr},
};

double getEma(void* h, int i) {
    Series* d = (Series*)h;
    return (d && i == 0) ? d->alpha : 0.0;
}
int poeEma(void* h, int i, double v) {
    Series* d = (Series*)h;
    if (!d || i != 0) return 0;
    if (!(v > 0.0)) v = 0.001;
    if (v > 1.0) v = 1.0;
    d->alpha = v;
    return 1;
}
double getLag(void* h, int i) {
    Series* d = (Series*)h;
    return (d && i == 0) ? d->n : 0.0;
}
int poeLag(void* h, int i, double v) {
    Series* d = (Series*)h;
    if (!d || i != 0) return 0;
    int n = (int)std::llround(v);
    if (n < 1) n = 1;
    if (n > MAX_LAG) n = MAX_LAG;
    d->n = n;
    adjustBuffer(d);
    return 1;
}

// ---- state (blob) ----
char* save(void* h) {
    Series* d = (Series*)h;
    if (!d) return nullptr;
    std::string s;
    char b[80];
    std::snprintf(b, sizeof b, "prev=%.17g temPrev=%d ", d->prev, d->hasPrev ? 1 : 0);
    s += b;
    std::snprintf(b, sizeof b, "y=%.17g hasY=%d alpha=%.17g n=%d buf=", d->y, d->hasY ? 1 : 0,
                  d->alpha, d->n);
    s += b;
    for (size_t i = 0; i < d->buf.size(); ++i) {
        std::snprintf(b, sizeof b, "%.17g", d->buf[i]);
        s += b;
        if (i + 1 < d->buf.size()) s += ',';
    }
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int load(void* h, const char* s) {
    Series* d = (Series*)h;
    if (!d || !s) return 0;
    const std::string str(s);
    auto value = [&](const char* key) -> std::string {
        const std::string k = std::string(key) + "=";
        size_t p = str.find(k);
        if (p == std::string::npos) return std::string();
        p += k.size();
        size_t e = str.find(' ', p);
        return str.substr(p, e == std::string::npos ? std::string::npos : e - p);
    };
    const std::string sprev = value("prev"), stem = value("temPrev"), sy = value("y"),
                    stemY = value("hasY"), salpha = value("alpha"), sn = value("n"),
                    sbuf = value("buf");
    if (sprev.empty() || sy.empty() || salpha.empty() || sn.empty()) return 0;

    double prev = 0, y = 0, alpha = 0.1;
    int hasPrev = 0, hasY = 0, n = 1;
    if (std::sscanf(sprev.c_str(), "%lf", &prev) != 1) return 0;
    if (std::sscanf(sy.c_str(), "%lf", &y) != 1) return 0;
    if (std::sscanf(salpha.c_str(), "%lf", &alpha) != 1) return 0;
    if (std::sscanf(sn.c_str(), "%d", &n) != 1) return 0;
    std::sscanf(stem.c_str(), "%d", &hasPrev);
    std::sscanf(stemY.c_str(), "%d", &hasY);
    if (n < 1) n = 1;
    if (n > MAX_LAG) n = MAX_LAG;
    if (!(alpha > 0.0)) alpha = 0.001;
    if (alpha > 1.0) alpha = 1.0;

    std::deque<double> buf;
    const char* p = sbuf.c_str();
    while (*p) {
        char* e = nullptr;
        double v = std::strtod(p, &e);
        if (e == p) break;
        buf.push_back(v);
        p = e;
        if (*p == ',') ++p;
    }

    d->prev = prev;
    d->hasPrev = hasPrev != 0;
    d->y = y;
    d->hasY = hasY != 0;
    d->alpha = alpha;
    d->n = n;
    d->buf.swap(buf);
    // Do not normalize the buffer here: the saved state must be preserved exactly as
    // it is. The lag step calls ajustaBuf() before reading/writing.
    return 1;
}

NsNodeType types[] = {
    {"diff", "Δ (difference)", {120, 200, 255}, create, destroy, tick<0>, save, load},
    {"logret", "Log-retorno", {100, 170, 240}, create, destroy, tick<1>, save, load},
    {"ema", "EMA", {150, 205, 255}, create, destroy, tick<2>, save, load,
     1, pEma, getEma, poeEma},
    {"lag", "Delay (lag)", {90, 160, 220}, create, destroy, tick<3>, save, load,
     1, pLag, getLag, poeLag},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("series", "1.0", 4, types, "Signal");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
