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



void mapHandleEvent(const SDL_Event& e, MapView& view, ScopeData& data, MapCache& cache,

                    int W, int H, const std::function<void()>* beforeEdit) {

    (void)W;

    (void)H;

    auto checkpoint = [&]() {

        if (!view.editCheckpointed && beforeEdit && *beforeEdit) {

            (*beforeEdit)();

            view.editCheckpointed = true;

        }

    };

    auto noPlot = [&](int x, int y) {

        return x < view.plotX || x >= view.plotX + view.plotW ||

               y < view.plotY || y >= view.plotY + view.plotH;

    };

    auto hitAny = [&](int x, int y, int& layer) -> std::string {

        layer = 0;

        if (!cache.loaded || noPlot(x, y)) return {};

        for (size_t li = data.overlays.size(); li-- > 0;) {

            const auto& mc = data.overlays[li];

            if (!mc || !mc->loaded) continue;

            const int h = mapFeatureAt(mc->map, view, x, y);

            if (h >= 0 && h < (int)mc->map.features.size()) {

                layer = (int)li + 1;

                return mc->map.features[(size_t)h].id;

            }

        }

        const int h = mapFeatureAt(cache.map, view, x, y);

        if (h >= 0 && h < (int)cache.map.features.size())

            return cache.map.features[(size_t)h].id;

        return {};

    };

    auto paintAt = [&](int x, int y) {

        int layer = 0;

        const std::string key = hitAny(x, y, layer);

        if (key.empty()) return;

        const bool remove = (SDL_GetModState() & KMOD_SHIFT) != 0;

        checkpoint();

        auto& ps = data.mapState.paints;

        for (auto it = ps.begin(); it != ps.end(); ++it)

            if (it->layer == layer && it->key == key) {

                if (remove) ps.erase(it);

                else {

                    it->hasColor = true;

                    it->hasValue = false;

                    it->color = paintColor(data.mapState.paintColor);

                }

                return;

            }

        if (!remove) {

            MapPaint p;

            p.layer = layer;

            p.key = key;

            p.hasColor = true;

            p.color = paintColor(data.mapState.paintColor);

            ps.push_back(std::move(p));

        }

    };

    auto addDraft = [&](int x, int y) {

        if (noPlot(x, y)) return;

        checkpoint();

        GeoPoint q;

        mapScreenToWorld(view, x, y, q.x, q.y);

        data.mapState.draft.push_back(q);

    };

    auto addMeasure = [&](int x, int y) {

        if (noPlot(x, y)) return;

        GeoPoint q;

        mapScreenToWorld(view, x, y, q.x, q.y);

        data.mapState.measure.push_back(q);

    };

    if (e.type == SDL_MOUSEMOTION) {

        view.mouseX = e.motion.x;

        view.mouseY = e.motion.y;

        if (view.dragging) {

            if (data.mapState.tool == 1) {

                paintAt(e.motion.x, e.motion.y);

            } else if (data.mapState.tool == 0) {

                const double dx = e.motion.x - view.dragX;

                const double dy = e.motion.y - view.dragY;

                view.cx -= dx / view.scale;

                view.cy += dy / view.scale;

                view.dragX = e.motion.x;

                view.dragY = e.motion.y;

                if (std::abs(e.motion.x - view.dragStartX) +

                        std::abs(e.motion.y - view.dragStartY) > 4)

                    view.dragMoved = true;

                view.needFit = false;

            } else if (data.mapState.tool == 4 && !noPlot(e.motion.x, e.motion.y)) {

                GeoPoint q;

                mapScreenToWorld(view, e.motion.x, e.motion.y, q.x, q.y);

                auto& ls = data.mapState.lasso;

                if (ls.empty() || std::hypot(q.x - ls.back().x, q.y - ls.back().y) *

                                       view.scale > 3.0)

                    ls.push_back(q);

                if (std::abs(e.motion.x - view.dragStartX) +

                        std::abs(e.motion.y - view.dragStartY) > 4)

                    view.dragMoved = true;

            }

        }

        if (!cache.loaded) view.hover = -1;

        else view.hover = mapFeatureAt(cache.map, view, view.mouseX, view.mouseY);

    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {

        if (!noPlot(e.button.x, e.button.y)) {

            view.dragging = true;

            view.editCheckpointed = false;

            view.dragX = view.dragStartX = e.button.x;

            view.dragY = view.dragStartY = e.button.y;

            view.dragMoved = false;

            view.mouseX = e.button.x;

            view.mouseY = e.button.y;

            if (data.mapState.tool == 4) { // starts the lasso

                data.mapState.lasso.clear();

                GeoPoint q;

                mapScreenToWorld(view, e.button.x, e.button.y, q.x, q.y);

                data.mapState.lasso.push_back(q);

            }

        }

    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {

        if (view.dragging) {

            view.dragging = false;

            const bool additive = (SDL_GetModState() & (KMOD_SHIFT | KMOD_CTRL)) != 0;

            if (data.mapState.tool == 4) { // closes the lasso and selects the features

                auto& ls = data.mapState.lasso;

                if (view.dragMoved && ls.size() >= 3) {

                    checkpoint();

                    if (!additive) data.mapState.clearSelection();

                    auto get = [&](const GeoMap& map, int layer) {

                        for (const auto& f : map.features)

                            if (featureInLasso(f, ls) &&

                                !data.mapState.selected(layer, f.id))

                                data.mapState.selection.push_back({layer, f.id});

                    };

                    get(cache.map, 0);

                    for (size_t li = 0; li < data.overlays.size(); ++li)

                        if (data.overlays[li] && data.overlays[li]->loaded)

                            get(data.overlays[li]->map, (int)li + 1);

                } else if (!view.dragMoved) { // simple click: same selection as the S tool

                    int layer = 0;

                    const std::string key = hitAny(e.button.x, e.button.y, layer);

                    checkpoint();

                    if (!key.empty())

                        data.mapState.toggleSelection(layer, key, additive);

                    else if (!additive)

                        data.mapState.clearSelection();

                }

                ls.clear();

            } else if (!view.dragMoved) {

                if (data.mapState.tool == 0) {

                    int layer = 0;

                    const std::string key = hitAny(e.button.x, e.button.y, layer);

                    checkpoint();

                    if (!key.empty())

                        data.mapState.toggleSelection(layer, key, additive);

                    else if (!additive)

                        data.mapState.clearSelection();

                } else if (data.mapState.tool == 1) {

                    paintAt(e.button.x, e.button.y);

                } else if (data.mapState.tool == 2) {

                    addDraft(e.button.x, e.button.y);

                } else if (data.mapState.tool == 3) {

                    addMeasure(e.button.x, e.button.y);

                }

            }

        }

    } else if (e.type == SDL_MOUSEWHEEL) {

        int mx = view.mouseX, my = view.mouseY;

        if (mx < 0 || my < 0) SDL_GetMouseState(&mx, &my);

        if (noPlot(mx, my)) return;

        const double beforeX = view.cx + ((double)mx - (view.plotX + view.plotW * 0.5)) / view.scale;

        const double prevViewY = view.cy - ((double)my - (view.plotY + view.plotH * 0.5)) / view.scale;

        const double f = std::pow(1.15, (double)e.wheel.y);

        view.scale = clampd(view.scale * f, 1e-12, 1e18);

        view.cx = beforeX - ((double)mx - (view.plotX + view.plotW * 0.5)) / view.scale;

        view.cy = prevViewY + ((double)my - (view.plotY + view.plotH * 0.5)) / view.scale;

        view.needFit = false;

        view.hover = cache.loaded ? mapFeatureAt(cache.map, view, mx, my) : -1;

    } else if (e.type == SDL_KEYDOWN) {

        const SDL_Keycode k = e.key.keysym.sym;

        if (k == SDLK_f) view.needFit = true;

        else if (k == SDLK_s) data.mapState.tool = 0;

        else if (k == SDLK_b) data.mapState.tool = 1;

        else if (k == SDLK_a) { data.mapState.tool = 2; data.mapState.draftType = MapAnnotation::LINE; }

        else if (k == SDLK_g) { data.mapState.tool = 2; data.mapState.draftType = MapAnnotation::POLYGON; }

        else if (k == SDLK_m) data.mapState.tool = 3;

        else if (k == SDLK_l) { data.mapState.tool = 4; data.mapState.lasso.clear(); }

        else if (k >= SDLK_1 && k <= SDLK_6) data.mapState.paintColor = k - SDLK_1;

        else if (k == SDLK_BACKSPACE && data.mapState.tool == 2 && !data.mapState.draft.empty())

            data.mapState.draft.pop_back();

        else if (k == SDLK_ESCAPE) {

            data.mapState.draft.clear();

            data.mapState.measure.clear();

            data.mapState.lasso.clear();

        } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {

            if (data.mapState.tool == 2 && data.mapState.draft.size() >= 2) {

                view.editCheckpointed = false; // the commit deserves its own snapshot

                checkpoint();

                MapAnnotation a;

                a.type = data.mapState.draftType;

                a.pts = data.mapState.draft;

                a.color = {255, 220, 90, 255};

                data.mapState.annotations.push_back(std::move(a));

                data.mapState.draft.clear();

            }

        }

    }

}