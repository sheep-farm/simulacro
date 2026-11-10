// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: logic gates (binary circuits) — AND, OR, NOT, NAND, NOR, XOR,
// XNOR, BUF and 2:1 MUX.
//
// Each node INPUT (socket) is an operand: true ⇔ socket value != 0.
// A socket with several wires is worth their sum (host default semantics), so
// "A" and "B" go on separate sockets — use 'entradas' = 2 on binary gates.
//
// No internal state: the operation comes from the type (not the instance), so
// there is no save/load. The default number of inputs comes from
// NsNodeType.def_in (2 on binary gates, 1 on NOT/BUF, 3 on the MUX: A, B, SEL);
// still editable in the panel.
//
// Shape: the gates declare NsShape::CUSTOM and draw their own glyph
// (AND's D, OR/XOR's shield, NOT/BUF's triangle, MUX's trapezoid) with the
// 'draw' hook — the host only adds sockets and the selection frame. The plugin
// default is the box (NsShape::BOX).
//
// Build: make plugins | Usage: menu Add -> Logic -> AND / OR / XOR …
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

enum class Mode { AND, OR, NOT, NAND, NOR, XOR, XNOR, BUF, MUX };

struct Gate {}; // stateless: the gate is the type, not the instance

void* create() { return new (std::nothrow) Gate(); }
void destroy(void* h) { delete (Gate*)h; }

// value of socket s = sum of the wires of that socket (see NsContext::sockOff)
bool truth(double v) { return v != 0.0; } // binary: present ⇔ nonzero

int sockCount(const NsContext* c) { return (c && c->nsock > 0) ? c->nsock : 1; }

bool allTrue(const NsContext* c) {
    int n = sockCount(c);
    for (int s = 0; s < n; ++s) if (!truth(sockValue(c, s))) return false;
    return true;
}
bool anyTrue(const NsContext* c) {
    int n = sockCount(c);
    for (int s = 0; s < n; ++s) if (truth(sockValue(c, s))) return true;
    return false;
}
bool parity(const NsContext* c) { // odd = 1
    int n = sockCount(c), ones = 0;
    for (int s = 0; s < n; ++s) if (truth(sockValue(c, s))) ++ones;
    return (ones & 1) != 0;
}

template <Mode M>
double step(void* /*h*/, const NsContext* c) {
    if (!c) return 0.0;
    switch (M) {
        case Mode::AND:  return allTrue(c) ? 1.0 : 0.0;
        case Mode::OR:   return anyTrue(c) ? 1.0 : 0.0;
        case Mode::NOT:  return truth(sockValue(c, 0)) ? 0.0 : 1.0;
        case Mode::NAND: return allTrue(c) ? 0.0 : 1.0;
        case Mode::NOR:  return anyTrue(c) ? 0.0 : 1.0;
        case Mode::XOR:  return parity(c) ? 1.0 : 0.0;
        case Mode::XNOR: return parity(c) ? 0.0 : 1.0;
        case Mode::BUF:  return truth(sockValue(c, 0)) ? 1.0 : 0.0;
        case Mode::MUX: { // inputs: A=0, B=1, SEL=2 -> SEL ? B : A
            bool sel = truth(sockValue(c, 2));
            return (sel ? truth(sockValue(c, 1)) : truth(sockValue(c, 0))) ? 1.0 : 0.0;
        }
    }
    return 0.0;
}

// ---------- gate glyphs (NsShape::CUSTOM) ----------
// The host does not draw a box/title: here comes the classic silhouette of each
// gate (AND's D, OR's shield, NOT/BUF's triangle, MUX's trapezoid) + label.
// Everything in NsDraw primitives (no SDL), in screen pixels.
const unsigned K_FILL = 0x261E33FF; // body (very dark violet)
const unsigned K_EDGE = 0xBE96FFFF; // outline (= type color)
const unsigned K_TEXT = 0xEDE8FAFF; // label

struct Glyph { // node box in screen pixels
    const NsDraw* g = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    float X(float u) const { return x + u * (float)w; }
    float Y(float v) const { return y + v * (float)h; }
};

// outline in normalized coords 0..1 (y down); filled by scanline
struct Poly {
    float u[96], v[96];
    int n = 0;
    void add(float a, float b) { if (n < 96) { u[n] = a; v[n] = b; ++n; } }
    void quad(float u0, float v0, float cu, float cv, float u1, float v1, int seg) {
        for (int i = 0; i <= seg; ++i) {
            float t = (float)i / seg, s = 1.0f - t;
            add(s * s * u0 + 2 * s * t * cu + t * t * u1, s * s * v0 + 2 * s * t * cv + t * t * v1);
        }
    }
    void ell(float cu, float cv, float ru, float rv, float a0, float a1, int seg) {
        for (int i = 0; i <= seg; ++i) {
            float a = a0 + (a1 - a0) * (float)i / seg;
            add(cu + ru * std::cos(a), cv + rv * std::sin(a));
        }
    }
};

void fillPoly(const Glyph& G, const Poly& p, unsigned col) {
    int band = G.h > 96 ? G.h / 48 : 1; // ~48 bands (fewer rect calls)
    if (band < 1) band = 1;
    float xs[96];
    for (int j = 0; j < G.h; j += band) {
        float vc = (j + band * 0.5f) / (float)G.h;
        int m = 0;
        for (int i = 0; i < p.n; ++i) {
            int k = (i + 1) % p.n;
            float v0 = p.v[i], v1 = p.v[k];
            if ((v0 <= vc && v1 > vc) || (v1 <= vc && v0 > vc)) {
                if (m >= 96) break;
                float t = (vc - v0) / (v1 - v0);
                xs[m++] = p.u[i] + t * (p.u[k] - p.u[i]);
            }
        }
        for (int i = 1; i < m; ++i) // insertion sort
            for (int k = i; k > 0 && xs[k] < xs[k - 1]; --k) { float s = xs[k]; xs[k] = xs[k - 1]; xs[k - 1] = s; }
        for (int i = 0; i + 1 < m; i += 2) {
            int x0 = (int)(G.X(xs[i]) + 0.5f), x1 = (int)(G.X(xs[i + 1]) + 0.5f);
            if (x1 > x0) G.g->rect(G.g->ctx, x0, G.y + j, x1 - x0, band, col);
        }
    }
}
void strokePoly(const Glyph& G, const Poly& p, unsigned col) {
    for (int i = 0; i < p.n; ++i) {
        int k = (i + 1) % p.n;
        G.g->line(G.g->ctx, (int)(G.X(p.u[i]) + 0.5f), (int)(G.Y(p.v[i]) + 0.5f),
                  (int)(G.X(p.u[k]) + 0.5f), (int)(G.Y(p.v[k]) + 0.5f), col);
    }
}
void disc(const Glyph& G, float cu, float cv, float r, unsigned fill, unsigned edge) {
    float cx = G.X(cu), cy = G.Y(cv);
    for (int yy = (int)(cy - r); yy <= (int)(cy + r); ++yy) {
        float d = (float)yy + 0.5f - cy, q = r * r - d * d;
        if (q <= 0) continue;
        float dx = std::sqrt(q);
        G.g->rect(G.g->ctx, (int)(cx - dx + 0.5f), yy, (int)(2 * dx + 0.5f), 1, fill);
    }
    int px = 0, py = 0;
    for (int i = 0; i <= 16; ++i) {
        float a = 6.2831853f * (float)i / 16.0f;
        int qx = (int)(cx + r * std::cos(a) + 0.5f), qy = (int)(cy + r * std::sin(a) + 0.5f);
        if (i) G.g->line(G.g->ctx, px, py, qx, qy, edge);
        px = qx; py = qy;
    }
}

struct Geo { float l, r, t, b; };

Poly polyAnd(const Geo& q, float cap) { // left straight edge + elliptical cap
    Poly p;
    float vc = (q.t + q.b) * 0.5f, rv = (q.b - q.t) * 0.5f, flat = q.r - cap;
    p.add(q.l, q.t);
    p.add(flat, q.t);
    p.ell(flat, vc, cap, rv, -1.5707963f, 1.5707963f, 12);
    p.add(q.l, q.b);
    return p;
}
Poly polyOrBack(const Geo& q, float du) { // concave back (XOR's = du < 0)
    Poly p;
    float wu = q.r - q.l, vc = (q.t + q.b) * 0.5f;
    p.quad(q.l + du, q.b, q.l + du + wu * 0.34f, vc, q.l + du, q.t, 12);
    return p;
}
Poly polyOr(const Geo& q) { // shield: top -> tip -> base -> back
    Poly p;
    float wu = q.r - q.l, hv = q.b - q.t, vc = (q.t + q.b) * 0.5f;
    p.quad(q.l, q.t, q.l + wu * 0.66f, q.t + hv * 0.03f, q.r, vc, 14);
    p.quad(q.r, vc, q.l + wu * 0.66f, q.b - hv * 0.03f, q.l, q.b, 14);
    Poly back = polyOrBack(q, 0.0f);
    for (int i = 0; i < back.n; ++i) p.add(back.u[i], back.v[i]);
    return p;
}
Poly polyTri(const Geo& q) { // NOT / BUF
    Poly p;
    p.add(q.l, q.t);
    p.add(q.r, (q.t + q.b) * 0.5f);
    p.add(q.l, q.b);
    return p;
}
Poly polyMux(const Geo& q) { // trapezoid (inputs on the left, output on the right)
    Poly p;
    float hv = q.b - q.t, vc = (q.t + q.b) * 0.5f;
    p.add(q.l, q.t);
    p.add(q.r, vc - hv * 0.30f);
    p.add(q.r, vc + hv * 0.30f);
    p.add(q.l, q.b);
    return p;
}

void drawGate(const NsDraw* g, const NsRect* a, Mode m) {
    if (!g || !g->rect || !g->line || !a || a->w < 6 || a->h < 6) return;
    Glyph G{g, a->x, a->y, a->w, a->h};
    bool neg = (m == Mode::NAND || m == Mode::NOR || m == Mode::XNOR || m == Mode::NOT);
    bool shield = (m == Mode::OR || m == Mode::NOR || m == Mode::XOR || m == Mode::XNOR);
    bool tri = (m == Mode::NOT || m == Mode::BUF);
    float mU = (m == Mode::XOR || m == Mode::XNOR) ? 0.10f : 0.04f, mV = 0.07f;
    float bub = neg ? std::max(2.0f, G.h * 0.08f) : 0.0f; // negation bubble (px)
    Geo q{mU, 1.0f - mU, mV, 1.0f - mV};
    if (neg) q.r -= (2.0f * bub + 2.0f) / (float)G.w; // pull the tip back to fit the bubble
    Poly body;
    if (m == Mode::AND || m == Mode::NAND) body = polyAnd(q, (q.r - q.l) * 0.38f);
    else if (shield) body = polyOr(q);
    else if (m == Mode::MUX) body = polyMux(q);
    else body = polyTri(q); // NOT / BUF
    if (m == Mode::XOR || m == Mode::XNOR) { // extra arc at the input
        Poly back = polyOrBack(q, -0.05f);
        strokePoly(G, back, K_EDGE);
    }
    fillPoly(G, body, K_FILL);
    strokePoly(G, body, K_EDGE);
    if (neg) disc(G, q.r + (bub + 1.0f) / (float)G.w, 0.5f, bub, K_FILL, K_EDGE);
    // label (user-given name or the gate's) inside the shape
    if (g->text && g->label && *g->label && G.w >= 52) {
        int tw = g->textW ? g->textW(g->ctx, g->label) : 0;
        if (tw + 10 < (int)(G.w * (tri ? 0.42f : 0.56f))) {
            float cu = tri ? 0.30f : (m == Mode::AND || m == Mode::NAND ? 0.34f : 0.40f);
            g->text(g->ctx, (int)(G.X(cu) - tw * 0.5f), (int)(G.Y(0.5f) - 7), g->label, K_TEXT);
        }
    }
}

template <Mode M>
void draw(void* /*h*/, const NsDraw* g, const NsRect* a) { drawGate(g, a, M); }

char* portLabelIn(void*, int p) {
    const char* s = p == 0 ? "A" : (p == 1 ? "B" : (p == 2 ? "SEL" : nullptr));
    if (!s) return nullptr;
    char* r = (char*)std::malloc(std::strlen(s) + 1);
    if (r) std::memcpy(r, s, std::strlen(s) + 1);
    return r;
}

// fill a type without repeating the null table on every line
NsNodeType mk(const char* id, const char* label, double (*st)(void*, const NsContext*),
              int defIn, void (*dr)(void*, const NsDraw*, const NsRect*)) {
    NsNodeType t{};
    t.id = id;
    t.label = label;
    t.color[0] = 190; t.color[1] = 150; t.color[2] = 255; // violet
    t.create = create;
    t.destroy = destroy;
    t.step = st;
    t.def_in = defIn;
    t.pref_h = 64;             // height to fit the gate glyph
    t.draw = dr;
    t.shape = NS_SHAPE_CUSTOM; // the plugin draws the silhouette (the default is the box)
    t.portLabelIn = portLabelIn;
    return t;
}

NsNodeType types[] = {
    mk("and",  "AND",  step<Mode::AND>,  2, draw<Mode::AND>),
    mk("or",   "OR",   step<Mode::OR>,   2, draw<Mode::OR>),
    mk("not",  "NOT",  step<Mode::NOT>,  1, draw<Mode::NOT>),
    mk("nand", "NAND", step<Mode::NAND>, 2, draw<Mode::NAND>),
    mk("nor",  "NOR",  step<Mode::NOR>,  2, draw<Mode::NOR>),
    mk("xor",  "XOR",  step<Mode::XOR>,  2, draw<Mode::XOR>),
    mk("xnor", "XNOR", step<Mode::XNOR>, 2, draw<Mode::XNOR>),
    mk("buf",  "BUF",  step<Mode::BUF>,  1, draw<Mode::BUF>),
    mk("mux",  "MUX 2:1", step<Mode::MUX>, 3, draw<Mode::MUX>),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("logic", "1.0", 9, types, "Logic");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
