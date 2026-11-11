// simulacro — auto-refit da vista (carga + mudança de pane, fome de paint)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// O fit do grafo vive aqui: em repouso (PAUSED) o canvas só pinta quando há
// dano, então um fit "adiado" (N paints) nunca executa. Os refits desta
// unidade são IMEDIATOS e limitados (fitRound_, ≤3 rodadas):
//   - centerOnGraph(): ajusta zoom/câmera e grava o tamanho do pane (lastFit);
//   - onSize(): refit quando o pane muda ≥25% contra o último fit;
//   - ensureInView(): refit quando o grafo está <30% dentro da vista (um
//     calço de poucos px conta como invisível — o fit rodou em outro pane).
// Diagnóstico opcional: NODESIM_DEBUG_UI (uma linha por frame).

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

void GraphCanvas::ensureInView() {
    // Reflexo da carga: quando TODOS os nós estão fora da área visível após
    // os panes assentarem, refit (limitado a 3 rodadas; não afeta o zoom do
    // usuário quando eles estão na tela).
    if (fitRound_ == 0 || fitRound_ >= 3 || e_->g.nodes.empty()) return;
    static const bool dbg = std::getenv("NODESIM_DEBUG_UI") != nullptr;
    FRect bb{1e9f, 1e9f, -1e9f, -1e9f};
    for (const auto& n : e_->g.nodes) {
        FRect b = nodeBox(n);
        bb.x = std::min(bb.x, b.x);
        bb.y = std::min(bb.y, b.y);
        bb.w = std::max(bb.w, b.x + b.w);
        bb.h = std::max(bb.h, b.y + b.h);
    }
    // box visível (em coords de mundo) do viewport atual
    Vec2 vTL = s2w({0, 0});
    Vec2 vBR = s2w({(float)bmp_.width(), (float)bmp_.height()});
    const FRect view{vTL.x, vTL.y, vBR.x - vTL.x, vBR.y - vTL.y};
    // fração do grafo visível: exigir sobreposição SUBSTANCIAL (um calço de
    // poucos px conta como invisível: o fit rodou com outro tamanho de pane)
    const float ox0 = std::max(bb.x, view.x), ox1 = std::min(bb.w, view.x + view.w);
    const float oy0 = std::max(bb.y, view.y), oy1 = std::min(bb.h, view.y + view.h);
    const float frac = ox1 > ox0 && oy1 > oy0
                           ? ((ox1 - ox0) * (oy1 - oy0)) /
                                 std::max(1.0f, (bb.w - bb.x) * (bb.h - bb.y))
                           : 0.0f;
    bool vis = frac >= 0.3f;
    if (dbg)
        std::fprintf(stderr,
                     "drawNodes n=%zu zoom=%.2f cam=(%.0f,%.0f) bmp=%dx%d vis=%.2f\n",
                     e_->g.nodes.size(), (double)zoom_, (double)camX_, (double)camY_,
                     bmp_.width(), bmp_.height(), frac);
    if (vis) return;
    ++fitRound_;
    // refit imediato: em painéis parados (PAUSED) NÃO chegam mais paints,
    // então um fit "adiado" fica órfão; GetClientSize() já é o size assentado.
    centerOnGraph();
}

void GraphCanvas::requestFit() {
    frameDelay_ = 12;   // paints until re-fit (pane layout settles)
}

void GraphCanvas::consumePendingView() {
    // A view salva da sessão vence o fit UMA vez (qualquer fit a consome:
    // frameDelay, onSize, ensureInView, menu). Grafo vazio descarta-a — ela
    // pertencia a um grafo que não está mais na tela.
    if (!pendView_) return;
    pendView_ = false;
    if (e_->g.nodes.empty()) return;
    zoom_ = std::max(0.15f, std::min(4.0f, pendZoom_));
    camX_ = pendCamX_;
    camY_ = pendCamY_;
}

void GraphCanvas::centerOnGraph() {
    if (e_->g.nodes.empty()) {
        zoom_ = 1;
        camX_ = camY_ = 0;
        consumePendingView();
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
    // todo fit grava o tamanho do pane em que coube: os refits automáticos
    // (onSize/ensureInView) comparam contra ESTE tamanho, não contra o do
    // último paint — que pode nunca chegar (painel ocioso => fome de paints).
    lastFitW_ = (float)sz.x;
    lastFitH_ = (float)sz.y;
    if (fitRound_ == 0) fitRound_ = 1; // abre a cadeia dos refits automáticos
    consumePendingView();              // sessão salva vence este fit (1x)
    Refresh(false);
}

void GraphCanvas::onSize(wxSizeEvent& ev) {
    // Refit when a load's follow-up pane resize is substantial (max 3 rounds):
    // loadFile fits with the stale pane size, so load-time graphs {"caíam" da
    // view} quando o AUI terminava de assentar. Refit IMEDIATO: em repouso
    // (PAUSED) não chegam mais paints para despachar um fit adiado.
    if (fitRound_ >= 1 && fitRound_ < 3 && lastFitW_ > 1 && lastFitH_ > 1) {
        const wxSize sz = GetClientSize();
        const float dw = std::fabs((float)sz.x - lastFitW_) / std::max(1.0f, lastFitW_);
        const float dh = std::fabs((float)sz.y - lastFitH_) / std::max(1.0f, lastFitH_);
        if (dw > 0.25f || dh > 0.25f) {
            centerOnGraph();
            if (fitRound_ < 3) ++fitRound_;
            frameDelay_ = 0;
            Refresh(false);
            ev.Skip();
            return;
        }
    }
    Refresh(false);
    ev.Skip();
}

