// simulacro — policy of where the node parameter widgets are drawn
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// See nodeui.h. This module keeps the env parse tiny and testable; nothing
// engine-side depends on it.
#include "nodeui.h"
#include <cstdlib>
#include <cstring>

namespace nodeui {
namespace {

Mode g_mode = Mode::Inner;
bool g_read = false;

} // namespace

Mode mode() {
    if (!g_read) {
        g_read = true;
        const char* e = std::getenv("SIMULACRO_NODE_UI");
        if (e && *e) {
            if (std::strcmp(e, "below") == 0) g_mode = Mode::Below;
            else if (std::strcmp(e, "off") == 0 || std::strcmp(e, "0") == 0) g_mode = Mode::Off;
            // "inner" (also the empty value) keeps the default
        }
    }
    return g_mode;
}

} // namespace nodeui
