// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Omarchy theme: reads the colors.toml of the theme in use and publishes a
// semantic palette for the interface. No SDL at runtime (only the SDL_Color
// type), so it can be tested in the core.
#pragma once
#include <SDL2/SDL.h>
#include <string>

struct ThemeColors {
    bool omarchy = false;
    std::string name = "nodesim";
    std::string path;      // colors.toml read
    SDL_Color background{18, 18, 22, 255};       // canvas
    SDL_Color darkBackground{14, 14, 18, 255}; // bars/window background
    SDL_Color panel{28, 28, 36, 255};      // panels and modals
    SDL_Color box{26, 26, 32, 255};       // node body
    SDL_Color edge{74, 74, 94, 255};
    SDL_Color strongEdge{100, 100, 130, 255};
    SDL_Color grid{32, 32, 40, 255};
    SDL_Color strongGrid{48, 48, 58, 255};
    SDL_Color text{200, 200, 205, 255};
    SDL_Color weak{130, 130, 140, 255};
    SDL_Color strong{235, 235, 245, 255};
    SDL_Color section{140, 150, 185, 255};
    SDL_Color label{165, 165, 178, 255};
    SDL_Color highlight{255, 220, 100, 255};
    SDL_Color hover{48, 48, 60, 255};
    SDL_Color active{46, 40, 62, 255};
    SDL_Color error{255, 120, 120, 255};
    SDL_Color warning{255, 200, 90, 255};
    SDL_Color ok{140, 255, 150, 255};
    SDL_Color textStr{255, 205, 120, 255};
};

const ThemeColors& theme();
bool themeInit();                     // loads; true if an Omarchy theme was found
bool themePoll(bool force = false);  // re-reads if the file changed; true if applied
void themeApply();                   // publishes the palette to the draw.h globals
void themeApplyColors(const ThemeColors& t); // publishes a specific palette
std::string themeDescription();         // "Terminus (Omarchy)" or "nodesim (default)"

// Theme used in EXPORTED FIGURES (diagram, Scope, map):
//   0 = current (Omarchy when present; otherwise the nodesim default)
//   1 = nodesim default
//   2 = Omarchy (falls back to the default if absent)
enum ThemeExportMode { THEME_EXPORT_CURRENT = 0, THEME_EXPORT_DEFAULT = 1, THEME_EXPORT_OMARCHY = 2 };
int themeExportMode();
void themeExportModeSet(int mode);
const char* themeExportName(int mode); // "current", "default", "Omarchy"
ThemeColors defaultTheme();
ThemeColors themeExport();
