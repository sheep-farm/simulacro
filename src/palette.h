// simulacro — paleta de nós (wxTreeCtrl + busca)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <functional>
#include <map>
#include <string>

#include "engine.h"

// Native replacement for nodesim's Ctrl+K command palette: a searchable tree
// with the built-in nodes and every plugin type grouped by category.
class PalettePanel : public wxPanel {
public:
    PalettePanel(wxWindow* parent, Engine* engine);

    void rebuild();
    void focusSearch();

    // key: "script", "scope", "map", "join", "split" or a plugin type id.
    std::function<void(const std::string&)> onAdd;

private:
    wxTreeCtrl* tree_ = nullptr;
    wxTextCtrl* search_ = nullptr;
    wxStaticText* hint_ = nullptr;
    Engine* e_;

    wxString query_;

    void addItem(wxTreeItemId parent, const std::string& key, const std::string& label);
    void activate(wxTreeItemId id);
};
