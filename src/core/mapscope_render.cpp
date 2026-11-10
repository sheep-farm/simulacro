// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mapscope_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <sys/stat.h>
#include <vector>

using namespace mapscope_detail;

void drawMapScope(SDL_Renderer* r, const TextPainter& p, int W, int H,
                  const ScopeCfg& cfg, const ScopeData& data,
                  MapCache* cache, MapView* view, const std::string& title) {
    if (!r) return;
    const int x0 = 10, y0 = 30, pw = W - 20, ph = H - 78;
    if (pw < 24 || ph < 24) return;
    MapView local;
    MapView& v = view ? *view : local;
    if (v.plotX != x0 || v.plotY != y0 || v.plotW != pw || v.plotH != ph) {
        v.plotX = x0;
        v.plotY = y0;
        v.plotW = pw;
        v.plotH = ph;
        // does not force fit: the view may have come from pstate
    }
    SDL_Rect area{x0, y0, pw, ph};
    setColor(r, kUi.darkBackground);
    SDL_RenderFillRect(r, &area);
    setColor(r, kUi.edge);
    SDL_RenderDrawRect(r, &area);

    if (!cache || cache->map.features.empty()) {
        std::string msg = cache && !cache->error.empty() ? cache->error
                                                        : i18n::text("choose a GeoJSON file");
        int tw = 0, th = 0;
        if (p.measure(msg, tw, th))
            p.draw(x0 + (pw - tw) / 2, y0 + (ph - th) / 2, msg,
                   cache && !cache->error.empty() ? kErr : kDim);
        return;
    }
    if (v.needFit) mapFit(cache->map, x0, y0, pw, ph, v);

    if (cache->loaded && v.mouseX >= 0)
        v.hover = mapFeatureAt(cache->map, v, v.mouseX, v.mouseY);

    MapVals mv = mapValues(cfg, data, *cache);
    const int n = (int)cache->map.features.size();
    const std::vector<double>& vals = mv.v;
    const std::vector<char>& has = mv.has;
    const int withValue = mv.withValue;
    const double vmin = mv.vmin, vmax = mv.vmax;
    const double span = vmax > vmin ? vmax - vmin : 1.0;

    SDL_RenderSetClipRect(r, &area);
    for (int i = 0; i < n; ++i) {
        const GeoFeature& f = cache->map.features[(size_t)i];
        const double sx0 = screenX(v, f.minx), sx1 = screenX(v, f.maxx);
        const double sy0 = screenY(v, f.maxy), sy1 = screenY(v, f.miny);
        if (sx1 < x0 || sx0 > x0 + pw || sy1 < y0 || sy0 > y0 + ph) continue;
        const MapPaint* pp = paintFor(data.mapState, 0, f.id);
        const bool hv = pp && pp->hasValue ? true : has[(size_t)i] != 0;
        const double vv = pp && pp->hasValue ? pp->value : vals[(size_t)i];
        SDL_Color fill = mixColor(kUi.box, kUi.background, 0.25f);
        if (pp && pp->hasColor) fill = pp->color;
        else if (mv.hasCat[(size_t)i]) fill = mv.cat[(size_t)i];
        else if (hv) fill = mapColor((vv - vmin) / span, cfg.mapPalette);
        SDL_Color border = kUi.darkBackground;
        const bool sel = data.mapState.selected(0, f.id);
        if (sel) border = {255, 220, 90, 255};
        else if (i == v.hover) border = {235, 235, 245, 255};
        if (!f.polygons.empty()) fillFeature(r, f, v, fill, border);
    }
    // lines on top of polygons (highways, rivers, routes)
    for (int i = 0; i < n; ++i) {
        const GeoFeature& f = cache->map.features[(size_t)i];
        if (f.lines.empty()) continue;
        const double sx0 = screenX(v, f.minx), sx1 = screenX(v, f.maxx);
        const double sy0 = screenY(v, f.maxy), sy1 = screenY(v, f.miny);
        if (sx1 < x0 || sx0 > x0 + pw || sy1 < y0 || sy0 > y0 + ph) continue;
        const MapPaint* pp = paintFor(data.mapState, 0, f.id);
        const bool hv = pp && pp->hasValue ? true : has[(size_t)i] != 0;
        const double vv = pp && pp->hasValue ? pp->value : vals[(size_t)i];
        SDL_Color col = hv ? mapColor((vv - vmin) / span, cfg.mapPalette)
                           : kUi.highlight;
        if (mv.hasCat[(size_t)i]) col = mv.cat[(size_t)i];
        if (pp && pp->hasColor) col = pp->color;
        if (data.mapState.selected(0, f.id)) col = {255, 220, 90, 255};
        else if (i == v.hover) col = {255, 255, 255, 255};
        drawLines(r, f, v, col);
    }
    // background GeoJSON points
    for (int i = 0; i < n; ++i) {
        const GeoFeature& f = cache->map.features[(size_t)i];
        if (f.points.empty()) continue;
        const MapPaint* pp = paintFor(data.mapState, 0, f.id);
        const bool hv = pp && pp->hasValue ? true : has[(size_t)i] != 0;
        const double vv = pp && pp->hasValue ? pp->value : vals[(size_t)i];
        SDL_Color col = hv ? mapColor((vv - vmin) / span, cfg.mapPalette)
                           : kUi.label;
        if (mv.hasCat[(size_t)i]) col = mv.cat[(size_t)i];
        if (pp && pp->hasColor) col = pp->color;
        drawPoints(r, f, v, col);
    }

    // overlay layers (e.g. highways over municipalities). They do not use the base
    // value scale; lines and points use their own overlay colors.
    int overlayHoverLayer = -1, overlayHover = -1;
    for (size_t li = data.overlays.size(); li-- > 0;) {
        const auto& mc = data.overlays[li];
        if (!mc || !mc->loaded || mc->map.features.empty()) continue;
        if (v.mouseX >= 0) {
            const int h = mapFeatureAt(mc->map, v, v.mouseX, v.mouseY);
            if (h >= 0) { overlayHoverLayer = (int)li; overlayHover = h; break; }
        }
    }
    for (size_t li = 0; li < data.overlays.size(); ++li) {
        const auto& mc = data.overlays[li];
        if (!mc || !mc->loaded || mc->map.features.empty()) continue;
        const int no = (int)mc->map.features.size();
        for (int i = 0; i < no; ++i) {
            const GeoFeature& f = mc->map.features[(size_t)i];
            const double sx0 = screenX(v, f.minx), sx1 = screenX(v, f.maxx);
            const double sy0 = screenY(v, f.maxy), sy1 = screenY(v, f.miny);
            if (sx1 < x0 || sx0 > x0 + pw || sy1 < y0 || sy0 > y0 + ph) continue;
            const bool hover = ((int)li == overlayHoverLayer && i == overlayHover);
            const bool sel = data.mapState.selected((int)li + 1, f.id);
            const MapPaint* pp = paintFor(data.mapState, (int)li + 1, f.id);
            if (!f.polygons.empty()) {
                SDL_Color fill = pp && pp->hasColor ? pp->color : mixColor(kUi.box, kUi.background, 0.15f);
                SDL_Color border = kUi.edge;
                if (sel) border = {255, 220, 90, 255};
                else if (hover) border = {255, 255, 255, 255};
                fillFeature(r, f, v, fill, border);
            }
            if (!f.lines.empty()) {
                SDL_Color col = pp && pp->hasColor ? pp->color : kUi.highlight;
                if (sel) col = {255, 220, 90, 255};
                else if (hover) col = {255, 255, 255, 255};
                drawLines(r, f, v, col);
            }
            if (!f.points.empty())
                drawPoints(r, f, v, pp && pp->hasColor ? pp->color : SDL_Color{240, 220, 160, 255});
        }
    }

    drawAnnotations(r, p, data.mapState, v);
    drawTransient(r, p, data.mapState, v);

    // socket 1 (optional): agents/points. N×2 (x,y) or N×3 (x,y,value) matrix;
    // a flat list [x0,y0,x1,y1,...] is also accepted. lon/lat coordinates when
    // the map was projected to Mercator.
    if (data.ch.size() > 1 && data.rows.size() > 1 && data.cols.size() > 1 &&
        (!data.ch[1].empty()) && (data.rows[1] > 0 || data.cols[1] > 0)) {
        const auto& q = data.ch[1];
        int rows = data.rows[1] > 0 ? data.rows[1] : 1;
        int cols = data.cols[1] > 0 ? data.cols[1] : (int)q.size();
        const bool matrix = rows > 1 && cols >= 2;
        const int steps = matrix ? rows : (int)q.size() / 2;
        for (int i = 0; i < steps; ++i) {
            double x, y, val = std::numeric_limits<double>::quiet_NaN();
            if (matrix) {
                const size_t base = (size_t)i * (size_t)cols;
                if (base + 1 >= q.size()) break;
                x = q[base];
                y = q[base + 1];
                if (cols >= 3 && base + 2 < q.size()) val = q[base + 2];
            } else {
                x = q[(size_t)i * 2];
                y = q[(size_t)i * 2 + 1];
            }
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            geoProjectPoint(x, y, cache->map.mercator);
            const int sx = (int)std::lround(screenX(v, x));
            const int sy = (int)std::lround(screenY(v, y));
            if (sx < x0 || sx >= x0 + pw || sy < y0 || sy >= y0 + ph) continue;
            SDL_Color col = kUi.strong;
            if (std::isfinite(val)) col = mapColor((val - vmin) / span, cfg.mapPalette);
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            drawCircle(r, sx, sy, 3);
        }
    }
    SDL_RenderSetClipRect(r, nullptr);

    if (cfg.mapLabels) {
        int labels = 0;
        for (int i = 0; i < n && labels < 250; ++i) {
            const GeoFeature& f = cache->map.features[(size_t)i];
            if (f.label.empty()) continue;
            const int sx = (int)std::lround(screenX(v, f.cx));
            const int sy = (int)std::lround(screenY(v, f.cy));
            if (sx < x0 || sx >= x0 + pw || sy < y0 || sy >= y0 + ph) continue;
            if ((f.maxx - f.minx) * v.scale < 24 || (f.maxy - f.miny) * v.scale < 10) continue;
            int tw = 0, th = 0;
            if (!p.measure(f.label, tw, th)) continue;
            chip(p, std::max(x0 + 2, std::min(x0 + pw - tw - 2, sx - tw / 2)),
                 std::max(y0 + 2, std::min(y0 + ph - th - 2, sy - th / 2)), f.label, kTxt);
            ++labels;
        }
    }

    if (cfg.legend) drawLegend(r, p, W, H, cfg, withValue > 0, vmin, vmax);
    char st[200];
    int nOverlay = 0;
    for (const auto& mc : data.overlays)
        if (mc && mc->loaded) nOverlay += (int)mc->map.features.size();
    if (nOverlay > 0)
        std::snprintf(st, sizeof st, "%d features · %d overlays · %d with value · %s",
                      n, nOverlay, withValue, baseName(cache->path));
    else
        std::snprintf(st, sizeof st, "%d features · %d with value · %s", n, withValue, baseName(cache->path));
    int tw = 0, th = 0;
    if (p.measure(st, tw, th)) p.draw(std::max(x0, W - tw - 12), H - 24, elideText(p, st, W - 24), kDim);
    p.draw(x0, H - 50, std::string(toolName(data.mapState.tool)) + " (S/B/A/G/L/M · 1..6 color)", kDim);

    if (!data.mapState.selection.empty()) {
        const GeoFeature* f = featureByRef(data, data.mapState.selection.back());
        if (f) {
            std::string title = f->label.empty() ? f->id : f->label;
            if (!f->id.empty() && f->id != f->label) title += "  (" + f->id + ")";
            const int px = x0 + 8, py = y0 + 8, pw2 = std::min(260, pw - 16);
            int lines = 1;
            for (const auto& kv : f->props) {
                (void)kv;
                if (lines > 7) break;
                ++lines;
            }
            const int ph2 = 10 + lines * 15;
            setColor(r, withAlpha(kUi.darkBackground, 230));
            SDL_Rect card{px, py, pw2, ph2};
            SDL_RenderFillRect(r, &card);
            setColor(r, kUi.strongEdge);
            SDL_RenderDrawRect(r, &card);
            p.draw(px + 6, py + 3, elideText(p, title, pw2 - 12), kTxt);
            int yy = py + 20;
            int n = 0;
            for (const auto& kv : f->props) {
                if (n++ >= 7) break;
                std::string line = kv.first + ": " + kv.second;
                p.draw(px + 6, yy, elideText(p, line, pw2 - 12), kDim);
                yy += 15;
            }
        }
    }

    if (!cache->error.empty()) chip(p, x0 + 4, y0 + 4, "map: " + cache->error, kErr);
    int errY = y0 + 24;
    for (size_t li = 0; li < data.overlays.size(); ++li) {
        const auto& mc = data.overlays[li];
        if (!mc || mc->error.empty()) continue;
        chip(p, x0 + 4, errY, "overlay " + std::to_string(li + 1) + ": " + mc->error, kErr);
        errY += 20;
    }

    auto showTooltip = [&](const GeoFeature& f, bool valueTooltip, double value) {
        std::string s = f.label.empty() ? f.id : f.label;
        if (!f.id.empty() && f.id != f.label) s += "  (" + f.id + ")";
        if (valueTooltip) s += "  = " + valText(value);
        int w = 0, h = 0;
        if (p.measure(s, w, h)) {
            int tx = v.mouseX + 14, ty = v.mouseY + 14;
            if (tx + w + 8 > W) tx = v.mouseX - w - 10;
            if (ty + h + 8 > H) ty = v.mouseY - h - 10;
            chip(p, std::max(2, tx), std::max(2, ty), s, kTxt);
        }
    };
    if (v.mouseX >= x0 && v.mouseX < x0 + pw && v.mouseY >= y0 && v.mouseY < y0 + ph) {
        if (overlayHoverLayer >= 0 && overlayHover >= 0 &&
            overlayHoverLayer < (int)data.overlays.size()) {
            const auto& mc = data.overlays[(size_t)overlayHoverLayer];
            if (mc && overlayHover < (int)mc->map.features.size()) {
                const GeoFeature& f = mc->map.features[(size_t)overlayHover];
                showTooltip(f, false, 0);
            }
        } else if (v.hover >= 0 && v.hover < n) {
            const GeoFeature& f = cache->map.features[(size_t)v.hover];
            showTooltip(f, has[(size_t)v.hover] != 0, vals[(size_t)v.hover]);
        }
    }
    (void)title;
}
