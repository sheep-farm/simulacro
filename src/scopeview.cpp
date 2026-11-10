// simulacro — painel de Scopes (wxAuiNotebook + raster SDL)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scopeview.h"
#include "i18n.h"
#include "png.h"
#include "themewx.h"

#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
wxString tr(const char* s) { return wxString::FromUTF8(i18n::text(s)); }

std::string scopeTitle(Engine* e, int id) {
    const Node* n = e->g.find(id);
    if (!n) return "Scope #" + std::to_string(id);
    return e->nodeLabel(*n);
}

double dtStepOf(Engine* e, const ScopeCfg& c) {
    return e->dt * (c.dec < 1 ? 1.0 : (double)c.dec);
}
} // namespace

ScopeView::ScopeView(wxWindow* parent, Engine* engine, int nodeId)
    : wxPanel(parent, wxID_ANY), e_(engine), id_(nodeId) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(wxColourOf(kUi.background));
    Bind(wxEVT_PAINT, &ScopeView::onPaint, this);
    Bind(wxEVT_RIGHT_DOWN, &ScopeView::onRightDown, this);
    Bind(wxEVT_LEFT_DOWN, &ScopeView::onLeftDown, this);
    Bind(wxEVT_MOTION, &ScopeView::onMotion, this);
    Bind(wxEVT_LEFT_UP, &ScopeView::onLeftUp, this);
    Bind(wxEVT_MOUSEWHEEL, &ScopeView::onWheel, this);
    Bind(wxEVT_LEFT_DCLICK, &ScopeView::onDoubleClick, this);
    if (const Node* n = e_->g.find(id_))
        e_->ensureMapState(*n, e_->scopeData[id_]);
}

void ScopeView::onPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const wxSize sz = GetClientSize();
    if (!bmp_.ensure(sz.x, sz.y)) return;
    Node* n = e_->g.find(id_);
    if (!n) return;
    const ScopeCfg cfg = parseScopeCfg(n->params);
    ScopeData& d = e_->scopeData[id_];
    e_->ensureMapState(*n, d);
    e_->ensureScopeMap(d, cfg);
    drawScope(bmp_.ren(), bmp_.painter(), sz.x, sz.y, cfg, d, scopeTitle(e_, id_),
              dtStepOf(e_, cfg), &d.mapState.view);
    bmp_.present(dc);
}

void ScopeView::tick() { Refresh(false); }

void ScopeView::toggleFreeze() {
    ScopeData& d = e_->scopeData[id_];
    d.frozen = !d.frozen;
    Refresh(false);
}

void ScopeView::clearHistory() {
    ScopeData& d = e_->scopeData[id_];
    for (auto& q : d.ch) q.clear();
    d.tick = 0;
    Refresh(false);
}

void ScopeView::exportSvg() {
    wxFileDialog dlg(this, tr("Export Scope as SVG"), wxEmptyString,
                     scopeTitle(e_, id_) + ".svg", "SVG (*.svg)|*.svg",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    std::string err;
    if (!e_->exportScopeSvg(id_, dlg.GetPath().utf8_string(), err))
        wxMessageBox(wxString::FromUTF8(err), tr("Export failed"), wxOK | wxICON_ERROR, this);
}

void ScopeView::exportPng() {
    wxFileDialog dlg(this, tr("Export Scope as PNG"), wxEmptyString,
                     scopeTitle(e_, id_) + ".png", "PNG (*.png)|*.png",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    Node* n = e_->g.find(id_);
    if (!n) return;
    const ScopeCfg cfg = parseScopeCfg(n->params);
    ScopeData& d = e_->scopeData[id_];
    e_->ensureMapState(*n, d);
    e_->ensureScopeMap(d, cfg);
    SdlCanvas out;
    const int W = 1200, H = 620;
    if (!out.ensure(W, H)) return;
    drawScope(out.ren(), out.painter(), W, H, cfg, d, scopeTitle(e_, id_),
              dtStepOf(e_, cfg), &d.mapState.view);
    std::vector<unsigned char> rgba;
    if (!out.copyRgba(rgba)) return;
    const std::string png = pngRgba(W, H, rgba.data());
    std::ofstream f(dlg.GetPath().utf8_string(), std::ios::binary);
    if (!f) {
        wxMessageBox(tr("Could not write the file."), tr("Export failed"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    f.write(png.data(), (std::streamsize)png.size());
}

void ScopeView::onRightDown(wxMouseEvent& ev) {
    enum : int { M_SVG = 7001, M_PNG, M_CLEAR, M_FREEZE, M_CLOSE };
    wxMenu menu;
    menu.Append(M_SVG, tr("Export SVG…"));
    menu.Append(M_PNG, tr("Export PNG…"));
    menu.AppendSeparator();
    menu.Append(M_CLEAR, tr("Clear history"));
    menu.Append(M_FREEZE, e_->scopeData[id_].frozen ? tr("Unfreeze") : tr("Freeze"));
    menu.AppendSeparator();
    menu.Append(M_CLOSE, tr("Close"));
    const int choice = GetPopupMenuSelectionFromUser(menu, ev.GetPosition());
    switch (choice) {
        case M_SVG: exportSvg(); break;
        case M_PNG: exportPng(); break;
        case M_CLEAR: clearHistory(); break;
        case M_FREEZE: toggleFreeze(); break;
        case M_CLOSE: {
            auto* nb = dynamic_cast<ScopeNotebook*>(GetParent());
            if (nb) nb->closeScope(id_);
            break;
        }
        default: break;
    }
}

// --- map navigation (other chart types ignore the mouse) ---
void ScopeView::onLeftDown(wxMouseEvent& ev) {
    Node* n = e_->g.find(id_);
    if (!n || parseScopeCfg(n->params).type != ScopeType::MAP) return;
    MapView& v = e_->scopeData[id_].mapState.view;
    v.needFit = false;
    dragging_ = true;
    dragStart_ = ev.GetPosition();
    dragCx_ = v.cx;
    dragCy_ = v.cy;
    CaptureMouse();
}

void ScopeView::onMotion(wxMouseEvent& ev) {
    if (!dragging_) return;
    Node* n = e_->g.find(id_);
    if (!n) return;
    MapView& v = e_->scopeData[id_].mapState.view;
    const double sc = v.scale != 0 ? v.scale : 1.0;
    const wxPoint d = ev.GetPosition() - dragStart_;
    v.cx = dragCx_ - d.x / sc;
    v.cy = dragCy_ - d.y / sc;
    Refresh(false);
}

void ScopeView::onLeftUp(wxMouseEvent&) {
    if (HasCapture()) ReleaseMouse();
    dragging_ = false;
}

void ScopeView::onWheel(wxMouseEvent& ev) {
    Node* n = e_->g.find(id_);
    if (!n || parseScopeCfg(n->params).type != ScopeType::MAP) {
        ev.Skip();
        return;
    }
    MapView& v = e_->scopeData[id_].mapState.view;
    const int rot = ev.GetWheelRotation();
    if (!rot) return;
    const double f = std::pow(1.15, rot / 120.0);
    v.needFit = false;
    v.scale = std::max(1e-6, v.scale * f);
    Refresh(false);
}

void ScopeView::onDoubleClick(wxMouseEvent&) {
    Node* n = e_->g.find(id_);
    if (!n || parseScopeCfg(n->params).type != ScopeType::MAP) return;
    e_->scopeData[id_].mapState.view.needFit = true;
    Refresh(false);
}

// ---------------------------------------------------------------- notebook

ScopeNotebook::ScopeNotebook(wxWindow* parent, Engine* engine)
    : wxAuiNotebook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                    wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS |
                        wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_WINDOWLIST_BUTTON),
      e_(engine) {
    SetBackgroundColour(wxColourOf(kUi.darkBackground));
}

ScopeView* ScopeNotebook::find(int id) const {
    for (size_t i = 0; i < GetPageCount(); ++i) {
        auto* v = dynamic_cast<ScopeView*>(GetPage(i));
        if (v && v->nodeId() == id) return v;
    }
    return nullptr;
}

ScopeView* ScopeNotebook::currentView() const {
    return dynamic_cast<ScopeView*>(GetCurrentPage());
}

void ScopeNotebook::openScope(int id) {
    if (ScopeView* v = find(id)) {
        SetSelection(GetPageIndex(v));
        v->SetFocus();
        return;
    }
    const Node* n = e_->g.find(id);
    if (!n || n->type != NodeType::SCOPE) return;
    auto* v = new ScopeView(this, e_, id);
    AddPage(v, wxString::FromUTF8(scopeTitle(e_, id)), true);
    SetSelection(GetPageCount() - 1);
}

void ScopeNotebook::closeScope(int id) {
    if (ScopeView* v = find(id)) {
        const int page = GetPageIndex(v);
        if (page != wxNOT_FOUND) DeletePage((size_t)page);
    }
}

void ScopeNotebook::refreshNode(int id) {
    if (ScopeView* v = find(id)) {
        const int page = GetPageIndex(v);
        if (page != wxNOT_FOUND)
            SetPageText((size_t)page, wxString::FromUTF8(scopeTitle(e_, id)));
        v->tick();
    }
}

void ScopeNotebook::refreshAll() {
    prune();
    for (size_t i = 0; i < GetPageCount(); ++i) {
        auto* v = dynamic_cast<ScopeView*>(GetPage(i));
        if (!v) continue;
        SetPageText(i, wxString::FromUTF8(scopeTitle(e_, v->nodeId())));
        v->tick();
    }
}

void ScopeNotebook::prune() {
    for (int i = (int)GetPageCount() - 1; i >= 0; --i) {
        auto* v = dynamic_cast<ScopeView*>(GetPage((size_t)i));
        const Node* n = v ? e_->g.find(v->nodeId()) : nullptr;
        if (!v || !n || n->type != NodeType::SCOPE) DeletePage((size_t)i);
    }
}

void ScopeNotebook::closeAll() {
    while (GetPageCount() > 0) DeletePage(0);
}
