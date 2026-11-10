// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: window collector — keeps the last N samples of each socket and
// publishes a MATRIX (N × sockets) on the wire. It is the bridge between the
// stream (one value per step) and the batch nodes (retrospective filters,
// full-sample regression).
//
// Inputs: 1 socket per series (edit 'entradas'; up to 16). The value of each
// socket is the sum of the wires (as in 'in').
// Parameters:
//   janela  samples kept (2…2000; default 240)
//   taxa    samples per second (0 = one per step)
// Output (1 port): matrix with rows = samples and cols = sockets, row by row
//   (oldest first; column k = socket k). The port's numeric fallback is
//   the number of rows collected so far (shows in the trace/while the node is scalar).
//
// The matrix is republished every step while the window changes (the host
// copies), so large windows cost O(rows × columns) per step: use `csv` in
// batch mode for long series.
// Build: make plugins | Usage: menu Add -> Data -> Window (matrix)
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

const int JAN_MIN = 2, JAN_MAX = 2000;

struct Collector {
    double window = 240, rate = 0, key = -1e308;
    int cols = 0;                  // sockets of the last sample
    std::vector<double> buf;       // flattened rows: row * cols + socket
    int rows = 0;
    double last = 0;
};

int nsockOf(const NsContext* c) {
    if (!c || !c->sockOff) return 0;
    return c->nsock > 0 ? c->nsock : 0;
}

int windowOf(const Collector* d) {
    int jan = (int)std::lround(d->window);
    if (jan < JAN_MIN) jan = JAN_MIN;
    if (jan > JAN_MAX) jan = JAN_MAX;
    return jan;
}

void sample(Collector* d, const NsContext* c) {
    int ns = nsockOf(c);
    if (ns < 1) ns = 1;
    if (ns != d->cols) { // number of sockets changed: restart (incompatible layout)
        d->cols = ns;
        d->buf.clear();
        d->rows = 0;
    }
    for (int s = 0; s < ns; ++s) d->buf.push_back(sockValue(c, s));
    ++d->rows;
    int jan = windowOf(d);
    while (d->rows > jan) { // discard the oldest row
        d->buf.erase(d->buf.begin(), d->buf.begin() + d->cols);
        --d->rows;
    }
    d->last = sockValue(c, 0);
}

void update(Collector* d, const NsContext* c) {
    if (!d || !c) return;
    bool newItem = true;
    if (d->rate > 0) {
        double k = std::floor((double)c->t * d->rate);
        newItem = (k != d->key);
        if (newItem) d->key = k;
    }
    if (newItem) sample(d, c);
    if (getenv("NS_DEBUG_COL")) std::fprintf(stderr, "col: rows=%d cols=%d jan=%g rate=%g t=%.3f\n", d->rows, d->cols, d->window, d->rate, (double)c->t);
}

void* newValue() { return new (std::nothrow) Collector(); }
void dispose(void* h) { delete (Collector*)h; }

double step(void* h, const NsContext* c) {
    Collector* d = (Collector*)h;
    update(d, c);
    return d ? (double)d->rows : 0.0;
}
int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Collector* d = (Collector*)h;
    if (!d) return 0;
    update(d, c);
    if (nout > 0) out[0] = d->rows;
    for (int p = 1; p < nout; ++p) out[p] = 0; // the matrix goes via outValue
    return 1;
}
int numOut(void*) { return 1; }

char* portLabel(void* h, int p) {
    Collector* d = (Collector*)h;
    if (!d || p != 0) return nullptr;
    char b[48];
    std::snprintf(b, sizeof b, "%d×%d", d->rows, d->cols);
    char* r = (char*)std::malloc(std::strlen(b) + 1);
    if (r) std::memcpy(r, b, std::strlen(b) + 1);
    return r;
}

// publish the matrix (N × cols); 0 = no data yet
int outValue(void* h, int port, NsValue* v) {
    Collector* d = (Collector*)h;
    if (!d || !v || port != 0 || d->rows <= 0 || d->cols <= 0) return 0;
    v->kind = NS_VAL_MAT;
    v->num = (double)d->rows;
    v->data = d->buf.data();
    v->rows = d->rows;
    v->cols = d->cols;
    return 1;
}

// param 0=janela 1=taxa
NsParam params[] = {
    {"window", JAN_MIN, JAN_MAX, 240, NS_PARAM_INT, 1, "samples",
     "window size published as a matrix", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
};
double getParam(void* h, int i) {
    Collector* d = (Collector*)h;
    if (!d) return 0;
    return i == 0 ? d->window : d->rate;
}
int setParam(void* h, int i, double val) {
    Collector* d = (Collector*)h;
    if (!d) return 0;
    if (i == 0) {
        if (val < JAN_MIN || val > JAN_MAX) return 0;
        d->window = val;
        int jan = windowOf(d);
        while (d->rows > jan) { // shrank: trim the beginning
            d->buf.erase(d->buf.begin(), d->buf.begin() + d->cols);
            --d->rows;
        }
        return 1;
    }
    d->rate = val < 0 ? 0 : val;
    return 1;
}

char* save(void* h) {
    Collector* d = (Collector*)h;
    if (!d) return nullptr;
    char b[128];
    std::snprintf(b, sizeof b, "window=%.10g rate=%.10g key=%.10g cols=%d rows=%d buf=",
                  d->window, d->rate, d->key, d->cols, d->rows);
    std::string s = b;
    char v[40];
    for (size_t i = 0; i < d->buf.size(); ++i) {
        std::snprintf(v, sizeof v, i ? ",%.9g" : "%.9g", d->buf[i]);
        s += v;
    }
    char* out = (char*)std::malloc(s.size() + 1);
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

int load(void* h, const char* blob) {
    Collector* d = (Collector*)h;
    if (!d || !blob) return 0;
    double window = d->window, rate = d->rate, key = -1e308;
    int cols = 0, rows = 0;
    std::vector<double> buf;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string name(p, e - p);
        p = e + 1;
        if (name == "buf") {
            while (*p && *p != ' ') {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                buf.push_back(v);
                p = end;
                while (*p == ',') ++p;
            }
            continue;
        }
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "cols") cols = (int)v;
        else if (name == "rows") rows = (int)v;
        else if (name == "window") window = v;
        else if (name == "rate") rate = v;
        else if (name == "key") key = v;
    }
    if (window < JAN_MIN) window = JAN_MIN;
    if (window > JAN_MAX) window = JAN_MAX;
    d->window = window;
    d->rate = rate < 0 ? 0 : rate;
    d->key = key;
    d->cols = cols > 0 ? cols : 0;
    d->rows = rows >= 0 ? rows : 0;
    if (d->cols <= 0 || d->rows <= 0) { d->rows = 0; d->cols = 0; d->buf.clear(); return 1; }
    size_t n = (size_t)d->rows * (size_t)d->cols;
    if (buf.size() < n) { // truncated blob: use as much as possible
        d->rows = (int)(buf.size() / (size_t)d->cols);
        n = (size_t)d->rows * (size_t)d->cols;
    }
    buf.resize(n);
    d->buf = buf;
    return 1;
}

NsNodeType types[] = {
    {"collector", "Window (matrix)", {150, 210, 245}, newValue, dispose, step, save, load,
     2, params, getParam, setParam, stepMulti,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, portLabel,
     1, 0, 0, nullptr, NS_SHAPE_BOX, outValue},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("collector", "1.1", 1, types, "Data");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
