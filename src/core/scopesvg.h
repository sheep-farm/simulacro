// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Scope chart export to SVG (no SDL, testable in the core):
// the same history the window draws, in vector coordinates — good for
// paper figures (infinite scale, selectable text). The Scope window and
// --run mode use this function; the PNG comes from the window's own renderer.
#pragma once
#include <string>
#include <vector>

// SVG frame/legend colors (the data background follows the theme chosen
// for the export). Curve colors remain those of the channels.
struct ScopeSvgTheme {
    int background[3] = {18, 18, 22};
    int panel[3] = {28, 28, 36};
    int grid[3] = {48, 48, 58};
    int text[3] = {200, 200, 205};
    int weak[3] = {130, 130, 140};
    int highlight[3] = {255, 220, 100};
};

struct ScopeSvgCfg {
    int W = 900, H = 480;    // SVG size in pixels
    ScopeSvgTheme theme;        // background/text colors (export theme)
    double window = 5.0;     // visible seconds (as in the Scope)
    double dt = 1.0 / 60.0;  // time between samples (already decimated)
    bool grid = true, legend = true, zero = true;
    bool yauto = true;
    double ymin = -1.0, ymax = 1.0; // used when yauto == false
    double scale = 1.0;            // gain applied to the values
};

struct ScopeSvgChannel {
    std::string name;
    int r = 160, g = 160, b = 160;
    std::vector<double> values; // from the oldest sample to the newest
};

// default color of channel k (same palette as the interface)
void scopeSvgColor(int k, int& r, int& g, int& b);

// Complete SVG (string) with axes, grid, curves and legend.
std::string scopeSvg(const std::vector<ScopeSvgChannel>& channels, const std::string& title,
                     const ScopeSvgCfg& cfg);
