// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Small SVG helpers shared by the diagram, Scope and map exporters.
#pragma once
#include <SDL2/SDL.h>
#include <string>

namespace svg {

std::string escape(const std::string& s);
std::string color(SDL_Color c);
std::string number(double v, const char* format = "%.2f");

} // namespace svg
