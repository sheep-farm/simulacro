// simulacro — controle nativo: botão pushdown (por herança de nsui::Pushdown)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Família "ui": o push (Botão pushdown [0;1]) construído POR HERANÇA de
// nsui::Pushdown (src/sdk/nodesim_ui_base.h) — o esqueleto ABI 14 vem da
// base; o controle só declara identidade/cor e a tabela de ACTS. Este
// arquivo é também o exemplo "como escrever um controle herdável como
// plugin de terceiros" (docs/abi.md).
//
//   push  Botão pushdown clicável no canvas: out[0] = 1 enquanto a FACE
//         estiver pressionada (NS_ACT_HOLD) e 0 na soltura. A borda do box
//         fica como margem livre (NS_PLUGIN_ACTS_IN), então SELECIONAR e
//         ARRASTAR o nó continuam possíveis. Terceiros derivam de
//         nsui::Pushdown e herdam o comportamento (docs/abi.md).
#include <plugin.h>               // facade do host (core/plugin.h)
#include "nodesim_ui_base.h"      // nsui::Control / Host / makeType (sdk)

#include <cmath>
#include <string>

namespace {

static const int kIn = 1;   // entradas no tipo (1)
static const int kOut = 1;  // portas de saída (1)

// ------------------------------------------------------------------ botão
// Botão pushdown POR HERANÇA de nsui::Pushdown (src/sdk/nodesim_ui_base.h):
// a base traz estado, hooks e o desenho autoral; aqui restam a identidade,
// a cor e a tabela de ACTS (o contrato que diz ao host *onde* clicar):
//   {"push", NS_ACT_HOLD, 0} = params[0] (pulsa) vai a 1 enquanto o botão
//   está pressionado no canvas e volta a 0 na soltura (pushdown de verdade).
// Um plugin de terceiros que queira o MESMO comportamento deriva de
// nsui::Pushdown (ou declara NS_PLUGIN_ACTS no seu próprio param) — o
// host trata qualquer tipo assim, sem pré-conhecer nada.
struct Botao : nsui::Pushdown {
    static constexpr const char* kTypeId = "push";
    static constexpr const char* kLabel = "Botão (pushdown)";
    static void defColor(int rgb[3]) {
        rgb[0] = 120;
        rgb[1] = 235;
        rgb[2] = 180;
    }
};

// NS_PLUGIN_ACTS_IN: clique na FACE (inset de margem interna), a borda do
// box fica livre para selecionar/arrastar o nó.
static const NsAct kActsBotao[] = {
    {"push", NS_ACT_HOLD, 0},
};
NS_PLUGIN_ACTS_IN(kActsBotao, 1, Botao::kPressInset);

} // namespace

// --------------------------------------------------------------- plugin
// Tabela de tipos gerada a partir das subclasses (herança na prática).
extern "C" const NsPlugin* nodesim_plugin() {
    static const NsNodeType kTipos[] = {{nsui::makeType<Botao>()}};
    static const NsPlugin kPlugin =
        NS_PLUGIN_BUILTIN("ui", "1.1", 1, kTipos, "Control");
    return &kPlugin;
}
