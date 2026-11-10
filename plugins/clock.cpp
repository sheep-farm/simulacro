// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: digital clock display (seven-segment).
//
// Shows the time as an LCD display drawn in the node itself — uses the host's
// drawing hook (NsDraw, no SDL in the plugin): dark background, "ghost"
// segments and lit digits, with the colon blinking.
//
// Input (optional): time in seconds on socket 0. With no wire, uses the
// simulation time (t). The display shows `tempo*escala + desloc`, modulo 24 h.
//   params  : escala (0.001…1000, def 1) · desloc (s, −86400…86400, def 0)
//   texto   : format — %H %M %S (hour/min/sec with 2 digits), %h (12 h),
//             %p (AM/PM), %s (seconds of the day), %T (=%H:%M:%S), %% literal.
// Outputs: port 0 = seconds (number) + formatted text; with more outputs,
//   ports 1..3 = hour, minute, second (numbers).
//
// Build: make plugins | Usage: menu Add -> Display -> Digital clock
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

struct Clock {
    double scale = 1.0;
    double offset = 0.0;
    char fmt[32] = "%H:%M:%S";
    char text[64] = "00:00:00";
    bool colon = true;
};

void* create() { return new (std::nothrow) Clock(); }
void destroy(void* h) { delete (Clock*)h; }

// ---- time value (seconds) -> formatted text ----
int fmtOut(const char* f, double total, char* out, int cap) {
    const double DAY = 86400.0;
    double s = std::fmod(total, DAY);
    if (s < 0) s += DAY;
    int H = (int)(s / 3600), M = ((int)(s / 60)) % 60, S = (int)s % 60;
    int h12 = H % 12; if (h12 == 0) h12 = 12;
    int o = 0;
    auto put = [&](const char* piece) {
        for (; *piece && o < cap - 1; ++piece) out[o++] = *piece;
    };
    char nb[16];
    for (const char* p = f; *p && o < cap - 1; ++p) {
        if (*p != '%') { out[o++] = *p; continue; }
        ++p;
        switch (*p) {
            case 'H': std::snprintf(nb, sizeof nb, "%02d", H); put(nb); break;
            case 'M': std::snprintf(nb, sizeof nb, "%02d", M); put(nb); break;
            case 'S': std::snprintf(nb, sizeof nb, "%02d", S); put(nb); break;
            case 'h': std::snprintf(nb, sizeof nb, "%d", h12); put(nb); break;
            case 'p': put(H < 12 ? "AM" : "PM"); break;
            case 's': std::snprintf(nb, sizeof nb, "%d", (int)s); put(nb); break;
            case 'T': std::snprintf(nb, sizeof nb, "%02d:%02d:%02d", H, M, S); put(nb); break;
            case '%': out[o++] = '%'; break;
            case '\0': --p; break;
            default: out[o++] = '%'; if (o < cap - 1) out[o++] = *p; break;
        }
    }
    out[o] = '\0';
    return o;
}

double update(void* h, const NsContext* c) { // computes time/display; returns seconds
    Clock* d = (Clock*)h;
    if (!d) return 0.0;
    double base = (c && c->nin > 0 && c->in) ? sockSum(c, 0) : (c ? c->t : 0.0);
    double total = base * d->scale + d->offset;
    fmtOut(d->fmt, total, d->text, (int)sizeof d->text);
    double frac = total - std::floor(total);
    d->colon = frac < 0.5; // blink the colon (1 Hz)
    return total;
}

double step(void* h, const NsContext* c) { return update(h, c); }

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    if (!out || nout <= 0) return 0;
    double total = update(h, c);
    const double DAY = 86400.0;
    double s = std::fmod(total, DAY);
    if (s < 0) s += DAY;
    out[0] = total;
    if (nout > 1) out[1] = (int)(s / 3600);
    if (nout > 2) out[2] = ((int)(s / 60)) % 60;
    if (nout > 3) out[3] = (int)s % 60;
    return 1;
}

char* outText(void* h, int p) {
    Clock* d = (Clock*)h;
    if (!d || p != 0) return nullptr;
    return dupStr(d->text);
}

char* portLabel(void* h, int p) {
    (void)h;
    static const char* names[4] = {"time", "h", "min", "s"};
    return (p >= 0 && p < 4) ? dupStr(names[p]) : nullptr;
}

// ---- parameters/persistence ----
double getParam(void* h, int i) {
    Clock* d = (Clock*)h;
    if (!d) return 0;
    return i == 0 ? d->scale : (i == 1 ? d->offset : 0);
}
int setParam(void* h, int i, double v) {
    Clock* d = (Clock*)h;
    if (!d) return 0;
    if (i == 0) d->scale = v;
    else if (i == 1) d->offset = v;
    else return 0;
    return 1;
}

char* save(void* h) {
    Clock* d = (Clock*)h;
    if (!d) return nullptr;
    char b[96];
    std::snprintf(b, sizeof b, "%.6g %.6g \"%s\"", d->scale, d->offset, d->fmt);
    return dupStr(b);
}
int load(void* h, const char* s) {
    Clock* d = (Clock*)h;
    if (!d || !s) return 0;
    double e = 0, o = 0;
    char fmt[32];
    if (std::sscanf(s, "%lf %lf \"%31[^\"]\"", &e, &o, fmt) != 3) return 0;
    d->scale = e;
    d->offset = o;
    std::snprintf(d->fmt, sizeof d->fmt, "%s", fmt);
    return 1;
}

char* getText(void* h, int i) {
    Clock* d = (Clock*)h;
    return (d && i == 0) ? dupStr(d->fmt) : nullptr;
}
int setText(void* h, int i, const char* s) {
    Clock* d = (Clock*)h;
    if (!d || i != 0 || !s) return 0;
    std::snprintf(d->fmt, sizeof d->fmt, "%s", s);
    return 1;
}

// ---- drawing: seven-segment display ----
const unsigned SEG_ON  = 0x6CF3C0FF; // phosphor green
const unsigned SEG_OFF = 0x1C3A2EFF; // segment off (LCD)
const unsigned LCD_BG  = 0x0A120DFF; // display background

// bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
const unsigned char SEG[10] = {
    0x3F, // 0 abcdef
    0x06, // 1 bc
    0x5B, // 2 abg e d
    0x4F, // 3 abgcd
    0x66, // 4 fgbc
    0x6D, // 5 afgcd
    0x7D, // 6 afgecd
    0x07, // 7 abc
    0x7F, // 8
    0x6F, // 9 abcdfg
};

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

void drawDigit(const NsDraw* g, int x, int y, int w, int h, int t, int mask) {
    int hh = (h - 3 * t) / 2;
    if (hh < 1) hh = 1;
    for (int s = 0; s < 7; ++s) if (!(mask & (1 << s))) segRect(g, x, y, w, h, t, hh, s, SEG_OFF);
    for (int s = 0; s < 7; ++s) if (mask & (1 << s)) segRect(g, x, y, w, h, t, hh, s, SEG_ON);
}

bool digitsOnly(const char* s) {
    for (const char* p = s; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || *p == ':' || *p == ' ')) return false;
    return *s != '\0';
}

void drawDisplay(const NsDraw* g, const NsRect& a, const char* s, bool colon) {
    g->rect(g->ctx, a.x, a.y, a.w, a.h, LCD_BG); // display background
    int W = a.w - 16, H = a.h - 10;
    if (W < 20 || H < 14 || !digitsOnly(s)) { // small/formats with letters: plain text
        int tw = g->textW ? g->textW(g->ctx, s) : 0;
        if (g->text) g->text(g->ctx, a.x + (a.w - tw) / 2, a.y + a.h / 2 - 8, s, SEG_ON);
        return;
    }
    int nd = 0, nc = 0, nch = 0;
    for (const char* p = s; *p; ++p) {
        if (*p >= '0' && *p <= '9') ++nd;
        else if (*p == ':') ++nc;
        ++nch;
    }
    if (nd == 0) return;
    double k = nd * 0.60 + nc * 0.30 + (nch - 1) * 0.12; // total width / digit height
    int Dh = (int)(W / (k > 0 ? k : 1));
    if (Dh > H) Dh = H;
    if (Dh > 120) Dh = 120;
    int dw = (int)(Dh * 0.60), cw = (int)(Dh * 0.30), gap = (int)(Dh * 0.12);
    int t = Dh / 9;
    if (t < 2) t = 2;
    if (dw > 4 && t > dw / 2) t = dw / 2;
    int total = nd * dw + nc * cw + (nch - 1) * gap;
    int x = a.x + (a.w - total) / 2;
    int y = a.y + (a.h - Dh) / 2;
    for (const char* p = s; *p; ++p) {
        if (*p >= '0' && *p <= '9') { drawDigit(g, x, y, dw, Dh, t, SEG[*p - '0']); x += dw + gap; }
        else if (*p == ':') {
            int dot = t > 1 ? t : 2;
            if (colon) {
                int cx = x + cw / 2 - dot / 2;
                g->rect(g->ctx, cx, y + Dh * 30 / 100, dot, dot, SEG_ON);
                g->rect(g->ctx, cx, y + Dh * 66 / 100, dot, dot, SEG_ON);
            }
            x += cw + gap;
        } else x += gap;
    }
}

void draw(void* h, const NsDraw* g, const NsRect* area) {
    if (!h || !g || !g->rect || !area) return;
    Clock* d = (Clock*)h;
    drawDisplay(g, *area, d->text, d->colon);
}

NsParam params[] = {
    {"scale", 0.001, 1000.0, 1.0, NS_PARAM_NUM, 0.1, nullptr,
     "clock seconds per simulation second", 0, nullptr},
    {"offset", -86400.0, 86400.0, 0.0, NS_PARAM_NUM, 1, "s",
     "deslocamento em segundos", 0, nullptr},
};
const char* textNames[] = {"format"};

NsNodeType types[] = {
    {"clock", "Clock", {120, 235, 210}, create, destroy, step, save, load,
     2, params, getParam, setParam, stepMulti,
     1, textNames, getText, setText,
     false, nullptr, outText, portLabel,
     1, 170, 62, draw},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("clock", "1.1", 1, types, "Display");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
