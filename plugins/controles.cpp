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
//   push    Botão pushdown clicável no canvas: out[0] = 1 enquanto a FACE
//           estiver pressionada (NS_ACT_HOLD) e 0 na soltura. A borda do box
//           fica como margem livre (NS_PLUGIN_ACTS_IN), então SELECIONAR e
//           ARRASTAR o nó continuam possíveis. Terceiros derivam de
//           nsui::Pushdown e herdam o comportamento (docs/abi.md).
//   switch  Chave liga/desliga (por herança de nsui::Switch): cada clique na
//           FACE alterna (NS_ACT_TOGGLE) e o estado FICA; out[0] = 1 ligada.
//           Space/Enter no canvas também alternam (foco de teclado nos
//           controles).
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
    void defColor(int rgb[3]) const override {
        rgb[0] = 120;
        rgb[1] = 235;
        rgb[2] = 180;
    }
};

// ------------------------------------------------------------------ chave
// Chave liga/desliga POR HERANÇA de nsui::Switch: a base traz o estado
// (`ligado`), o blob automático e o desenho da pílula ON/OFF; o contrato do
// clique é NS_ACT_TOGGLE (cada clique alterna e o valor FICA — o oposto do
// NS_ACT_HOLD do push, que volta a 0 na soltura).
struct Chave : nsui::Switch {
    static constexpr const char* kTypeId = "switch";
    static constexpr const char* kLabel = "Chave (toggle)";
    void defColor(int rgb[3]) const override {
        rgb[0] = 70;
        rgb[1] = 130;
        rgb[2] = 235;
    }
};

// Widgets declarados (ativos no build com SIMULACRO_NODE_UI; o editor padrão
// cobre os mesmos campos): switch+led do estado e os textos rótulo/cores.
// UMA tabela por .so (as macros definem o símbolo dlsym uma única vez), com
// um spec POR TIPO.
static const NsUiItem kItensButton[] = {
    {NS_UI_SWITCH, "", 0, 0, 0, 0, 0},           // pulsa
    {NS_UI_LED,    "", 0, 0, 0, 0, 0},
    {NS_UI_VSPACE, nullptr, -1, 0, 0, 0, 0},
    {NS_UI_TEXT,   "rótulo", 0, 0, 0, 0, 0},
    {NS_UI_TEXT,   "cor face (hex)", 1, 0, 0, 0, 0},
    {NS_UI_TEXT,   "cor clique (hex)", 2, 0, 0, 0, 0},
};
static const NsUiItem kItensSwitch[] = {
    {NS_UI_SWITCH, "", 0, 0, 0, 0, 0},           // ligado
    {NS_UI_LED,    "", 0, 0, 0, 0, 0},
    {NS_UI_VSPACE, nullptr, -1, 0, 0, 0, 0},
    {NS_UI_TEXT,   "rótulo", 0, 0, 0, 0, 0},
    {NS_UI_TEXT,   "cor ON (hex)", 1, 0, 0, 0, 0},
    {NS_UI_TEXT,   "cor OFF (hex)", 2, 0, 0, 0, 0},
};
static const NsUiSpec kSpecsUi[] = {{"push", 6, kItensButton},
                                    {"switch", 6, kItensSwitch}};
NS_PLUGIN_UI(kSpecsUi, 2);

// NS_PLUGIN_ACTS_IN: clique na FACE (inset de margem interna), a borda do
// box fica livre para selecionar/arrastar o nó. O inset é DA TABELA (uma
// por .so): os dois tipos da família compartilham a mesma margem.
static const NsAct kActs[] = {
    {"push", NS_ACT_HOLD, 0},     // aperta enquanto pressionado
    {"switch", NS_ACT_TOGGLE, 0}, // alterna e o valor FICA
};
NS_PLUGIN_ACTS_IN(kActs, 2, Botao::kPressInset);

} // namespace

// --------------------------------------------------------------- plugin
// Tabela de tipos gerada a partir das subclasses (herança na prática).
extern "C" const NsPlugin* nodesim_plugin() {
    static const NsNodeType kTipos[] = {{nsui::makeType<Botao>()},
                                        {nsui::makeType<Chave>()}};
    static const NsPlugin kPlugin =
        NS_PLUGIN_BUILTIN("ui", "1.2", 2, kTipos, "Control");
    return &kPlugin;
}
