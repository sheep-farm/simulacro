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

std::string mapSvg(const ScopeCfg& cfg, const ScopeData& data, int W, int H,
                   const std::string& title) {
    std::ostringstream o;
    if (!data.map || data.map->map.features.empty()) return {};
    MapView v = data.mapState.view;
    const int x0 = 10, y0 = 30, pw = W - 20, ph = H - 78;
    v.plotX = x0; v.plotY = y0; v.plotW = std::max(1, pw); v.plotH = std::max(1, ph);
    if (v.needFit) mapFit(data.map->map, x0, y0, pw, ph, v);
    MapVals mv = mapValues(cfg, data, *data.map);

    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W << "\" height=\""
      << H << "\" viewBox=\"0 0 " << W << ' ' << H << "\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"" << svg::color(kUi.background) << "\"/>\n";
    o << "<rect x=\"" << x0 << "\" y=\"" << y0 << "\" width=\"" << pw << "\" height=\""
      << ph << "\" fill=\"" << svg::color(kUi.darkBackground) << "\" stroke=\"" << svg::color(kUi.edge) << "\"/>\n";
    o << "<clipPath id=\"plot\"><rect x=\"" << x0 << "\" y=\"" << y0 << "\" width=\""
      << pw << "\" height=\"" << ph << "\"/></clipPath>\n";
    o << "<g clip-path=\"url(#plot)\">\n";

    auto draw = [&](const GeoMap& map, int layer, bool overlay) {
        for (size_t i = 0; i < map.features.size(); ++i) {
            const GeoFeature& f = map.features[(size_t)i];
            const bool sel = data.mapState.selected(layer, f.id);
            SDL_Color fallback = overlay ? kUi.edge : mixColor(kUi.box, kUi.background, 0.25f);
            SDL_Color fill = overlay ? featureColor(mv, data.mapState, (int)i, f, fallback,
                                                   cfg.mapPalette)
                                     : featureColor(mv, data.mapState, (int)i, f, fallback,
                                                   cfg.mapPalette);
            const MapPaint* pp = paintFor(data.mapState, layer, f.id);
            if (pp && pp->hasColor) fill = pp->color;
            if (overlay && !(pp && pp->hasColor)) fill = mixColor(kUi.box, kUi.background, 0.15f);
            const std::string edge = svg::color(sel ? kUi.highlight : (overlay ? kUi.edge : kUi.darkBackground));
            for (const auto& poly : f.polygons) {
                o << "<path fill=\"" << svg::color(fill) << "\" fill-rule=\"evenodd\" stroke=\""
                  << edge << "\" stroke-width=\"0.8\" d=\"";
                for (const auto& ring : poly) {
                    if (ring.pts.size() < 2) continue;
                    for (size_t k = 0; k < ring.pts.size(); ++k)
                        o << (k ? " L " : "M ") << svg::number(screenX(v, ring.pts[k].x)) << ' '
                          << svg::number(screenY(v, ring.pts[k].y));
                    o << " Z ";
                }
                o << "\"/>\n";
            }
            for (const auto& line : f.lines) {
                if (line.size() < 2) continue;
                SDL_Color col = overlay ? kUi.highlight : fill;
                if (pp && pp->hasColor) col = pp->color;
                if (sel) col = kUi.highlight;
                o << "<polyline fill=\"none\" stroke=\"" << svg::color(col)
                  << "\" stroke-width=\"1.4\" points=\"";
                for (const auto& q : line)
                    o << svg::number(screenX(v, q.x)) << ',' << svg::number(screenY(v, q.y)) << ' ';
                o << "\"/>\n";
            }
            for (const auto& q : f.points) {
                SDL_Color col = overlay ? kUi.textStr : fill;
                if (pp && pp->hasColor) col = pp->color;
                o << "<circle cx=\"" << svg::number(screenX(v, q.x)) << "\" cy=\""
                  << svg::number(screenY(v, q.y)) << "\" r=\"2.2\" fill=\"" << svg::color(col)
                  << "\"/>\n";
            }
        }
    };
    draw(data.map->map, 0, false);
    for (size_t li = 0; li < data.overlays.size(); ++li)
        if (data.overlays[li] && data.overlays[li]->loaded)
            draw(data.overlays[li]->map, (int)li + 1, true);

    // annotations
    for (const auto& a : data.mapState.annotations) {
        const std::string col = svg::color(a.color);
        if (a.type == MapAnnotation::POINT && !a.pts.empty()) {
            o << "<circle cx=\"" << svg::number(screenX(v, a.pts[0].x)) << "\" cy=\""
              << svg::number(screenY(v, a.pts[0].y)) << "\" r=\"4\" fill=\"" << col << "\"/>\n";
        } else if ((a.type == MapAnnotation::LINE || a.type == MapAnnotation::POLYGON) &&
                   a.pts.size() >= 2) {
            o << "<" << (a.type == MapAnnotation::POLYGON ? "polygon" : "polyline")
              << " fill=\"" << (a.type == MapAnnotation::POLYGON ? col : "none")
              << "\" fill-opacity=\"0.15\" stroke=\"" << col << "\" stroke-width=\"1.5\" points=\"";
            for (const auto& q : a.pts)
                o << svg::number(screenX(v, q.x)) << ',' << svg::number(screenY(v, q.y)) << ' ';
            o << "\"/>\n";
        } else if (a.type == MapAnnotation::TEXT && !a.pts.empty()) {
            o << "<text x=\"" << svg::number(screenX(v, a.pts[0].x)) << "\" y=\""
              << svg::number(screenY(v, a.pts[0].y)) << "\" fill=\"" << col
              << "\" font-family=\"sans-serif\" font-size=\"12\">"
              << svg::escape(a.text) << "</text>\n";
        }
    }
    // feature labels
    if (cfg.mapLabels) {
        int nrot = 0;
        for (const auto& f : data.map->map.features) {
            if (f.label.empty() || nrot++ >= 250) continue;
            o << "<text x=\"" << svg::number(screenX(v, f.cx)) << "\" y=\""
              << svg::number(screenY(v, f.cy)) << "\" text-anchor=\"middle\" fill=\"#d8d8e0\""
                 " font-family=\"sans-serif\" font-size=\"10\">"
              << svg::escape(f.label) << "</text>\n";
        }
    }
    o << "</g>\n";

    // title and legend
    o << "<text x=\"" << x0 << "\" y=\"20\" fill=\"" << svg::color(kUi.strong) << "\" font-family=\"sans-serif\""
         " font-size=\"13\">" << svg::escape(title) << "</text>\n";
    if (cfg.legend) {
        const int lx = 12, ly = H - 38, lw = 180, lh = 10;
        if (mv.categorical) {
            o << "<text x=\"" << lx << "\" y=\"" << (ly + 9)
              << "\" fill=\"" << svg::color(kUi.weak) << "\" font-family=\"sans-serif\" font-size=\"11\">campo "
              << svg::escape(cfg.mapColorField) << " (categorical)</text>\n";
        } else if (mv.withValue > 0) {
            for (int i = 0; i < lw; ++i) {
                SDL_Color c = mapColor((double)i / (double)(lw - 1), cfg.mapPalette);
                o << "<rect x=\"" << (lx + i) << "\" y=\"" << ly << "\" width=\"1\" height=\""
                  << lh << "\" fill=\"" << svg::color(c) << "\"/>\n";
            }
            char b[96];
            std::snprintf(b, sizeof b, "%.4g  →  %.4g", mv.vmin, mv.vmax);
            o << "<text x=\"" << lx << "\" y=\"" << (ly + 24)
              << "\" fill=\"" << svg::color(kUi.weak) << "\" font-family=\"sans-serif\" font-size=\"11\">"
              << svg::escape(b) << "</text>\n";
        }
    }
    char status[200];
    std::snprintf(status, sizeof status, "%d features · %d with value · %s",
                  (int)data.map->map.features.size(), mv.withValue,
                  baseName(data.map->path));
    o << "<text x=\"" << (W - 12) << "\" y=\"" << (H - 10)
      << "\" text-anchor=\"end\" fill=\"" << svg::color(kUi.weak) << "\" font-family=\"sans-serif\""
         " font-size=\"11\">" << svg::escape(status) << "</text>\n";
    o << "</svg>\n";
    return o.str();
}
