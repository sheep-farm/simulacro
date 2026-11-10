// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: time-series models with state.
//
//   ar            AR(p) estimated by Yule-Walker in a moving window; publishes the
//                 one-step forecast. If the node has more than one output, the
//                 extra ports expose φ1..φp and the intercept.
//   holt          double exponential smoothing (level + trend); publishes the
//                 one-step forecast. Extra outputs: level and trend.
//   holt_winters  additive triple smoothing (level + trend + seasonality);
//                 publishes the one-step forecast. Extra outputs: level,
//                 trend and the seasonal indices.
//
// Socket 0 is the input series. The state (window, coefficients, seasonal
// phase, etc.) goes into the save/load blob; "Restart" (F5) and resuming a save
// reproduce the same forecast.
//
// Build: make plugins | Usage: Add menu -> Math -> Time series
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

enum { AR = 0, HOLT = 1, HOLT_WINTERS = 2 };

struct Ts {
    int kind = AR;
    double order = 2, window = 120;
    double alpha = 0.2, beta = 0.05, gamma = 0.1, period = 12;

    // AR
    std::deque<double> buf;
    std::vector<double> coef; // [intercept, φ1..φp]
    double forecast = 0.0;

    // Holt
    double level = 0.0, trend = 0.0;
    bool started = false;

    // Holt-Winters
    std::vector<double> init; // samples until 2 periods are complete
    std::vector<double> seas; // seasonal indices
    double hwLevel = 0.0, hwTend = 0.0;
    double hwIdx = 0.0;
    bool hwPronto = false;
};

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

void normalize(Ts* d) {
    if (!d) return;
    d->order = clampD(std::floor(d->order + 0.5), 1.0, 16.0);
    d->window = clampD(std::floor(d->window + 0.5), d->order + 2.0, 2000.0);
    d->alpha = clampD(d->alpha, 0.001, 1.0);
    d->beta = clampD(d->beta, 0.001, 1.0);
    d->gamma = clampD(d->gamma, 0.001, 1.0);
    d->period = clampD(std::floor(d->period + 0.5), 2.0, 144.0);

    if (d->kind == AR) {
        const size_t p = (size_t)d->order;
        while (d->buf.size() > (size_t)d->window) d->buf.pop_front();
        if (d->coef.size() != p + 1) d->coef.assign(p + 1, 0.0);
    }
    if (d->kind == HOLT_WINTERS) {
        const int per = (int)d->period;
        if ((int)d->seas.size() != per) {
            d->seas.assign((size_t)per, 0.0);
            d->init.clear();
            d->hwLevel = d->hwTend = 0.0;
            d->hwIdx = 0.0;
            d->hwPronto = false;
        }
        if (d->hwIdx < 0.0 || d->hwIdx >= (double)per) d->hwIdx = 0.0;
    }
}

template <int T>
void* newValue() {
    Ts* d = new (std::nothrow) Ts();
    if (d) {
        d->kind = T;
        normalize(d);
    }
    return d;
}
void dispose(void* h) { delete (Ts*)h; }

// ---------- AR ----------

bool resolveAr(double A[16][16], double b[16], int p, double* x) {
    double a[16][17];
    for (int i = 0; i < p; ++i) {
        for (int j = 0; j < p; ++j) a[i][j] = A[i][j];
        a[i][p] = b[i];
    }
    for (int col = 0; col < p; ++col) {
        int piv = col;
        for (int r = col + 1; r < p; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (std::fabs(a[piv][col]) < 1e-12) return false;
        if (piv != col)
            for (int j = col; j <= p; ++j) std::swap(a[piv][j], a[col][j]);
        const double div = a[col][col];
        for (int j = col; j <= p; ++j) a[col][j] /= div;
        for (int r = 0; r < p; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            if (f == 0.0) continue;
            for (int j = col; j <= p; ++j) a[r][j] -= f * a[col][j];
        }
    }
    for (int i = 0; i < p; ++i) x[i] = a[i][p];
    return true;
}

void advanceAr(Ts* d, double x) {
    d->buf.push_back(x);
    while (d->buf.size() > (size_t)d->window) d->buf.pop_front();
    const int p = (int)d->order;
    if ((int)d->buf.size() < (int)d->window) {
        d->forecast = 0.0;
        return;
    }
    const int n = (int)d->buf.size();
    double mean = 0.0;
    for (double v : d->buf) mean += v;
    mean /= n;
    double gamma[17] = {0};
    for (int k = 0; k <= p; ++k) {
        double s = 0.0;
        for (int t = k; t < n; ++t)
            s += (d->buf[(size_t)t] - mean) * (d->buf[(size_t)(t - k)] - mean);
        gamma[k] = s / n;
    }
    double phi[16] = {0};
    if (gamma[0] > 1e-15) {
        double A[16][16] = {{0}};
        double b[16] = {0};
        for (int i = 0; i < p; ++i) {
            b[i] = gamma[i + 1];
            for (int j = 0; j < p; ++j) A[i][j] = gamma[std::abs(i - j)];
        }
        if (!resolveAr(A, b, p, phi)) {
            for (int i = 0; i < p; ++i) phi[i] = 0.0;
        }
    }
    double sum = 0.0;
    for (int i = 0; i < p; ++i) sum += phi[i];
    d->coef.assign((size_t)p + 1, 0.0);
    d->coef[0] = mean * (1.0 - sum);
    for (int i = 0; i < p; ++i) d->coef[(size_t)i + 1] = phi[i];
    double prev = d->coef[0];
    for (int i = 1; i <= p; ++i) prev += d->coef[(size_t)i] * d->buf[(size_t)(n - i)];
    d->forecast = prev;
}

// ---------- Holt and Holt-Winters ----------

void advanceHolt(Ts* d, double x) {
    if (!d->started) {
        d->level = x;
        d->trend = 0.0;
        d->started = true;
        d->forecast = x;
        return;
    }
    const double prevLevel = d->level;
    d->level = d->alpha * x + (1.0 - d->alpha) * (d->level + d->trend);
    d->trend = d->beta * (d->level - prevLevel) + (1.0 - d->beta) * d->trend;
    d->forecast = d->level + d->trend;
}

void advanceHw(Ts* d, double x) {
    const int per = (int)d->period;
    if (!d->hwPronto) {
        d->init.push_back(x);
        if ((int)d->init.size() < 2 * per) {
            d->forecast = 0.0;
            return;
        }
        double m1 = 0.0, m2 = 0.0;
        for (int i = 0; i < per; ++i) m1 += d->init[(size_t)i];
        for (int i = 0; i < per; ++i) m2 += d->init[(size_t)(per + i)];
        m1 /= per;
        m2 /= per;
        d->hwLevel = m1;
        d->hwTend = (m2 - m1) / per;
        d->seas.assign((size_t)per, 0.0);
        for (int i = 0; i < per; ++i) d->seas[(size_t)i] = d->init[(size_t)i] - m1;
        d->hwIdx = 0.0;
        d->hwPronto = true;
        d->init.clear();
        d->forecast = 0.0;
        return;
    }
    int s = (int)d->hwIdx;
    if (s < 0 || s >= per) s = 0;
    const double prevLevel = d->hwLevel;
    d->hwLevel = d->alpha * (x - d->seas[(size_t)s]) +
                 (1.0 - d->alpha) * (d->hwLevel + d->hwTend);
    d->hwTend = d->beta * (d->hwLevel - prevLevel) + (1.0 - d->beta) * d->hwTend;
    d->seas[(size_t)s] = d->gamma * (x - d->hwLevel) + (1.0 - d->gamma) * d->seas[(size_t)s];
    d->hwIdx = (double)((s + 1) % per);
    const int next = (int)d->hwIdx;
    d->forecast = d->hwLevel + d->hwTend + d->seas[(size_t)next];
}

bool advance(Ts* d, const NsContext* c) {
    if (!d || !c || c->nin <= 0) return false;
    const double x = sockValue(c, 0);
    if (!std::isfinite(x)) return false;
    if (d->kind == AR) advanceAr(d, x);
    else if (d->kind == HOLT) advanceHolt(d, x);
    else advanceHw(d, x);
    return true;
}

double step(void* h, const NsContext* c) {
    Ts* d = (Ts*)h;
    if (!d) return 0.0;
    advance(d, c);
    return d->forecast;
}

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Ts* d = (Ts*)h;
    if (!d || !out) return 0;
    advance(d, c);
    for (int i = 0; i < nout; ++i) out[i] = 0.0;
    if (nout > 0) out[0] = d->forecast;
    if (d->kind == AR) {
        const int p = (int)d->order;
        for (int i = 1; i <= p && i < nout; ++i)
            out[i] = d->coef.size() > (size_t)i ? d->coef[(size_t)i] : 0.0;
        if (p + 1 < nout) out[p + 1] = d->coef.empty() ? 0.0 : d->coef[0];
    } else if (d->kind == HOLT) {
        if (nout > 1) out[1] = d->level;
        if (nout > 2) out[2] = d->trend;
    } else {
        if (nout > 1) out[1] = d->hwLevel;
        if (nout > 2) out[2] = d->hwTend;
        for (int i = 0; i < (int)d->seas.size() && i + 3 < nout; ++i)
            out[i + 3] = d->seas[(size_t)i];
    }
    return 1;
}

// ---------- parameters ----------

NsParam paramsAr[] = {
    {"order", 1, 16, 2, NS_PARAM_INT, 1, "samples", "AR model order p", 0, nullptr},
    {"window", 4, 2000, 120, NS_PARAM_INT, 1, "samples",
     "estimation window (minimum order+2)", 0, nullptr},
};
NsParam paramsHolt[] = {
    {"alpha", 0.001, 1, 0.2, NS_PARAM_NUM, 0.01, nullptr, "level smoothing", 0, nullptr},
    {"beta", 0.001, 1, 0.05, NS_PARAM_NUM, 0.01, nullptr, "trend smoothing", 0, nullptr},
};
NsParam paramsHw[] = {
    {"alpha", 0.001, 1, 0.2, NS_PARAM_NUM, 0.01, nullptr, "level smoothing", 0, nullptr},
    {"beta", 0.001, 1, 0.05, NS_PARAM_NUM, 0.01, nullptr, "trend smoothing", 0, nullptr},
    {"gamma", 0.001, 1, 0.1, NS_PARAM_NUM, 0.01, nullptr, "seasonality smoothing", 0, nullptr},
    {"period", 2, 144, 12, NS_PARAM_INT, 1, "samples", "seasonal period", 0, nullptr},
};

double getParam(void* h, int i) {
    Ts* d = (Ts*)h;
    if (!d) return 0.0;
    if (d->kind == AR) {
        if (i == 0) return d->order;
        if (i == 1) return d->window;
        return 0.0;
    }
    if (d->kind == HOLT) {
        if (i == 0) return d->alpha;
        if (i == 1) return d->beta;
        return 0.0;
    }
    if (i == 0) return d->alpha;
    if (i == 1) return d->beta;
    if (i == 2) return d->gamma;
    if (i == 3) return d->period;
    return 0.0;
}

int setParam(void* h, int i, double v) {
    Ts* d = (Ts*)h;
    if (!d) return 0;
    if (d->kind == AR) {
        if (i == 0) d->order = v;
        else if (i == 1) d->window = v;
        else return 0;
    } else if (d->kind == HOLT) {
        if (i == 0) d->alpha = v;
        else if (i == 1) d->beta = v;
        else return 0;
    } else {
        if (i == 0) d->alpha = v;
        else if (i == 1) d->beta = v;
        else if (i == 2) d->gamma = v;
        else if (i == 3) d->period = v;
        else return 0;
    }
    normalize(d);
    return 1;
}

// ---------- save/load ----------

void addNum(std::string& s, const char* k, double v) {
    char b[160];
    std::snprintf(b, sizeof b, "%s=%.17g ", k, v);
    s += b;
}

template <typename V>
void addVec(std::string& s, const char* k, const V& v) {
    s += k;
    s += '=';
    for (size_t i = 0; i < v.size(); ++i) {
        char b[48];
        std::snprintf(b, sizeof b, i ? ",%.17g" : "%.17g", v[i]);
        s += b;
    }
    s += ' ';
}

char* save(void* h) {
    Ts* d = (Ts*)h;
    if (!d) return nullptr;
    std::string s;
    addNum(s, "type", d->kind);
    addNum(s, "order", d->order);
    addNum(s, "window", d->window);
    addNum(s, "alpha", d->alpha);
    addNum(s, "beta", d->beta);
    addNum(s, "gamma", d->gamma);
    addNum(s, "period", d->period);
    addNum(s, "forecast", d->forecast);
    addNum(s, "started", d->started ? 1.0 : 0.0);
    addNum(s, "level", d->level);
    addNum(s, "trend", d->trend);
    addNum(s, "hwLevel", d->hwLevel);
    addNum(s, "hwTend", d->hwTend);
    addNum(s, "hwIdx", d->hwIdx);
    addNum(s, "hwReady", d->hwPronto ? 1.0 : 0.0);
    addVec(s, "buf", d->buf);
    addVec(s, "coef", d->coef);
    addVec(s, "init", d->init);
    addVec(s, "seas", d->seas);
    const size_t n = s.size() + 1;
    char* r = (char*)std::malloc(n);
    if (r) std::memcpy(r, s.c_str(), n);
    return r;
}

template <typename V>
void leVec(const char*& q, V& v) {
    v.clear();
    while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') {
        char* end = nullptr;
        const double x = std::strtod(q, &end);
        if (end == q) break;
        v.push_back(x);
        q = end;
        if (*q == ',') ++q;
        else break;
    }
}

int load(void* h, const char* blob) {
    Ts* d = (Ts*)h;
    if (!d || !blob) return 0;
    double order = d->order, window = d->window, alpha = d->alpha, beta = d->beta;
    double gamma = d->gamma, period = d->period, forecast = d->forecast;
    double level = d->level, trend = d->trend;
    double hwLevel = d->hwLevel, hwTend = d->hwTend, hwIdx = d->hwIdx;
    bool started = d->started, hwPronto = d->hwPronto;
    std::deque<double> buf;
    std::vector<double> coef, init, seas;
    const char* q = blob;
    while (*q) {
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') ++q;
        const char* e = q;
        while (*e && *e != '=' && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r') ++e;
        if (!*e || *e != '=') {
            while (*e && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r') ++e;
            q = e;
            continue;
        }
        const std::string k(q, e - q);
        q = e + 1;
        if (k == "buf") { leVec(q, buf); continue; }
        if (k == "coef") { leVec(q, coef); continue; }
        if (k == "init") { leVec(q, init); continue; }
        if (k == "seas") { leVec(q, seas); continue; }
        char* end = nullptr;
        const double v = std::strtod(q, &end);
        if (end == q) {
            while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') ++q;
            continue;
        }
        q = end;
        if (k == "order") order = v;
        else if (k == "window") window = v;
        else if (k == "alpha") alpha = v;
        else if (k == "beta") beta = v;
        else if (k == "gamma") gamma = v;
        else if (k == "period") period = v;
        else if (k == "forecast") forecast = v;
        else if (k == "started") started = v > 0.5;
        else if (k == "level") level = v;
        else if (k == "trend") trend = v;
        else if (k == "hwLevel") hwLevel = v;
        else if (k == "hwTend") hwTend = v;
        else if (k == "hwIdx") hwIdx = v;
        else if (k == "hwReady") hwPronto = v > 0.5;
        else if (k == "type") { /* the node type takes precedence */ }
    }
    d->order = order;
    d->window = window;
    d->alpha = alpha;
    d->beta = beta;
    d->gamma = gamma;
    d->period = period;
    d->forecast = forecast;
    d->started = started;
    d->level = level;
    d->trend = trend;
    d->hwLevel = hwLevel;
    d->hwTend = hwTend;
    d->hwIdx = hwIdx;
    d->hwPronto = hwPronto;
    d->buf = buf;
    d->coef = coef;
    d->init = init;
    d->seas = seas;
    normalize(d);
    return 1;
}

char* labelIn(void*, int p) {
    if (p != 0) return nullptr;
    char* r = (char*)std::malloc(2);
    if (r) std::memcpy(r, "x", 2);
    return r;
}

NsNodeType kind(const char* id, const char* label, float r, float g, float b,
                void* (*newFn)(), NsParam* params, int nparams) {
    NsNodeType t{};
    t.id = id;
    t.label = label;
    t.color[0] = r;
    t.color[1] = g;
    t.color[2] = b;
    t.create = newFn;
    t.destroy = dispose;
    t.step = step;
    t.stepMulti = stepMulti;
    t.save = save;
    t.load = load;
    t.params = params;
    t.num_params = nparams;
    t.getParam = getParam;
    t.setParam = setParam;
    t.def_in = 1;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_INPUT_LABELS;
    return t;
}

NsNodeType types[] = {
    kind("ar", "AR(p) — forecast", 115, 185, 230, newValue<AR>, paramsAr, 2),
    kind("holt", "Holt — level and trend", 130, 195, 225, newValue<HOLT>, paramsHolt, 2),
    kind("holt_winters", "Holt-Winters — seasonal", 145, 205, 215, newValue<HOLT_WINTERS>, paramsHw, 4),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("timeseries", "1.0", 3, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
