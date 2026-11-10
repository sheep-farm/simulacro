// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal PNG writer (no zlib/libpng): 8 bits per channel, RGBA, no
// interlacing, with "stored" deflate blocks — the file is larger than the
// compressed one, but it is a valid, deterministic PNG with no dependencies.
#pragma once
#include <string>

// w×h RGBA pixels (row by row, 4 bytes per pixel). Returns the complete PNG
// file; empty string on invalid dimensions/null pointer.
std::string pngRgba(int w, int h, const unsigned char* rgba);
