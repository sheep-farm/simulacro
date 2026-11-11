// simulacro — testes de comportamento da família de controles (headless)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Testa os controles POR HERANÇA direto na base (sem host, sem SDL): a
// semântica do step (botão físico + nível de entrada), o desenho autoral
// (cores empacotadas no contrato 0xRRGGBBAA e textos), o blob
// params/texts (round-trip) e a chave TOGGLE. Estes testes existem porque
// a família inteira de controles de terceiros atravessa este caminho.
//
// Saída: "ok <nome>" por grupo; FAIL + exit 1 no primeiro desacordo.

#include "nodesim_ui_base.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int gFail = 0;
void ok(const char* name) { std::printf("ok   %s\n", name); }
void fail(const char* name, const char* what) {
    std::printf("FAIL %s: %s\n", name, what);
    gFail = 1;
}
void expect(bool cond, const char* name, const char* what) {
    if (cond) ok(name);
    else fail(name, what);
}

// ---- subclasses "de terceiros" (iguais às de plugins/controles.cpp) ----
struct BotaoT : nsui::Pushdown {
    static constexpr const char* kTypeId = "push";
    static constexpr const char* kLabel = "Botão (pushdown)";
    void defColor(int rgb[3]) const override {
        rgb[0] = 120;
        rgb[1] = 235;
        rgb[2] = 180;
    }
};
struct ChaveT : nsui::Switch {
    static constexpr const char* kTypeId = "switch";
    static constexpr const char* kLabel = "Chave (toggle)";
    void defColor(int rgb[3]) const override {
        rgb[0] = 70;
        rgb[1] = 130;
        rgb[2] = 235;
    }
};

// ---- captura do desenho (o "host" de teste) ----
struct Capture {
    std::vector<unsigned> fills;       // cor de cada g->rect (0xRRGGBBAA)
    std::vector<std::string> textos;   // cada g->text
};
static void capRect(void* ctx, int, int, int, int, unsigned c) {
    static_cast<Capture*>(ctx)->fills.push_back(c);
}
static void capLine(void*, int, int, int, int, unsigned) {}
static void capText(void* ctx, int, int, const char* s, unsigned) {
    static_cast<Capture*>(ctx)->textos.push_back(s ? s : "");
}
static int capTextW(void*, const char* s) {
    return s ? (int)std::strlen(s) * 7 : 0;
}

Capture drawOnce(nsui::Control& c, bool pressed) {
    Capture cap;
    NsDraw g{&cap, capRect, capLine, capText, capTextW, "botão"};
    NsRect area{0, 0, BotaoT::kPrefW, BotaoT::kPrefH};
    c.draw(&g, &area);
    (void)pressed;
    return cap;
}

unsigned unpackR(unsigned c) { return (c >> 24) & 0xFF; }
unsigned unpackG(unsigned c) { return (c >> 16) & 0xFF; }
unsigned unpackB(unsigned c) { return (c >> 8) & 0xFF; }
unsigned unpackA(unsigned c) { return c & 0xFF; }

bool isColor(unsigned c, int r, int g, int b) {
    return unpackR(c) == (unsigned)r && unpackG(c) == (unsigned)g &&
           unpackB(c) == (unsigned)b && unpackA(c) == 0xFF;
}

// ---- contexto de simulação com um socket de entrada ----
struct Ctx {
    double in[4] = {0, 0, 0, 0};
    int sockOff[2] = {0, 1};
    NsContext make() {
        NsContext c{};
        c.in = in;
        c.nin = 1;
        c.sockOff = sockOff;
        c.nsock = 1;
        return c;
    }
};

} // namespace

int main() {
    // ---- 1) pushdown: pulso do botão físico (params[0]) ----
    {
        BotaoT b;
        Ctx cx;
        NsContext c = cx.make();
        expect(b.step(c) == 0.0, "push idle out 0", "soltto deveria valer 0");
        b.paramSet(0, 1.0);
        expect(b.step(c) == 1.0, "push pressed out 1", "pulsa=1 deveria valer 1");
        b.paramSet(0, 0.0);
        expect(b.step(c) == 0.0, "push released out 0", "pulsa=0 deveria valer 0");
    }

    // ---- 2) pushdown: nível de entrada (a ENTRADA pressiona) ----
    {
        BotaoT b;
        Ctx cx;
        cx.in[0] = 1.0;
        NsContext c = cx.make();
        expect(b.step(c) == 1.0, "push level-in out 1",
               "entrada 1 deveria simular pressão");
        cx.in[0] = 0.0;
        expect(b.step(c) == 0.0, "push level-out out 0",
               "entrada 0 deveria soltar (sem EXT)");
        // com botão FÍSICO apertado e entrada solta: 1 (pulsa manda)
        b.paramSet(0, 1.0);
        expect(b.step(c) == 1.0, "push physical while input 0",
               "pulsa=1 manda mesmo com entrada 0");
    }

    // ---- 3) pushdown: hooks de transição ----
    {
        BotaoT b;
        int pressed = 0, released = 0;
        struct : nsui::Pushdown {
            int* p; int* r;
            void onPressed() override { ++*p; }
            void onReleased() override { ++*r; }
        } h;
        h.p = &pressed; h.r = &released;
        h.paramSet(0, 1.0);
        h.paramSet(0, 1.0); // repetida: sem hook
        h.paramSet(0, 0.0);
        expect(pressed == 1 && released == 1, "push hooks once per transition",
               "hooks deveriam disparar 1x cada");
    }

    // ---- 4) pushdown: design (cores no contrato 0xRRGGBBAA + textos) ----
    {
        BotaoT b;
        b.textSet(0, "START");     // rotulo
        b.textSet(1, "F5B361");    // face solta
        b.textSet(2, "C12929");    // face pressionada
        Capture loose = drawOnce(b, false);
        expect(!loose.fills.empty(), "push draw paints", "draw não pintou nada");
        // o 2º rect é o CORPO da face (o 1º é a moldura)
        const unsigned bodyLoose = loose.fills[1];
        expect(isColor(bodyLoose, 245, 179, 97), "push loose face F5B361",
               "face solta não é o hex do texto (RRGGBBAA quebrado?)");
        b.paramSet(0, 1.0);
        Capture press = drawOnce(b, true);
        const unsigned bodyPress = press.fills[1];
        expect(isColor(bodyPress, 193, 41, 41), "push pressed face C12929",
               "face pressionada não é o hex do texto");
        expect(loose.textos.size() == 1 && loose.textos[0] == "START",
               "push caption START", "rótulo não chega ao texto");
        // EXT: pressão EXTERNA (nível) sem botão físico
        BotaoT b2;
        Ctx cx2;
        cx2.in[0] = 1.0;
        b2.step(cx2.make());
        Capture ext = drawOnce(b2, false);
        expect(ext.textos.size() == 1 &&
                   ext.textos[0].find("EXT") != std::string::npos,
               "push EXT marker", "nível externo deveria marcar EXT");
    }

    // ---- 5) blob: round-trip params + texts (formato dos built-ins) ----
    {
        BotaoT b;
        b.paramSet(0, 1.0);
        b.textSet(0, "VAI");
        b.textSet(1, "112233");
        b.textSet(2, "ABCDEF");
        const std::string blob = b.saveBlob();
        BotaoT b2;
        expect(b2.loadBlob(blob) > 0, "push blob load applied",
               "loadBlob não aplicou nada");
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.17g", b2.paramGet(0));
        expect(std::string(buf) == "1", "push blob param round-trip",
               "pulsa não sobreviveu ao blob");
        expect(std::string(b2.textGet(0)) == "VAI", "push blob text 0",
               "rotulo não sobreviveu ao blob");
        expect(std::string(b2.textGet(1)) == "112233", "push blob text 1",
               "face hex não sobreviveu ao blob");
        // fallback posicional (blob só com números, convenção pgain)
        BotaoT b3;
        expect(b3.loadBlob("1") > 0, "push blob positional",
               "blob posicional deveria aplicar params[0]=1");
    }

    // ---- 6) switch: TOGGLE mantém; sem entrada de nível ----
    {
        ChaveT s;
        Ctx cx;
        NsContext c = cx.make();
        expect(s.step(c) == 0.0, "switch idle out 0", "desligada deveria valer 0");
        s.paramSet(0, 1.0);
        expect(s.step(c) == 1.0, "switch on out 1", "ligada deveria valer 1");
        s.paramSet(0, 1.0); // repetida: fica
        expect(s.step(c) == 1.0, "switch stays on", "TOGGLE mantém o valor");
        s.paramSet(0, 0.0);
        expect(s.step(c) == 0.0, "switch off out 0", "desligada de novo");
        // level-in ignorado (kDefIn=0: sem fio, NsContext vazio não explode)
        NsContext empty{};
        expect(s.step(empty) == 0.0, "switch empty context safe",
               "switch deveria tolerar contexto sem sockets");
    }

    // ---- 7) switch: design ON/OFF ----
    {
        ChaveT s;
        s.textSet(1, "0A84FF"); // cor ON
        Capture off = drawOnce(s, false);
        expect(!off.fills.empty(), "switch draw paints", "draw não pintou nada");
        const unsigned bodyOff = off.fills[1];
        // OFF = derivado escuro da identidade (70,130,235)/3 = (23,43,78)
        expect(isColor(bodyOff, 23, 43, 78), "switch off color derived",
               "cor OFF deveria ser a identidade escurecida");
        s.paramSet(0, 1.0);
        Capture on = drawOnce(s, true);
        const unsigned bodyOn = on.fills[1];
        expect(isColor(bodyOn, 10, 132, 255), "switch on color 0A84FF",
               "cor ON não é o hex do texto");
        expect(on.textos.size() == 1 &&
                   on.textos[0].find('I') != std::string::npos,
               "switch shows I when on", "estado ON deveria aparecer no texto");
    }

    // ---- 8) identidade: makeType preenche a tabela ABI 14 ----
    {
        NsNodeType t = nsui::makeType<ChaveT>();
        expect(std::string(t.id) == "switch" && t.num_params == 1 &&
                   t.num_text == 3 && t.shape == NS_SHAPE_CUSTOM &&
                   t.self_out == 1,
               "makeType switch table", "tabela ABI incompleta");
        expect(t.text_names != nullptr && std::string(t.text_names[0]) == "rotulo",
               "makeType texts wired", "texts[] não chegou à tabela");
    }

    if (gFail) return 1;
    std::printf("all control tests passed\n");
    return 0;
}
