// simulacro — SDL off-screen raster canvas for the custom views
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// SDL is used here for what wxWidgets is not: a small immediate-mode drawing
// API with the node/scope renderers inherited from nodesim. Everything is
// rendered into an in-memory surface and then blitted into a wxDC.
#pragma once

#include <wx/wx.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include "draw.h"

class SdlCanvas {
public:
    SdlCanvas() = default;
    ~SdlCanvas();

    bool ensure(int w, int h, int fontPx = 14);
    void destroy();
    void clear(SDL_Color c);
    void present(wxDC& dc);

    SDL_Renderer* ren() const { return ren_; }
    SDL_Surface* surface() const { return surf_; }
    TextPainter& painter() { return painter_; }
    const TextPainter& painter() const { return painter_; }
    int width() const { return w_; }
    int height() const { return h_; }
    bool ok() const { return ren_ != nullptr; }

    // Raw RGBA copy of the last frame (for PNG export).
    bool copyRgba(std::vector<unsigned char>& out) const;

private:
    SDL_Surface* surf_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    TTF_Font* font_ = nullptr;
    TextPainter painter_{};
    int w_ = 0, h_ = 0;
    std::string fontPath_;
};
