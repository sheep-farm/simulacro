// simulacro — SDL off-screen raster canvas for the custom views
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdlcanvas.h"

#include <mutex>

namespace {
void initSdlOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        SDL_Init(0);
        TTF_Init();
    });
}

const char* kFonts[] = {
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
};
} // namespace

SdlCanvas::~SdlCanvas() { destroy(); }

void SdlCanvas::destroy() {
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    if (ren_) {
        SDL_DestroyRenderer(ren_);
        ren_ = nullptr;
    }
    if (surf_) {
        SDL_FreeSurface(surf_);
        surf_ = nullptr;
    }
    w_ = h_ = 0;
    painter_ = TextPainter{};
}

bool SdlCanvas::ensure(int w, int h, int fontPx) {
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    if (ren_ && surf_ && w_ == w && h_ == h && font_) return true;
    initSdlOnce();
    if (w_ != w || h_ != h || !surf_) {
        if (ren_) {
            SDL_DestroyRenderer(ren_);
            ren_ = nullptr;
        }
        if (surf_) {
            SDL_FreeSurface(surf_);
            surf_ = nullptr;
        }
        surf_ = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surf_) return false;
        ren_ = SDL_CreateSoftwareRenderer(surf_);
        if (!ren_) {
            SDL_FreeSurface(surf_);
            surf_ = nullptr;
            return false;
        }
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        w_ = w;
        h_ = h;
    }
    if (!font_) {
        for (const char* p : kFonts) {
            font_ = TTF_OpenFont(p, fontPx);
            if (font_) {
                fontPath_ = p;
                break;
            }
        }
    }
    painter_ = TextPainter{ren_, font_, 1.0f};
    return true;
}

void SdlCanvas::clear(SDL_Color c) {
    if (!ren_) return;
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(ren_);
}

void SdlCanvas::present(wxDC& dc) {
    if (!surf_ || !ren_) return;
    wxImage img(w_, h_);
    if (!img.IsOk()) return;
    SDL_ConvertPixels(w_, h_, SDL_PIXELFORMAT_ARGB8888, surf_->pixels, surf_->pitch,
                      SDL_PIXELFORMAT_RGB24, img.GetData(), w_ * 3);
    wxBitmap bmp(img);
    dc.DrawBitmap(bmp, 0, 0, false);
}

bool SdlCanvas::copyRgba(std::vector<unsigned char>& out) const {
    if (!surf_) return false;
    out.resize((size_t)w_ * h_ * 4);
    return SDL_ConvertPixels(w_, h_, SDL_PIXELFORMAT_ARGB8888, surf_->pixels, surf_->pitch,
                             SDL_PIXELFORMAT_RGBA32, out.data(), w_ * 4) == 0;
}
