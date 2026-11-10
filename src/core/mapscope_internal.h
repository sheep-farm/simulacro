// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Internal helpers shared by the split map Scope translation units.
#pragma once
#include "mapscope.h"
#include "scope.h"
#include "svg.h"
#include <string>
#include <vector>

namespace mapscope_detail {

struct MapVals {
    std::vector<double> v;
    std::vector<char> has;
    std::vector<SDL_Color> cat;
    std::vector<char> hasCat;
    double vmin = 0, vmax = 1;
    int withValue = 0;
    bool categorical = false;
};

double clampd(double v, double lo, double hi);
SDL_Color mapColor(double t, int palette);
SDL_Color paintColor(int i);
const char* toolName(int t);
bool pointInPolygon(const std::vector<GeoPoint>& poly, double x, double y);
bool featureInLasso(const GeoFeature& f, const std::vector<GeoPoint>& poly);
const char* baseName(const std::string& s);
std::string valText(double v);
bool idEqual(const std::string& s, double v);
void chip(const TextPainter& p, int x, int y, const std::string& s, SDL_Color c);
double screenX(const MapView& v, double x);
double screenY(const MapView& v, double y);
const MapPaint* paintFor(const MapState& st, int layer, const std::string& key);
void drawAnnotations(SDL_Renderer* r, const TextPainter& p, const MapState& st,
                     const MapView& v);
void fillFeature(SDL_Renderer* r, const GeoFeature& f, const MapView& v, SDL_Color fill,
                 SDL_Color border);
void drawPoints(SDL_Renderer* r, const GeoFeature& f, const MapView& v, SDL_Color col);
void drawLines(SDL_Renderer* r, const GeoFeature& f, const MapView& v, SDL_Color col);
void drawTransient(SDL_Renderer* r, const TextPainter& p, const MapState& st,
                   const MapView& v);
void drawLegend(SDL_Renderer* r, const TextPainter& p, int W, int H, const ScopeCfg& c,
                bool hasValues, double vmin, double vmax);
const GeoFeature* featureByRef(const ScopeData& data, const MapFeatureRef& ref);
MapVals mapValues(const ScopeCfg& cfg, const ScopeData& data, const MapCache& cache);
SDL_Color featureColor(const MapVals& mv, const MapState& st, int i, const GeoFeature& f,
                       SDL_Color fallback, int palette);

} // namespace mapscope_detail
