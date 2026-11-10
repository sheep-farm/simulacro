// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: linear algebra over lists/matrices arriving on the wires.
//
//   matmul     product A·B (2 inputs)
//   transpose  transpose of A
//   inv        inverse of A (n×n) via Gauss-Jordan with partial pivoting
//   solve      solves A·x = b (A n×n; b vector or n×1)
//   det        determinant of A (numeric output)
//
// Input values come from NsContext::xval (list = 1×N; matrix = rows×cols).
// Types that publish a matrix use outValue/NS_VAL_MAT and leave the first
// element as numeric fallback; `det` returns a number on port 0. The
// size is capped at 64×64 to keep the step predictable; missing inputs,
// non-finite ones or incompatible dimensions return 0 (without publishing a matrix).
//
// There is no state between steps: output is always a function of current inputs.
//
// Build: make plugins | Usage: Add menu -> Math -> Algebra
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

const int MAXDIM = 64;
const double EPS = 1e-12;

struct Mat {
    int rows = 0, cols = 0;
    std::vector<double> v;
};

struct Algebra {
    Mat out;
    double scalar = 0.0;
    bool ok = false;
};

void* create() { return new (std::nothrow) Algebra(); }
void destroy(void* h) { delete (Algebra*)h; }

bool leMat(const NsContext* c, int sock, Mat& m) {
    m = Mat{};
    if (!c || !c->xval || sock < 0 || sock >= c->nsock) return false;
    const NsValue& v = c->xval[sock];
    if (!v.data || v.rows <= 0 || v.cols <= 0) return false;
    if (v.rows > MAXDIM || v.cols > MAXDIM) return false;
    const long long n = (long long)v.rows * (long long)v.cols;
    if (n <= 0 || n > (long long)MAXDIM * MAXDIM) return false;
    for (long long i = 0; i < n; ++i)
        if (!std::isfinite(v.data[i])) return false;
    m.rows = v.rows;
    m.cols = v.cols;
    m.v.assign(v.data, v.data + n);
    return true;
}

void failure(Algebra* d) {
    d->out = Mat{};
    d->scalar = 0.0;
    d->ok = false;
}

template <int M>
void compute(Algebra* d, const NsContext* c) {
    if (!d) return;
    failure(d);
    Mat A, B;
    if (M == 0) { // product
        if (!leMat(c, 0, A) || !leMat(c, 1, B)) return;
        if (A.cols != B.rows) return;
        d->out.rows = A.rows;
        d->out.cols = B.cols;
        d->out.v.assign((size_t)A.rows * B.cols, 0.0);
        for (int i = 0; i < A.rows; ++i)
            for (int k = 0; k < A.cols; ++k) {
                const double a = A.v[(size_t)i * A.cols + k];
                if (a == 0.0) continue;
                for (int j = 0; j < B.cols; ++j)
                    d->out.v[(size_t)i * B.cols + j] += a * B.v[(size_t)k * B.cols + j];
            }
        d->ok = true;
    } else if (M == 1) { // transpose
        if (!leMat(c, 0, A)) return;
        d->out.rows = A.cols;
        d->out.cols = A.rows;
        d->out.v.assign((size_t)d->out.rows * d->out.cols, 0.0);
        for (int i = 0; i < A.rows; ++i)
            for (int j = 0; j < A.cols; ++j)
                d->out.v[(size_t)j * d->out.cols + i] = A.v[(size_t)i * A.cols + j];
        d->ok = true;
    } else if (M == 2) { // inverse
        if (!leMat(c, 0, A) || A.rows != A.cols) return;
        const int n = A.rows;
        std::vector<double> a((size_t)n * 2 * n, 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) a[(size_t)i * 2 * n + j] = A.v[(size_t)i * n + j];
            a[(size_t)i * 2 * n + n + i] = 1.0;
        }
        for (int col = 0; col < n; ++col) {
            int piv = col;
            for (int r = col + 1; r < n; ++r)
                if (std::fabs(a[(size_t)r * 2 * n + col]) > std::fabs(a[(size_t)piv * 2 * n + col]))
                    piv = r;
            if (std::fabs(a[(size_t)piv * 2 * n + col]) < EPS) return;
            if (piv != col)
                for (int j = 0; j < 2 * n; ++j)
                    std::swap(a[(size_t)piv * 2 * n + j], a[(size_t)col * 2 * n + j]);
            const double div = a[(size_t)col * 2 * n + col];
            for (int j = 0; j < 2 * n; ++j) a[(size_t)col * 2 * n + j] /= div;
            for (int r = 0; r < n; ++r) {
                if (r == col) continue;
                const double f = a[(size_t)r * 2 * n + col];
                if (f == 0.0) continue;
                for (int j = 0; j < 2 * n; ++j)
                    a[(size_t)r * 2 * n + j] -= f * a[(size_t)col * 2 * n + j];
            }
        }
        d->out.rows = n;
        d->out.cols = n;
        d->out.v.assign((size_t)n * n, 0.0);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) d->out.v[(size_t)i * n + j] = a[(size_t)i * 2 * n + n + j];
        d->ok = true;
    } else if (M == 3) { // solve A x = b
        if (!leMat(c, 0, A) || !leMat(c, 1, B)) return;
        if (A.rows != A.cols) return;
        const int n = A.rows;
        int nb = 0;
        if (B.rows == 1) nb = B.cols;
        else if (B.cols == 1) nb = B.rows;
        else return;
        if (nb != n) return;
        std::vector<double> a((size_t)n * (n + 1), 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) a[(size_t)i * (n + 1) + j] = A.v[(size_t)i * n + j];
            a[(size_t)i * (n + 1) + n] = B.rows == 1 ? B.v[(size_t)i] : B.v[(size_t)i * B.cols];
        }
        for (int col = 0; col < n; ++col) {
            int piv = col;
            for (int r = col + 1; r < n; ++r)
                if (std::fabs(a[(size_t)r * (n + 1) + col]) > std::fabs(a[(size_t)piv * (n + 1) + col]))
                    piv = r;
            if (std::fabs(a[(size_t)piv * (n + 1) + col]) < EPS) return;
            if (piv != col)
                for (int j = col; j <= n; ++j)
                    std::swap(a[(size_t)piv * (n + 1) + j], a[(size_t)col * (n + 1) + j]);
            const double div = a[(size_t)col * (n + 1) + col];
            for (int j = col; j <= n; ++j) a[(size_t)col * (n + 1) + j] /= div;
            for (int r = 0; r < n; ++r) {
                if (r == col) continue;
                const double f = a[(size_t)r * (n + 1) + col];
                if (f == 0.0) continue;
                for (int j = col; j <= n; ++j)
                    a[(size_t)r * (n + 1) + j] -= f * a[(size_t)col * (n + 1) + j];
            }
        }
        d->out.rows = n;
        d->out.cols = 1;
        d->out.v.resize((size_t)n);
        for (int i = 0; i < n; ++i) d->out.v[(size_t)i] = a[(size_t)i * (n + 1) + n];
        d->ok = true;
    } else { // determinant
        if (!leMat(c, 0, A) || A.rows != A.cols) return;
        const int n = A.rows;
        std::vector<double> a = A.v;
        double det = 1.0;
        int signal = 1;
        for (int col = 0; col < n; ++col) {
            int piv = col;
            for (int r = col + 1; r < n; ++r)
                if (std::fabs(a[(size_t)r * n + col]) > std::fabs(a[(size_t)piv * n + col]))
                    piv = r;
            if (std::fabs(a[(size_t)piv * n + col]) < EPS) {
                d->scalar = 0.0;
                d->ok = true;
                return;
            }
            if (piv != col) {
                for (int j = 0; j < n; ++j)
                    std::swap(a[(size_t)piv * n + j], a[(size_t)col * n + j]);
                signal = -signal;
            }
            const double p = a[(size_t)col * n + col];
            det *= p;
            for (int r = col + 1; r < n; ++r) {
                const double f = a[(size_t)r * n + col] / p;
                for (int j = col + 1; j < n; ++j)
                    a[(size_t)r * n + j] -= f * a[(size_t)col * n + j];
            }
        }
        d->scalar = (double)signal * det;
        d->ok = true;
    }
}

template <int M>
double tick(void* h, const NsContext* c) {
    Algebra* d = (Algebra*)h;
    if (!d || !c) return 0.0;
    compute<M>(d, c);
    if (!d->ok) return 0.0;
    if (M == 4) return d->scalar;
    return d->out.v.empty() ? 0.0 : d->out.v[0];
}

int numOut(void*) { return 1; }

template <int M>
int outValue(void* h, int port, NsValue* v) {
    Algebra* d = (Algebra*)h;
    if (!d || !v || port != 0 || !d->ok || d->out.v.empty()) return 0;
    v->kind = NS_VAL_MAT;
    v->num = d->out.v[0];
    v->txt = nullptr;
    v->data = d->out.v.data();
    v->rows = d->out.rows;
    v->cols = d->out.cols;
    return 1;
}

char* labelMat(void*, int p) {
    if (p != 0) return nullptr;
    return dupStr("matriz");
}

template <int M>
char* labelIn(void*, int p) {
    if (M == 0 || M == 3) {
        if (p == 0) return dupStr("A");
        if (p == 1) return dupStr("B");
        return nullptr;
    }
    return p == 0 ? dupStr("A") : nullptr;
}

NsNodeType kind(const char* id, const char* label, float r, float g, float b,
                double (*step)(void*, const NsContext*),
                int (*value)(void*, int, NsValue*), int defIn, bool matrix) {
    NsNodeType t{};
    t.id = id;
    t.label = label;
    t.color[0] = r;
    t.color[1] = g;
    t.color[2] = b;
    t.create = create;
    t.destroy = destroy;
    t.step = step;
    t.def_in = defIn;
    t.portLabelIn = labelIn<0>; // replaced below by the concrete type
    t.self_out = matrix ? 1 : 0;
    t.numOut = matrix ? numOut : nullptr;
    t.outValue = value;
    t.portLabel = matrix ? labelMat : nullptr;
    t.capabilities = NS_CAP_INPUT_LABELS | (matrix ? (NS_CAP_VALUE_OUT | NS_CAP_DYNAMIC_OUT) : 0);
    return t;
}

NsNodeType types[] = {
    kind("matmul", "Matrix product", 120, 170, 240, tick<0>, outValue<0>, 2, true),
    kind("transpose", "Transpose", 140, 180, 235, tick<1>, outValue<1>, 1, true),
    kind("inv", "Inverse", 120, 195, 225, tick<2>, outValue<2>, 1, true),
    kind("solve", "Solve A·x=b", 130, 200, 245, tick<3>, outValue<3>, 2, true),
    kind("det", "Determinant", 160, 180, 220, tick<4>, nullptr, 1, false),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("matrix", "1.0", 5, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
