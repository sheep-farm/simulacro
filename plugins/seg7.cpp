// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: 7-segment display with 7 ACTIVATION PINS (a..g).
//
// Each input of the node lights one segment of the display (true ⇔ socket value
// != 0): socket 0 = a (top), 1 = b (upper right), 2 = c (lower right),
// 3 = d (bottom), 4 = e (lower left), 5 = f (upper left), 6 = g (middle).
// The display is drawn in the node itself (host drawing hook, no SDL in the
// plugin) with the pin names beside the lines.
//
// Used to build counters/decoders with the logic gates: connect the
// gate outputs (AND/OR/NOT/…) directly to pins a..g.
//
//   params  : cor (0..5, def 0) — green, amber, red, blue, violet, white
//   output  : port 0 = digit shown (0..9) or −1 if the pattern is not a digit
//
// Build: make plugins | Usage: Add menu -> Display -> 7-seg display
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

struct Seg7 {
    int color = 0;
    int mask = 0;   // bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
    int digit = -1; // recognized digit (0..9) or -1
};

void* create() { return new (std::nothrow) Seg7(); }
void destroy(void* h) { delete (Seg7*)h; }

// mask of each digit (same table as the clock plugin)
const unsigned char SEG[10] = {
    0x3F, // 0 abcdef
    0x06, // 1 bc
    0x5B, // 2 abdeg
    0x4F, // 3 abcdg
    0x66, // 4 bcfg
    0x6D, // 5 acdfg
    0x7D, // 6 acdefg
    0x07, // 7 abc
    0x7F, // 8
    0x6F, // 9 abcdfg
};

int digitOf(int mask) {
    for (int i = 0; i < 10; ++i)
        if (SEG[i] == mask) return i;
    return -1;
}

int readMask(void* h, const NsContext* c) {
    Seg7* d = (Seg7*)h;
    int m = 0;
    for (int s = 0; s < 7; ++s)
        if (sockSum(c, s) != 0.0) m |= 1 << s;
    d->mask = m;
    d->digit = digitOf(m);
    return m;
}

double step(void* h, const NsContext* c) {
    if (!h) return 0.0;
    readMask(h, c);
    return (double)((Seg7*)h)->digit;
}

// ---- parameters/persistence ----
double getParam(void* h, int i) {
    Seg7* d = (Seg7*)h;
    return (d && i == 0) ? (double)d->color : 0.0;
}
int setParam(void* h, int i, double v) {
    Seg7* d = (Seg7*)h;
    if (!d || i != 0) return 0;
    int k = (int)(v + 0.5);
    if (k < 0) k = 0;
    if (k > 5) k = 5;
    d->color = k;
    return 1;
}
char* save(void* h) {
    Seg7* d = (Seg7*)h;
    if (!d) return nullptr;
    char b[32];
    std::snprintf(b, sizeof b, "%d", d->color);
    return dupStr(b);
}
int load(void* h, const char* s) {
    Seg7* d = (Seg7*)h;
    if (!d || !s) return 0;
    int k = 0;
    if (std::sscanf(s, "%d", &k) != 1) return 0;
    if (k < 0) k = 0;
    if (k > 5) k = 5;
    d->color = k;
    return 1;
}

char* portLabel(void* h, int p) {
    (void)h;
    return p == 0 ? dupStr("digit") : nullptr;
}
char* portLabelIn(void* h, int p) {
    (void)h;
    static const char* names[] = {"a", "b", "c", "d", "e", "f", "g"};
    return (p >= 0 && p < 7) ? dupStr(names[p]) : nullptr;
}

// ---- drawing: seven-segment display ----
unsigned palCol(int i) {
    static const unsigned P[6] = {
        0x6CF3C0FF, // phosphor green
        0xFFC066FF, // amber
        0xFF6A6AFF, // red
        0x74C0FFFF, // blue
        0xC79CFFFF, // violet
        0xE8F0F0FF, // white
    };
    return P[(i < 0 || i > 5) ? 0 : i];
}
unsigned dim(unsigned c, double k) {
    unsigned r = (unsigned)(((c >> 24) & 0xFF) * k);
    unsigned g = (unsigned)(((c >> 16) & 0xFF) * k);
    unsigned b = (unsigned)(((c >> 8) & 0xFF) * k);
    return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

void segRect(const NsDraw* g, int x, int y, int w, int h, int t, int hh, int idx, unsigned col) {
    switch (idx) {
        case 0: g->rect(g->ctx, x + t, y, w - 2 * t, t, col); break;            // a
        case 1: g->rect(g->ctx, x + w - t, y + t, t, hh, col); break;           // b
        case 2: g->rect(g->ctx, x + w - t, y + 2 * t + hh, t, hh, col); break;  // c
        case 3: g->rect(g->ctx, x + t, y + h - t, w - 2 * t, t, col); break;    // d
        case 4: g->rect(g->ctx, x, y + 2 * t + hh, t, hh, col); break;          // e
        case 5: g->rect(g->ctx, x, y + t, t, hh, col); break;                   // f
        case 6: g->rect(g->ctx, x + t, y + t + hh, w - 2 * t, t, col); break;   // g
    }
}

void drawDigit(const NsDraw* g, int x, int y, int w, int h, int t, int mask, unsigned on, unsigned off) {
    int hh = (h - 3 * t) / 2;
    if (hh < 1) hh = 1;
    for (int s = 0; s < 7; ++s) segRect(g, x, y, w, h, t, hh, s, (mask & (1 << s)) ? on : off);
}

void draw(void* h, const NsDraw* g, const NsRect* a) {
    if (!h || !g || !g->rect || !a) return;
    Seg7* d = (Seg7*)h;
    unsigned on = palCol(d->color), off = dim(on, 0.20);
    g->rect(g->ctx, a->x, a->y, a->w, a->h, 0x0A120DFF); // display background
    // pin names next to each input line (a..g)
    static const char* N[7] = {"a", "b", "c", "d", "e", "f", "g"};
    float rh = (float)a->h / 8.0f;
    for (int k = 0; k < 7; ++k) {
        int y = a->y + (int)(rh * (k + 1)) - 7;
        int tw = g->textW ? g->textW(g->ctx, N[k]) : 8;
        if (g->text) g->text(g->ctx, a->x + 4 + (12 - tw) / 2, y, N[k], (d->mask & (1 << k)) ? on : off);
    }
    // digit (occupies the rest of the body)
    int w0 = a->w - 6 - 24, h0 = a->h - 12;
    if (w0 < 12 || h0 < 12) return;
    int dh = h0, dw = (int)(dh * 0.6f);
    if (dw > w0) { dw = w0; dh = (int)(dw / 0.6f); }
    int t = dh / 9;
    if (t < 2) t = 2;
    if (dw > 4 && t > dw / 2) t = dw / 2;
    int x = a->x + 24 + (w0 - dw) / 2, y = a->y + (a->h - dh) / 2;
    drawDigit(g, x, y, dw, dh, t, d->mask, on, off);
}

const char* const choicesColor[] = {"green", "amber", "red", "blue", "violet", "white"};
NsParam params[] = {
    {"color", 0, 5, 0, NS_PARAM_ENUM, 1, nullptr,
     "color of lit segments", 6, choicesColor},
};

NsNodeType types[] = {
    {"seg7", "7-seg display", {255, 200, 120}, create, destroy, step, save, load,
     1, params, getParam, setParam, nullptr,
     0, nullptr, nullptr, nullptr,
     false, nullptr, nullptr, portLabel,
     7, 168, 148, draw, NS_SHAPE_BOX, nullptr, portLabelIn},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("seg7", "1.0", 1, types, "Display");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
