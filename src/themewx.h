// simulacro — ponte entre a paleta do tema (core) e o wxWidgets
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// The chart/canvas renderers already consume the kUi globals published by
// tema.cpp. Here we translate the same palette to wxColour so the native
// chrome (panels, tabs, property grid) follows the Omarchy theme too.
#pragma once

#include <wx/wx.h>
#include "tema.h"

inline wxColour wxColourOf(SDL_Color c) {
    return wxColour(c.r, c.g, c.b, c.a);
}

inline wxColour wxColourOf(unsigned c) {
    return wxColour((unsigned char)((c >> 24) & 0xFF), (unsigned char)((c >> 16) & 0xFF),
                    (unsigned char)((c >> 8) & 0xFF), (unsigned char)(c & 0xFF));
}

inline void themeWindow(wxWindow* w) {
    if (!w) return;
    w->SetBackgroundColour(wxColourOf(kUi.panel));
    w->SetForegroundColour(wxColourOf(kUi.text));
}

inline void themeWindowDeep(wxWindow* w) {
    if (!w) return;
    themeWindow(w);
    for (wxWindow* c : w->GetChildren()) themeWindowDeep(c);
    w->Refresh(false);
}
