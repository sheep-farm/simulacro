// simulacro — node graph canvas (wxPanel + SDL off-screen renderer)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <wx/wx.h>
#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "engine.h"
#include "sdlcanvas.h"

class GraphCanvas : public wxPanel {
public:
    explicit GraphCanvas(wxWindow* parent, Engine* engine);

    const std::set<int>& selection() const { return sel_; }
    int singleSelection() const {
        return sel_.size() == 1 ? *sel_.begin() : -1;
    }
    void selectNode(int id, bool add = false);
    void selectAll();
    void clearSelection();
    void notifySelection();

    void deleteSelection();
    void copySelection();
    void pasteClipboard();
    void duplicateSelection();

    void zoomBy(float factor, wxPoint anchor);
    void zoomReset();
    void centerOnGraph();
    void refreshFromModel();

    void setShowValues(bool v) { showVals_ = v; Refresh(false); }
    bool showValues() const { return showVals_; }
    void setGrid(bool v) { grid_ = v; Refresh(false); }
    bool grid() const { return grid_; }
    void setCurveWires(bool v) { curve_ = v; Refresh(false); }
    bool curveWires() const { return curve_; }

    wxPoint spawnWorld() const;  // graph coordinates at the view centre
    wxPoint mouseWorld() const;

    std::function<void()> onSelectionChanged;
    std::function<void()> onDocumentChanged;
    std::function<void(int)> onOpenScope;
    std::function<void(int)> onEditScript;
    std::function<void(const std::string&)> onMessage;

private:
    struct ClipNode {
        Node node;
        std::string blob;
    };

    void onPaint(wxPaintEvent&);
    void onSize(wxSizeEvent&);
    void onLeftDown(wxMouseEvent&);
    void onLeftUp(wxMouseEvent&);
    void onMotion(wxMouseEvent&);
    void onRightDown(wxMouseEvent&);
    void onMiddleDown(wxMouseEvent&);
    void onMiddleUp(wxMouseEvent&);
    void onMouseWheel(wxMouseEvent&);
    void onDoubleClick(wxMouseEvent&);
    void onKeyDown(wxKeyEvent&);
    void onEraseBackground(wxEraseEvent&) {}
    void onCaptureLost(wxMouseCaptureLostEvent&);

    void render();
    void drawGrid();
    void drawGroups();
    void drawEdges();
    void drawNodes();
    void drawPending();
    void drawRubber();

    Vec2 w2s(Vec2 w) const {
        return {(w.x - camX_) * zoom_, (w.y - camY_) * zoom_};
    }
    Vec2 s2w(Vec2 s) const {
        return {s.x / zoom_ + camX_, s.y / zoom_ + camY_};
    }
    int nodeAt(Vec2 world) const;
    bool portAt(Vec2 world, int& node, int& port, bool& input) const;
    bool wireAt(Vec2 screen, int& edge) const;
    bool nodeHidden(int id) const;
    FRect groupRect(const Group& gr) const;
    int groupHeaderAt(Vec2 world) const;
    int groupBodyAt(Vec2 world) const;
    std::string portLabel(const Node& n, int port, bool input) const;
    std::string oneLine(const std::string& s) const;
    void connectPending(Vec2 world);
    void startMove(Vec2 world);
    void applyMove(Vec2 world);
    void endMove();
    void addNodeContext(int kind, const std::string& plugin, Vec2 world);
    void openNode(int id);
    void invalidateAll();

    Engine* e_;
    SdlCanvas bmp_;

    float zoom_ = 1.0f;
    float camX_ = 0, camY_ = 0;
    bool showVals_ = false;
    bool grid_ = true;
    bool curve_ = true;

    std::set<int> sel_;
    std::set<std::tuple<int, int, int, int>> selWires_;
    int hoverNode_ = -1;
    int hoverWire_ = -1;

    wxPoint mouse_{0, 0};
    Vec2 mouseWorld_{0, 0};

    // connection gesture
    int pendingFrom_ = -1;   // node id of the armed port
    int pendingPort_ = 0;
    bool pendingInput_ = false;
    bool pending_ = false;

    // node move
    bool moving_ = false;
    Vec2 moveStart_{0, 0};
    std::vector<std::pair<int, Vec2>> moveOrig_;
    int moveGroup_ = -1;
    Vec2 groupOrig_{0, 0};

    // rubber band
    bool rubber_ = false;
    Vec2 rubberStart_{0, 0};
    Vec2 rubberCur_{0, 0};

    // pan
    bool panning_ = false;
    wxPoint panStart_;
    float panCamX_ = 0, panCamY_ = 0;

    std::vector<ClipNode> clip_;
    std::vector<Edge> clipEdges_;
    int pasteOffset_ = 0;

    bool dragging_ = false;
    bool moved_ = false;
};
