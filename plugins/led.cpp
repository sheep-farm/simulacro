// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: LED with configurable RGB color and custom drawing (no default box).
//
//   led / "LED"
//     - input 0: 0 = off (dim), != 0 = on (bright);
//     - RGB: parameters r, g, b (0…255);
//     - output 0: mirrors the 0/1 state (useful for chaining);
//     - the body is drawn by the plugin (dome, base, glow and label).
// Build: make plugins | Usage: menu Add -> Display -> LED
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

struct Led {
    double r = 255, g = 50, b = 40;
    bool on = false;
};

void* create() { return new (std::nothrow) Led(); }
void destroy(void* h) { delete (Led*)h; }

double sock0(const NsContext* c) {
    if (!c || !c->in || !c->sockOff || c->nsock < 1) return 0.0;
    double v = 0.0;
    for (int k = c->sockOff[0]; k < c->sockOff[1]; ++k) v += c->in[k];
    return v;
}

double tick(void* h, const NsContext* c) {
    Led* d = (Led*)h;
    if (!d || !c) return 0.0;
    d->on = sock0(c) != 0.0;
    return d->on ? 1.0 : 0.0;
}

int channel(double v) {
    int i = (int)std::lround(v);
    return std::max(0, std::min(255, i));
}
unsigned color(const Led* d, double scale, int alpha) {
    const int rr = channel(d->r * scale);
    const int gg = channel(d->g * scale);
    const int bb = channel(d->b * scale);
    return ((unsigned)rr << 24) | ((unsigned)gg << 16) | ((unsigned)bb << 8) |
           (unsigned)(alpha & 0xFF);
}

// NsDraw only has rectangles/lines/text: circles are filled row by row.
void circle(const NsDraw* g, const Led* d, int cx, int cy, int radius, double scale,
             int alpha) {
    if (!g || !g->rect || radius < 1) return;
    const unsigned c = color(d, scale, alpha);
    for (int y = -radius; y <= radius; ++y) {
        const int w = (int)std::sqrt((double)(radius * radius - y * y));
        if (w < 0) continue;
        g->rect(g->ctx, cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

void borderCircle(const NsDraw* g, int cx, int cy, int radius, unsigned c) {
    if (!g || !g->rect || radius < 1) return;
    for (int y = -radius; y <= radius; ++y) {
        const int w = (int)std::sqrt((double)(radius * radius - y * y));
        if (w < 0) continue;
        g->rect(g->ctx, cx - w, cy + y, 2, 1, c);
        g->rect(g->ctx, cx + w - 1, cy + y, 2, 1, c);
    }
}

void drawing(void* h, const NsDraw* g, const NsRect* area) {
    if (!h || !g || !g->rect || !area || area->w < 20 || area->h < 20) return;
    Led* d = (Led*)h;
    const int cx = area->x + area->w / 2;
    const int cy = area->y + area->h / 2 - 4;
    const int radius = std::max(12, std::min(area->w, area->h) / 3);
    const int rGlow = radius + (d->on ? 9 : 2);

    // input/output wire
    if (g->line) {
        const unsigned wire = 0x707078FF;
        g->line(g->ctx, area->x, area->y + area->h / 2, cx - radius - 2, cy, wire);
        g->line(g->ctx, cx + radius + 2, cy, area->x + area->w, area->y + area->h / 2, wire);
    }
    // halo (stronger when on)
    if (d->on)
        circle(g, d, cx, cy, rGlow, 1.0, 55);
    else
        circle(g, d, cx, cy, rGlow, 0.55, 18);
    // base/body: dark edge + dome
    borderCircle(g, cx, cy, radius + 2, 0x14141AFF);
    circle(g, d, cx, cy, radius, d->on ? 1.0 : 0.27, 255);
    // glass reflection
    const int hr = std::max(2, radius / 4);
    const int hx = cx - radius / 3;
    const int hy = cy - radius / 3;
    Led white;
    white.r = white.g = white.b = 255;
    circle(g, &white, hx, hy, hr, 1.0, d->on ? 115 : 38);
    // opaque foot/base
    if (g->rect)
        g->rect(g->ctx, cx - radius - 2, cy + radius + 1, 2 * radius + 5, 4, 0x17171FFF);
    // node label at the bottom
    if (g->text) {
        const char* t = g->label && *g->label ? g->label : "LED";
        int tw = g->textW ? g->textW(g->ctx, t) : 0;
        g->text(g->ctx, area->x + (area->w - tw) / 2, area->y + area->h - 15, t,
                d->on ? 0xE8E8F0FF : 0x9A9AA4FF);
    }
}

// Accepts #RGB, RGB, #RRGGBB or RRGGBB (with or without 0x, upper/lower case).
bool parseHex(const char* s, int& r, int& g, int& b) {
    if (!s) return false;
    std::string t;
    for (const char* p = s; *p; ++p)
        if (!std::isspace((unsigned char)*p)) t += (char)std::tolower((unsigned char)*p);
    if (t.rfind("0x", 0) == 0) t.erase(0, 2);
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() == 3) {
        std::string o;
        for (char c : t) { o += c; o += c; }
        t = o;
    }
    if (t.size() != 6) return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int v[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const int a = hex(t[(size_t)i * 2]), c = hex(t[(size_t)i * 2 + 1]);
        if (a < 0 || c < 0) return false;
        v[i] = a * 16 + c;
    }
    r = v[0]; g = v[1]; b = v[2];
    return true;
}

const char* textNames[] = {"hex"};
char* getText(void* h, int i) {
    Led* d = (Led*)h;
    if (!d || i != 0) return nullptr;
    char b[8];
    std::snprintf(b, sizeof b, "#%02X%02X%02X", channel(d->r), channel(d->g), channel(d->b));
    return dupStr(b);
}
int setText(void* h, int i, const char* s) {
    Led* d = (Led*)h;
    if (!d || i != 0) return 0;
    int r = 0, g = 0, b = 0;
    if (!parseHex(s, r, g, b)) return 0;
    d->r = r; d->g = g; d->b = b;
    return 1;
}

char* save(void* h) {
    Led* d = (Led*)h;
    if (!d) return nullptr;
    char b[128];
    std::snprintf(b, sizeof b, "r=%.17g g=%.17g b=%.17g on=%d", d->r, d->g, d->b,
                  d->on ? 1 : 0);
    return dupStr(b);
}

int load(void* h, const char* blob) {
    Led* d = (Led*)h;
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
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        if (name == "r") d->r = v;
        else if (name == "g") d->g = v;
        else if (name == "b") d->b = v;
        else if (name == "on") d->on = v != 0.0;
        else break;
        some = true;
    }
    if (!some) return 0;
    d->r = channel(d->r);
    d->g = channel(d->g);
    d->b = channel(d->b);
    return 1;
}

NsParam params[] = {
    {"r", 0, 255, 255, NS_PARAM_INT, 1, nullptr, "componente vermelha", 0, nullptr},
    {"g", 0, 255, 50, NS_PARAM_INT, 1, nullptr, "componente verde", 0, nullptr},
    {"b", 0, 255, 40, NS_PARAM_INT, 1, nullptr, "componente azul", 0, nullptr},
};

double getParam(void* h, int i) {
    Led* d = (Led*)h;
    if (!d || i < 0 || i > 2) return 0;
    return i == 0 ? d->r : (i == 1 ? d->g : d->b);
}

int setParam(void* h, int i, double v) {
    Led* d = (Led*)h;
    if (!d || i < 0 || i > 2) return 0;
    const int c = channel(v);
    if (i == 0) d->r = c;
    else if (i == 1) d->g = c;
    else d->b = c;
    return 1;
}

char* labelIn(void*, int p) { return p == 0 ? dupStr("0/1") : nullptr; }
char* labelOut(void*, int p) { return p == 0 ? dupStr("state") : nullptr; }

NsNodeType kind() {
    NsNodeType t{};
    t.id = "led";
    t.label = "LED";
    t.color[0] = 250; t.color[1] = 90; t.color[2] = 90;
    t.create = create;
    t.destroy = destroy;
    t.step = tick;
    t.save = save;
    t.load = load;
    t.num_params = 3;
    t.params = params;
    t.getParam = getParam;
    t.setParam = setParam;
    t.def_in = 1;
    t.num_text = 1;
    t.text_names = textNames;
    t.getText = getText;
    t.setText = setText;
    t.pref_w = 124;
    t.pref_h = 108;
    t.draw = drawing;
    t.shape = NS_SHAPE_CUSTOM;
    t.portLabel = labelOut;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_STATE | NS_CAP_PARAMS | NS_CAP_TEXTS |
                     NS_CAP_CUSTOM_DRAW | NS_CAP_INPUT_LABELS;
    return t;
}
NsNodeType types[] = {kind()};

// Declarative in-node UI (additive ABI 14 extension): three RGB sliders bound
// to params 0..2 plus static captions. Host-drawn; the plugin keeps only data.
static const NsUiItem kItensL[] = {
    {NS_UI_LABEL, "RGB", -1, 0, 0, 0, 0},
    {NS_UI_INT, "", 0, 0, 0, 0, 0},
    {NS_UI_INT, "", 1, 0, 0, 0, 0},
    {NS_UI_INT, "", 2, 0, 0, 0, 0},
};
static const NsUiSpec kSpecsL[] = {{"led", 4, kItensL}};
NS_PLUGIN_UI(kSpecsL, 1);

NsPlugin plugin = NS_PLUGIN_BUILTIN("led", "1.1", 1, types, "Display");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
