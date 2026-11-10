// simulacro — controles nativos: botão pushdown, entrada [0;1], saída [0;1]
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Família "ui": três nós de painel [0;1] construídos POR HERANÇA DE
// nsui::Control (src/sdk/nodesim_ui_base.h) — o esqueleto ABI 14
// (create/destroy/step/save/load/params/interpretation de blob) vem da base;
// cada controle só declara identidade + tabela estática de params/texts +
// estado (paramGet/paramSet) + step. Este arquivo é também o exemplo de
// "como escrever um controle como plugin de terceiros" (docs/abi.md).
//
//   push       Botão pushdown HERDÁVEL (nsui::Pushdown): out[0] = 1
//              enquanto pressionado no canvas (NS_ACT_HOLD) e 0 na soltura;
//              sem canvas, o valor entra pelo wxPropertyGrid. Terceiros
//              derivam e obtêm o mesmo comportamento (docs/abi.md).
//   entrada01  Entrada [0;1]: out[0] = `valor` (0..1). Slider no canvas com
//              a UI declarada; caso contrário, pelo editor padrão.
//   saida01    Saída [0;1]: entrada[n] [0;1]; renderiza o valor dentro do
//              corpo (draw hook do próprio plugin — visível em qualquer
//              build, independente de widgets).
#include <plugin.h>               // facade do host (core/plugin.h)
#include "nodesim_ui_base.h"      // nsui::Control / Host / makeType (sdk)

#include <cmath>
#include <string>

namespace {

static const int kIn = 1;   // entradas no tipo (1)
static const int kOut = 1;  // portas de saída (1)

// ------------------------------------------------------------------ botão
// Botɔ pushdown POR HERANÇA de nsui::Pushdown (src/sdk/nodesim_ui_base.h):
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

static const NsAct kActsBotao[] = {
    {"push", NS_ACT_HOLD, 0},
};
NS_PLUGIN_ACTS(kActsBotao, 1);

// ---------------------------------------------------------------- entrada
struct Entrada01 : nsui::Control {
    static constexpr const char* kTypeId = "entrada01";
    static constexpr const char* kLabel = "Entrada [0;1]";
    static void defColor(int rgb[3]) { rgb[0] = 130; rgb[1] = 210; rgb[2] = 255; }
    static const int kNumParams = 1;
    static const NsParam kParams[kNumParams];
    static const int kDefIn = kIn;
    static const int kPrefW = 0;
    static const int kPrefH = 0;
    static const bool kCustomShape = false;
    static const bool kSelfOut = true;
    static const int kNumOut = 1;

    double valor = 0.5; // [0;1]

    int numParams() const override { return kNumParams; }
    const NsParam* params() const override { return kParams; }
    double paramGet(int i) const override { return i == 0 ? valor : 0.0; }
    void paramSet(int i, double v) override {
        if (i == 0) {
            double c = std::isfinite(v) ? v : 0.0;
            valor = c < 0 ? 0 : (c > 1 ? 1 : c);
        }
    }
    double step(const NsContext& c) override {
        (void)c;
        return paramGet(0);
    }
};

const NsParam Entrada01::kParams[Entrada01::kNumParams] = {
    {"valor", 0, 1, 0.5, NS_PARAM_NUM, 0.01, nullptr,
     "valor publicado na saída (0..1)", 0, nullptr},
};

// ---------------------------------------------------------------- saída
struct Saida01 : nsui::Control {
    static constexpr const char* kTypeId = "saida01";
    static constexpr const char* kLabel = "Saída [0;1]";
    static void defColor(int rgb[3]) { rgb[0] = 255; rgb[1] = 205; rgb[2] = 120; }
    static const int kNumParams = 0;
    static constexpr const NsParam* kParams = nullptr;
    static const int kDefIn = kIn;
    static const int kPrefW = 0;
    static const int kPrefH = 0;
    static const bool kCustomShape = true; // corpo autorende (display)
    static const bool kSelfOut = true;
    static const int kNumOut = 0;          // sink: exatamente 0 saídas

    double valor = 0.0; // último valor visto (0..1), transiente

    double step(const NsContext& c) override {
        const double v = c.nin > 0 && c.in[0] && std::isfinite(c.in[0]) ? c.in[0] : 0.0;
        valor = v < 0 ? 0 : (v > 1 ? 1 : v);
        return 0.0;
    }
    int stepMulti(const NsContext& c, double* out, int nout) override {
        (void)c;
        (void)out;
        (void)nout;
        return 0;
    }
    // display ~ "0.83" com barra de progresso (NsDraw: apenas rect/text)
    void draw(const NsDraw* g, const NsRect* area) override {
        if (!g || !area) return;
        const unsigned frame = 0x1E1E28FF;
        const unsigned bg = 0x14141AFF;
        const unsigned fill = 0x8AD88AFF;
        const unsigned txt = 0xE8E8F0FF;
        g->rect(g->ctx, area->x, area->y, area->w, area->h, bg);
        g->rect(g->ctx, area->x, area->y, area->w, 2, frame);
        int barY = area->y + 5;
        int barH = 10;
        g->rect(g->ctx, area->x + 4, barY, area->w - 8, barH, 0x2A2A36FF);
        const int bw = static_cast<int>((area->w - 10) * valor);
        g->rect(g->ctx, area->x + 5, barY + 1, bw, barH - 2, fill);
        char buf[24];
        std::snprintf(buf, sizeof buf, "%.2f", valor);
        if (g->text)
            g->text(g->ctx, area->x + (area->w - (g->textW ? g->textW(g->ctx, buf) : 0)) / 2,
                    barY + barH + 2, buf, txt);
    }
};

} // namespace

// --------------------------------------------------------------- plugin
// A tabela de tipos é gerada a partir das subclasses (herança na prática).
extern "C" const NsPlugin* nodesim_plugin() {
    static const NsNodeType kTipos[] = {
        nsui::makeType<Botao>(),
        nsui::makeType<Entrada01>(),
        nsui::makeType<Saida01>(),
    };
    static const NsPlugin kPlugin = NS_PLUGIN_BUILTIN("ui", "1.0", 3, kTipos, "Control");
    return &kPlugin;
}
