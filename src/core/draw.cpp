// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "draw.h"
#include "i18n.h"
#include <algorithm>
#include <cmath>

void drawCircle(SDL_Renderer* r, int cx, int cy, int rad) {
    for (int w = 0; w < rad * 2; ++w)
        for (int h = 0; h < rad * 2; ++h) {
            int dx = rad - w, dy = rad - h;
            if (dx*dx + dy*dy <= rad*rad) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
}

void drawRing(SDL_Renderer* r, int cx, int cy, int rad) {
    int x = rad, y = 0, err = 0;
    while (x >= y) {
        SDL_RenderDrawPoint(r, cx+x, cy+y); SDL_RenderDrawPoint(r, cx+y, cy+x);
        SDL_RenderDrawPoint(r, cx-y, cy+x); SDL_RenderDrawPoint(r, cx-x, cy+y);
        SDL_RenderDrawPoint(r, cx-x, cy-y); SDL_RenderDrawPoint(r, cx-y, cy-x);
        SDL_RenderDrawPoint(r, cx+y, cy-x); SDL_RenderDrawPoint(r, cx+x, cy-y);
        y++; err += 1 + 2*y; if (2*(err-x)+1 > 0) { x--; err += 1 - 2*x; }
    }
}

namespace {
Vec2 vnorm(Vec2 v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y);
    return l > 0.001f ? Vec2{v.x / l, v.y / l} : Vec2{-1, 0};
}
} // namespace

float wireOff(Vec2 p1, Vec2 p2) {
    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    return std::max(30.0f, std::sqrt(dx * dx + dy * dy) * 0.5f);
}

void drawWire(SDL_Renderer* ren, Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2, WireStyle style,
              bool arrow, int dash) {
    Vec2 u1 = vnorm(d1), u2 = vnorm(d2);
    float off = wireOff(p1, p2);
    Vec2 c2{p2.x - u2.x * off, p2.y - u2.y * off};
    Vec2 endDir = (style == WireStyle::CURVE)
                      ? vnorm(Vec2{p2.x - c2.x, p2.y - c2.y})
                      : vnorm(Vec2{p2.x - p1.x, p2.y - p1.y});
    if (dash <= 0) {
        if (style == WireStyle::CURVE) {
            const int N = 24;
            SDL_Point pts[N + 1];
            for (int i = 0; i <= N; ++i) {
                Vec2 q = wirePoint(p1, u1, p2, u2, style, (float)i / N);
                pts[i] = {(int)q.x, (int)q.y};
            }
            SDL_RenderDrawLines(ren, pts, N + 1);
        } else {
            SDL_RenderDrawLine(ren, (int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y);
        }
    } else {
        float dx = p2.x - p1.x, dy = p2.y - p1.y;
        int N = style == WireStyle::CURVE ? 48 : std::max(1, (int)(std::hypot(dx, dy) / 5.0f));
        Vec2 prev = style == WireStyle::CURVE ? wirePoint(p1, u1, p2, u2, style, 0.0f) : p1;
        for (int i = 1; i <= N; ++i) {
            float u = (float)i / N;
            Vec2 q = style == WireStyle::CURVE ? wirePoint(p1, u1, p2, u2, style, u)
                                               : Vec2{p1.x + dx * u, p1.y + dy * u};
            if (((i - 1) / dash) % 2 == 0)
                SDL_RenderDrawLine(ren, (int)prev.x, (int)prev.y, (int)q.x, (int)q.y);
            prev = q;
        }
    }
    if (arrow) {
        Vec2 px{-endDir.y, endDir.x};
        SDL_RenderDrawLine(ren, (int)p2.x, (int)p2.y,
            (int)(p2.x - endDir.x * 9 + px.x * 4.5f), (int)(p2.y - endDir.y * 9 + px.y * 4.5f));
        SDL_RenderDrawLine(ren, (int)p2.x, (int)p2.y,
            (int)(p2.x - endDir.x * 9 - px.x * 4.5f), (int)(p2.y - endDir.y * 9 - px.y * 4.5f));
    }
}

void ringColor(NodeType t, Uint8& r, Uint8& g, Uint8& b) {
    switch (t) {
        case NodeType::SCRIPT: r = 255; g = 110; b = 220; break; // pink
        case NodeType::PLUGIN: r = 200; g = 200; b = 200; break; // gray (registry may override)
        case NodeType::SCOPE:  r = 140; g = 255; b = 150; break; // green (chart sink)
        case NodeType::MUX:    r = 120; g = 215; b = 255; break; // blue (join/split)
    }
}

const char* nodeTypeName(NodeType t) {
    switch (t) {
        case NodeType::SCRIPT: return "SCRIPT";
        case NodeType::PLUGIN: return "PLUGIN";
        case NodeType::SCOPE:  return "SCOPE";
        case NodeType::MUX:    return "MUX";
    }
    return "?";
}

namespace {
Vec2 sideNormal(int side) {
    switch (side) {
        case 0: return {-1, 0};  // left
        case 2: return {1, 0};   // right
        case 1: return {0, -1};  // top
        default: return {0, 1};  // bottom
    }
}
int sideOf(int s) { return (s < 0 || s > 3) ? 0 : s; }
int vertRows(const Node& n) {
    int vi = (sideOf(n.inSide) == 0 || sideOf(n.inSide) == 2) ? (n.nIn < 1 ? 1 : n.nIn) : 0;
    int vo = (sideOf(n.outSide) == 0 || sideOf(n.outSide) == 2) ? n.nOut : 0;
    int r = vi > vo ? vi : vo;
    return r < 1 ? 1 : r;
}
Vec2 sidePort(const Node& n, int side, int k, int count) {
    FRect b = nodeBox(n);
    int cnt = count < 1 ? 1 : count;
    if (k < 0) k = 0;
    if (k >= cnt) k = cnt - 1;
    float top = (n.type == NodeType::PLUGIN && n.shape == NS_SHAPE_CUSTOM) ? 0.0f : NODE_TITLE_H;
    float bodyY = b.y + top, bodyH = b.h - top;
    switch (sideOf(side)) {
        case 0: return {b.x, bodyY + (bodyH / (cnt + 1)) * (k + 1)};
        case 2: return {b.x + b.w, bodyY + (bodyH / (cnt + 1)) * (k + 1)};
        case 1: return {b.x + (b.w / (cnt + 1)) * (k + 1), b.y};
        default: return {b.x + (b.w / (cnt + 1)) * (k + 1), b.y + b.h};
    }
}
} // namespace

float nodeHeight(const Node& n) {
    bool custom = (n.type == NodeType::PLUGIN && n.shape == NS_SHAPE_CUSTOM);
    float h = (custom ? 0.0f : NODE_TITLE_H) + vertRows(n) * NODE_ROW_H;
    if (custom && h < NODE_ROW_H * 2) h = NODE_ROW_H * 2;
    if (n.type == NodeType::PLUGIN && n.szH > h) h = n.szH;
    return h;
}

float nodeWidth(const Node& n) {
    if (n.type == NodeType::PLUGIN && n.szW > NODE_W) return n.szW;
    std::string t = n.title;
    if (t.empty()) t = (n.type == NodeType::PLUGIN)
        ? (n.disp.empty() ? n.plugin : n.disp)
        : std::string(nodeTypeName(n.type));
    float w = 20.0f + (float)t.size() * 7.0f;
    if (w < NODE_W) w = NODE_W;
    if (w > 240.0f) w = 240.0f;
    return w;
}

FRect nodeBox(const Node& n) {
    float h = nodeHeight(n), w = nodeWidth(n);
    return {n.pos.x - w * 0.5f, n.pos.y - h * 0.5f, w, h};
}

const char* sideName(int s) {
    switch (sideOf(s)) {
        case 0: return "esq";
        case 1: return "topo";
        case 2: return "dir";
        default: return "base";
    }
}

Vec2 inPort(const Node& n, int k)  { return sidePort(n, n.inSide, k, n.nIn < 1 ? 1 : n.nIn); }
Vec2 outPort(const Node& n, int k) { return sidePort(n, n.outSide, k, n.nOut < 1 ? 1 : n.nOut); }

Vec2 inDir(const Node& n, int)  { return sideNormal(sideOf(n.inSide)); }
Vec2 outDir(const Node& n, int) { return sideNormal(sideOf(n.outSide)); }

SDL_Color socketColor(bool input, int k) {
    static const SDL_Color IN[8] = {
        {110, 230, 140, 255}, {90, 200, 255, 255}, {150, 160, 255, 255}, {120, 235, 220, 255},
        {170, 225, 120, 255}, {120, 195, 165, 255}, {160, 210, 255, 255}, {95, 230, 195, 255},
    };
    static const SDL_Color OUT[8] = {
        {255, 170, 90, 255}, {255, 120, 120, 255}, {255, 220, 90, 255}, {255, 140, 200, 255},
        {210, 160, 255, 255}, {255, 190, 120, 255}, {240, 120, 90, 255}, {255, 205, 120, 255},
    };
    int i = k < 0 ? 0 : k % 8;
    return input ? IN[i] : OUT[i];
}

bool inBox(FRect r, Vec2 p) {
    return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
}

Vec2 wirePoint(Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2, WireStyle style, float t) {
    if (style == WireStyle::STRAIGHT)
        return {p1.x + (p2.x - p1.x) * t, p1.y + (p2.y - p1.y) * t};
    Vec2 u1 = vnorm(d1), u2 = vnorm(d2);
    float off = wireOff(p1, p2);
    float u = 1.0f - t;
    return {
        u*u*u*p1.x + 3*u*u*t*(p1.x + u1.x * off) + 3*u*t*t*(p2.x - u2.x * off) + t*t*t*p2.x,
        u*u*u*p1.y + 3*u*u*t*(p1.y + u1.y * off) + 3*u*t*t*(p2.y - u2.y * off) + t*t*t*p2.y,
    };
}

namespace {
float ptSeg(Vec2 p, Vec2 a, Vec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float cx = a.x + dx * t - p.x, cy = a.y + dy * t - p.y;
    return std::sqrt(cx * cx + cy * cy);
}
} // namespace

bool wireHit(Vec2 p, Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2, WireStyle style, float tol) {
    if (style == WireStyle::STRAIGHT) return ptSeg(p, p1, p2) <= tol;
    Vec2 prev = p1;
    for (int i = 1; i <= 24; ++i) {
        Vec2 q = wirePoint(p1, d1, p2, d2, style, (float)i / 24);
        if (ptSeg(p, prev, q) <= tol) return true;
        prev = q;
    }
    return false;
}

bool hitPt(Vec2 p, Vec2 c, float r) {
    float dx = p.x - c.x, dy = p.y - c.y; return dx*dx + dy*dy <= r*r;
}

bool inRect(SDL_Rect r, Vec2 p) {
    return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

void TextPainter::draw(int x, int y, const std::string& s, SDL_Color c) const {
    if (!ren || !font || s.empty()) return;
    SDL_Surface* su = TTF_RenderUTF8_Blended(font, s.c_str(), c);
    if (!su) return;
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren, su);
    if (tx) {
        const float k = scale > 0.0f ? scale : 1.0f;
        SDL_Rect d{x, y, (int)std::lround(su->w / k), (int)std::lround(su->h / k)};
        SDL_RenderCopy(ren, tx, nullptr, &d);
        SDL_DestroyTexture(tx);
    }
    SDL_FreeSurface(su);
}

bool TextPainter::measure(const std::string& s, int& w, int& h) const {
    if (!font) return false;
    if (TTF_SizeUTF8(font, s.c_str(), &w, &h) != 0) return false;
    const float k = scale > 0.0f ? scale : 1.0f;
    w = (int)std::lround(w / k);
    h = (int)std::lround(h / k);
    return true;
}

namespace {
bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }
} // namespace

std::string elideText(const TextPainter& p, std::string s, int maxw) {
    int w = 0, h = 0;
    if (maxw <= 0) return {};
    if (!p.measure(s, w, h) || w <= maxw) return s;
    const std::string ell = "…";
    int ew = 0;
    p.measure(ell, ew, h);
    while (!s.empty()) {
        s.pop_back();
        while (!s.empty() && isCont((unsigned char)s.back())) s.pop_back();
        if (!p.measure(s, w, h)) return ell;
        if (w + ew <= maxw) return s + ell;
    }
    return ell;
}

std::string elideText(const TextPainter& p, const i18n::Key& k, int maxw) {
    return elideText(p, i18n::text(k.en), maxw);
}
