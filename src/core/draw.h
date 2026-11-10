// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// 2D drawing primitives + text — no app state.
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "i18n.h"
#include <cmath>
#include <string>
#include "graph.h"

// Semantic UI palette. tema.cpp fills kUi and the text shortcuts
// from Omarchy's colors.toml; the defaults below apply without Omarchy.
struct UiTheme {
    SDL_Color background{18, 18, 22, 255};       // canvas
    SDL_Color darkBackground{14, 14, 18, 255}; // menubar/rail
    SDL_Color panel{28, 28, 36, 255};      // panels and modals
    SDL_Color box{26, 26, 32, 255};       // node body
    SDL_Color edge{74, 74, 94, 255};
    SDL_Color strongEdge{100, 100, 130, 255};
    SDL_Color grid{32, 32, 40, 255};
    SDL_Color strongGrid{48, 48, 58, 255};
    SDL_Color text{200, 200, 205, 255};
    SDL_Color weak{130, 130, 140, 255};
    SDL_Color strong{235, 235, 245, 255};
    SDL_Color section{140, 150, 185, 255};
    SDL_Color label{165, 165, 178, 255};
    SDL_Color highlight{255, 220, 100, 255};
    SDL_Color hover{48, 48, 60, 255};
    SDL_Color active{46, 40, 62, 255};
    SDL_Color error{255, 120, 120, 255};
    SDL_Color warning{255, 200, 90, 255};
    SDL_Color ok{140, 255, 150, 255};
    SDL_Color textStr{255, 205, 120, 255};
};
inline UiTheme kUi;

// text shortcuts used throughout the UI (temaAplica updates them)
inline SDL_Color kTxt{200, 200, 205, 255};
inline SDL_Color kDim{130, 130, 140, 255};
inline SDL_Color kErr{255, 120, 120, 255};
inline SDL_Color kSec{140, 150, 185, 255}; // section title
inline SDL_Color kLbl{165, 165, 178, 255}; // field label
inline SDL_Color kStr{255, 205, 120, 255}; // text value (non-numeric)

inline SDL_Color mixColor(SDL_Color a, SDL_Color b, float t) {
    auto m = [&](int x, int y) { return (int)(x + (y - x) * t); };
    return {(Uint8)m(a.r, b.r), (Uint8)m(a.g, b.g), (Uint8)m(a.b, b.b), (Uint8)m(a.a, b.a)};
}
inline SDL_Color withAlpha(SDL_Color c, Uint8 a) { c.a = a; return c; }

// Relative luminance (WCAG) and maximum-contrast text color: dark
// background -> light text; light background -> dark text. Used where the background is
// computed (node title bar, group header, etc.).
inline double linearChannel(Uint8 v) {
    const double s = v / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}
inline double luminance(SDL_Color c) {
    return 0.2126 * linearChannel(c.r) + 0.7152 * linearChannel(c.g) +
           0.0722 * linearChannel(c.b);
}
inline SDL_Color contrastColor(SDL_Color background) {
    return luminance(background) > 0.179 ? SDL_Color{8, 8, 10, 255}
                                    : SDL_Color{250, 250, 250, 255};
}
inline void setColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void drawCircle(SDL_Renderer* r, int cx, int cy, int rad);
void drawRing(SDL_Renderer* r, int cx, int cy, int rad);
// output->input wire: straight or cubic bezier
enum class WireStyle { STRAIGHT = 0, CURVE = 1 };
// dash > 0: dashed wire (dash = number of drawn/skipped segments, ~6 px each)
void drawWire(SDL_Renderer* ren, Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2, WireStyle style,
              bool arrow, int dash = 0);
// point on the wire at t (0=output, 1=input) — same path as drawWire
Vec2 wirePoint(Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2, WireStyle style, float t);
// true if p is within tol px of the wire
bool wireHit(Vec2 p, Vec2 p1, Vec2 d1, Vec2 p2, Vec2 d2, WireStyle style, float tol);
// output/arrival direction of the ports (follows the node's angleDeg)
Vec2 outDir(const Node& n, int k = 0);
Vec2 inDir(const Node& n, int k = 0);
void ringColor(NodeType t, Uint8& r, Uint8& g, Uint8& b);
const char* nodeTypeName(NodeType t);

// ---- node box geometry ----
// sides: 0 = left, 1 = top, 2 = right, 3 = bottom
inline constexpr float NODE_W = 124.0f;        // box width (world)
inline constexpr float NODE_TITLE_H = 22.0f;   // title bar height
inline constexpr float NODE_ROW_H = 18.0f;     // height of each port row
struct FRect { float x = 0, y = 0, w = 0, h = 0; };
FRect nodeBox(const Node& n);                  // box in world coords (center = pos)
float nodeHeight(const Node& n);
float nodeWidth(const Node& n);                // width (grows to fit the title)
const char* sideName(int s);                   // "esq"/"topo"/"dir"/"base" (left/top/right/bottom)
Vec2 inPort(const Node& n, int k = 0);         // k-th input socket
Vec2 outPort(const Node& n, int k = 0);        // k-th output socket
SDL_Color socketColor(bool input, int k);      // distinct color per socket
bool inBox(FRect r, Vec2 p);

bool hitPt(Vec2 p, Vec2 c, float r);
bool inRect(SDL_Rect r, Vec2 p);

// Text via SDL_ttf (no-op if font == nullptr).
class TextPainter {
public:
    SDL_Renderer* ren = nullptr;
    TTF_Font* font = nullptr;
    // Renderer logical -> physical scale (HiDPI). The font is opened at the physical
    // size (13 * scale); measurements and destination return to logical space.
    float scale = 1.0f;
    // Plain strings are drawn verbatim (values, titles, plugin data).
    void draw(int x, int y, const std::string& s, SDL_Color c) const;
    bool measure(const std::string& s, int& w, int& h) const;
    // Explicitly translated UI labels.
    void draw(int x, int y, const i18n::Key& k, SDL_Color c) const {
        draw(x, y, i18n::text(k.en), c);
    }
    bool measure(const i18n::Key& k, int& w, int& h) const {
        return measure(i18n::text(k.en), w, h);
    }
};
// truncates the string to fit in maxw px, with an ellipsis (keeps valid UTF-8)
std::string elideText(const TextPainter& p, std::string s, int maxw);
std::string elideText(const TextPainter& p, const i18n::Key& k, int maxw);
