// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Scope as a NODE: graphical sink with inputs (1 channel per socket). Each node has
// its own window (opened by double-click / O key). History is sampled every
// step by the App (App::sampleScopes) and lives outside the graph (it is not saved).
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "draw.h"
#include "mapscope.h"

// chart types selectable in the properties
enum class ScopeType { LINE = 0, XY = 1, BARS = 2, HIST = 3, FFT = 4, MAP = 5 };
const char* scopeTypeName(ScopeType t);          // short name (line/xy/…)
int scopeTypeCount();                            // number of types (to cycle in the panel)

// chart settings, serialized in the node's "params" field (k=v, k=v, …)
struct ScopeCfg {
    ScopeType type = ScopeType::LINE;
    float window = 5.0f;      // time window (s)
    bool yauto = true;        // automatic Y scale
    float ymin = -1, ymax = 1; // used when yauto == false
    float scale = 1.0f;       // gain applied to values (Y zoom)
    int dec = 1;              // sample 1 out of every N steps
    bool grid = true;         // chart grid
    bool legend = true;       // legend with current values
    bool zero = true;         // highlighted zero line
    int xc = 0, yc = 1;       // X/Y channels (X-Y mode)
    int bins = 32;            // number of bands (histogram)

    // ---- map (type=map) ----
    std::string mapFile;              // "arquivo": local GeoJSON
    std::string mapId = "id";         // "idcampo": identity property
    std::string mapLabel;             // "rotulo": name property
    std::string mapProj = "auto";     // "projecao": auto|mercator|linear
    int mapPalette = 0;               // "paleta"
    bool mapValueAuto = true;         // "vauto"
    float mapVMin = 0, mapVMax = 0;   // "vmin"/"vmax"
    bool mapLabels = false;           // "rotulos"
    int mapIdCol = 0, mapValCol = 1;  // columns of the N×2 matrix
    std::string mapColorField;        // "corcampo": GeoJSON property for color (numeric or categorical)

    // overlay layers (e.g. highways over municipalities). List of paths
    // separated by "|" — generic for N layers.
    std::vector<std::string> mapOverlayFiles; // "sobrepor"
    std::string mapOverlayProj = "auto";      // "projecao2"
    std::string mapOverlayId = "id";          // "idcampo2"
    std::string mapOverlayLabel = "nome";     // "rotulo2"
};
ScopeCfg parseScopeCfg(const std::string& params);
// k=v helpers used by the properties panel
std::string scopeGetParam(const std::string& params, const std::string& key, const std::string& def);
std::string scopeSetParam(const std::string& params, const std::string& key, const std::string& val);
std::string scopeJoinList(const std::vector<std::string>& v); // separates paths with "|"

// history of a Scope node (transient: sampled every step, not saved)
struct ScopeData {
    std::vector<std::deque<double>> ch;     // one queue per channel (socket)
    std::vector<std::string> names;         // channel label
    std::vector<SDL_Color> colors;          // channel color
    std::vector<const void*> srcId;         // list/matrix displayed per channel (identity)
    std::vector<int> rows, cols;            // shape of the list/matrix per channel (0 = scalar)
    std::shared_ptr<MapCache> map;                    // base geometry (transient)
    std::vector<std::shared_ptr<MapCache>> overlays;  // overlay layers (N)
    MapState mapState;                     // persisted view/selection/painting/annotations
    bool mapStateLoaded = false;           // already read the node's pstate?
    int tick = 0;                           // counter for decimation
    bool frozen = false;                    // frozen: does not sample
};

// one window per Scope node
struct ScopeWin {
    int nodeId = -1;
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    TextPainter painter;                    // uses the app font (shared)
    bool open = false;
    Uint32 winID = 0;
    int W = 660, H = 380;
    bool create(int id, const std::string& title, TTF_Font* font); // window + renderer
    void destroy();
    void setTitle(const std::string& t);
    void handleEvent(const SDL_Event& e, const ScopeCfg* cfg = nullptr,
                     ScopeData* data = nullptr,
                     const std::function<void()>* beforeEdit = nullptr);
};

// draws the node's chart (uses v.painter/v.ren). dtStep = time between samples.
void renderScope(ScopeWin& v, const ScopeCfg& cfg, ScopeData& d,
                 const std::string& title, double dtStep);

// draws the chart on any renderer/target (without present) — the window uses
// this function and PNG export renders into an offscreen texture target.
void drawScope(SDL_Renderer* r, const TextPainter& p, int W, int H, const ScopeCfg& cfg,
               const ScopeData& d, const std::string& title, double dtStep,
               MapView* mapView = nullptr);
// thumbnail (sparkline) drawn inside the node box on the canvas
void drawSparkline(SDL_Renderer* ren, SDL_Rect box,
                   const std::vector<std::deque<double>>& ch,
                   const std::vector<SDL_Color>& colors);
