// simulacro — Dear ImGui shell (widgets sobre o canvas de nós)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal Dear ImGui shim for the off-screen SDL canvas. The shell owns five
// things and deliberately nothing else (no SDL event pump, no window, no
// docking):
//   1. context creation/shutdown and font loading;
//   2. mapping of the project palette (tema.h kUi) to ImGuiStyle;
//   3. feeding wx-side mouse and key events into ImGuiIO;
//   4. NewFrame/Render driven by the canvas paint cycle;
//   5. a kill switch: SIMULACRO_IMGUI=0 (or `off`) disables everything early.
#pragma once

#include <SDL2/SDL.h>

namespace imui {

bool enabled();                       // false if SIMULACRO_IMGUI=0/off
bool init(SDL_Renderer* ren);
void renChanged(SDL_Renderer* ren);   // full shell rebuild (atlas + backend)
void shutdown();                      // context teardown
void theme();                         // re-apply the palette (kUi) to ImGuiStyle
void newFrame(int w, int h);          // sets display size + dt, then NewFrame()
void renderFrame(SDL_Renderer* ren);  // Render() + atlas blit (no swap)
void newFrameEnd();                   // to be called at the end of a paint cycle

// --- event feed (from the wx canvas handlers) ---
void mouseMove(int x, int y);
void mouseLeaveWindow();              // invalidate mouse position
void mouseButton(int button, bool down);
void mouseWheel(float dx, float dy);
void key(int key, bool down);         // key + modifiers keys
void textUtf8(const char* utf8);      // typed characters (wxEVT_CHAR)

// --- hit queries, used by the canvas before acting on native events ---
bool wantsMouseNow();                 // io.WantCaptureMouse (excluding window)
bool wantsTextInputNow();             // io.WantTextInput
bool wantsKeyboardNow();              // io.WantCaptureKeyboard (nav/focus)
bool anyWidgetHot();                  // any window active/held by the mouse

} // namespace imui
