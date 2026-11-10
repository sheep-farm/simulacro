// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Scope map layer: GeoJSON cache, projection, view, and SDL drawing.
#pragma once
#include "draw.h"
#include "geojson.h"
#include <ctime>
#include <functional>
#include <string>

struct ScopeCfg;
struct ScopeData;

// per-Scope-node cache; stores the raw GeoJSON and the projected copy
struct MapCache {
    GeoMap raw;                  // original coordinates
    GeoMap map;                  // projected coordinates (used for drawing)
    std::string path;            // resolved path
    std::string proj = "auto";   // applied projection
    std::string idField = "id";
    std::string labelField;
    std::time_t mtime = 0;
    long long size = 0;
    std::string error;
    bool loaded = false;

    // Reloads if path/mtime/size/projection/fields changed.
    bool ensure(const std::string& path, const ScopeCfg& cfg);
};

// window state: zoom/pan, hover, and selection
struct MapView {
    double cx = 0, cy = 0;
    double scale = 1.0;
    bool needFit = true;
    int hover = -1;
    bool dragging = false;
    int mouseX = -1, mouseY = -1;
    int dragX = 0, dragY = 0;
    int dragStartX = 0, dragStartY = 0;
    bool dragMoved = false;
    bool editCheckpointed = false; // current gesture already produced an undo snapshot
    int plotX = 10, plotY = 28, plotW = 640, plotH = 288;
};

// stable reference: layer 0 = base; 1.. = overlays
struct MapFeatureRef {
    int layer = 0;
    std::string key;
};

// manual override of a value/color per feature
struct MapPaint {
    int layer = 0;
    std::string key;
    double value = 0;
    bool hasValue = false;
    SDL_Color color{255, 220, 90, 255};
    bool hasColor = false;
};

// vector annotation independent of the GeoJSON layers
struct MapAnnotation {
    enum Type { POINT = 0, LINE = 1, POLYGON = 2, TEXT = 3 };
    int type = LINE;
    std::vector<GeoPoint> pts;
    SDL_Color color{255, 220, 90, 255};
    std::string text;
};


// state persisted in the node's pstate (initial) and pstateLive (current)
struct MapState {
    MapView view;
    std::vector<MapFeatureRef> selection;
    std::vector<MapPaint> paints;
    std::vector<MapAnnotation> annotations;
    int tool = 0; // 0=pan/select, 1=paint, 2=annotate, 3=measure

    // tool transients (not part of pstate)
    int paintColor = 0;
    int draftType = MapAnnotation::LINE;
    std::vector<GeoPoint> draft;
    std::vector<GeoPoint> measure;
    std::vector<GeoPoint> lasso; // trace of the lasso in progress (transient)

    std::string serialize() const;
    bool deserialize(const std::string& blob);
    bool selected(int layer, const std::string& key) const;
    void toggleSelection(int layer, const std::string& key, bool additive);
    void clearSelection() { selection.clear(); }
};

// Fits the view to the map bbox.
void mapFit(const GeoMap& map, int x, int y, int w, int h, MapView& view);

// Screen coordinates -> projected world.
void mapScreenToWorld(const MapView& view, int sx, int sy, double& x, double& y);

// Feature under the screen point, or -1.
int mapFeatureAt(const GeoMap& map, const MapView& view, int sx, int sy);

// Wheel/pan/hover/selection in the map window. Ignores events outside the area.
void mapHandleEvent(const SDL_Event& e, MapView& view, ScopeData& data, MapCache& cache,
                    int W, int H, const std::function<void()>* beforeEdit = nullptr);

// Vector SVG of the map (base + overlays + annotations + legend).
std::string mapSvg(const ScopeCfg& cfg, const ScopeData& data, int W, int H,
                   const std::string& title);

// Draws the map. If view == nullptr, uses a fitted local view (export).
void drawMapScope(SDL_Renderer* r, const TextPainter& p, int W, int H,
                  const ScopeCfg& cfg, const ScopeData& data,
                  MapCache* cache, MapView* view, const std::string& title);
