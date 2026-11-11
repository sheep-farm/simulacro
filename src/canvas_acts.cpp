// simulacro — atos dos controles no canvas (NS_PLUGIN_ACTS, clique + teclado)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// O contrato de clique (ABI 14 aditivo): o ato está DECLARADO pelo plugin
// (NS_PLUGIN_ACTS / NS_PLUGIN_ACTS_IN); o host aperta/solta/alterna o param
// no CORPO do nó, sem arrasto e sem checkpoint de undo (interação de painel).
// A margem interna (inset) segue comportamento normal de nó (selecionar/
// arrastar). O teclado reaproveita o mesmo ato: com um controle selecionado,
// Space aperta/solta (HOLD) e Enter alterna (TOGGLE).

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

bool GraphCanvas::beginActPress(const Vec2& world, bool add) {
    // Clique no corpo de um controle (NS_PLUGIN_ACTS): decide FACE vs MARGEM
    // pelo inset declarado e executa o ato. Devolve true quando o clique
    // pertence ao controle (o chamador não segue para seleção/arrasto).
    const int nidAct = nodeAt(world);
    if (nidAct < 0) return false;
    const Node* nA = e_->g.find(nidAct);
    if (!nA) return false;
    const Engine::ClickAct act = e_->clickAct(*nA);
    if (act.kind != NS_ACT_HOLD && act.kind != NS_ACT_TOGGLE) return false;
    const FRect nbAct = nodeBox(*nA);
    const bool face =
        act.inset > 0
            ? world.x >= nbAct.x + act.inset &&
                  world.x <= nbAct.x + nbAct.w - act.inset &&
                  world.y >= nbAct.y + act.inset &&
                  world.y <= nbAct.y + nbAct.h - act.inset
            : true; // inset 0 (plugins antigos): corpo inteiro
    if (!face) {
        // clique na MARGEM: comportamento padrão do nó (selecionar/arrastar).
        // Não aperta o botão.
        actNode_ = -1;
        actParam_ = -1;
        return false;
    }
    if (act.kind == NS_ACT_HOLD) {
        actNode_ = nidAct;
        actParam_ = act.param;
        e_->runClickAct(*nA, act, true);
        if (onParamEdited) onParamEdited(nidAct);
        if (!add) {
            sel_.clear();
            selWires_.clear();
            sel_.insert(nidAct);
        }
        notifySelection();
        Refresh(false);
        return true;
    }
    // NS_ACT_TOGGLE: cada clique alterna e o valor FICA.
    e_->toggleClickAct(*nA, act);
    if (onParamEdited) onParamEdited(nidAct);
    if (!add) {
        sel_.clear();
        selWires_.clear();
        sel_.insert(nidAct);
    }
    notifySelection();
    Refresh(false);
    return true;
}

bool GraphCanvas::endActPress(bool releaseCapture) {
    // Fim da pressão do controle (HOLD): encerra TAMBÉM o estado de arrasto
    // aberto no down (dragging_=true), senão o PRÓXIMO clique no botão cai
    // fora ("if (dragging_...) return").
    if (actNode_ < 0 || actParam_ < 0) return false;
    dragging_ = false;
    if (releaseCapture && HasCapture()) ReleaseMouse(); // captura aberta no down
    if (Node* nRel = e_->g.find(actNode_)) {
        const Engine::ClickAct act = e_->clickAct(*nRel);
        if (act.kind == NS_ACT_HOLD) e_->runClickAct(*nRel, act, false);
        if (onParamEdited) onParamEdited(actNode_);
    }
    actNode_ = -1;
    actParam_ = -1;
    Refresh(false);
    return true;
}

bool GraphCanvas::actKey(const wxKeyEvent& ev, bool down) {
    const int id = singleSelection();
    if (id < 0) return false;
    const Node* n = e_->g.find(id);
    if (!n) return false;
    const Engine::ClickAct act = e_->clickAct(*n);
    if (act.kind != NS_ACT_HOLD && act.kind != NS_ACT_TOGGLE) return false;
    const int code = ev.GetKeyCode();
    if (code != WXK_SPACE && code != WXK_RETURN && code != WXK_NUMPAD_ENTER)
        return false;
    if (act.kind == NS_ACT_TOGGLE) {
        if (!down) return true; // TOGGLE: a soltura não repete
        e_->toggleClickAct(*n, act);
        if (onParamEdited) onParamEdited(id);
        Refresh(false);
        return true;
    }
    // HOLD: Space mantém (down press/up release, repeats ignorados); Enter
    // toca um pulso (down+up), igual ao clique da face.
    if (code == WXK_RETURN || code == WXK_NUMPAD_ENTER) {
        e_->runClickAct(*n, act, down);
        if (onParamEdited) onParamEdited(id);
        Refresh(false);
        return true;
    }
    if (down) {
        if (kbActHeld_) return true; // auto-repeat do teclado
        kbActHeld_ = true;
        e_->runClickAct(*n, act, true);
    } else {
        if (!kbActHeld_) return true;
        kbActHeld_ = false;
        e_->runClickAct(*n, act, false);
    }
    if (onParamEdited) onParamEdited(id);
    Refresh(false);
    return true;
}

void GraphCanvas::actKeyUp(const wxKeyEvent& ev) {
    if (!kbActHeld_ || ev.GetKeyCode() != WXK_SPACE) return;
    kbActHeld_ = false;
    if (const Node* n = e_->g.find(singleSelection())) {
        const Engine::ClickAct act = e_->clickAct(*n);
        if (act.kind == NS_ACT_HOLD && act.param >= 0) {
            e_->runClickAct(*n, act, false);
            if (onParamEdited) onParamEdited(n->id);
            Refresh(false);
        }
    }
}

