// simulacro — native wxWidgets property editor
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <wx/wx.h>
#include <wx/propgrid/propgrid.h>
#include <functional>
#include <map>
#include <string>

#include "engine.h"

class PropertiesPanel : public wxPanel {
public:
    PropertiesPanel(wxWindow* parent, Engine* engine);

    void setNode(int id);
    int node() const { return nodeId_; }
    void rebuild();
    void applyTheme();

    std::function<void()> onChanged;
    std::function<void(int)> onEditScript;
    std::function<void(int)> onOpenScope;

private:
    void onChangedEvent(wxPropertyGridEvent& ev);
    wxString pgName();
    int pgSeq_ = 0;
    void addNodeCategory();
    void addScriptCategory();
    void addPluginCategory();
    void addScopeCategory();
    void addProp(wxPGProperty* p, std::function<void()> apply, const wxString& help = {});
    void commit();

    Engine* e_;
    wxPropertyGrid* pg_;
    wxButton* editBtn_ = nullptr;
    wxButton* scopeBtn_ = nullptr;
    int nodeId_ = -1;
    bool updating_ = false;
    std::map<wxPGProperty*, std::function<void()>> apply_;
};
