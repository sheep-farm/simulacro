// simulacro — painel de Scopes (wxAuiNotebook + raster SDL)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <wx/wx.h>
#include <wx/aui/auibook.h>
#include <vector>

#include "engine.h"
#include "sdlcanvas.h"

class ScopeView : public wxPanel {
public:
    ScopeView(wxWindow* parent, Engine* engine, int nodeId);

    int nodeId() const { return id_; }
    void tick();
    void exportSvg();
    void exportPng();
    void toggleFreeze();
    void clearHistory();

private:
    void onPaint(wxPaintEvent&);
    void onRightDown(wxMouseEvent&);
    void onLeftDown(wxMouseEvent&);
    void onMotion(wxMouseEvent&);
    void onLeftUp(wxMouseEvent&);
    void onWheel(wxMouseEvent&);
    void onDoubleClick(wxMouseEvent&);

    Engine* e_;
    int id_;
    SdlCanvas bmp_;
    bool dragging_ = false;
    wxPoint dragStart_;
    double dragCx_ = 0, dragCy_ = 0;
};

class ScopeNotebook : public wxAuiNotebook {
public:
    ScopeNotebook(wxWindow* parent, Engine* engine);

    void openScope(int id);
    void closeScope(int id);
    void refreshAll();
    void refreshNode(int id);
    void prune();
    ScopeView* find(int id) const;
    ScopeView* currentView() const;
    void closeAll();

private:
    Engine* e_;
};
