// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scope.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
const SDL_Color kPal[8] = {
    {110, 230, 140, 255}, {90, 200, 255, 255}, {255, 170, 90, 255}, {255, 120, 120, 255},
    {205, 150, 255, 255}, {255, 220, 90, 255}, {120, 235, 220, 255}, {255, 150, 210, 255},
};
SDL_Color chColor(const ScopeData& d, int k) {
    if (k >= 0 && k < (int)d.colors.size()) return d.colors[k];
    return kPal[k < 0 ? 0 : (k & 7)];
}
std::string chName(const ScopeData& d, int k) {
    if (k >= 0 && k < (int)d.names.size() && !d.names[k].empty()) return d.names[k];
    return "ch" + std::to_string(k);
}
// draws the text over a dark background (readable on top of bars/curves)
void chip(const TextPainter& p, int x, int y, const std::string& s, SDL_Color c) {
    int w = 0, h = 0;
    if (p.measure(s, w, h) && p.ren) {
        setColor(p.ren, withAlpha(kUi.darkBackground, 225));
        SDL_Rect bg{x - 3, y - 1, w + 6, h + 2};
        SDL_RenderFillRect(p.ren, &bg);
    }
    p.draw(x, y, s, c);
}
// same, right-aligned at xr (useful to avoid covering peaks at the left edge)
void chipR(const TextPainter& p, int xr, int y, const std::string& s, SDL_Color c) {
    int w = 0, h = 0;
    int x = p.measure(s, w, h) ? xr - w : xr - 80;
    chip(p, x, y, s, c);
}

// ---- parsing of "k=v, k=v" params ----
void fftRadix2(std::vector<float>& re, std::vector<float>& im) {
    size_t n = re.size();
    if (n < 2) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * 3.14159265358979323846 / (double)len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k) {
                double wr = std::cos(ang * k), wi = std::sin(ang * k);
                size_t a = i + k, b = i + k + len / 2;
                float ur = re[a], ui = im[a];
                float vr = re[b] * (float)wr - im[b] * (float)wi;
                float vi = re[b] * (float)wi + im[b] * (float)wr;
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
            }
    }
}

// ---- grid / frames ----
void frame(SDL_Renderer* r, int x0, int y0, int pw, int ph, float mn, float mx, const ScopeCfg& c) {
    if (c.grid) {
        setColor(r, kUi.grid);
        for (int i = 0; i <= 4; ++i) { int y = y0 + ph * i / 4; SDL_RenderDrawLine(r, x0, y, x0 + pw, y); }
        for (int i = 0; i <= 6; ++i) { int x = x0 + pw * i / 6; SDL_RenderDrawLine(r, x, y0, x, y0 + ph); }
    }
    if (c.zero && mx > mn && mn <= 0 && 0 <= mx) {
        setColor(r, kUi.label);
        int y = y0 + (int)((mx / (mx - mn)) * ph);
        SDL_RenderDrawLine(r, x0, y, x0 + pw, y);
    }
    setColor(r, kUi.edge);
    SDL_Rect b{x0, y0, pw, ph};
    SDL_RenderDrawRect(r, &b);
}
void valueRange(const std::vector<std::deque<double>>& ch, float scale, float& mn, float& mx) {
    bool any = false;
    mn = mx = 0;
    for (auto& q : ch)
        for (double v : q) {
            float w = (float)(v * scale);
            if (!any) { mn = mx = w; any = true; }
            else { if (w < mn) mn = w; if (w > mx) mx = w; }
        }
    if (!any) { mn = -1; mx = 1; return; }
    if (mn == mx) { mn -= 1; mx += 1; return; }
    float pad = (mx - mn) * 0.1f;
    mn -= pad; mx += pad;
}

// ---- charts ----
void plotLine(SDL_Renderer* r, const ScopeData& d, const ScopeCfg& c, int x0, int y0, int pw, int ph) {
    float mn, mx;
    if (c.yauto) valueRange(d.ch, c.scale, mn, mx);
    else { mn = c.ymin; mx = c.ymax; if (mx <= mn) mx = mn + 1; }
    frame(r, x0, y0, pw, ph, mn, mx, c);
    for (size_t k = 0; k < d.ch.size(); ++k) {
        const auto& q = d.ch[k];
        if (q.size() < 2) continue;
        SDL_Color col = chColor(d, (int)k);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        size_t n = q.size();
        size_t stride = n > (size_t)pw ? (n + pw - 1) / pw : 1;
        int px = -1, py = -1;
        for (size_t i = 0; i < n; i += stride) {
            int x = x0 + (int)((double)i * pw / (double)(n - 1));
            int y = y0 + (int)((mx - q[i] * c.scale) / (mx - mn) * ph);
            if (px >= 0) SDL_RenderDrawLine(r, px, py, x, y);
            px = x; py = y;
        }
    }
}
void plotXY(SDL_Renderer* r, const ScopeData& d, const ScopeCfg& c, int x0, int y0, int pw, int ph, const TextPainter& p) {
    setColor(r, kUi.edge);
    SDL_Rect box{x0, y0, pw, ph};
    SDL_RenderDrawRect(r, &box);
    if (d.ch.size() < 2) { p.draw(x0 + 8, y0 + 8, i18n::tr("X-Y needs 2 channels (inputs)"), kErr); return; }
    int xi = std::min(c.xc, (int)d.ch.size() - 1);
    int yi = std::min(c.yc, (int)d.ch.size() - 1);
    const auto& qx = d.ch[xi];
    const auto& qy = d.ch[yi];
    size_t n = std::min(qx.size(), qy.size());
    if (n < 2) return;
    float mnx, mxx, mny, mxy;
    if (c.yauto) { valueRange({qx}, c.scale, mnx, mxx); valueRange({qy}, c.scale, mny, mxy); }
    else { mnx = mny = c.ymin; mxx = mxy = c.ymax; if (mxx <= mnx) mxx = mnx + 1; }
    if (c.grid) {
        setColor(r, kUi.grid);
        for (int i = 1; i < 4; ++i) { int y = y0 + ph * i / 4; SDL_RenderDrawLine(r, x0, y, x0 + pw, y); }
        for (int i = 1; i < 6; ++i) { int x = x0 + pw * i / 6; SDL_RenderDrawLine(r, x, y0, x, y0 + ph); }
    }
    SDL_Color col = chColor(d, yi);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    int lx = -1, ly = -1;
    for (size_t i = 0; i < n; ++i) {
        int sx = x0 + (int)((qx[i] * c.scale - mnx) / (mxx - mnx) * pw);
        int sy = y0 + (int)((mxy - qy[i] * c.scale) / (mxy - mny) * ph);
        if (lx >= 0) SDL_RenderDrawLine(r, lx, ly, sx, sy);
        lx = sx; ly = sy;
    }
    chip(p, x0 + 6, y0 + ph - 16, "x: " + chName(d, xi), kDim);
    chip(p, x0 + 6, y0 + 4, "y: " + chName(d, yi), kDim);
}
void plotBars(SDL_Renderer* r, const ScopeData& d, const ScopeCfg& c, int x0, int y0, int pw, int ph, const TextPainter& p) {
    setColor(r, kUi.edge);
    SDL_Rect box{x0, y0, pw, ph};
    SDL_RenderDrawRect(r, &box);
    int n = (int)d.ch.size();
    if (n <= 0) return;
    int labW = std::min(150, std::max(64, pw / 3));
    int bx0 = x0 + labW + 6, bw = x0 + pw - bx0 - 4;
    if (bw < 10) return;
    int zeroX = bx0 + bw / 2;
    double maxAbs = 1e-6;
    for (auto& q : d.ch) if (!q.empty()) maxAbs = std::max(maxAbs, std::fabs(q.back() * c.scale));
    setColor(r, kUi.label);
    SDL_RenderDrawLine(r, zeroX, y0 + 2, zeroX, y0 + ph - 2);
    int slot = ph / n;
    int rowH = std::min(20, std::max(6, slot - 4));
    for (int k = 0; k < n; ++k) {
        int y = y0 + k * slot + (slot - rowH) / 2;
        double v = d.ch[k].empty() ? 0 : d.ch[k].back() * c.scale;
        int len = (int)((v / maxAbs) * (bw / 2));
        SDL_Color col = chColor(d, k);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        SDL_Rect bar = len >= 0 ? SDL_Rect{zeroX, y, len, rowH} : SDL_Rect{zeroX + len, y, -len, rowH};
        SDL_RenderFillRect(r, &bar);
        char lb[96];
        std::snprintf(lb, sizeof lb, "%s = %.2f", chName(d, k).c_str(), v);
        p.draw(x0 + 4, y + (rowH - 12) / 2, elideText(p, lb, labW - 4), kTxt);
    }
}
void plotHist(SDL_Renderer* r, const ScopeData& d, const ScopeCfg& c, int x0, int y0, int pw, int ph, const TextPainter& p) {
    setColor(r, kUi.edge);
    SDL_Rect box{x0, y0, pw, ph};
    SDL_RenderDrawRect(r, &box);
    int k = std::min(c.xc, (int)d.ch.size() - 1);
    if (k < 0 || k >= (int)d.ch.size() || d.ch[k].empty()) return;
    const auto& q = d.ch[k];
    double mn = q[0] * c.scale, mx = mn;
    for (double v : q) { double w = v * c.scale; if (w < mn) mn = w; if (w > mx) mx = w; }
    if (mx <= mn) mx = mn + 1;
    int B = c.bins < 4 ? 4 : (c.bins > 256 ? 256 : c.bins);
    std::vector<int> hit((size_t)B, 0);
    for (double v : q) { int b = (int)((v * c.scale - mn) / (mx - mn) * B); if (b < 0) b = 0; if (b >= B) b = B - 1; hit[b]++; }
    int hmax = 1;
    for (int h : hit) hmax = std::max(hmax, h);
    SDL_Color col = chColor(d, k);
    for (int b = 0; b < B; ++b) {
        int h = hit[b] * (ph - 2) / hmax;
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        SDL_Rect bar{x0 + b * pw / B, y0 + ph - h, std::max(1, pw / B - 1), h};
        SDL_RenderFillRect(r, &bar);
    }
    char lb[96];
    std::snprintf(lb, sizeof lb, "%s   %.3g … %.3g   (%d bands)", chName(d, k).c_str(), mn, mx, B);
    chip(p, x0 + 6, y0 + 4, lb, kDim);
}
void plotFFT(SDL_Renderer* r, const ScopeData& d, const ScopeCfg& c, int x0, int y0, int pw, int ph, double dtStep, const TextPainter& p) {
    setColor(r, kUi.edge);
    SDL_Rect box{x0, y0, pw, ph};
    SDL_RenderDrawRect(r, &box);
    int k = std::min(c.xc, (int)d.ch.size() - 1);
    if (k < 0 || k >= (int)d.ch.size() || d.ch[k].size() < 8) return;
    const auto& q = d.ch[k];
    size_t N = 1;
    while (N * 2 <= q.size() && N < 1024) N *= 2;
    std::vector<float> re(N, 0), im(N, 0);
    size_t off = q.size() - N;
    for (size_t i = 0; i < N; ++i) {
        double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (double)i / (double)(N - 1));
        re[i] = (float)(q[off + i] * c.scale * w);
    }
    fftRadix2(re, im);
    size_t half = N / 2;
    float mmax = 1e-12f;
    std::vector<float> mag(half);
    for (size_t i = 0; i < half; ++i) { mag[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]); mmax = std::max(mmax, mag[i]); }
    if (c.grid) {
        setColor(r, kUi.grid);
        for (int i = 1; i < 4; ++i) { int y = y0 + ph * i / 4; SDL_RenderDrawLine(r, x0, y, x0 + pw, y); }
    }
    SDL_Color col = chColor(d, k);
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    for (size_t i = 1; i < half; ++i) {
        int x = x0 + (int)((double)i / (double)(half - 1) * pw);
        int h = (int)(mag[i] / mmax * (ph - 2));
        SDL_RenderDrawLine(r, x, y0 + ph, x, y0 + ph - h);
    }
    double rate = dtStep > 0 ? 1.0 / dtStep : 60.0;
    char lb[96];
    std::snprintf(lb, sizeof lb, "%s   0 … %.0f Hz   (N=%zu)", chName(d, k).c_str(), rate / 2.0f, N);
    chipR(p, x0 + pw - 6, y0 + 4, lb, kDim); // on the right: does not cover the peak (low frequencies)
}

void drawLegend(SDL_Renderer* r, const TextPainter& p, int H, const ScopeData& d, int x0, int y0, int pw, int ph) {
    int cols = pw >= 380 ? 4 : (pw >= 200 ? 2 : 1);
    int colW = pw / cols;
    int ly = y0 + ph + 8;
    for (size_t k = 0; k < d.ch.size(); ++k) {
        if (ly > H - 16) break;
        int cx = x0 + (int)(k % cols) * colW;
        SDL_Color col = chColor(d, (int)k);
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        SDL_Rect sw{cx, ly + 3, 10, 10};
        SDL_RenderFillRect(r, &sw);
        char vb[32] = "—";
        if (!d.ch[k].empty()) std::snprintf(vb, sizeof vb, "%.4g", d.ch[k].back());
        p.draw(cx + 14, ly, elideText(p, chName(d, (int)k) + " = " + vb, colW - 18), kTxt);
        if ((int)k % cols == cols - 1) ly += 17;
    }
}
} // namespace

bool ScopeWin::create(int id, const std::string& title, TTF_Font* font) {
    if (open) return true;
    nodeId = id;
    win = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) return false;
    SDL_SetWindowMinimumSize(win, 360, 220);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { SDL_DestroyWindow(win); win = nullptr; return false; }
    painter = {ren, font};
    winID = SDL_GetWindowID(win);
    open = true;
    return true;
}
void ScopeWin::destroy() {
    if (ren) SDL_DestroyRenderer(ren);
    ren = nullptr;
    painter = {nullptr, nullptr};
    if (win) SDL_DestroyWindow(win);
    win = nullptr;
    open = false;
    winID = 0;
    nodeId = -1;
}
void ScopeWin::setTitle(const std::string& t) { if (win) SDL_SetWindowTitle(win, t.c_str()); }
void ScopeWin::handleEvent(const SDL_Event& e, const ScopeCfg* cfg, ScopeData* data,
                           const std::function<void()>* beforeEdit) {
    if (cfg && cfg->type == ScopeType::MAP && data && data->map)
        mapHandleEvent(e, data->mapState.view, *data, *data->map, W, H, beforeEdit);
    if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_CLOSE) open = false;
        else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            W = e.window.data1;
            H = e.window.data2;
            if (data) data->mapState.view.needFit = true;
        }
    }
}

void drawScope(SDL_Renderer* r, const TextPainter& p, int W, int H, const ScopeCfg& c,
               const ScopeData& d, const std::string& title, double dtStep,
               MapView* mapView) {
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    setColor(r, kUi.background);
    SDL_RenderClear(r);
    int x0 = 10, y0 = 28, pw = W - 20, ph = H - 92;
    if (pw < 24 || ph < 24) return;

    char hb[192];
    if (c.type == ScopeType::MAP)
        std::snprintf(hb, sizeof hb, "%s  ·  %s%s", title.c_str(), i18n::text("map").c_str(),
                      d.frozen ? ("  " + i18n::text("[FROZEN]")).c_str() : "");
    else
        std::snprintf(hb, sizeof hb, "%s  ·  %s%s   %s %.1fs", title.c_str(),
                      i18n::text(scopeTypeName(c.type)).c_str(),
                      d.frozen ? ("  " + i18n::text("[FROZEN]")).c_str() : "",
                      i18n::text("window").c_str(), c.window);
    p.draw(x0, 8, hb, kTxt);
    { // re-dock hint: F6 returns this window to the dock
        const std::string hint = "[F6 " + std::string(i18n::text("re-dock")) + "]";
        int hw = 0, hh = 0;
        if (p.measure(hint, hw, hh))
            p.draw(W - hw - 10, 8, hint, kDim);
    }

    switch (c.type) {
        case ScopeType::LINE: plotLine(r, d, c, x0, y0, pw, ph); break;
        case ScopeType::XY:   plotXY(r, d, c, x0, y0, pw, ph, p); break;
        case ScopeType::BARS: plotBars(r, d, c, x0, y0, pw, ph, p); break;
        case ScopeType::HIST: plotHist(r, d, c, x0, y0, pw, ph, p); break;
        case ScopeType::FFT:  plotFFT(r, d, c, x0, y0, pw, ph, dtStep, p); break;
        case ScopeType::MAP:
            drawMapScope(r, p, W, H, c, d, d.map.get(), mapView, title);
            break;
    }
    if (c.legend && c.type != ScopeType::MAP) drawLegend(r, p, H, d, x0, y0, pw, ph);
}

void renderScope(ScopeWin& v, const ScopeCfg& c, ScopeData& d, const std::string& title, double dtStep) {
    if (!v.open || !v.ren) return;
    drawScope(v.ren, v.painter, v.W, v.H, c, d, title, dtStep, &d.mapState.view);
    SDL_RenderPresent(v.ren);
}

void drawSparkline(SDL_Renderer* ren, SDL_Rect box,
                   const std::vector<std::deque<double>>& ch,
                   const std::vector<SDL_Color>& colors) {
    if (!ren || box.w < 12 || box.h < 8) return;
    for (size_t k = 0; k < ch.size(); ++k) {
        const auto& q = ch[k];
        if (q.size() < 2) continue;
        double mn = q[0], mx = q[0];
        for (double val : q) { if (val < mn) mn = val; if (val > mx) mx = val; }
        if (mx - mn < 1e-9f) { mn -= 1; mx += 1; }
        SDL_Color col = k < colors.size() ? colors[k] : (SDL_Color){160, 160, 170, 255};
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
        size_t n = q.size();
        size_t stride = n > (size_t)box.w ? (n + box.w - 1) / box.w : 1;
        int px = -1, py = -1;
        for (size_t i = 0; i < n; i += stride) {
            int x = box.x + (int)((double)i * (box.w - 1) / (double)(n - 1));
            int y = box.y + box.h - 1 - (int)((q[i] - mn) / (mx - mn) * (box.h - 1));
            if (px >= 0) SDL_RenderDrawLine(ren, px, py, x, y);
            px = x; py = y;
        }
    }
}
