// simulacro — Dear ImGui shell: SDL/Atlas + paleta + eventos (canvas de nós)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Scope: this shell serves the **node canvas only**. Scope charts, maps and
// the native wxWidgets chrome are untouched by design. The SDL software
// renderer is already in use, so the renderer backend (imgui_impl_sdlrenderer2)
// renders on the same software surface the graph renderer draws into.
#include "imgui_shell.h"

#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"
#include "draw.h"
#include "tema.h"

#include <SDL2/SDL_ttf.h>
#include <wx/gdicmn.h>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace imui {
namespace {

bool g_enabled = true;   // read once; SIMULACRO_IMGUI=0/off disables early
SDL_Renderer* g_ren = nullptr;  // renderer the backend is attached to

const char* kFonts[] = {
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
};

ImVec4 v4Of(const SDL_Color& c) {
    return {(float)c.r / 255.0f, (float)c.g / 255.0f, (float)c.b / 255.0f,
            (float)c.a / 255.0f};
}

} // namespace

bool enabled() {
    static int read = 0;
    if (!read) {
        read = 1;
        const char* e = std::getenv("SIMULACRO_IMGUI");
        if (e && (!*e || *e == '0' || std::strcmp(e, "off") == 0)) g_enabled = false;
    }
    return g_enabled;
}

bool init(SDL_Renderer* ren) {
    if (!enabled()) return false;
    if (!ImGui::GetCurrentContext()) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;          // do not touch the user's disk layout
        io.DisplayFramebufferScale = ImVec2(1, 1);
        if (ren) g_ren = ren;
        if (ren && !ImGui_ImplSDLRenderer2_Init(ren)) {
            ImGui::DestroyContext();
            g_ren = nullptr;
            return false;
        }
        // Font: same monospace family the core canvas uses, at the same size.
        const int px = 14;
        ImFont* f = nullptr;
        for (const char* p : kFonts) {
            ImFontConfig cfg;
            cfg.OversampleH = 2;
            cfg.OversampleV = 2;
            f = io.Fonts->AddFontFromFileTTF(p, (float)px, &cfg,
                                             io.Fonts->GetGlyphRangesDefault());
            if (f) break;
        }
        if (!f) f = io.Fonts->AddFontDefault();
        theme();
    }
    g_ren = ren ? ren : g_ren;
    return true;
}

void renChanged(SDL_Renderer* ren) {
    // The off-screen canvas rebuilds its SDL_Renderer on pane resize; the font
    // atlas texture dies with the old renderer, so rebuild the whole shell
    // (context + backend + fonts). Rare, only on real geometry changes.
    if (!enabled()) return;
    if (ImGui::GetCurrentContext()) shutdown();
    if (init(ren)) g_ren = ren;
}

void shutdown() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
}

void theme() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 5.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.ScrollbarSize = 12.0f;
    s.ItemSpacing = ImVec2(6, 3);
    s.FramePadding = ImVec2(5, 2);
    s.WindowPadding = ImVec2(6, 5);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = v4Of(kUi.darkBackground);
    c[ImGuiCol_Border] = v4Of(kUi.edge);
    c[ImGuiCol_Text] = v4Of(kUi.text);
    c[ImGuiCol_TextDisabled] = v4Of(kUi.weak);
    c[ImGuiCol_FrameBg] = v4Of(kUi.panel);
    c[ImGuiCol_FrameBgHovered] = v4Of(kUi.hover);
    c[ImGuiCol_FrameBgActive] = v4Of(kUi.active);
    c[ImGuiCol_TitleBg] = v4Of(kUi.panel);
    c[ImGuiCol_TitleBgActive] = v4Of(kUi.active);
    c[ImGuiCol_CheckMark] = v4Of(kUi.ok);
    c[ImGuiCol_SliderGrab] = v4Of(kUi.ok);
    c[ImGuiCol_SliderGrabActive] = v4Of(kUi.highlight);
    c[ImGuiCol_Button] = v4Of(kUi.panel);
    c[ImGuiCol_ButtonHovered] = v4Of(kUi.hover);
    c[ImGuiCol_ButtonActive] = v4Of(kUi.active);
    c[ImGuiCol_PlotHistogram] = v4Of(kUi.section);
}

void newFrame(int w, int h) {
    if (!ImGui::GetCurrentContext()) return;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (dt > 1.0f || dt <= 0) dt = 1.0f / 60.0f;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w < 1 ? 1 : (float)w, (float)h < 1 ? 1 : (float)h);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = dt;
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui::NewFrame();
}

void newFrameEnd() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::Render();
}

// Manual render loop (mirrors imgui_impl_sdlrenderer2) with per-command error
// reporting; also renders cmd lists with a valid SDL texture handle from the
// atlas platform IO instead of trusting ImDrawCmd::GetTexID.
static void renderDrawDataManual(ImDrawData* dd, SDL_Renderer* ren, bool verbose) {
    if (!dd || dd->TotalIdxCount == 0) return;
    struct Backup { SDL_Rect vp; bool clip; SDL_Rect cr; } old{};
    old.clip = SDL_RenderIsClipEnabled(ren) == SDL_TRUE;
    SDL_RenderGetViewport(ren, &old.vp);
    SDL_RenderGetClipRect(ren, &old.cr);
    SDL_RenderSetViewport(ren, nullptr);
    SDL_RenderSetClipRect(ren, nullptr);

    SDL_Texture* atlas = nullptr;
    if (dd->Textures) {
        for (ImTextureData* tex : *dd->Textures) {
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplSDLRenderer2_UpdateTexture(tex);   // vendored upload
            if (atlas == nullptr) atlas = (SDL_Texture*)(intptr_t)tex->GetTexID();
        }
    }

    for (const ImDrawList* dl : dd->CmdLists) {
        for (const ImDrawCmd& cmd : dl->CmdBuffer) {
            if (cmd.UserCallback != nullptr) continue;
            ImVec2 cl(cmd.ClipRect.x - dd->DisplayPos.x, cmd.ClipRect.y - dd->DisplayPos.y);
            ImVec2 cr(cmd.ClipRect.z - dd->DisplayPos.x, cmd.ClipRect.w - dd->DisplayPos.y);
            if (cl.x < 0) cl.x = 0;
            if (cl.y < 0) cl.y = 0;
            if (cr.x > (float)dd->DisplaySize.x) cr.x = (float)dd->DisplaySize.x;
            if (cr.y > (float)dd->DisplaySize.y) cr.y = (float)dd->DisplaySize.y;
            if (cr.x <= cl.x || cr.y <= cl.y) continue;
            SDL_Rect r{(int)cl.x, (int)cl.y, (int)(cr.x - cl.x), (int)(cr.y - cl.y)};
            SDL_RenderSetClipRect(ren, &r);
            const float* xy = (const float*)(const void*)((const char*)
                (dl->VtxBuffer.Data + cmd.VtxOffset) + offsetof(ImDrawVert, pos));
            const float* uv = (const float*)(const void*)((const char*)
                (dl->VtxBuffer.Data + cmd.VtxOffset) + offsetof(ImDrawVert, uv));
            const SDL_Color* col = (const SDL_Color*)(const void*)((const char*)
                (dl->VtxBuffer.Data + cmd.VtxOffset) + offsetof(ImDrawVert, col));
            SDL_Texture* tex = (SDL_Texture*)(intptr_t)cmd.GetTexID();
            if (!tex) tex = atlas;
            SDL_ClearError();
            int rc = SDL_RenderGeometryRaw(ren, tex, xy, sizeof(ImDrawVert), col,
                                           sizeof(ImDrawVert), uv, sizeof(ImDrawVert),
                                           dl->VtxBuffer.Size - cmd.VtxOffset,
                                           dl->IdxBuffer.Data + cmd.IdxOffset,
                                           cmd.ElemCount, sizeof(ImDrawIdx));
            if (verbose)
                std::fprintf(stderr, "man: tex=%p elem=%d clip=(%d,%d %d,%d) rc=%d err='%s'\n",
                             (void*)tex, (int)cmd.ElemCount, r.x, r.y, r.w, r.h, rc,
                             SDL_GetError());
        }
    }
    SDL_RenderSetViewport(ren, &old.vp);
    SDL_RenderSetClipRect(ren, old.clip ? &old.cr : nullptr);
}

void renderFrame(SDL_Renderer* ren) {
    if (!ImGui::GetCurrentContext()) return;
    const bool dbg = std::getenv("SIMULACRO_IMGUI_DEBUG") != nullptr;
    ImDrawData* dd = ImGui::GetDrawData();
    renderDrawDataManual(dd, ren, dbg);
}

// ------------------------------------------------------------------ events

void mouseMove(int x, int y) {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::GetIO().AddMousePosEvent((float)x, (float)y);
}

void mouseLeaveWindow() {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

void mouseButton(int button, bool down) {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::GetIO().AddMouseButtonEvent(button, down);
}

void mouseWheel(float dx, float dy) {
    if (!ImGui::GetCurrentContext()) return;
    ImGui::GetIO().AddMouseWheelEvent(dx, dy);
}

void key(int key, bool down) {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (key) {
        case WXK_SHIFT: io.AddKeyEvent(ImGuiKey_LeftShift, down); break;
        case WXK_CONTROL: io.AddKeyEvent(ImGuiKey_LeftCtrl, down); break;
        case WXK_ALT: io.AddKeyEvent(ImGuiKey_LeftAlt, down); break;
        case WXK_LEFT: io.AddKeyEvent(ImGuiKey_LeftArrow, down); break;
        case WXK_RIGHT: io.AddKeyEvent(ImGuiKey_RightArrow, down); break;
        case WXK_UP: io.AddKeyEvent(ImGuiKey_UpArrow, down); break;
        case WXK_DOWN: io.AddKeyEvent(ImGuiKey_DownArrow, down); break;
        case WXK_BACK: io.AddKeyEvent(ImGuiKey_Backspace, down); break;
        case WXK_DELETE: io.AddKeyEvent(ImGuiKey_Delete, down); break;
        case WXK_HOME: io.AddKeyEvent(ImGuiKey_Home, down); break;
        case WXK_END: io.AddKeyEvent(ImGuiKey_End, down); break;
        case WXK_TAB: io.AddKeyEvent(ImGuiKey_Tab, down); break;
        case WXK_ESCAPE: io.AddKeyEvent(ImGuiKey_Escape, down); break;
        case WXK_RETURN: case WXK_NUMPAD_ENTER:
            io.AddKeyEvent(ImGuiKey_Enter, down); break;
        case WXK_SPACE: io.AddKeyEvent(ImGuiKey_Space, down); break;
        default:
            if (key > 0 && key < 256 && std::isalnum((unsigned char)key))
                io.AddKeyEvent(ImGuiKey(key), down);
            break;
    }
}

void textUtf8(const char* utf8) {
    if (!ImGui::GetCurrentContext() || !utf8) return;
    ImGui::GetIO().AddInputCharactersUTF8(utf8);
}

// ------------------------------------------------------------------ queries

bool wantsMouseNow() {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;
}

bool wantsTextInputNow() {
    return ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
}

bool wantsKeyboardNow() {
    if (!ImGui::GetCurrentContext()) return false;
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard || io.WantTextInput;
}

bool anyWidgetHot() {
    if (!ImGui::GetCurrentContext()) return false;
    return ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered();
}

} // namespace imui

// Fallback manual render loop placeholder (no-op; the vendored backend stays
// the source of truth until the manual renderer proves itself).
