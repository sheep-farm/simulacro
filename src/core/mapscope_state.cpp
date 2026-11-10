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



std::string MapState::serialize() const {

    std::ostringstream o;

    o << "v 1\n";

    if (!view.needFit) // a view not yet fitted should not become a fixed camera

        o << "view " << std::setprecision(17) << view.cx << ' ' << view.cy << ' '

          << view.scale << '\n';

    o << "tool " << tool << '\n';

    for (const auto& s : selection)

        o << "sel " << s.layer << ' ' << std::quoted(s.key) << '\n';

    for (const auto& p : paints)

        o << "paint " << p.layer << ' ' << (p.hasValue ? 1 : 0) << ' '

          << std::setprecision(17) << p.value << ' ' << (p.hasColor ? 1 : 0) << ' '

          << (int)p.color.r << ' ' << (int)p.color.g << ' ' << (int)p.color.b << ' '

          << std::quoted(p.key) << '\n';

    for (const auto& a : annotations) {

        o << "ann " << a.type << ' ' << (int)a.color.r << ' ' << (int)a.color.g << ' '

          << (int)a.color.b << ' ' << a.pts.size();

        for (const auto& p : a.pts) o << ' ' << std::setprecision(17) << p.x << ' ' << p.y;

        o << ' ' << std::quoted(a.text) << '\n';

    }

    return o.str();

}



bool MapState::deserialize(const std::string& blob) {

    MapState newValue;

    std::istringstream in(blob);

    std::string line;

    bool sawVersion = false;

    while (std::getline(in, line)) {

        if (line.empty()) continue;

        std::istringstream s(line);

        std::string kind;

        if (!(s >> kind)) continue;

        if (kind == "v") {

            int v = 0;

            if (!(s >> v) || v != 1) return false;

            sawVersion = true;

        } else if (kind == "view") {

            double cx = 0, cy = 0, scale = 0;

            if (s >> cx >> cy >> scale && std::isfinite(cx) && std::isfinite(cy) &&

                std::isfinite(scale) && scale > 0) {

                newValue.view.cx = cx;

                newValue.view.cy = cy;

                newValue.view.scale = scale;

                newValue.view.needFit = false;

            }

        } else if (kind == "tool") {

            s >> newValue.tool;

            if (newValue.tool < 0 || newValue.tool > 3) newValue.tool = 0;

        } else if (kind == "sel") {

            MapFeatureRef r;

            if (s >> r.layer >> std::quoted(r.key)) newValue.selection.push_back(std::move(r));

        } else if (kind == "paint") {

            MapPaint p;

            int hv = 0, hc = 0, r = 0, g = 0, b = 0;

            if (s >> p.layer >> hv >> p.value >> hc >> r >> g >> b >> std::quoted(p.key)) {

                p.hasValue = hv != 0;

                p.hasColor = hc != 0;

                p.color = SDL_Color{(Uint8)r, (Uint8)g, (Uint8)b, 255};

                newValue.paints.push_back(std::move(p));

            }

        } else if (kind == "ann") {

            MapAnnotation a;

            size_t n = 0;

            int r = 0, g = 0, b = 0;

            if (s >> a.type >> r >> g >> b >> n && n <= 100000) {

                a.color = SDL_Color{(Uint8)r, (Uint8)g, (Uint8)b, 255};

                a.pts.reserve(n);

                bool ok = true;

                for (size_t i = 0; i < n; ++i) {

                    GeoPoint p;

                    if (!(s >> p.x >> p.y) || !std::isfinite(p.x) || !std::isfinite(p.y)) {

                        ok = false;

                        break;

                    }

                    a.pts.push_back(p);

                }

                if (ok && (s >> std::quoted(a.text))) {

                    if (a.type < 0 || a.type > 3) a.type = MapAnnotation::LINE;

                    newValue.annotations.push_back(std::move(a));

                }

            }

        }

    }

    if (!sawVersion) return false;

    *this = std::move(newValue);

    return true;

}



bool MapState::selected(int layer, const std::string& key) const {

    for (const auto& s : selection)

        if (s.layer == layer && s.key == key) return true;

    return false;

}



void MapState::toggleSelection(int layer, const std::string& key, bool additive) {

    if (!additive) {

        selection.clear();

        selection.push_back({layer, key});

        return;

    }

    for (auto it = selection.begin(); it != selection.end(); ++it)

        if (it->layer == layer && it->key == key) {

            selection.erase(it);

            return;

        }

    selection.push_back({layer, key});

}



bool MapCache::ensure(const std::string& newPath, const ScopeCfg& cfg) {

    if (newPath.empty()) {

        error = "choose a GeoJSON file";

        return false;

    }

    struct stat st{};

    if (stat(newPath.c_str(), &st) != 0) {

        error = "file not found";

        return false;

    }

    const bool sameFile = loaded && path == newPath && mtime == st.st_mtime && size == (long long)st.st_size;

    const bool sameFields = proj == cfg.mapProj && idField == cfg.mapId && labelField == cfg.mapLabel;

    if (sameFile && sameFields) return true;



    if (!sameFile) {

        GeoMap newValue;

        std::string err;

        if (!geoJSONLoad(newPath, newValue, err)) {

            error = err;

            return false;

        }

        raw = std::move(newValue);

        path = newPath;

        mtime = st.st_mtime;

        size = (long long)st.st_size;

        loaded = true;

    }

    GeoMap copy = raw;

    geoAssignFields(copy, cfg.mapId, cfg.mapLabel);

    std::string err;

    if (!geoProject(copy, cfg.mapProj, err)) {

        error = err;

        return false;

    }

    map = std::move(copy);

    proj = cfg.mapProj;

    idField = cfg.mapId;

    labelField = cfg.mapLabel;

    error.clear();

    return true;

}



void mapFit(const GeoMap& map, int x, int y, int w, int h, MapView& view) {

    view.plotX = x;

    view.plotY = y;

    view.plotW = std::max(1, w);

    view.plotH = std::max(1, h);

    const double dx = map.maxx - map.minx;

    const double dy = map.maxy - map.miny;

    view.cx = (map.minx + map.maxx) * 0.5;

    view.cy = (map.miny + map.maxy) * 0.5;

    if (!(dx > 0) || !(dy > 0)) {

        view.scale = 1.0;

    } else {

        const double margin = 10.0;

        const double sx = (view.plotW - 2 * margin) / dx;

        const double sy = (view.plotH - 2 * margin) / dy;

        view.scale = std::max(1e-12, std::min(sx, sy));

    }

    view.needFit = false;

}



void mapScreenToWorld(const MapView& view, int sx, int sy, double& x, double& y) {

    x = view.cx + ((double)sx - (view.plotX + view.plotW * 0.5)) / view.scale;

    y = view.cy - ((double)sy - (view.plotY + view.plotH * 0.5)) / view.scale;

}



int mapFeatureAt(const GeoMap& map, const MapView& view, int sx, int sy) {

    if (sx < view.plotX || sx >= view.plotX + view.plotW ||

        sy < view.plotY || sy >= view.plotY + view.plotH)

        return -1;

    double x = 0, y = 0;

    mapScreenToWorld(view, sx, sy, x, y);

    return geoFeatureAt(map, x, y, 4.0 / std::max(1e-12, view.scale));

}