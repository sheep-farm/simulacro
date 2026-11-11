// simulacro — fase 4: nós como janelas ImGui (chrome sobre o raster SDL)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// O renderer SDL mantém os pintores de DADO (corpo, rótulos de porta,
// valores, draw do plugin) e os fios: como sempre. Esta camada adiciona o
// CHROME: moldura arredondada com a cor do tipo, barra de título, texto,
// contorno de hover/seleção e o fio pendente na lista foreground (acima dos
// nós). Plugins custom (NS_SHAPE_CUSTOM) ficam de fora: a silhueta inteira
// é autoral. Padrão do build: dentro (SIMULACRO_NODE_WINDOW=1).

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

#if defined(SIMULACRO_NODE_WINDOW)
// ---- fase 4: node = ImGui window (chrome) ----
// O renderer SDL mantém os pintores de DADO (corpo, rótulos/valores de
// porta, sparklines, draw do plugin) e os fios: exatamente como antes.
// A camada ImGui adiciona o CHROME: moldura arredondada com a cor do
// tipo, barra de título e texto, contorno de hover/seleção e o fio
// pendente na lista foreground (que passa acima dos nós). Plugins
// custom (NS_SHAPE_CUSTOM) ficam de fora: a silhueta inteira é autoral.
void GraphCanvas::drawNodeChrome(const Node& n) {
    FRect wb = nodeBox(n);
    Vec2 tl = w2s({wb.x, wb.y}), br = w2s({wb.x + wb.w, wb.y + wb.h});
    if (br.x < 0 || br.y < 0 || tl.x > bmp_.width() || tl.y > bmp_.height()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const bool selected = sel_.count(n.id) > 0;
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
    const int titleH = std::max(2, (int)(NODE_TITLE_H * zoom_));
    const float rounding = 6.0f;

    // hover/selection outline outside the window
    if (selected || n.id == hoverNode_) {
        const ImVec2 o1(tl.x - (selected ? 3.0f : 2.0f), tl.y - (selected ? 3.0f : 2.0f));
        const ImVec2 o2(br.x + (selected ? 3.0f : 2.0f), br.y + (selected ? 3.0f : 2.0f));
        dl->AddRect(o1, o2, selected ? IM_COL32(kUi.strong.r, kUi.strong.g,
                                       kUi.strong.b, 255)
                                     : IM_COL32((int)(kUi.strong.r * .75f),
                                                (int)(kUi.strong.g * .75f),
                                                (int)(kUi.strong.b * .75f), 255),
                    rounding);
    }
    // dark title bar painted over the SDL one (window header feel)
    const SDL_Color tbBg = mixColor({cr, cg, cb, 255}, kUi.panel, 0.72f);
    dl->AddRectFilled(ImVec2(tl.x, tl.y), ImVec2(br.x, tl.y + titleH),
                      IM_COL32(tbBg.r, tbBg.g, tbBg.b, 255), rounding,
                      ImDrawFlags_RoundCornersTop);
#if defined(SIMULACRO_NODE_WINDOW_OVERWRITE_FRAME)
    // (opt-in) also re-paint the window border in ImGui to win over the SDL one
    dl->AddRect(ImVec2(tl.x, tl.y), ImVec2(br.x, br.y),
                IM_COL32(cr, cg, cb, 255), rounding);
#else
    (void)cr;
#endif
    if (zoom_ >= 0.6f) {
        const std::string ntitle = e_->nodeLabel(n);
        const ImU32 fg = contrastColor(tbBg).r;
        dl->AddText(ImVec2(tl.x + 5, tl.y + 3), fg, ntitle.c_str());
    }
}

void GraphCanvas::drawAllFrameChrome() {
    if (!ImGui::GetCurrentContext()) return;
    for (const auto& n : e_->g.nodes) {
        if (nodeHidden(n.id)) continue;
        if (n.type == NodeType::PLUGIN && n.shape == NS_SHAPE_CUSTOM) continue;
        drawNodeChrome(n);
    }
}

// Pending wire in the *foreground* draw list: passes above node windows.
void GraphCanvas::drawPendingIm() {
    if (!pending_ || !ImGui::GetCurrentContext()) return;
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
    // same cubic as core wirePoint(): u = 1 - t
    const float off = std::max(30.0f, std::hypot(p2.x - p1.x, p2.y - p1.y) * 0.5f);
    Vec2 u1 = d1, u2 = d2;
    {
        float l1 = std::hypot(d1.x, d1.y), l2 = std::hypot(d2.x, d2.y);
        if (l1 > 0.001f) u1 = {d1.x / l1, d1.y / l1};
        if (l2 > 0.001f) u2 = {d2.x / l2, d2.y / l2};
    }
    auto point = [&](float t) {
        const float u = 1.0f - t;
        return Vec2{
            u * u * u * p1.x + 3 * u * u * t * (p1.x + u1.x * off) +
                3 * u * t * t * (p2.x - u2.x * off) + t * t * t * p2.x,
            u * u * u * p1.y + 3 * u * u * t * (p1.y + u1.y * off) +
                3 * u * t * t * (p2.y - u2.y * off) + t * t * t * p2.y,
        };
    };
    const ImU32 color =
        IM_COL32(kUi.highlight.r, kUi.highlight.g, kUi.highlight.b, 255);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (curve_) {
        const int N = 24;
        Vec2 prev = p1;
        for (int i = 1; i <= N; ++i) {
            Vec2 q = point((float)i / N);
            dl->AddLine(ImVec2(prev.x, prev.y), ImVec2(q.x, q.y), color, 2.0f);
            prev = q;
        }
    } else {
        dl->AddLine(ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), color, 2.0f);
    }
}
#endif // SIMULACRO_NODE_WINDOW
