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

namespace mapscope_detail {

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

SDL_Color mapColor(double t, int palette) {
    t = clampd(t, 0.0, 1.0);
    auto mix = [](int a, int b, double u) { return (int)std::lround(a + (b - a) * u); };
    auto mix3 = [&](const SDL_Color& a, const SDL_Color& b, double u) {
        return SDL_Color{(Uint8)mix(a.r, b.r, u), (Uint8)mix(a.g, b.g, u),
                         (Uint8)mix(a.b, b.b, u), 255};
    };
    if (palette == 1) { // approximate viridis
        static const SDL_Color st[5] = {{68, 1, 84, 255}, {59, 82, 139, 255}, {33, 145, 140, 255},
                                        {94, 201, 98, 255}, {253, 231, 37, 255}};
        const double p = t * 4.0;
        int i = std::min(3, (int)p);
        return mix3(st[i], st[i + 1], p - i);
    }
    if (palette == 2) { // grayscale
        const int c = (int)std::lround(48 + 190 * t);
        return SDL_Color{(Uint8)c, (Uint8)c, (Uint8)c, 255};
    }
    static const SDL_Color a{44, 123, 182, 255}, b{255, 255, 191, 255}, c{215, 25, 28, 255};
    return t < 0.5 ? mix3(a, b, t * 2.0) : mix3(b, c, (t - 0.5) * 2.0);
}

SDL_Color paintColor(int i) {
    static const SDL_Color colors[6] = {
        {255, 220, 90, 255}, {255, 120, 120, 255}, {110, 230, 140, 255},
        {90, 200, 255, 255}, {205, 150, 255, 255}, {255, 170, 90, 255},
    };
    return colors[i < 0 ? 0 : (i % 6)];
}

const char* toolName(int t) {
    switch (t) {
        case 1: return "paint";
        case 2: return "annotate";
        case 3: return "measure";
        case 4: return "lasso";
        default: return "select";
    }
}

bool pointInPolygon(const std::vector<GeoPoint>& poly, double x, double y) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const GeoPoint& a = poly[i];
        const GeoPoint& b = poly[j];
        if (((a.y > y) != (b.y > y)) &&
            (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x))
            inside = !inside;
    }
    return inside;
}

// A feature is in the lasso if the centroid OR some vertex falls inside it.
bool featureInLasso(const GeoFeature& f, const std::vector<GeoPoint>& poly) {
    if (pointInPolygon(poly, f.cx, f.cy)) return true;
    for (const auto& p : f.points)
        if (pointInPolygon(poly, p.x, p.y)) return true;
    for (const auto& line : f.lines)
        for (const auto& p : line)
            if (pointInPolygon(poly, p.x, p.y)) return true;
    for (const auto& fp : f.polygons)
        for (const auto& ring : fp)
            for (const auto& p : ring.pts)
                if (pointInPolygon(poly, p.x, p.y)) return true;
    return false;
}

const char* baseName(const std::string& s) {
    const size_t p = s.find_last_of('/');
    return p == std::string::npos ? s.c_str() : s.c_str() + p + 1;
}

std::string valText(double v) {
    if (!std::isfinite(v)) return "—";
    char b[48];
    std::snprintf(b, sizeof b, "%.4g", v);
    return b;
}

bool idEqual(const std::string& s, double v) {
    if (s.empty() || !std::isfinite(v)) return false;
    char b[64];
    std::snprintf(b, sizeof b, "%.15g", v);
    if (s == b) return true;
    char* end = nullptr;
    const double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(d)) return false;
    return std::fabs(d - v) <= std::max(1e-9, std::fabs(d) * 1e-12);
}

void chip(const TextPainter& p, int x, int y, const std::string& s, SDL_Color c) {
    int w = 0, h = 0;
    if (p.measure(s, w, h) && p.ren) {
        setColor(p.ren, withAlpha(kUi.darkBackground, 230));
        SDL_Rect bg{x - 3, y - 1, w + 6, h + 2};
        SDL_RenderFillRect(p.ren, &bg);
    }
    p.draw(x, y, s, c);
}

double screenX(const MapView& v, double x) {
    return v.plotX + v.plotW * 0.5 + (x - v.cx) * v.scale;
}
double screenY(const MapView& v, double y) {
    return v.plotY + v.plotH * 0.5 - (y - v.cy) * v.scale;
}

const MapPaint* paintFor(const MapState& st, int layer, const std::string& key) {
    for (const auto& p : st.paints)
        if (p.layer == layer && p.key == key) return &p;
    return nullptr;
}

void drawAnnotations(SDL_Renderer* r, const TextPainter& p, const MapState& st,
                     const MapView& v) {
    for (const auto& a : st.annotations) {
        SDL_SetRenderDrawColor(r, a.color.r, a.color.g, a.color.b, 255);
        if (a.type == MapAnnotation::POINT && !a.pts.empty()) {
            const int x = (int)std::lround(screenX(v, a.pts[0].x));
            const int y = (int)std::lround(screenY(v, a.pts[0].y));
            drawCircle(r, x, y, 4);
        } else if ((a.type == MapAnnotation::LINE || a.type == MapAnnotation::POLYGON) &&
                   a.pts.size() >= 2) {
            std::vector<SDL_Point> pts;
            for (const auto& q : a.pts)
                pts.push_back({(int)std::lround(screenX(v, q.x)),
                               (int)std::lround(screenY(v, q.y))});
            SDL_RenderDrawLines(r, pts.data(), (int)pts.size());
            if (a.type == MapAnnotation::POLYGON)
                SDL_RenderDrawLine(r, pts.back().x, pts.back().y, pts.front().x, pts.front().y);
        } else if (a.type == MapAnnotation::TEXT && !a.pts.empty()) {
            p.draw((int)std::lround(screenX(v, a.pts[0].x)),
                   (int)std::lround(screenY(v, a.pts[0].y)), a.text, a.color);
        }
    }
}

void fillFeature(SDL_Renderer* r, const GeoFeature& f, const MapView& v, SDL_Color fill,
                 SDL_Color border) {
    if (!r || f.polygons.empty()) return;
    std::vector<std::vector<SDL_FPoint>> rings;
    int ymin = std::numeric_limits<int>::max();
    int ymax = std::numeric_limits<int>::min();
    for (const auto& poly : f.polygons) {
        for (const auto& ring : poly) {
            if (ring.pts.size() < 2) continue;
            std::vector<SDL_FPoint> out;
            out.reserve(ring.pts.size());
            for (const auto& q : ring.pts) {
                const double x = screenX(v, q.x), y = screenY(v, q.y);
                if (!std::isfinite(x) || !std::isfinite(y)) continue;
                out.push_back({(float)x, (float)y});
                ymin = std::min(ymin, (int)std::floor(y));
                ymax = std::max(ymax, (int)std::ceil(y));
            }
            if (out.size() >= 2) rings.push_back(std::move(out));
        }
    }
    if (rings.empty() || ymin > ymax) return;
    const int cy0 = std::max(ymin, v.plotY);
    const int cy1 = std::min(ymax, v.plotY + v.plotH - 1);
    if (cy0 > cy1) return;

    SDL_SetRenderDrawColor(r, fill.r, fill.g, fill.b, 255);
    for (int y = cy0; y <= cy1; ++y) {
        std::vector<double> xs;
        for (const auto& ring : rings) {
            for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
                const SDL_FPoint& a = ring[i];
                const SDL_FPoint& b = ring[j];
                if (((a.y > y) != (b.y > y)) && std::fabs(b.y - a.y) > 1e-12)
                    xs.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
            }
        }
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int xa = (int)std::ceil(xs[k]);
            int xb = (int)std::floor(xs[k + 1]);
            if (xb >= xa) SDL_RenderDrawLine(r, xa, y, xb, y);
        }
    }
    if (fill.a == 0) return;
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
    for (const auto& ring : rings) {
        if (ring.size() < 2) continue;
        std::vector<SDL_Point> pts;
        pts.reserve(ring.size());
        for (const auto& q : ring) pts.push_back({(int)std::lround(q.x), (int)std::lround(q.y)});
        SDL_RenderDrawLines(r, pts.data(), (int)pts.size());
        SDL_RenderDrawLine(r, pts.back().x, pts.back().y, pts.front().x, pts.front().y);
    }
}

void drawPoints(SDL_Renderer* r, const GeoFeature& f, const MapView& v, SDL_Color col) {
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    for (const auto& q : f.points) {
        const int x = (int)std::lround(screenX(v, q.x));
        const int y = (int)std::lround(screenY(v, q.y));
        SDL_Rect p{x - 2, y - 2, 4, 4};
        SDL_RenderFillRect(r, &p);
    }
}

void drawLines(SDL_Renderer* r, const GeoFeature& f, const MapView& v, SDL_Color col) {
    if (f.lines.empty()) return;
    SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
    for (const auto& line : f.lines) {
        if (line.size() < 2) continue;
        std::vector<SDL_Point> pts;
        pts.reserve(line.size());
        for (const auto& q : line) {
            const double sx = screenX(v, q.x), sy = screenY(v, q.y);
            if (!std::isfinite(sx) || !std::isfinite(sy)) continue;
            pts.push_back({(int)std::lround(std::max(-1e7, std::min(1e7, sx))),
                           (int)std::lround(std::max(-1e7, std::min(1e7, sy)))});
        }
        if (pts.size() >= 2) SDL_RenderDrawLines(r, pts.data(), (int)pts.size());
    }
}

void drawTransient(SDL_Renderer* r, const TextPainter& p, const MapState& st,
                   const MapView& v) {
    auto line = [&](const std::vector<GeoPoint>& pts, SDL_Color col, bool close) {
        if (pts.size() < 2) return;
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        std::vector<SDL_Point> out;
        for (const auto& q : pts)
            out.push_back({(int)std::lround(screenX(v, q.x)),
                           (int)std::lround(screenY(v, q.y))});
        SDL_RenderDrawLines(r, out.data(), (int)out.size());
        if (close) SDL_RenderDrawLine(r, out.back().x, out.back().y, out.front().x, out.front().y);
    };
    if (!st.draft.empty()) {
        const SDL_Color col = {255, 230, 120, 255};
        line(st.draft, col, st.draftType == MapAnnotation::POLYGON);
        for (const auto& q : st.draft) {
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            drawCircle(r, (int)std::lround(screenX(v, q.x)),
                       (int)std::lround(screenY(v, q.y)), 3);
        }
    }
    if (st.tool == 4 && !st.lasso.empty()) {
        const SDL_Color col = {255, 210, 90, 255};
        line(st.lasso, col, true);
    }
    if (!st.measure.empty()) {
        const SDL_Color col = {120, 235, 255, 255};
        line(st.measure, col, false);
        double dist = 0;
        for (size_t i = 1; i < st.measure.size(); ++i)
            dist += std::hypot(st.measure[i].x - st.measure[i - 1].x,
                               st.measure[i].y - st.measure[i - 1].y);
        if (st.measure.size() >= 2) {
            char b[64];
            std::snprintf(b, sizeof b, "%.4g un", dist);
            const auto& q = st.measure.back();
            p.draw((int)std::lround(screenX(v, q.x)) + 8,
                   (int)std::lround(screenY(v, q.y)) - 8, b, col);
        }
    }
}

void drawLegend(SDL_Renderer* r, const TextPainter& p, int W, int H, const ScopeCfg& c,
                bool hasValues, double vmin, double vmax) {
    const int x = 12, y = H - 38, w = 180, h = 10;
    if (hasValues) {
        for (int i = 0; i < w; ++i) {
            SDL_Color col = mapColor((double)i / (double)(w - 1), c.mapPalette);
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            SDL_RenderDrawLine(r, x + i, y, x + i, y + h);
        }
        setColor(r, kUi.label);
        SDL_Rect box{x, y, w, h};
        SDL_RenderDrawRect(r, &box);
        p.draw(x, y + 13, valText(vmin), kDim);
        std::string vx = valText(vmax);
        int tw = 0, th = 0;
        if (p.measure(vx, tw, th)) p.draw(x + w - tw, y + 13, vx, kDim);
    } else {
        setColor(r, kUi.box);
        SDL_Rect box{x, y, w, h};
        SDL_RenderFillRect(r, &box);
        setColor(r, kUi.label);
        SDL_RenderDrawRect(r, &box);
        p.draw(x + 5, y + 12, i18n::tr("no values"), kDim);
    }
    (void)W;
}

const GeoFeature* featureByRef(const ScopeData& data, const MapFeatureRef& ref) {
    if (ref.layer == 0) {
        if (!data.map || !data.map->loaded) return nullptr;
        for (const auto& f : data.map->map.features)
            if (f.id == ref.key) return &f;
        return nullptr;
    }
    const size_t li = (size_t)(ref.layer - 1);
    if (li >= data.overlays.size() || !data.overlays[li] || !data.overlays[li]->loaded)
        return nullptr;
    for (const auto& f : data.overlays[li]->map.features)
        if (f.id == ref.key) return &f;
    return nullptr;
}

MapVals mapValues(const ScopeCfg& cfg, const ScopeData& data, const MapCache& cache) {
    MapVals mv;
    const int n = (int)cache.map.features.size();
    mv.v.assign((size_t)n, std::numeric_limits<double>::quiet_NaN());
    mv.has.assign((size_t)n, 0);
    mv.cat.assign((size_t)n, SDL_Color{52, 54, 66, 255});
    mv.hasCat.assign((size_t)n, 0);
    // socket 0 (optional): list or N×2/N×3 matrix
    const bool hasList = data.rows.size() > 0 && data.cols.size() > 0 &&
                          (data.rows[0] > 0 || data.cols[0] > 0);
    if (hasList && !data.ch.empty() && !data.ch[0].empty() && n > 0) {
        const auto& q = data.ch[0];
        int rows = data.rows[0];
        int cols = data.cols[0];
        if (rows <= 0) rows = 1;
        if (cols <= 0) cols = (int)q.size();
        const bool matrix = rows > 1 && cols >= 2;
        if (matrix) {
            const int ic = std::max(0, std::min(cols - 1, cfg.mapIdCol));
            const int vc = std::max(0, std::min(cols - 1, cfg.mapValCol));
            int matched = 0;
            for (int rr = 0; rr < rows; ++rr) {
                const size_t base = (size_t)rr * (size_t)cols;
                if (base + (size_t)std::max(ic, vc) >= q.size()) break;
                const double id = q[base + (size_t)ic];
                const double val = q[base + (size_t)vc];
                if (!std::isfinite(val)) continue;
                for (int i = 0; i < n; ++i) {
                    if (idEqual(cache.map.features[(size_t)i].id, id)) {
                        mv.v[(size_t)i] = val;
                        mv.has[(size_t)i] = 1;
                        ++matched;
                        break;
                    }
                }
            }
            if (matched == 0 && rows <= n) { // tolerates a matrix without matchable ids
                for (int i = 0; i < rows; ++i) {
                    const size_t base = (size_t)i * (size_t)cols;
                    if (base + (size_t)vc >= q.size()) break;
                    if (std::isfinite(q[base + (size_t)vc])) {
                        mv.v[(size_t)i] = q[base + (size_t)vc];
                        mv.has[(size_t)i] = 1;
                    }
                }
            }
        } else {
            const size_t lim = std::min((size_t)n, q.size());
            for (size_t i = 0; i < lim; ++i) {
                if (!std::isfinite(q[i])) continue;
                mv.v[i] = q[i];
                mv.has[i] = 1;
            }
        }
    }
    // GeoJSON property field (wins over the matrix)
    if (!cfg.mapColorField.empty() && n > 0) {
        std::vector<std::string> text((size_t)n);
        bool hasNonNumeric = false, hasNumber = false;
        for (int i = 0; i < n; ++i) {
            const auto& f = cache.map.features[(size_t)i];
            auto it = f.props.find(cfg.mapColorField);
            if (it == f.props.end() || it->second.empty()) continue;
            text[(size_t)i] = it->second;
            char* e = nullptr;
            const double d = std::strtod(it->second.c_str(), &e);
            const bool num = e && *e == '\0' && std::isfinite(d);
            if (num) hasNumber = true;
            else hasNonNumeric = true;
        }
        if (hasNonNumeric) { // categorical: color by distinct value (alphabetical order)
            mv.categorical = true;
            std::vector<std::string> uniq;
            for (const auto& t : text)
                if (!t.empty() && std::find(uniq.begin(), uniq.end(), t) == uniq.end())
                    uniq.push_back(t);
            std::sort(uniq.begin(), uniq.end());
            for (int i = 0; i < n; ++i) {
                if (text[(size_t)i].empty()) continue;
                const size_t idx = (size_t)(std::find(uniq.begin(), uniq.end(),
                                                     text[(size_t)i]) - uniq.begin());
                const double t = uniq.size() <= 1 ? 0.5 : (double)idx / (double)(uniq.size() - 1);
                mv.cat[(size_t)i] = mapColor(t, cfg.mapPalette);
                mv.hasCat[(size_t)i] = 1;
            }
        } else if (hasNumber) {
            for (int i = 0; i < n; ++i) {
                if (text[(size_t)i].empty()) continue;
                const double d = std::strtod(text[(size_t)i].c_str(), nullptr);
                if (!std::isfinite(d)) continue;
                mv.v[(size_t)i] = d;
                mv.has[(size_t)i] = 1;
            }
        }
    }
    for (int i = 0; i < n; ++i)
        if (mv.has[(size_t)i] ||
            (!mv.hasCat.empty() && mv.hasCat[(size_t)i]))
            ++mv.withValue;
    if (!mv.categorical) {
        bool any = false;
        for (double x : mv.v) {
            if (!std::isfinite(x)) continue;
            if (!any) { mv.vmin = mv.vmax = x; any = true; }
            else { mv.vmin = std::min(mv.vmin, x); mv.vmax = std::max(mv.vmax, x); }
        }
        if (!any) { mv.vmin = 0; mv.vmax = 1; }
        else if (mv.vmax <= mv.vmin) { mv.vmin -= 1; mv.vmax += 1; }
    } else {
        mv.vmin = 0; mv.vmax = 1;
    }
    return mv;
}


SDL_Color featureColor(const MapVals& mv, const MapState& st, int i, const GeoFeature& f,
                      SDL_Color fallback, int palette) {
    if (const MapPaint* pp = paintFor(st, 0, f.id))
        if (pp->hasColor) return pp->color;
    if (i >= 0 && i < (int)mv.hasCat.size() && mv.hasCat[(size_t)i]) return mv.cat[(size_t)i];
    if (i >= 0 && i < (int)mv.has.size() && mv.has[(size_t)i]) {
        const double span = mv.vmax > mv.vmin ? mv.vmax - mv.vmin : 1.0;
        return mapColor((mv.v[(size_t)i] - mv.vmin) / span, palette);
    }
    return fallback;
}

} // namespace mapscope_detail
