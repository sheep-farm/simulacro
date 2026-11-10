// simulacro — janela principal (wxWidgets + wxAui)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// The wx front-end owns the frame, menus, toolbar, docks and dialogs. The
// engine (src/engine.*) is headless; the graph canvas and the scope charts
// still render through SDL into off-screen surfaces (src/sdlcanvas.*).
#pragma once

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/timer.h>

#include <string>

#include "engine.h"

class GraphCanvas;
class PropertiesPanel;
class PalettePanel;
class ScopeNotebook;

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

    bool loadFile(const std::string& path);
    void newDocument();

private:
    // chrome
    void buildMenu();
    void buildToolbar();
    void buildLayout();
    void bindUi();
    void applyTheme();
    void applyLanguage();
    void updateChecks();
    void updateTitle();
    void updateStatus();
    void setSimLabel();

    // commands
    void onTimer(wxTimerEvent&);
    void onNew(wxCommandEvent&);
    void onOpen(wxCommandEvent&);
    void onSave(wxCommandEvent&);
    void onSaveAs(wxCommandEvent&);
    void onQuit(wxCommandEvent&);
    void onClose(wxCloseEvent&);
    void onUndo(wxCommandEvent&);
    void onRedo(wxCommandEvent&);
    void onCut(wxCommandEvent&);
    void onCopy(wxCommandEvent&);
    void onPaste(wxCommandEvent&);
    void onDuplicate(wxCommandEvent&);
    void onDelete(wxCommandEvent&);
    void onSelectAll(wxCommandEvent&);
    void onGroup(wxCommandEvent&);
    void onUngroup(wxCommandEvent&);
    void onAddKey(const std::string& key);
    void onAddScript(wxCommandEvent&);
    void onAddScope(wxCommandEvent&);
    void onAddMap(wxCommandEvent&);
    void onAddJoin(wxCommandEvent&);
    void onAddSplit(wxCommandEvent&);
    void onSimToggle(wxCommandEvent&);
    void onSimStep(wxCommandEvent&);
    void onSimReset(wxCommandEvent&);
    void onSimClear(wxCommandEvent&);
    void onConverge(wxCommandEvent&);
    void onEvents(wxCommandEvent&);
    void onRate(wxCommandEvent&);
    void onIterations(wxCommandEvent&);
    void onShowValues(wxCommandEvent&);
    void onGrid(wxCommandEvent&);
    void onCurve(wxCommandEvent&);
    void onZoomIn(wxCommandEvent&);
    void onZoomOut(wxCommandEvent&);
    void onZoomReset(wxCommandEvent&);
    void onCenter(wxCommandEvent&);
    void onTogglePane(wxCommandEvent&);
    void onSaveInitial(wxCommandEvent&);
    void onExportScope(wxCommandEvent&);
    void onLang(wxCommandEvent&);
    void onReloadTheme(wxCommandEvent&);
    void onShortcuts(wxCommandEvent&);
    void onAbout(wxCommandEvent&);

    void openScope(int id);
    void editScript(int id);
    void documentChanged(bool scopes = true);
    void refreshEverything();
    int askSave();

    Engine engine_;
    wxAuiManager aui_;

    GraphCanvas* canvas_ = nullptr;
    PropertiesPanel* props_ = nullptr;
    PalettePanel* palette_ = nullptr;
    ScopeNotebook* scopes_ = nullptr;

    wxTimer timer_;
    wxMenuItem* simItem_ = nullptr;
    long long lastPaintTick_ = 0;
    int ticks_ = 0;

    wxString startupMessage_;
};
