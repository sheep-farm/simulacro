// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: moving-window statistics over the input series.
//
// Socket 0 = x (main series); socket 1 = y (2nd series, for correlation/beta/cov).
// Parameters:
//   tipo   = 0 mean | 1 variance | 2 stddev | 3 minimum | 4 maximum | 5 sum
//            6 z of current x | 7 corr(x,y) | 8 beta of y~x (OLS in the window)
//            9 cov(x,y) | 10 number of samples in the window
//   janela = number of SAMPLES in the window (2..600, default 60)
//   taxa   = samples per SIMULATION SECOND (0 = one per step), as in the
//            CSV/RNG plugin: between samples the statistic stays put
//
// The window buffer goes into the save/load blob => "Restart" (F5) reproduces
// exactly the same series of statistics.
//
// Build: make plugins | Usage: Add menu -> Math -> Statistics
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

struct Stats {
    double kind = 0, window = 60, rate = 0;
    double key = -1e308;      // sample (t*taxa) of the last record
    std::vector<double> xs, ys;
};

// sample mean/variance (n-1) and sample covariance of the buffers
void moments(const Stats* d, double& mx, double& vx, double& my, double& vy, double& cxy) {
    size_t n = d->xs.size();
    mx = vx = my = vy = cxy = 0.0;
    if (n == 0) return;
    for (size_t k = 0; k < n; ++k) { mx += d->xs[k]; my += d->ys[k]; }
    mx /= n;
    my /= n;
    if (n < 2) return;
    for (size_t k = 0; k < n; ++k) {
        double dx = d->xs[k] - mx, dy = d->ys[k] - my;
        vx += dx * dx;
        vy += dy * dy;
        cxy += dx * dy;
    }
    vx /= (n - 1);
    vy /= (n - 1);
    cxy /= (n - 1);
}

double compute(const Stats* d, double currentX) {
    if (d->xs.empty()) return 0.0;
    int kind = (int)(d->kind + 0.5);
    if (kind == 3 || kind == 4) { // minimum / maximum
        double v = d->xs[0];
        for (double v2 : d->xs) v = kind == 3 ? std::min(v, v2) : std::max(v, v2);
        return v;
    }
    if (kind == 5) { // sum
        double s = 0;
        for (double v : d->xs) s += v;
        return s;
    }
    if (kind == 10) return (double)d->xs.size(); // count
    double mx, vx, my, vy, cxy;
    moments(d, mx, vx, my, vy, cxy);
    switch (kind) {
        case 1: return vx;
        case 2: return std::sqrt(vx > 0 ? vx : 0);
        case 6: { // z of the current value
            double s = std::sqrt(vx > 0 ? vx : 0);
            return s > 1e-12 ? (currentX - mx) / s : 0.0;
        }
        case 7: { // correlation
            if (vx <= 1e-12 || vy <= 1e-12) return 0.0;
            return cxy / std::sqrt(vx * vy);
        }
        case 8: return vx > 1e-12 ? cxy / vx : 0.0; // beta of y~x
        case 9: return cxy;                         // covariance
        default: return mx;                         // mean
    }
}

void* newValue() { return new (std::nothrow) Stats(); }
void dispose(void* h) { delete (Stats*)h; }

double step(void* h, const NsContext* c) {
    Stats* d = (Stats*)h;
    if (!d || !c) return 0.0;
    double x = sockValue(c, 0), y = sockValue(c, 1);
    bool newPoint = true;
    if (d->rate > 0) {
        double k = std::floor((double)c->t * d->rate);
        newPoint = (k != d->key);
        if (newPoint) d->key = k;
    }
    if (newPoint) {
        d->xs.push_back(x);
        d->ys.push_back(y);
        size_t cap = (size_t)std::max(2.0, std::floor(d->window + 0.5));
        while (d->xs.size() > cap) {
            d->xs.erase(d->xs.begin());
            d->ys.erase(d->ys.begin());
        }
    }
    return compute(d, x);
}

// param 0=type 1=window 2=rate
const char* const kindChoices[] = {
    "mean", "variance", "stddev", "min", "max", "sum",
    "z of current x", "corr(x,y)", "beta y~x", "cov(x,y)", "n samples",
};
NsParam params[] = {
    {"type", 0, 10, 0, NS_PARAM_ENUM, 1, nullptr, "statistic computed in the window",
     11, kindChoices},
    {"window", 2, 600, 60, NS_PARAM_INT, 1, "samples",
     "moving-window size (in samples)", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
};
double getParam(void* h, int i) {
    Stats* d = (Stats*)h;
    if (!d) return 0;
    if (i == 0) return d->kind;
    if (i == 1) return d->window;
    return d->rate;
}
int setParam(void* h, int i, double v) {
    Stats* d = (Stats*)h;
    if (!d) return 0;
    if (i == 0) { d->kind = std::floor(v + 0.5); return 1; }
    if (i == 1) { // changes the window keeping the most recent samples
        d->window = std::floor(v + 0.5);
        size_t cap = (size_t)std::max(2.0, d->window);
        if (d->xs.size() > cap) {
            d->xs.erase(d->xs.begin(), d->xs.end() - (long)cap);
            d->ys.erase(d->ys.begin(), d->ys.end() - (long)cap);
        }
        return 1;
    }
    d->rate = v;
    return 1;
}

void list(std::string& s, const char* name, const std::vector<double>& v) {
    char b[48];
    std::snprintf(b, sizeof b, " %s=", name);
    s += b;
    for (size_t k = 0; k < v.size(); ++k) {
        std::snprintf(b, sizeof b, k ? ",%.9g" : "%.9g", v[k]);
        s += b;
    }
}

char* save(void* h) {
    Stats* d = (Stats*)h;
    if (!d) return nullptr;
    char b[128];
    std::snprintf(b, sizeof b, "type=%.10g window=%.10g rate=%.10g key=%.10g",
                  d->kind, d->window, d->rate, d->key);
    std::string s = b;
    list(s, "xs", d->xs);
    list(s, "ys", d->ys);
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

void readList(const char*& p, std::vector<double>& v) {
    while (*p) {
        char* end = nullptr;
        double x = std::strtod(p, &end);
        if (end == p) break;
        v.push_back(x);
        p = end;
        if (*p != ',') break;
        ++p;
    }
}

int load(void* h, const char* blob) {
    Stats* d = (Stats*)h;
    if (!d || !blob) return 0;
    double kind = d->kind, window = d->window, rate = d->rate, key = d->key;
    std::vector<double> xs, ys;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string k(p, e - p);
        p = e + 1;
        if (k == "xs") { readList(p, xs); continue; }
        if (k == "ys") { readList(p, ys); continue; }
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        if (k == "type") kind = v;
        else if (k == "window") window = v;
        else if (k == "rate") rate = v;
        else if (k == "key") key = v;
        p = end;
    }
    d->kind = kind;
    d->window = window < 2 ? 2 : (window > 600 ? 600 : window);
    d->rate = rate;
    d->key = key;
    size_t cap = (size_t)std::floor(d->window + 0.5);
    if (xs.size() > cap) xs.erase(xs.begin(), xs.end() - (long)cap);
    if (ys.size() > cap) ys.erase(ys.begin(), ys.end() - (long)cap);
    d->xs.swap(xs);
    d->ys.swap(ys);
    return 1;
}

NsNodeType types[] = {
    {"stats", "Statistics", {150, 190, 240}, newValue, dispose, step, save, load,
     3, params, getParam, setParam, nullptr,
     0, nullptr, nullptr, nullptr,
     false, nullptr, nullptr, nullptr,
     2, 0, 0, nullptr},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("stats", "1.0", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
