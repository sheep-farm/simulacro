// simulacro — node graph canvas (wxPanel + SDL off-screen renderer)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "canvas.h"
#include "i18n.h"
#include "numfmt.h"

#include <wx/dcbuffer.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace {

SDL_Color rgba(unsigned c) {
    return SDL_Color{(Uint8)((c >> 24) & 0xFF), (Uint8)((c >> 16) & 0xFF),
                     (Uint8)((c >> 8) & 0xFF), (Uint8)(c & 0xFF)};
}

struct HostDraw {
    SDL_Renderer* ren = nullptr;
    const TextPainter* tp = nullptr;
};

void hostRect(void* ctx, int x, int y, int w, int h, unsigned c) {
    HostDraw* d = (HostDraw*)ctx;
    if (w <= 0 || h <= 0) return;
    SDL_Color col = rgba(c);
    SDL_Rect r{x, y, w, h};
    SDL_SetRenderDrawColor(d->ren, col.r, col.g, col.b, col.a);
    SDL_RenderFillRect(d->ren, &r);
}

void hostLine(void* ctx, int x0, int y0, int x1, int y1, unsigned c) {
    HostDraw* d = (HostDraw*)ctx;
    SDL_Color col = rgba(c);
    SDL_SetRenderDrawColor(d->ren, col.r, col.g, col.b, col.a);
    SDL_RenderDrawLine(d->ren, x0, y0, x1, y1);
}

void hostText(void* ctx, int x, int y, const char* s, unsigned c) {
    HostDraw* d = (HostDraw*)ctx;
    if (d && d->tp && s) d->tp->draw(x, y, s, rgba(c));
}

int hostTextW(void* ctx, const char* s) {
    HostDraw* d = (HostDraw*)ctx;
    int w = 0, h = 0;
    if (d && d->tp && s) d->tp->measure(s, w, h);
    return w;
}

wxString T(const char* s) { return wxString::FromUTF8(i18n::text(s)); }

double dist(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

GraphCanvas::GraphCanvas(wxWindow* parent, Engine* engine)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE | wxWANTS_CHARS),
      e_(engine) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(wxColour(kUi.background.r, kUi.background.g, kUi.background.b));

    Bind(wxEVT_PAINT, &GraphCanvas::onPaint, this);
    Bind(wxEVT_SIZE, &GraphCanvas::onSize, this);
    Bind(wxEVT_LEFT_DOWN, &GraphCanvas::onLeftDown, this);
    Bind(wxEVT_LEFT_UP, &GraphCanvas::onLeftUp, this);
    Bind(wxEVT_MOTION, &GraphCanvas::onMotion, this);
    Bind(wxEVT_RIGHT_DOWN, &GraphCanvas::onRightDown, this);
    Bind(wxEVT_MIDDLE_DOWN, &GraphCanvas::onMiddleDown, this);
    Bind(wxEVT_MIDDLE_UP, &GraphCanvas::onMiddleUp, this);
    Bind(wxEVT_MOUSEWHEEL, &GraphCanvas::onMouseWheel, this);
    Bind(wxEVT_LEFT_DCLICK, &GraphCanvas::onDoubleClick, this);
    Bind(wxEVT_KEY_DOWN, &GraphCanvas::onKeyDown, this);
    Bind(wxEVT_CHAR, [this](wxKeyEvent& ev) {
        if (imui::wantsTextInputNow()) {
            const int uk = ev.GetUnicodeKey();
            const wxString utf8 = uk > 0 ? wxString((wchar_t)uk) : wxString::FromUTF8(" ");
            imui::textUtf8(utf8.utf8_str().data());
            return;
        }
        ev.Skip();
    });
    Bind(wxEVT_ERASE_BACKGROUND, &GraphCanvas::onEraseBackground, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &GraphCanvas::onCaptureLost, this);
}
void GraphCanvas::eventToImGui(const wxMouseEvent& ev) {
    if (!imui::enabled()) return;
    imui::mouseMove(ev.GetPosition().x, ev.GetPosition().y);
}
bool GraphCanvas::widgetHit(const wxPoint& p) const {
    for (const auto& b : widgetBoxes_)
        if (p.x >= b.x && p.x < b.x + b.w && p.y >= b.y && p.y < b.y + b.h) return true;
    return false;
}

// Applies one parameter change through the plugin ABI; engine side stays the
// same as the property grid path (checkpoint per gesture — see onLeftUp).
bool GraphCanvas::applyParam(Node* n, int i, double v) {
    if (!n || !n->pinst) return false;
    if (!widgetUndoActive_) {
        e_->checkpoint();
        widgetUndoActive_ = true;
    }
    n->pinst->callSetParam(n->pinst->handle, i, v);
    e_->refreshPstate(*n);
    e_->dirty = true;
    return true;
}

void GraphCanvas::applyText(Node* n, int i, const std::string& s) {
    if (!n || !n->pinst) return;
    if (!widgetUndoActive_) {
        e_->checkpoint();
        widgetUndoActive_ = true;
    }
    n->pinst->callSetText(n->pinst->handle, i, s.c_str());
    e_->refreshPstate(*n);
    e_->dirty = true;
}

void GraphCanvas::onPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    if (frameDelay_ > 0 && --frameDelay_ == 0) centerOnGraph();
    const wxSize sz = GetClientSize();
    if (bmp_.ensure(sz.x, sz.y)) {
        if (bmp_.rendererRebuilt()) {       // atlas/renderer pair died with the panel
            bmp_.clearRendererRebuilt();
            if (imui::enabled()) imui::renChanged(bmp_.ren());
        }
        render();
        if (imui::enabled()) {
            if (imui::init(bmp_.ren())) {
                imui::newFrame(sz.x, sz.y);
                drawNodeWidgets();
                imui::newFrameEnd();
                imui::renderFrame(bmp_.ren());
            }
        }
        bmp_.present(dc);
    }
}

void GraphCanvas::onSize(wxSizeEvent& ev) {
    Refresh(false);
    ev.Skip();
}

void GraphCanvas::onCaptureLost(wxMouseCaptureLostEvent&) {
    pending_ = false;
    moving_ = false;
    rubber_ = false;
    panning_ = false;
}

void GraphCanvas::render() {
    bmp_.clear(kUi.background);
    drawGrid();
    drawGroups();
    drawEdges();
    drawNodes();
    drawPending();
    drawRubber();
    if (zoom_ != 1.0f) {
        const std::string z = std::to_string((int)std::lround(zoom_ * 100)) + "%";
        bmp_.painter().draw(8, bmp_.height() - 18, z, kDim);
    }
}

void GraphCanvas::drawGrid() {
    if (!grid_) return;
    SDL_Renderer* r = bmp_.ren();
    const int W = bmp_.width(), H = bmp_.height();
    const float f = 20 * zoom_, c = 100 * zoom_;
    const float ox = -camX_ * zoom_, oy = -camY_ * zoom_;
    const float sx = ox - std::floor(ox / f) * f;
    const float sy = oy - std::floor(oy / f) * f;
    setColor(r, kUi.grid);
    for (float x = sx; x < W; x += f) SDL_RenderDrawLine(r, (int)x, 0, (int)x, H);
    for (float y = sy; y < H; y += f) SDL_RenderDrawLine(r, 0, (int)y, W, (int)y);
    setColor(r, kUi.strongGrid);
    const float cx0 = ox - std::floor(ox / c) * c;
    const float cy0 = oy - std::floor(oy / c) * c;
    for (float x = cx0; x < W; x += c)
        for (int k = 0; k < 3 && x + k < W; ++k) SDL_RenderDrawLine(r, (int)x + k, 0, (int)x + k, H);
    for (float y = cy0; y < H; y += c)
        for (int k = 0; k < 3 && y + k < H; ++k) SDL_RenderDrawLine(r, 0, (int)y + k, W, (int)y + k);
}

FRect GraphCanvas::groupRect(const Group& gr) const {
    if (gr.w > 0 && gr.h > 0) return {gr.x, gr.y, gr.w, gr.h};
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int id : gr.nodes) {
        const Node* n = e_->g.find(id);
        if (!n) continue;
        FRect b = nodeBox(*n);
        x0 = std::min(x0, b.x); y0 = std::min(y0, b.y);
        x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h);
    }
    if (x1 < x0) return {0, 0, 0, 0};
    const float m = 24;
    return {x0 - m, y0 - m, x1 - x0 + 2 * m, y1 - y0 + 2 * m};
}

bool GraphCanvas::nodeHidden(int id) const {
    const Group* gr = e_->g.groupOf(id);
    return gr && gr->collapsed;
}

void GraphCanvas::drawGroups() {
    SDL_Renderer* r = bmp_.ren();
    for (const auto& gr : e_->g.groups) {
        const FRect wr = groupRect(gr);
        if (wr.w <= 0 || wr.h <= 0) continue;
        Vec2 tl = w2s({wr.x, wr.y}), br = w2s({wr.x + wr.w, wr.y + wr.h});
        SDL_Rect box{(int)tl.x, (int)tl.y, (int)(br.x - tl.x), (int)(br.y - tl.y)};
        if (box.w <= 0 || box.h <= 0) continue;
        if (box.x > bmp_.width() || box.y > bmp_.height() || box.x + box.w < 0 ||
            box.y + box.h < 0)
            continue;
        bool marked = false;
        for (int id : gr.nodes)
            if (sel_.count(id)) { marked = true; break; }
        const int hh = std::max(2, (int)(26 * zoom_));
        SDL_Rect hdr{box.x, box.y, box.w, std::min(hh, box.h)};
        if (gr.collapsed) {
            setColor(r, marked ? kUi.active : kUi.box);
            SDL_RenderFillRect(r, &box);
        } else {
            setColor(r, withAlpha(kUi.darkBackground, 204));
            SDL_RenderFillRect(r, &box);
        }
        setColor(r, marked ? kUi.strongEdge : kUi.edge);
        SDL_RenderDrawRect(r, &box);
        setColor(r, marked ? kUi.strongEdge : kUi.edge);
        SDL_RenderFillRect(r, &hdr);
        if (zoom_ >= 0.5f) {
            const SDL_Color fg = contrastColor(marked ? kUi.strongEdge : kUi.edge);
            std::string title = gr.title.empty() ? ("Group " + std::to_string(gr.id)) : gr.title;
            bmp_.painter().draw(box.x + 6, box.y + 4,
                                elideText(bmp_.painter(), title, box.w - 28), fg);
            bmp_.painter().draw(box.x + box.w - 16, box.y + 4, gr.collapsed ? "▸" : "▾", fg);
        }
    }
}

void GraphCanvas::drawEdges() {
    SDL_Renderer* r = bmp_.ren();
    for (size_t ei = 0; ei < e_->g.edges.size(); ++ei) {
        const Edge& ed = e_->g.edges[ei];
        const Node* a = e_->g.find(ed.from);
        const Node* b = e_->g.find(ed.to);
        if (!a || !b) continue;
        if (nodeHidden(ed.from) || nodeHidden(ed.to)) continue;
        const Group* ga = e_->g.groupOf(ed.from);
        const Group* gb = e_->g.groupOf(ed.to);
        if (ga && gb && ga == gb && ga->collapsed) continue;
        int total = 1;
        const int order = e_->g.edgeFan(ed.from, ed.port, ed.to, ed.inPort, total);
        Vec2 wp1 = outPort(*a, ed.port);
        Vec2 wp2 = inPort(*b, ed.inPort);
        wp2.y += (order - (total - 1) * 0.5f) * 7.0f;
        Vec2 p1 = w2s(wp1), p2 = w2s(wp2);
        Vec2 d1 = outDir(*a, ed.port);
        Vec2 po = inDir(*b, ed.inPort), d2{-po.x, -po.y};
        const auto key = std::make_tuple(ed.from, ed.port, ed.to, ed.inPort);
        const bool esel = selWires_.count(key) > 0;
        const SDL_Color sc = socketColor(false, ed.port);
        if (esel) {
            setColor(r, kUi.highlight);
        } else {
            SDL_SetRenderDrawColor(r, (Uint8)(sc.r * 0.72f), (Uint8)(sc.g * 0.72f),
                                   (Uint8)(sc.b * 0.72f), 255);
        }
        const bool delay = e_->g.noDelay(ed.to);
        drawWire(r, p1, d1, p2, d2, curve_ ? WireStyle::CURVE : WireStyle::STRAIGHT,
                 true, delay ? 2 : 0);
        if (esel)
            drawWire(r, {p1.x + 1.5f, p1.y + 1.5f}, d1, {p2.x + 1.5f, p2.y + 1.5f}, d2,
                     curve_ ? WireStyle::CURVE : WireStyle::STRAIGHT, false, delay ? 2 : 0);
    }

    if (pending_) {
        Vec2 p1{0, 0}, d1{1, 0}, p2 = mouseWorld_, d2{-1, 0};
        const Node* n = e_->g.find(pendingFrom_);
        if (n) {
            if (pendingInput_) {
                Vec2 wp = inPort(*n, pendingPort_);
                p2 = w2s(wp);
                Vec2 po = inDir(*n, pendingPort_);
                d2 = {-po.x, -po.y};
                Vec2 to{p2.x - mouse_.x, p2.y - mouse_.y};
                float l = std::sqrt(to.x * to.x + to.y * to.y);
                d1 = l > 0.001f ? Vec2{to.x / l, to.y / l} : Vec2{1, 0};
            } else {
                Vec2 wp = outPort(*n, pendingPort_);
                p1 = w2s(wp);
                d1 = outDir(*n, pendingPort_);
                Vec2 from{p1.x - mouse_.x, p1.y - mouse_.y};
                float l = std::sqrt(from.x * from.x + from.y * from.y);
                d2 = l > 0.001f ? Vec2{from.x / l, from.y / l} : Vec2{-1, 0};
            }
        }
        setColor(r, kUi.highlight);
        drawWire(r, p1, d1, p2, d2, curve_ ? WireStyle::CURVE : WireStyle::STRAIGHT, false, 0);
    }
}

void GraphCanvas::drawNodes() {
    SDL_Renderer* r = bmp_.ren();
    for (const auto& n : e_->g.nodes) {
        if (nodeHidden(n.id)) continue;
        FRect wb = nodeBox(n);
        Vec2 tl = w2s({wb.x, wb.y}), br = w2s({wb.x + wb.w, wb.y + wb.h});
        SDL_Rect box{(int)tl.x, (int)tl.y, (int)(br.x - tl.x), (int)(br.y - tl.y)};
        if (box.x > bmp_.width() || box.y > bmp_.height() || box.x + box.w < 0 ||
            box.y + box.h < 0)
            continue;
        const int titleH = std::max(2, (int)(NODE_TITLE_H * zoom_));
        const bool customShape = (n.type == NodeType::PLUGIN && n.shape == NS_SHAPE_CUSTOM);
        Uint8 cr = 255, cg = 255, cb = 255;
        if (n.type == NodeType::PLUGIN) {
            float rgb[3];
            if (e_->plugins.colorOf(n.plugin, rgb)) {
                cr = (Uint8)rgb[0]; cg = (Uint8)rgb[1]; cb = (Uint8)rgb[2];
            } else {
                ringColor(n.type, cr, cg, cb);
            }
        } else {
            ringColor(n.type, cr, cg, cb);
        }
        if (n.id == hoverNode_) { cr = cg = cb = 255; }
        const bool selected = sel_.count(n.id) > 0;
        auto drawFrame = [&] {
            if (selected) {
                SDL_Rect o{box.x - 3, box.y - 3, box.w + 6, box.h + 6};
                setColor(r, kUi.strong);
                SDL_RenderDrawRect(r, &o);
            } else if (n.id == hoverNode_) {
                SDL_Rect o{box.x - 2, box.y - 2, box.w + 4, box.h + 4};
                setColor(r, mixColor(kUi.strong, kUi.background, 0.25f));
                SDL_RenderDrawRect(r, &o);
            }
        };
        const SDL_Color tbBg = mixColor({cr, cg, cb, 255}, kUi.panel, 0.72f);
        if (!customShape) {
            setColor(r, kUi.box);
            SDL_RenderFillRect(r, &box);
            SDL_Rect tb{box.x, box.y, box.w, titleH};
            setColor(r, tbBg);
            SDL_RenderFillRect(r, &tb);
            SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
            SDL_RenderDrawRect(r, &box);
            drawFrame();
        }
        const bool orphan = (n.type == NodeType::PLUGIN && !n.pinst);
        if (orphan || n.nonFiniteCount || n.limits) {
            SDL_Rect w{box.x + box.w - 10, box.y + 3, 7, 7};
            if (orphan || n.nonFiniteCount) setColor(r, kUi.error);
            else setColor(r, kUi.warning);
            SDL_RenderFillRect(r, &w);
        }
        if (n.type == NodeType::SCOPE && zoom_ >= 0.6f) {
            const ScopeCfg cfg = parseScopeCfg(n.params);
            auto sc = e_->scopeData.find(n.id);
            if (cfg.type != ScopeType::MAP && sc != e_->scopeData.end() && box.h - titleH > 8) {
                SDL_Rect body{box.x + 4, box.y + titleH + 2, box.w - 8, box.h - titleH - 4};
                drawSparkline(r, body, sc->second.ch, sc->second.colors);
            }
        }
        const std::string ntitle = e_->nodeLabel(n);
        const bool lod = zoom_ >= 0.6f;
        if (customShape) {
            if (n.pinst && n.pinst->drawFn && n.pinst->handle) {
                HostDraw hd{r, &bmp_.painter()};
                NsDraw d{&hd, hostRect, hostLine, hostText, hostTextW, ntitle.c_str()};
                NsRect area{box.x, box.y, box.w, box.h};
                n.pinst->callDraw(n.pinst->handle, &d, &area);
            }
            drawFrame();
        } else {
            if (lod)
                bmp_.painter().draw(box.x + 5, box.y + 3,
                                    elideText(bmp_.painter(), ntitle, box.w - 10),
                                    contrastColor(tbBg));
            if (lod && n.type == NodeType::PLUGIN && n.pinst && n.pinst->drawFn &&
                n.pinst->handle) {
                SDL_Rect body{box.x + 1, box.y + titleH, box.w - 2, box.h - titleH - 1};
                if (body.w > 12 && body.h > 12) {
                    HostDraw hd{r, &bmp_.painter()};
                    NsDraw d{&hd, hostRect, hostLine, hostText, hostTextW, ntitle.c_str()};
                    NsRect area{body.x, body.y, body.w, body.h};
                    SDL_RenderSetClipRect(r, &body);
                    n.pinst->callDraw(n.pinst->handle, &d, &area);
                    SDL_RenderSetClipRect(r, nullptr);
                }
            }
        }

        const int pr = std::max(3, (int)(6 * zoom_));
        const int nin = n.nIn < 1 ? 1 : n.nIn;
        for (int k = 0; k < nin; ++k) {
            Vec2 ip = w2s(inPort(n, k));
            SDL_Color c = socketColor(true, k);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
            drawCircle(r, (int)ip.x, (int)ip.y, pr);
            if (!lod) continue;
            std::string il = portLabel(n, k, true);
            if (il.empty()) continue;
            if (il.size() > 16) il = il.substr(0, 16) + "…";
            int iw = 0, ih = 0;
            bmp_.painter().measure(il, iw, ih);
            int tx = (int)ip.x + pr + 4, ty = (int)ip.y - 7;
            if (n.inSide == 2) tx = (int)ip.x - pr - 4 - iw;
            else if (n.inSide == 1) { tx = (int)ip.x - iw / 2; ty = (int)ip.y + pr + 2; }
            else if (n.inSide == 3) { tx = (int)ip.x - iw / 2; ty = (int)ip.y - pr - 15; }
            int x0 = box.x + 4, x1 = box.x + box.w - 4 - iw;
            if (tx < x0) tx = x0;
            if (tx > x1) tx = x1;
            bmp_.painter().draw(tx, ty, il, kDim);
        }
        for (int k = 0; k < n.nOut; ++k) {
            Vec2 op = w2s(outPort(n, k));
            SDL_Color c = socketColor(false, k);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
            drawCircle(r, (int)op.x, (int)op.y, pr);
            if (!lod) continue;
            std::string pl = portLabel(n, k, false);
            std::string val;
            bool isStr = false;
            if (showVals_) {
                isStr = k < Node::MAX_PORTS && !n.outTxt[k].empty();
                if (isStr) val = n.outTxt[k];
                else if (n.nOut > 1) val = formatNumShort(n.out[k]);
            }
            std::string lab = pl;
            if (!val.empty()) lab = pl.empty() ? val : pl + "=" + val;
            if (lab.empty()) continue;
            if (lab.size() > 14) lab = lab.substr(0, 14) + "…";
            int tw = 0, th = 0;
            bmp_.painter().measure(lab, tw, th);
            bmp_.painter().draw((int)op.x - tw - pr - 4, (int)op.y - 7, lab,
                                isStr ? kStr : kDim);
        }
        int ty = box.y + box.h + 4;
        if (n.type == NodeType::SCOPE) {
            if (showVals_ && lod) {
                ScopeCfg sc = parseScopeCfg(n.params);
                auto it = e_->scopeData.find(n.id);
                std::string sv;
                if (it != e_->scopeData.end())
                    for (size_t k = 0; k < it->second.ch.size() && k < 6; ++k) {
                        if (!sv.empty()) sv += " ";
                        sv += formatNumShort(it->second.ch[k].empty() ? 0.0
                                                                      : it->second.ch[k].back());
                    }
                bmp_.painter().draw(box.x, ty, elideText(bmp_.painter(), sv, box.w + 70), kTxt);
                ty += 15;
                bmp_.painter().draw(box.x, ty, scopeTypeName(sc.type), kDim);
            }
        } else if (showVals_ && lod) {
            if (!n.outTxt[0].empty()) {
                std::string t = n.outTxt[0];
                if (t.size() > 16) t = t.substr(0, 16) + "…";
                bmp_.painter().draw(box.x, ty, t, kStr);
            } else {
                bmp_.painter().draw(box.x, ty, formatNumShort(n.value), kTxt);
            }
            ty += 15;
            if (n.type == NodeType::SCRIPT) {
                std::string label = n.scriptError.empty() ? oneLine(n.script)
                                                          : "!" + n.scriptError;
                bmp_.painter().draw(box.x, ty, elideText(bmp_.painter(), label, box.w + 70),
                                    n.scriptError.empty() ? kDim : kErr);
            }
            if (n.type == NodeType::PLUGIN)
                bmp_.painter().draw(box.x, ty, n.plugin.empty() ? "plugin?" : n.plugin, kDim);
        } else if (lod && n.type == NodeType::SCRIPT && !n.scriptError.empty()) {
            bmp_.painter().draw(box.x, ty, elideText(bmp_.painter(), "!" + n.scriptError, box.w + 70),
                                kErr);
        }
    }
}

void GraphCanvas::drawPending() {}
void GraphCanvas::drawRubber() {
    if (!rubber_) return;
    Vec2 a = w2s(rubberStart_), b = w2s(rubberCur_);
    SDL_Rect r{(int)std::min(a.x, b.x), (int)std::min(a.y, b.y),
               (int)std::fabs(a.x - b.x), (int)std::fabs(a.y - b.y)};
    setColor(bmp_.ren(), withAlpha(kUi.highlight, 70));
    SDL_RenderFillRect(bmp_.ren(), &r);
    setColor(bmp_.ren(), kUi.highlight);
    SDL_RenderDrawRect(bmp_.ren(), &r);
}

std::string GraphCanvas::oneLine(const std::string& s) const {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    return o;
}

// ---- ImGui widgets over/in-node (parameter editors of the selected node) ----
// Drawn on top of the custom renderer; the UI that moves nodes/wires remains
// custom. On scope/map views this layer never runs.
//
// Layout precedence (fase 3): the declarative in-node layout exported by the
// plugin via `nodesim_plugin_ui` when present; otherwise the generic parameter
// list (fase 2). Modes come from nodeui::mode(): `inner` (default) anchors the
// strip inside the node body below the title, clipped to the node box; `below`
// keeps the fase-1 rail under the node; `off` disables canvas editing.
void GraphCanvas::drawNodeWidgets() {
    if (!imui::enabled() || !ImGui::GetCurrentContext()) return;
    if (nodeui::mode() == nodeui::Mode::Off) return;
    const int id = singleSelection();
    const Node* n = id >= 0 ? e_->g.find(id) : nullptr;
    if (!n) return;
    if (n->type != NodeType::PLUGIN) return;
    auto* inst = n->pinst.get();
    if (!inst) return;
    const auto* rt = e_->typeOf(*n);
    if (!rt) return;
    const bool authored = !rt->ui.empty();
    if (!authored && rt->params.empty()) return;
    if (zoom_ < 0.45f) return;   // window tiny: editing is for the property grid

    char win[40];
    std::snprintf(win, sizeof win, "##params%d", id);
    const FRect b = nodeBox(*n);
    const float pad = 6.0f, labelW = 60.0f;
    const float rowH = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y * 0.5f;
    const int rows = authored ? (int)rt->ui.size() : (int)rt->params.size();
    // LED rows are visual chips, not widgets; they still take one row each.
    float railH = (float)rows * rowH + 2 * pad + 4;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    Vec2 pos = w2s({b.x, b.y + b.h});
    float width = 196.0f;
    float height = railH;
    bool inner = false;
    if (nodeui::mode() == nodeui::Mode::Inner) {
        const Vec2 boxTl = w2s({b.x, b.y});
        const float boxW = b.w * zoom_;
        const float boxH = b.h * zoom_;
        const float titleH = NODE_TITLE_H * zoom_;
        const bool fits = boxW >= 118.0f && (boxH - titleH) >= rowH &&
                          (authored || (int)rt->params.size() <= 3);
        if (fits) {
            inner = true;
            pos = {boxTl.x, boxTl.y + titleH};
            width = boxW;
            height = boxH - titleH;
        }
    }

    widgetBoxes_.clear();
    widgetBoxes_.push_back({(int)pos.x, (int)pos.y, (int)width, (int)height});
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    if (!ImGui::Begin(win, nullptr, flags)) {
        ImGui::End();
        return;
    }
    const bool compact = inner && width < 150.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

    auto displayName = [&](const PluginRegistry::Param* pr,
                           const std::string& overrideName) {
        if (!overrideName.empty()) return overrideName;
        return pr ? pr->name : std::string();
    };

    // Generic/declarative row body: one widget per param (or Uint item).
    auto rowStart = [&](const std::string& shownName) {
        ImGui::SetCursorPosX(pad);
        if (compact) {
            ImGui::PushItemWidth(width - 2 * pad);
        } else {
            ImGui::TextUnformatted(shownName.c_str());
            ImGui::SameLine(pad + labelW);
            ImGui::PushItemWidth(width - labelW - 2 * pad);
        }
    };
    auto rowHover = [&](const std::string& shownName) {
        if (compact && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shownName.c_str());
    };

    for (int row = 0; row < rows; ++row) {
        // Declarative-only leafs that open with no parameter binding.
        if (authored) {
            const auto& it = rt->ui[(size_t)row];
            if (it.kind == NS_UI_END) break;
            if (it.kind == NS_UI_LABEL) {
                ImGui::SetCursorPosX(pad);
                ImGui::TextUnformatted(it.name.c_str());
                continue;
            }
            if (it.kind == NS_UI_VSPACE) {
                ImGui::Dummy(ImVec2(0.0f, rowH));
                continue;
            }
            if (it.kind == NS_UI_LED) {
                const int idx = it.index;
                const bool okBound =
                    idx >= 0 && idx < (int)rt->params.size() &&
                    rt->params[(size_t)idx].kind == NS_PARAM_BOOL;
                const double v = okBound ? inst->callGetParam(inst->handle, idx) : 0.0;
                const bool on = v > 0.5;
                const ImVec2 c = ImGui::GetCursorScreenPos();
                const float r = 5.0f;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(c.x + pad + r, c.y + rowH * 0.5f), r,
                    on ? IM_COL32(kUi.ok.r, kUi.ok.g, kUi.ok.b, 255)
                       : IM_COL32(kUi.weak.r, kUi.weak.g, kUi.weak.b, 255));
                ImGui::Dummy(ImVec2(width - pad - r, rowH));
                continue;
            }
        }

        ImGui::PushID(row);
        if (authored) {
            // Declarative item with binding.
            const auto& it = rt->ui[(size_t)row];
            const bool isText = it.kind == NS_UI_TEXT;
            if (isText) {
                const std::string shown = it.name.empty()
                                              ? (it.index < (int)rt->texts.size()
                                                     ? rt->texts[(size_t)it.index]
                                                     : std::string())
                                              : it.name;
                rowStart(shown);
                char cur[512] = {0};
                if (char* s = inst->callGetText(inst->handle, it.index)) {
                    strncpy(cur, s, sizeof(cur) - 1);
                    ::free(s);
                }
                ImGui::InputText("##tbl", cur, sizeof(cur));
                const bool entered =
                    ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemEdited();
                if (entered) {
                    widgetNode_ = id;
                    applyText((Node*)n, it.index, cur);
                    Refresh(false);
                }
                rowHover(shown);
                ImGui::PopItemWidth();
                ImGui::PopID();
                continue;
            }
            if (it.index < 0 || it.index >= (int)rt->params.size()) {
                ImGui::PopID();
                continue;  // loader validated but be defensive
            }
            const auto& pr = rt->params[(size_t)it.index];
            const double cur = inst->callGetParam(inst->handle, it.index);
            const std::string shownName = displayName(&pr, it.name);
            switch (pr.kind) {
                case NS_PARAM_BOOL: {
                    rowStart(shownName);
                    bool v = cur != 0.0;
                    if (ImGui::Checkbox("##sw", &v)) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, v ? 1.0 : 0.0);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
                case NS_PARAM_ENUM: {
                    std::vector<const char*> items;
                    items.reserve(pr.choices.size());
                    for (const auto& s : pr.choices) items.push_back(s.c_str());
                    int idx = (int)std::lround(cur);
                    if (idx < 0 || idx >= (int)items.size()) idx = 0;
                    rowStart(shownName);
                    if (ImGui::Combo("##combo", &idx, items.data(), (int)items.size())) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, (double)idx);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
                case NS_PARAM_INT: {
                    int v = (int)std::lround(cur);
                    rowStart(shownName);
                    const double lo = it.min != 0 ? it.min : pr.min;
                    const double hi = it.max != 0 ? it.max : pr.max;
                    const bool rangeOk =
                        std::isfinite(lo) && std::isfinite(hi) && hi > lo &&
                        (hi - lo) <= 100000.0;
                    bool changed = false;
                    if (rangeOk) {
                        changed = ImGui::SliderInt("##int", &v, (int)lo, (int)hi);
                    } else {
                        changed = ImGui::InputInt("##int", &v, 1, 100);
                        if (std::isfinite(lo)) v = std::max(v, (int)lo);
                        if (std::isfinite(hi)) v = std::min(v, (int)hi);
                    }
                    if (changed) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, (double)v);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
                default: {
                    rowStart(shownName);
                    const double lo = it.min != 0 ? it.min : pr.min;
                    const double hi = it.max != 0 ? it.max : pr.max;
                    const bool rangeOk =
                        std::isfinite(lo) && std::isfinite(hi) && hi > lo &&
                        (hi - lo) <= 1e6;
                    bool changed = false;
                    double out = cur;
                    if (rangeOk) {
                        float f = (float)cur;
                        changed =
                            ImGui::SliderFloat("##f", &f, (float)lo, (float)hi, "%.4f");
                        out = (double)f;
                    } else {
                        ImGui::InputDouble("##num", &out, 0.0, 0.0, "%.6g");
                        changed = ImGui::IsItemDeactivatedAfterEdit();
                        if (std::isfinite(lo)) out = std::max(out, lo);
                        if (std::isfinite(hi)) out = std::min(out, hi);
                    }
                    if (changed) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, out);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
            }
            ImGui::PopID();
            continue;
        }

        // Generic path (fase 2): one row per parameter.
        const auto& pr = rt->params[(size_t)row];
        const double cur = inst->callGetParam(inst->handle, row);
        const std::string shownName = pr.name;
        switch (pr.kind) {
            case NS_PARAM_BOOL: {
                rowStart(shownName);
                bool v = cur != 0.0;
                if (ImGui::Checkbox("##sw", &v)) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, v ? 1.0 : 0.0);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
            case NS_PARAM_ENUM: {
                std::vector<const char*> items;
                items.reserve(pr.choices.size());
                for (const auto& s : pr.choices) items.push_back(s.c_str());
                int idx = (int)std::lround(cur);
                if (idx < 0 || idx >= (int)items.size()) idx = 0;
                rowStart(shownName);
                if (ImGui::Combo("##combo", &idx, items.data(), (int)items.size())) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, (double)idx);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
            case NS_PARAM_INT: {
                int v = (int)std::lround(cur);
                rowStart(shownName);
                const double lo = pr.min, hi = pr.max;
                const bool rangeOk =
                    std::isfinite(lo) && std::isfinite(hi) && hi > lo &&
                    (hi - lo) <= 100000.0;
                bool changed = false;
                if (rangeOk) {
                    changed = ImGui::SliderInt("##int", &v, (int)lo, (int)hi);
                } else {
                    changed = ImGui::InputInt("##int", &v, 1, 100);
                    if (std::isfinite(lo)) v = std::max(v, (int)lo);
                    if (std::isfinite(hi)) v = std::min(v, (int)hi);
                }
                if (changed) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, (double)v);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
            default: {
                rowStart(shownName);
                const double lo = pr.min, hi = pr.max;
                const bool rangeOk =
                    std::isfinite(lo) && std::isfinite(hi) && hi > lo && (hi - lo) <= 1e6;
                bool changed = false;
                double out = cur;
                if (rangeOk) {
                    float f = (float)cur;
                    changed =
                        ImGui::SliderFloat("##f", &f, (float)lo, (float)hi, "%.4f");
                    out = (double)f;
                } else {
                    ImGui::InputDouble("##num", &out, 0.0, 0.0, "%.6g");
                    changed = ImGui::IsItemDeactivatedAfterEdit();
                    if (std::isfinite(lo)) out = std::max(out, lo);
                    if (std::isfinite(hi)) out = std::min(out, hi);
                }
                if (changed) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, out);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

std::string GraphCanvas::portLabel(const Node& n, int port, bool input) const {
    if (n.pinst) {
        char* s = input ? n.pinst->callPortLabelIn(n.pinst->handle, port)
                        : n.pinst->callPortLabel(n.pinst->handle, port);
        if (s) {
            std::string r = s;
            ::free(s);
            return r;
        }
    }
    return {};
}

int GraphCanvas::nodeAt(Vec2 world) const {
    for (auto it = e_->g.nodes.rbegin(); it != e_->g.nodes.rend(); ++it) {
        if (nodeHidden(it->id)) continue;
        if (inBox(nodeBox(*it), world)) return it->id;
    }
    return -1;
}

bool GraphCanvas::portAt(Vec2 world, int& node, int& port, bool& input) const {
    const float tol = 9.0f / zoom_;
    for (auto it = e_->g.nodes.rbegin(); it != e_->g.nodes.rend(); ++it) {
        const Node& n = *it;
        if (nodeHidden(n.id)) continue;
        for (int k = 0; k < n.nOut; ++k)
            if (dist(world, outPort(n, k)) <= tol) {
                node = n.id; port = k; input = false;
                return true;
            }
        const int nin = n.nIn < 1 ? 1 : n.nIn;
        for (int k = 0; k < nin; ++k)
            if (dist(world, inPort(n, k)) <= tol) {
                node = n.id; port = k; input = true;
                return true;
            }
    }
    return false;
}

bool GraphCanvas::wireAt(Vec2 screen, int& edge) const {
    for (int i = (int)e_->g.edges.size() - 1; i >= 0; --i) {
        const Edge& ed = e_->g.edges[i];
        const Node* a = e_->g.find(ed.from);
        const Node* b = e_->g.find(ed.to);
        if (!a || !b || nodeHidden(ed.from) || nodeHidden(ed.to)) continue;
        Vec2 p1 = w2s(outPort(*a, ed.port));
        Vec2 p2 = w2s(inPort(*b, ed.inPort));
        Vec2 d1 = outDir(*a, ed.port);
        Vec2 po = inDir(*b, ed.inPort), d2{-po.x, -po.y};
        if (wireHit(screen, p1, d1, p2, d2,
                    curve_ ? WireStyle::CURVE : WireStyle::STRAIGHT, 6.0f)) {
            edge = i;
            return true;
        }
    }
    return false;
}

int GraphCanvas::groupHeaderAt(Vec2 world) const {
    for (const auto& gr : e_->g.groups) {
        FRect r = groupRect(gr);
        if (r.w <= 0 || r.h <= 0) continue;
        const float hh = std::min(26.0f, r.h);
        if (world.x >= r.x && world.x <= r.x + r.w && world.y >= r.y &&
            world.y <= r.y + hh)
            return gr.id;
    }
    return -1;
}

int GraphCanvas::groupBodyAt(Vec2 world) const {
    for (const auto& gr : e_->g.groups) {
        if (!gr.collapsed) continue;
        FRect r = groupRect(gr);
        if (inBox(r, world)) return gr.id;
    }
    return -1;
}

void GraphCanvas::onLeftDown(wxMouseEvent& ev) {
    eventToImGui(ev);
    imui::mouseButton(0, true);
    if (imui::wantsMouseNow() || widgetHit(ev.GetPosition())) { Refresh(false); return; }
    if (dragging_ || rubber_ || panning_) return;
    SetFocus();
    if (!HasCapture()) CaptureMouse();
    dragging_ = true;
    moved_ = false;
    mouse_ = ev.GetPosition();
    mouseWorld_ = s2w({(float)mouse_.x, (float)mouse_.y});
    const bool add = ev.ControlDown() || ev.ShiftDown();

    int node = -1, port = 0;
    bool input = false;
    if (portAt(mouseWorld_, node, port, input)) {
        pending_ = true;
        pendingFrom_ = node;
        pendingPort_ = port;
        pendingInput_ = input;
        selWires_.clear();
        notifySelection();
        return;
    }

    const int gid = groupHeaderAt(mouseWorld_);
    if (gid >= 0) {
        const Group* gr = e_->g.findGroup(gid);
        if (gr) {
            if (!add) sel_.clear();
            for (int id : gr->nodes) sel_.insert(id);
            moveGroup_ = gid;
            groupOrig_ = {gr->x, gr->y};
        }
        startMove(mouseWorld_);
        notifySelection();
        return;
    }

    const int nid = nodeAt(mouseWorld_);
    if (nid >= 0) {
        if (add) {
            if (sel_.count(nid)) sel_.erase(nid);
            else sel_.insert(nid);
        } else if (!sel_.count(nid)) {
            sel_.clear();
            sel_.insert(nid);
        }
        selWires_.clear();
        moveGroup_ = -1;
        startMove(mouseWorld_);
        notifySelection();
        return;
    }

    int edge = -1;
    if (wireAt({(float)mouse_.x, (float)mouse_.y}, edge)) {
        const Edge& ed = e_->g.edges[edge];
        const auto key = std::make_tuple(ed.from, ed.port, ed.to, ed.inPort);
        if (!add) {
            sel_.clear();
            selWires_.clear();
        }
        if (selWires_.count(key)) selWires_.erase(key);
        else selWires_.insert(key);
        notifySelection();
        return;
    }

    const int body = groupBodyAt(mouseWorld_);
    if (body >= 0) {
        const Group* gr = e_->g.findGroup(body);
        if (gr) {
            if (!add) sel_.clear();
            for (int id : gr->nodes) sel_.insert(id);
        }
        notifySelection();
        return;
    }

    if (!add) {
        sel_.clear();
        selWires_.clear();
        notifySelection();
    }
    rubber_ = true;
    rubberStart_ = rubberCur_ = mouseWorld_;
}

void GraphCanvas::startMove(Vec2 world) {
    moving_ = true;
    moveStart_ = world;
    moveOrig_.clear();
    for (int id : sel_) {
        const Node* n = e_->g.find(id);
        if (n) moveOrig_.push_back({id, n->pos});
    }
}

void GraphCanvas::applyMove(Vec2 world) {
    if (!moving_) return;
    const Vec2 d{world.x - moveStart_.x, world.y - moveStart_.y};
    if (std::fabs(d.x) > 0.01f || std::fabs(d.y) > 0.01f) moved_ = true;
    for (const auto& p : moveOrig_) {
        Node* n = e_->g.find(p.first);
        if (n) n->pos = {p.second.x + d.x, p.second.y + d.y};
    }
    if (moveGroup_ >= 0) {
        if (Group* gr = e_->g.findGroup(moveGroup_)) {
            gr->x = groupOrig_.x + d.x;
            gr->y = groupOrig_.y + d.y;
        }
    }
}

void GraphCanvas::endMove() {
    if (moving_ && moved_) {
        e_->g.touch();
        e_->dirty = true;
        if (onDocumentChanged) onDocumentChanged();
    }
    moving_ = false;
    moveOrig_.clear();
    moveGroup_ = -1;
}

void GraphCanvas::connectPending(Vec2 world) {
    int node = -1, port = 0;
    bool input = false;
    if (pending_ && portAt(world, node, port, input)) {
        if (pendingInput_ && !input) {
            if (e_->g.connect(node, port, pendingFrom_, pendingPort_)) {
                e_->dirty = true;
                if (onDocumentChanged) onDocumentChanged();
            }
        } else if (!pendingInput_ && input) {
            if (e_->g.connect(pendingFrom_, pendingPort_, node, port)) {
                e_->dirty = true;
                if (onDocumentChanged) onDocumentChanged();
            }
        }
    }
    pending_ = false;
    pendingFrom_ = -1;
    notifySelection();
}

void GraphCanvas::onLeftUp(wxMouseEvent& ev) {
    if (imui::enabled()) {
        eventToImGui(ev);
        imui::mouseButton(0, false);
    }
    if (widgetUndoActive_) {
        widgetUndoActive_ = false;
        const int id = widgetNode_;
        widgetNode_ = -1;
        if (HasCapture()) ReleaseMouse();
        Refresh(false);
        if (id >= 0 && onParamEdited) onParamEdited(id);
        return;  // gesture ended on a widget: the canvas did not start a drag
    }
    if (!dragging_) return;
    dragging_ = false;
    mouse_ = ev.GetPosition();
    mouseWorld_ = s2w({(float)mouse_.x, (float)mouse_.y});
    if (HasCapture()) ReleaseMouse();

    if (pending_) {
        connectPending(mouseWorld_);
    } else if (rubber_) {
        rubber_ = false;
        const float x0 = std::min(rubberStart_.x, rubberCur_.x);
        const float x1 = std::max(rubberStart_.x, rubberCur_.x);
        const float y0 = std::min(rubberStart_.y, rubberCur_.y);
        const float y1 = std::max(rubberStart_.y, rubberCur_.y);
        for (const auto& n : e_->g.nodes) {
            if (nodeHidden(n.id)) continue;
            if (n.pos.x >= x0 && n.pos.x <= x1 && n.pos.y >= y0 && n.pos.y <= y1)
                sel_.insert(n.id);
        }
        notifySelection();
    } else {
        endMove();
    }
    Refresh(false);
}

void GraphCanvas::onMotion(wxMouseEvent& ev) {
    eventToImGui(ev);
    mouse_ = ev.GetPosition();
    mouseWorld_ = s2w({(float)mouse_.x, (float)mouse_.y});
    if ((imui::wantsMouseNow() || widgetHit(mouse_)) &&
        !dragging_ && !rubber_ && !moving_ && !panning_) {
        Refresh(false);
        ev.Skip();
        return;
    }
    if (panning_) {
        const wxPoint d = mouse_ - panStart_;
        camX_ = panCamX_ - d.x / zoom_;
        camY_ = panCamY_ - d.y / zoom_;
        Refresh(false);
        return;
    }
    if (moving_) {
        applyMove(mouseWorld_);
        Refresh(false);
        return;
    }
    if (rubber_) {
        rubberCur_ = mouseWorld_;
        Refresh(false);
        return;
    }
    if (nodeAt(mouseWorld_) != hoverNode_) {
        hoverNode_ = nodeAt(mouseWorld_);
        Refresh(false);
    }    if (pending_) Refresh(false);
    ev.Skip();
}

void GraphCanvas::onRightDown(wxMouseEvent& ev) {
    eventToImGui(ev);
    imui::mouseButton(1, true);
    if (imui::wantsMouseNow() || widgetHit(ev.GetPosition())) { Refresh(false); return; }
    SetFocus();
    mouse_ = ev.GetPosition();
    mouseWorld_ = s2w({(float)mouse_.x, (float)mouse_.y});
    const int nid = nodeAt(mouseWorld_);
    if (nid >= 0 && !sel_.count(nid)) {
        sel_.clear();
        sel_.insert(nid);
        selWires_.clear();
        notifySelection();
        Refresh(false);
    }

    enum { CM_EDIT = 1, CM_SCOPE, CM_DUP, CM_DEL, CM_SCRIPT, CM_SCOPE_ADD, CM_JOIN,
           CM_SPLIT, CM_PASTE, CM_FIT };
    wxMenu menu;
    if (nid >= 0) {
        const Node* n = e_->g.find(nid);
        if (n && n->type == NodeType::SCRIPT)
            menu.Append(CM_EDIT, T("Edit script…"));
        if (n && n->type == NodeType::SCOPE)
            menu.Append(CM_SCOPE, T("Open Scope"));
        if (n && n->type == NodeType::SCRIPT)
            menu.AppendSeparator();
        menu.Append(CM_DUP, T("Duplicate (Ctrl+D)"));
        menu.Append(CM_DEL, T("Delete (Del)"));
        menu.AppendSeparator();
    }
    menu.Append(CM_SCRIPT, T("Add Script here"));
    menu.Append(CM_SCOPE_ADD, T("Add Scope here"));
    menu.Append(CM_JOIN, T("Add Join here"));
    menu.Append(CM_SPLIT, T("Add Split here"));
    menu.AppendSeparator();
    menu.Append(CM_PASTE, T("Paste (Ctrl+V)"));
    menu.Append(CM_FIT, T("Fit view"));

    const int choice = GetPopupMenuSelectionFromUser(menu);
    if (choice == wxID_NONE) return;
    switch (choice) {
        case CM_EDIT: if (onEditScript) onEditScript(nid); break;
        case CM_SCOPE: if (onOpenScope) onOpenScope(nid); break;
        case CM_DUP: duplicateSelection(); break;
        case CM_DEL: deleteSelection(); break;
        case CM_SCRIPT: addNodeContext(0, {}, mouseWorld_); break;
        case CM_SCOPE_ADD: addNodeContext(1, {}, mouseWorld_); break;
        case CM_JOIN: addNodeContext(2, {}, mouseWorld_); break;
        case CM_SPLIT: addNodeContext(3, {}, mouseWorld_); break;
        case CM_PASTE: pasteClipboard(); break;
        case CM_FIT: centerOnGraph(); break;
        default: break;
    }
}

void GraphCanvas::addNodeContext(int kind, const std::string& plugin, Vec2 world) {
    Node* n = nullptr;
    if (kind == 0) n = e_->addScript(world.x, world.y);
    else if (kind == 1) n = e_->addScope(world.x, world.y);
    else if (kind == 2) n = e_->addMux(world.x, world.y, 2, 1);
    else if (kind == 3) n = e_->addMux(world.x, world.y, 1, 2);
    else if (kind == 4) n = e_->addPlugin(plugin, world.x, world.y);
    if (!n) return;
    sel_.clear();
    sel_.insert(n->id);
    selWires_.clear();
    notifySelection();
    Refresh(false);
    if (onDocumentChanged) onDocumentChanged();
}

void GraphCanvas::onMiddleDown(wxMouseEvent& ev) {
    eventToImGui(ev);
    imui::mouseButton(2, true);
    if (imui::wantsMouseNow() || widgetHit(ev.GetPosition())) { Refresh(false); return; }
    if (HasCapture()) return;
    panning_ = true;
    panStart_ = ev.GetPosition();
    panCamX_ = camX_;
    panCamY_ = camY_;
    CaptureMouse();
}

void GraphCanvas::onMiddleUp(wxMouseEvent& ev) {
    if (imui::enabled()) {
        eventToImGui(ev);
        imui::mouseButton(2, false);
    }
    panning_ = false;
    if (HasCapture()) ReleaseMouse();
}

void GraphCanvas::onMouseWheel(wxMouseEvent& ev) {
    eventToImGui(ev);
    imui::mouseWheel((float)ev.GetWheelAxis() == 0 ? 0.0f
                                                  : (float)ev.GetWheelDelta() / 120.0f,
                     (float)ev.GetWheelRotation() / 120.0f);
    if (imui::wantsMouseNow() || widgetHit(ev.GetPosition())) return;
    const int rot = ev.GetWheelRotation();
    if (!rot) return;
    const float factor = std::pow(1.12f, rot / 120.0f);
    zoomBy(factor, ev.GetPosition());
}

void GraphCanvas::zoomBy(float factor, wxPoint anchor) {
    const Vec2 before = s2w({(float)anchor.x, (float)anchor.y});
    zoom_ = std::max(0.15f, std::min(4.0f, zoom_ * factor));
    camX_ = before.x - anchor.x / zoom_;
    camY_ = before.y - anchor.y / zoom_;
    Refresh(false);
}

void GraphCanvas::zoomReset() {
    centerOnGraph();
}

void GraphCanvas::requestFit() {
    frameDelay_ = 12;   // paints until re-fit (pane layout settles)
}

void GraphCanvas::centerOnGraph() {
    if (e_->g.nodes.empty()) {
        zoom_ = 1;
        camX_ = camY_ = 0;
        Refresh(false);
        return;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const auto& n : e_->g.nodes) {
        FRect b = nodeBox(n);
        x0 = std::min(x0, b.x); y0 = std::min(y0, b.y);
        x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h);
    }
    const wxSize sz = GetClientSize();
    const float bw = std::max(1.0f, x1 - x0), bh = std::max(1.0f, y1 - y0);
    zoom_ = std::min(1.0f, std::min((sz.x - 80.0f) / bw, (sz.y - 80.0f) / bh));
    if (zoom_ < 0.15f) zoom_ = 0.15f;
    camX_ = (x0 + x1) * 0.5f - sz.x / (2 * zoom_);
    camY_ = (y0 + y1) * 0.5f - sz.y / (2 * zoom_);
    Refresh(false);
}

void GraphCanvas::onDoubleClick(wxMouseEvent& ev) {
    mouse_ = ev.GetPosition();
    mouseWorld_ = s2w({(float)mouse_.x, (float)mouse_.y});
    const int gid = groupHeaderAt(mouseWorld_);
    if (gid >= 0) {
        if (Group* gr = e_->g.findGroup(gid)) {
            e_->checkpoint();
            gr->collapsed = !gr->collapsed;
            Refresh(false);
        }
        return;
    }
    const int nid = nodeAt(mouseWorld_);
    if (nid >= 0) openNode(nid);
}

void GraphCanvas::openNode(int id) {
    const Node* n = e_->g.find(id);
    if (!n) return;
    if (n->type == NodeType::SCRIPT && onEditScript) onEditScript(id);
    else if (n->type == NodeType::SCOPE && onOpenScope) onOpenScope(id);
}

void GraphCanvas::onKeyDown(wxKeyEvent& ev) {
    switch (ev.GetKeyCode()) {
        case WXK_DELETE:
        case WXK_BACK:
            deleteSelection();
            return;
        case WXK_ESCAPE:
            pending_ = false;
            rubber_ = false;
            moving_ = false;
            panning_ = false;
            sel_.clear();
            selWires_.clear();
            notifySelection();
            Refresh(false);
            return;
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN: {
            if (sel_.empty()) break;
            const float d = ev.ShiftDown() ? 10.0f : 2.0f;
            e_->checkpoint();
            for (int id : sel_) {
                if (Node* n = e_->g.find(id)) {
                    if (ev.GetKeyCode() == WXK_LEFT) n->pos.x -= d;
                    if (ev.GetKeyCode() == WXK_RIGHT) n->pos.x += d;
                    if (ev.GetKeyCode() == WXK_UP) n->pos.y -= d;
                    if (ev.GetKeyCode() == WXK_DOWN) n->pos.y += d;
                }
            }
            e_->g.touch();
            Refresh(false);
            if (onDocumentChanged) onDocumentChanged();
            return;
        }
        default:
            break;
    }
    ev.Skip();
}

void GraphCanvas::selectNode(int id, bool add) {
    if (!add) {
        sel_.clear();
        selWires_.clear();
    }
    if (id >= 0) sel_.insert(id);
    notifySelection();
    Refresh(false);
}

void GraphCanvas::selectAll() {
    sel_.clear();
    for (const auto& n : e_->g.nodes) sel_.insert(n.id);
    selWires_.clear();
    notifySelection();
    Refresh(false);
}

void GraphCanvas::clearSelection() {
    sel_.clear();
    selWires_.clear();
    notifySelection();
    Refresh(false);
}

void GraphCanvas::notifySelection() {
    for (auto it = sel_.begin(); it != sel_.end();)
        if (!e_->g.find(*it)) it = sel_.erase(it);
        else ++it;
    for (auto it = selWires_.begin(); it != selWires_.end();) {
        bool ok = false;
        for (const auto& e : e_->g.edges)
            if (e.from == std::get<0>(*it) && e.port == std::get<1>(*it) &&
                e.to == std::get<2>(*it) && e.inPort == std::get<3>(*it)) {
                ok = true;
                break;
            }
        if (!ok) it = selWires_.erase(it);
        else ++it;
    }
    if (onSelectionChanged) onSelectionChanged();
}

void GraphCanvas::deleteSelection() {
    if (sel_.empty() && selWires_.empty()) return;
    e_->removeSelection(sel_, selWires_);
    sel_.clear();
    selWires_.clear();
    notifySelection();
    Refresh(false);
    if (onDocumentChanged) onDocumentChanged();
}

void GraphCanvas::copySelection() {
    clip_.clear();
    clipEdges_.clear();
    pasteOffset_ = 0;
    if (sel_.empty()) return;
    float cx = 0, cy = 0;
    int count = 0;
    for (int id : sel_)
        if (const Node* n = e_->g.find(id)) {
            cx += n->pos.x;
            cy += n->pos.y;
            ++count;
        }
    if (!count) return;
    cx /= count;
    cy /= count;
    for (int id : sel_) {
        const Node* n = e_->g.find(id);
        if (!n) continue;
        ClipNode c;
        c.node = *n;
        c.node.pinst.reset();
        c.node.prog.reset();
        c.node.pos = {n->pos.x - cx, n->pos.y - cy};
        if (n->type == NodeType::PLUGIN && n->pinst && n->pinst->saveFn)
            if (char* s = n->pinst->callSave(n->pinst->handle)) {
                c.blob = s;
                ::free(s);
            }
        clip_.push_back(std::move(c));
    }
    for (const auto& ed : e_->g.edges)
        if (sel_.count(ed.from) && sel_.count(ed.to)) clipEdges_.push_back(ed);
}

void GraphCanvas::pasteClipboard() {
    if (clip_.empty()) return;
    e_->checkpoint();
    pasteOffset_ += 24;
    const float ox = pasteOffset_, oy = pasteOffset_;
    std::map<int, int> remap;
    const float cx = spawnWorld().x;
    const float cy = spawnWorld().y;
    for (auto& c : clip_) {
        Node n = c.node;
        const int oldId = n.id;
        n.id = e_->g.nextId++;
        n.pos = {cx + n.pos.x + ox, cy + n.pos.y + oy};
        n.pinst.reset();
        n.prog.reset();
        n.value = 0;
        n.out = {};
        for (auto& s : n.outTxt) s.clear();
        n.outValTxt = {};
        n.pv = {};
        n.nonFiniteCount = n.limits = 0;
        if (n.type == NodeType::PLUGIN && !n.plugin.empty()) {
            n.pinst = e_->plugins.createInstance(n.plugin);
            if (n.pinst && !c.blob.empty() && n.pinst->loadFn)
                n.pinst->callLoad(n.pinst->handle, c.blob.c_str());
        }
        e_->g.nodes.push_back(n);
        remap[oldId] = n.id;
    }
    for (const auto& ed : clipEdges_) {
        auto a = remap.find(ed.from);
        auto b = remap.find(ed.to);
        if (a != remap.end() && b != remap.end())
            e_->g.connect(a->second, ed.port, b->second, ed.inPort);
    }
    for (auto& n : e_->g.nodes)
        if (n.type == NodeType::SCRIPT && !n.prog) e_->g.setScript(n.id, n.script);
    e_->g.touch();
    sel_.clear();
    for (const auto& kv : remap) sel_.insert(kv.second);
    selWires_.clear();
    notifySelection();
    Refresh(false);
    if (onDocumentChanged) onDocumentChanged();
}

void GraphCanvas::duplicateSelection() {
    copySelection();
    pasteClipboard();
}

wxPoint GraphCanvas::spawnWorld() const {
    const wxSize sz = GetClientSize();
    Vec2 w = s2w({sz.x * 0.5f, sz.y * 0.5f});
    return wxPoint((int)w.x, (int)w.y);
}

wxPoint GraphCanvas::mouseWorld() const {
    Vec2 w = s2w({(float)mouse_.x, (float)mouse_.y});
    return wxPoint((int)w.x, (int)w.y);
}

void GraphCanvas::refreshFromModel() {
    notifySelection();
    Refresh(false);
}
