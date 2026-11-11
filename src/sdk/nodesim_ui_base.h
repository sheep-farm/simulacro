// nodesim — C++ base for "control-style" node plugins (header-only)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Expansível a terceiros por HERANÇA: derive de nsui::Control e a base gera
// todo o esqueleto ABI 14 (create/destroy/step/stepMulti/save/load, params e
// texts). A ABI não muda: o `.so` continua exportando só `nodesim_plugin()`.
//
// Contrato:
//   - cada Controle é UM NsNodeType (a base também gera a tabela de tipos);
//   - identidade do tipo vem de CONSTANTES estáticas: kTypeId/kLabel/kRGB0..2,
//     kDefIn/kPrefW/kPrefH/kCustomShape e as tabelas estáticas kParams/texts;
//   - o ESTADO dos params/texts é serializado automaticamente no blob
//     ("nome=valor" separado por espaços; texts como nome="texto"), o mesmo
//     formato dos built-ins;
//   - step() devolve o valor da porta 0 (controles clássicos publicam [0;1]);
//     stepMulti() é opcional para nós de várias portas;
//   - draw() opcional: com kCustomShape=true o plugin autorende o corpo via
//     NsDraw e o host NÃO desenha corpo/título (NS_SHAPE_CUSTOM);
//   - a UI declarada (NS_PLUGIN_UI, docs/abi.md) fica opcional e independente:
//     o controle pode combiná-la com a herança quando quiser widgets no nó.
#pragma once

#include "nodesim_plugin.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nsui {

// Base: controle "estilo painel". Subclasses declaram as constantes estáticas
// (shadowing) e implementam o estado (paramGet/paramSet/textGet/textSet e o
// step). O que não for sobrescrito usa o default abaixo.
struct Control {
    // identidade: a subclasse declara os STATICs (kTypeId/kLabel/kParams...)
    // e AQUI os virtuais presentam o estado ao design/serializer (por causa
    // do shadowing, um static da subclasse é invisível dentro de Control)
    virtual void defColor(int rgb[3]) const {
        rgb[0] = 200;
        rgb[1] = 200;
        rgb[2] = 200;
    }

    virtual ~Control() = default;

    // ---- tabelas (a base lê por virtual: os STATICS da subclasse ficam
    //      invisíveis para funções definidas em Control; forneça por override)
    virtual int numParams() const { return 0; }
    virtual const NsParam* params() const { return nullptr; }
    virtual int numTexts() const { return 0; }
    virtual const char* const* texts() const { return nullptr; }
    // plugin decide o nº de saídas (com kSelfOut)
    virtual int numOut() const { return 1; }

    // ---- estado do controle (implementar) ----
    virtual double paramGet(int i) const {
        (void)i;
        return 0.0;
    }
    virtual void paramSet(int i, double v) {
        (void)i;
        (void)v;
    }
    virtual const char* textGet(int i) const {
        (void)i;
        return "";
    }
    virtual void textSet(int i, const char* s) {
        (void)i;
        (void)s;
    }

    // ---- simulação ----
    virtual double step(const NsContext& c) {
        (void)c;
        return 0.0;
    }
    virtual int stepMulti(const NsContext& c, double* out, int nout) {
        (void)c;
        (void)out;
        (void)nout;
        return 0; // 0 = host espelha out[0] nas portas restantes
    }
    virtual void draw(const NsDraw*, const NsRect*) {}
    // with kSelfOut: the host asks the plugin how many outputs it wants now

    // ---- blob automático de estado (params + texts) ----
    // Formatador de cor: aceita "#RGB", "RGB", "#RRGGBB", "RRGGBB" e 0x (hex),
// case-insensitive; devolve false quando a string não é uma cor válida.
inline bool parseHexColor(const char* s, int& r, int& g, int& b) {
    if (!s) return false;
    std::string t;
    for (const char* p = s; *p; ++p) {
        const char ch = *p;
        if (ch == ' ' || ch == '\t') continue;
        t += (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
    }
    if (t.rfind("0x", 0) == 0) t.erase(0, 2);
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    if (t.size() == 3) {
        std::string o;
        for (char c: t) { o += c; o += c; }
        t = o;
    }
    if (t.size() != 6) return false;
    for (char c : t)
        if (!std::isxdigit((unsigned char)c)) return false;
    auto v = [&](int k) { return std::stoul(t.substr(k * 2, 2), nullptr, 16); };
    r = (int)v(0);
    g = (int)v(1);
    b = (int)v(2);
    return true;
}

// Formatador de textos no blob: "nome=valor" (espaço) e
    // textos como nome="texto" (escapes de \" e \\).
    virtual std::string saveBlob() const {
        std::string blob;
        const int npar = numParams();
        const NsParam* par = params();
        for (int i = 0; i < npar; ++i) {
            if (!blob.empty()) blob += ' ';
            blob += par[i].name;
            blob += '=';
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", paramGet(i));
            blob += buf;
        }
        const int ntx = numTexts();
        const char* const* tnames = texts();
        for (int i = 0; i < ntx; ++i) {
            if (!blob.empty()) blob += ' ';
            blob += tnames[i];
            blob += "=\"";
            for (const char* p = textGet(i) ? textGet(i) : ""; *p; ++p) {
                if (*p == '"' || *p == '\\') blob += '\\';
                blob += *p;
            }
            blob += '"';
        }
        return blob;
    }
    virtual int loadBlob(const std::string& blob) {
        // positional fallback: a blob with only numbers/spaces (no '=')
        // assigns values in order to params[] — the convention of the single-
        // param built-ins (e.g. pgain) — so plain numbers keep working.
        const bool hasEq = blob.find('=') != std::string::npos;
        if (!hasEq && numParams() > 0) {
            int applied = 0;
            size_t pos = 0;
            int idx = 0;
            while (pos < blob.size() && idx < numParams()) {
                while (pos < blob.size() && (blob[pos] == ' ' || blob[pos] == '\t'))
                    ++pos;
                if (pos >= blob.size()) break;
                char* e = nullptr;
                const double v = std::strtod(blob.c_str() + pos, &e);
                if (e != blob.c_str() + pos) {
                    paramSet(idx, v);
                    ++applied;
                    ++idx;
                    pos = (size_t)(e - blob.c_str());
                } else ++pos;
            }
            if (applied > 0) return applied;
        }
        int applied = 0;
        size_t pos = 0;
        while (pos < blob.size()) {
            while (pos < blob.size() && blob[pos] == ' ') ++pos;
            if (pos >= blob.size()) break;
            size_t endKey = pos;
            while (endKey < blob.size() && blob[endKey] != '=' && blob[endKey] != ' ')
                ++endKey;
            if (endKey >= blob.size() || blob[endKey] != '=') break;
            const std::string key = blob.substr(pos, endKey - pos);
            pos = endKey + 1;
            std::string value;
            if (pos < blob.size() && blob[pos] == '"') {
                ++pos;
                while (pos < blob.size() && blob[pos] != '"') {
                    if (blob[pos] == '\\' && pos + 1 < blob.size()) ++pos;
                    value += blob[pos++];
                }
                if (pos < blob.size()) ++pos;
            } else {
                size_t e = pos;
                while (e < blob.size() && blob[e] != ' ') ++e;
                value = blob.substr(pos, e - pos);
                pos = e;
            }
            bool hit = false;
            const int npar = numParams();
            const NsParam* par = params();
            for (int i = 0; i < npar; ++i) {
                if (key == par[i].name) {
                    paramSet(i, value.empty() ? 0.0 : std::strtod(value.c_str(), nullptr));
                    hit = true;
                }
            }
            const int ntx = numTexts();
            const char* const* tnames = texts();
            for (int i = 0; i < ntx; ++i) {
                if (key == tnames[i]) {
                    textSet(i, value.c_str());
                    hit = true;
                }
            }
            if (hit) ++applied;
        }
        return applied;
    }

    // ---- constantes padrão (subclasses conferem com shadowing) ----
    static constexpr const char* kTypeId = "";
    static constexpr const char* kLabel = "";
    static const int kDefIn = 1;   // entradas na criação (1..8)
    static const int kPrefW = 0;   // largura preferida (0 = host resolve)
    static const int kPrefH = 0;   // altura preferida
    static const bool kCustomShape = false; // NS_SHAPE_CUSTOM + draw()
    static const bool kSelfOut = false;     // plugin decides the port count
    static const int kNumOut = -1;          // with kSelfOut: 0..NS_MAX_PORTS
    static const int kNumParams = 0;
    static constexpr const NsParam* kParams = nullptr;
    static const int kNumTexts = 0;
    static constexpr const char* const* kTexts = nullptr;

   protected:
    Control() = default;
};

// Formatador de textos no blob
namespace detail {
inline void dupStr(char** out, const std::string& s) {
    char* d = static_cast<char*>(::malloc(s.size() + 1));
    if (d) std::memcpy(d, s.c_str(), s.size() + 1);
    *out = d;
}
} // namespace detail

// ---- Adapter: C  <->  nsui::Control ----
template <class C>
struct Host {
    static C& self(void* h) { return *static_cast<C*>(h); }

    static void* create() { return new C(); }
    static void destroy(void* h) { delete static_cast<C*>(h); }

    static double step(void* h, const NsContext* c) { return self(h).step(*c); }
    static int stepMulti(void* h, const NsContext* c, double* out, int nout) {
        return self(h).stepMulti(*c, out, nout);
    }
    static char* save(void* h) {
        char* out = nullptr;
        detail::dupStr(&out, self(h).saveBlob());
        return out;
    }
    static int load(void* h, const char* blob) {
        return self(h).loadBlob(blob ? blob : "");
    }

    static double getParam(void* h, int i) { return self(h).paramGet(i); }
    static int setParam(void* h, int i, double v) {
        self(h).paramSet(i, v);
        return 1;
    }
    static char* getText(void* h, int i) {
        char* out = nullptr;
        detail::dupStr(&out, self(h).textGet(i));
        return out;
    }
    static int setText(void* h, int i, const char* s) {
        self(h).textSet(i, s ? s : "");
        return 1;
    }

    static void draw(void* h, const NsDraw* g, const NsRect* area) {
        self(h).draw(g, area);
    }
    static int numOut(void* h) { return self(h).numOut(); }
};

// Base virtuals for everything dynamic:
namespace control_defaults { }


// Cria o NsNodeType de um Controle (tabela estática obrigatória: os arrays
// params/texts das subclasses devem ser estáticos, como em qualquer plugin).
template <class C>
NsNodeType makeType() {
    static_assert(std::is_base_of<Control, C>::value, "derive from nsui::Control");
    NsNodeType t{};
    t.id = C::kTypeId;
    t.label = C::kLabel;
    int rgb[3] = {200, 200, 200};
    { C cell{}; cell.defColor(rgb); } // resolves via the virtual/table
    t.color[0] = static_cast<float>(rgb[0]);
    t.color[1] = static_cast<float>(rgb[1]);
    t.color[2] = static_cast<float>(rgb[2]);
    t.create = Host<C>::create;
    t.destroy = Host<C>::destroy;
    t.step = Host<C>::step;
    t.stepMulti = Host<C>::stepMulti;
    t.save = Host<C>::save;
    t.load = Host<C>::load;

    t.num_params = C::kNumParams;
    t.params = C::kParams;
    t.getParam = Host<C>::getParam;
    t.setParam = Host<C>::setParam;

    t.num_text = C::kNumTexts;
    t.text_names = C::kTexts;
    t.getText = Host<C>::getText;
    t.setText = Host<C>::setText;

    t.def_in = C::kDefIn;
    t.pref_w = C::kPrefW;
    t.pref_h = C::kPrefH;
    t.shape = C::kCustomShape ? NS_SHAPE_CUSTOM : NS_SHAPE_BOX;
    t.draw = C::kCustomShape ? &Host<C>::draw : nullptr;
    if (C::kSelfOut) {
        t.self_out = 1;
        t.numOut = &Host<C>::numOut;
    }
    return t;
}


// ---- Pushdown base (herança para "botões" de painel) ----
// Um controle de painel cuja SAÍDA publica [0;1] e cuja FACE é o botão:
//   - estado interno `pressed_` ligado ao params[0] (BOOL, nome `pulsa`);
//   - paramGet/paramSet automáticos + blob automático (via Control);
//   - `step()` devolve 1.0 enquanto pressionado, 0.0 solto;
//   - hooks virtuais onPressed()/onReleased() (chamados em transição);
//   - kCustomShape=true: o corpo é autorender por draw(), MAS apenas a FACE
//     ocupa o desenho: a borda do box fica com kPressInset px de margem
//     neutra, permitindo SELECIONAR/ARRASTAR o nó pela margem (o host
//     honra o inset declarado pela tabela NS_PLUGIN_ACTS_IN);
//   - kMomentary=true casa com NS_ACT_HOLD; kMomentary=false casa com
//     NS_ACT_TOGGLE (mantém 1 após soltar).
// Terceiros derivam; ambos os contratos (acts + design) ficam na subclasse.
// Nada no ABI muda: o `.so` segue ABI 14.
struct Pushdown : Control {
    bool pressed_ = false; // botão físico (canvas/property grid)
    bool inOn_ = false;    // pressão EXTERNA (entrada em nível 1, transiente)
    std::string caption_ = "";    // rotulo (textos[0]); vazio = "PUSH"
    std::string faceCol_ = "";    // cor da face solta (hex, textos[1])
    std::string pressCol_ = "";   // cor da face pressionada (hex, textos[2])

    // ---- estado via params[0] (`pulsa`) ----
    double paramGet(int i) const override {
        return i == 0 ? (pressed_ ? 1.0 : 0.0) : 0.0;
    }
    void paramSet(int i, double v) override {
        if (i != 0) return;
        const bool want = v > 0.5;
        if (want == pressed_) return;
        pressed_ = want;
        pressed_ ? onPressed() : onReleased();
    }

    int numOut() const override { return 1; } // uma porta [0;1]
    int numParams() const override { return 1; }
    const NsParam* params() const override { return kParams; }
    int numTexts() const override { return 3; }
    const char* const* texts() const override { return kTexts; }
    void defColor(int rgb[3]) const override {
        rgb[0] = 120;
        rgb[1] = 235;
        rgb[2] = 180;
    }

    // ---- textos editáveis: rotulo ("PUSH"), face/clique (hex) ----
    const char* textGet(int i) const override {
        if (i == 0) return caption_.c_str();
        if (i == 1) return faceCol_.c_str();
        if (i == 2) return pressCol_.c_str();
        return "";
    }
    void textSet(int i, const char* s) override {
        if (i == 0)
            caption_ = s ? s : "";
        else if (i == 1)
            faceCol_ = s ? s : "";
        else if (i == 2)
            pressCol_ = s ? s : "";
    }

    // ---- simulação: nível de entrada + botão físico ----
    // "se a entrada for 1 ele simula um click, ou enquanto for 1 ele está
    //  como pressionado": a SAÍDA é (pulsa || entrada>0.5) e o design mostra
    //  a face pressionada nos dois casos (com marcador EXT no 2.)
    double step(const NsContext& c) override {
        bool ext = false;
        const int a = c.sockOff[0], q = c.nsock > 0 ? c.sockOff[1] : a;
        for (int k = a; k < q; ++k)          // somatório do socket 0 (como led)
            if (c.in[k] != 0.0) ext = true;  // qualquer fio não-zero pressiona
        const bool was = inOn_;
        inOn_ = ext;
        if (inOn_ != was) onExtern(inOn_);
        return (pressed_ || inOn_) ? 1.0 : 0.0;
    }

    // ---- hooks para subclasses ----
    virtual void onPressed() {}
    virtual void onReleased() {}
    virtual void onExtern(bool on) {
        (void)on;
    }

    // ---- design default (kCustomShape=true => draw() autorende a FACE) ----
    void draw(const NsDraw* g, const NsRect* area) override {
        if (!g || !area) return;
        // a FACE é o box interno à margem: o resto do box fica livre p/ arrastar
        const int m = kPressInset;
        const int x = area->x + m, y = area->y + m;
        const int w = area->w - 2 * m, h = area->h - 2 * m;
        if (w <= 4 || h <= 4) return;

        // cores: face/clique vindas dos textos (hex legível) caem sobre as
        // derivadas de defColor (o fallback mantiene a identidade do plugin)
        int base[3];
        defColor(base);
        int faceRgb[3] = {base[0], base[1], base[2]};
        int pressRgb[3] = {(base[0] * 3) / 4, (base[1] * 3) / 4, (base[2] * 3) / 4};
        parseHexColor(faceCol_.empty() ? nullptr : faceCol_.c_str(),
                      faceRgb[0], faceRgb[1], faceRgb[2]);
        parseHexColor(pressCol_.empty() ? nullptr : pressCol_.c_str(),
                      pressRgb[0], pressRgb[1], pressRgb[2]);
        const bool on = pressed_ || inOn_;
        // pacote 0xRRGGBBAA (contrato do NsDraw; ver sdk/nodesim_plugin.h)
        auto pack = [](int r, int g, int b) {
            return ((unsigned)r << 24) | ((unsigned)g << 16) | ((unsigned)b << 8) | 0xFFu;
        };
        const unsigned faceCol = on ? pack(pressRgb[0], pressRgb[1], pressRgb[2])
                                    : pack(faceRgb[0], faceRgb[1], faceRgb[2]);
        if (!g->rect) return;
        const unsigned ring = pack(base[0], base[1], base[2]);
        // moldura da face + corpo da face
        g->rect(g->ctx, x, y, w, h, ring);
        g->rect(g->ctx, x + 1, y + 1, w - 2, h - 2, faceCol);
        // bisel: borda interna clara (solto) ou escura (pressionado)
        const unsigned rim = on ? 0x101018FFu : 0x3C3C4AFFu;
        const int b = std::max(3, std::min(w, h) / 10);
        g->rect(g->ctx, x, y, w, b, rim);
        g->rect(g->ctx, x, y, b, h, rim);
        // brilho do topo quando solto
        if (!on) g->rect(g->ctx, x + b + 2, y + b + 1, w - 2 * b - 4, 2,
                         0x2A2A38FFu);
        // legenda: rotulo do usuário, senão o rótulo do nó ("PUSH")
        std::string shown = caption_.empty()
                                ? std::string(g->label && *g->label ? g->label
                                                                    : "PUSH")
                                : caption_;
        if (inOn_ && !pressed_) shown += " EXT"; // pressão externa (nível 1)
        if (g->text) {
            const int tw = g->textW ? g->textW(g->ctx, shown.c_str()) : 0;
            const unsigned tcol = on ? 0x0D0D12FFu : 0xF0F0F8FFu;
            g->text(g->ctx, x + (w - tw) / 2, y + h / 2 - 7, shown.c_str(), tcol);
        }
    }

    // ---- constantes padrão do pushdown (subclasse faz shadowing) ----
    static constexpr int kNumParams = 1;
    static constexpr NsParam kParams[kNumParams] = {
        {"pulsa", 0, 1, 0, NS_PARAM_BOOL, 1, nullptr,
         "pressionado (0/1)", 0, nullptr},
    };
    static constexpr int kNumTexts = 3;
    static constexpr const char* kTexts[kNumTexts] = {
        "rotulo", // legenda da face (vazio = "PUSH")
        "face",   // hex da face solta   (vazio = cor do plugin)
        "clique", // hex da face pressionada (vazio = derivado)
    };
    static constexpr bool kMomentary = true; // NS_ACT_HOLD (casar com a tabela)
    static constexpr bool kSelfOut = true;
    static constexpr int kNumOut = 1;
    static constexpr int kDefIn = 1;
    static constexpr int kPrefW = 96;
    static constexpr int kPrefH = 72;
    static constexpr bool kCustomShape = true;
    static constexpr int kPressInset = 10; // margem interna da face (world px)

   protected:
    Pushdown() = default;
};

// ---- Switch base (herança para chaves ON/OFF de painel) ----
// Prima do Pushdown: o NS_ACT_TOGGLE comanda params[0] (cada clique alterna
// e o valor FICA), a saída publica o estado e a face mostra ON/OFF.
//   - estado interno `on_` ligado ao params[0] (BOOL, nome `ligado`);
//   - paramGet/paramSet automáticos + blob automático (via Control);
//   - kMomentary=false casa com NS_ACT_TOGGLE (mantém 1 após soltar);
//   - kDefIn=0: chave NÃO tem entrada de nível (o estado é do operador);
//   - textos rotulo/on/off (hex legível) caem sobre defColor;
//   - kCustomShape=true: o corpo é autorender por draw(), apenas a FACE
//     ocupa o desenho (margem kPressInset livre para selecionar/arrastar,
//     com o host honrando o inset da tabela NS_PLUGIN_ACTS_IN).
// Terceiros derivam; o contrato (acts + design) fica na subclasse.
struct Switch : Control {
    bool on_ = false;          // estado (params[0] `ligado`)
    std::string caption_ = ""; // rotulo (textos[0]); vazio = "SW"
    std::string onCol_ = "";   // hex com a chave ON  (textos[1])
    std::string offCol_ = "";  // hex com a chave OFF (textos[2])

    // ---- estado via params[0] (`ligado`) ----
    double paramGet(int i) const override {
        return i == 0 ? (on_ ? 1.0 : 0.0) : 0.0;
    }
    void paramSet(int i, double v) override {
        if (i != 0) return;
        const bool want = v > 0.5;
        if (want == on_) return;
        on_ = want;
        onToggled(on_);
    }

    int numOut() const override { return 1; } // uma porta [0;1]
    int numParams() const override { return 1; }
    const NsParam* params() const override { return kParams; }
    int numTexts() const override { return 3; }
    const char* const* texts() const override { return kTexts; }
    void defColor(int rgb[3]) const override {
        rgb[0] = 70;
        rgb[1] = 130;
        rgb[2] = 235;
    }

    // ---- textos editáveis: rotulo, on/off (hex) ----
    const char* textGet(int i) const override {
        if (i == 0) return caption_.c_str();
        if (i == 1) return onCol_.c_str();
        if (i == 2) return offCol_.c_str();
        return "";
    }
    void textSet(int i, const char* s) override {
        if (i == 0)
            caption_ = s ? s : "";
        else if (i == 1)
            onCol_ = s ? s : "";
        else if (i == 2)
            offCol_ = s ? s : "";
    }

    // ---- simulação: a chave publica o estado (sem nível externo) ----
    double step(const NsContext& c) override {
        (void)c;
        return on_ ? 1.0 : 0.0;
    }

    // ---- hook para subclasses ----
    virtual void onToggled(bool on) {
        (void)on;
    }

    // ---- design default (kCustomShape=true => draw() autorende a FACE) ----
    // Pílula com cursor: OFF encosta à esquerda, ON encosta à direita.
    void draw(const NsDraw* g, const NsRect* area) override {
        if (!g || !area) return;
        const int m = kPressInset;
        const int x = area->x + m, y = area->y + m;
        const int w = area->w - 2 * m, h = area->h - 2 * m;
        if (w <= 8 || h <= 8) return;

        // cores: textos (hex legível) caem sobre as derivadas de defColor
        int base[3];
        defColor(base);
        int onRgb[3] = {base[0], base[1], base[2]};
        int offRgb[3] = {base[0] / 3, base[1] / 3, base[2] / 3}; // escurecida
        parseHexColor(onCol_.empty() ? nullptr : onCol_.c_str(), onRgb[0], onRgb[1], onRgb[2]);
        parseHexColor(offCol_.empty() ? nullptr : offCol_.c_str(), offRgb[0], offRgb[1], offRgb[2]);
        // pacote 0xRRGGBBAA (contrato do NsDraw; ver sdk/nodesim_plugin.h)
        auto pack = [](int r, int gg, int b) {
            return ((unsigned)r << 24) | ((unsigned)gg << 16) | ((unsigned)b << 8) | 0xFFu;
        };
        if (!g->rect) return;
        const unsigned ring = pack(base[0], base[1], base[2]);
        const unsigned body = on_ ? pack(onRgb[0], onRgb[1], onRgb[2])
                                  : pack(offRgb[0], offRgb[1], offRgb[2]);
        // moldura da face + corpo da face
        g->rect(g->ctx, x, y, w, h, ring);
        g->rect(g->ctx, x + 1, y + 1, w - 2, h - 2, body);
        // cursor (knob): terço direito quando ON, terço esquerdo quando OFF
        const int kw = std::max(6, w / 3);
        const int kh = std::max(4, h / 2);
        const int kx = on_ ? x + w - kw - 2 : x + 2;
        const int ky = y + (h - kh) / 2;
        g->rect(g->ctx, kx, ky, kw, kh, 0xE8E8F0FFu);
        // bisel da pílula (topo claro quando OFF, sombra quando ON)
        const unsigned rim = on_ ? 0x101018FFu : 0x3C3C4AFFu;
        g->rect(g->ctx, x, y, w, std::max(2, h / 8), rim);
        // legenda: rotulo do usuário, senão o rótulo do nó ("SW")
        std::string shown = caption_.empty()
                                ? std::string(g->label && *g->label ? g->label
                                                                    : "SW")
                                : caption_;
        shown += on_ ? "  I" : "  O"; // estado legível também no texto
        if (g->text) {
            const int tw = g->textW ? g->textW(g->ctx, shown.c_str()) : 0;
            const unsigned tcol = on_ ? 0x0D0D12FFu : 0xF0F0F8FFu;
            g->text(g->ctx, x + (w - tw) / 2, y + h / 2 - 7, shown.c_str(), tcol);
        }
    }

    // ---- constantes padrão da switch (subclasse faz shadowing) ----
    static constexpr int kNumParams = 1;
    static constexpr NsParam kParams[kNumParams] = {
        {"ligado", 0, 1, 0, NS_PARAM_BOOL, 1, nullptr, "ligada (0/1)", 0, nullptr},
    };
    static constexpr int kNumTexts = 3;
    static constexpr const char* kTexts[kNumTexts] = {
        "rotulo", // legenda da face (vazio = "SW")
        "on",     // hex da pílula ligada (vazio = cor do plugin)
        "off",    // hex da pílula desligada (vazio = derivado escuro)
    };
    static constexpr bool kMomentary = false; // NS_ACT_TOGGLE (casar com a tabela)
    static constexpr bool kSelfOut = true;
    static constexpr int kNumOut = 1;
    static constexpr int kDefIn = 0; // chave não tem entrada de nível
    static constexpr int kPrefW = 96;
    static constexpr int kPrefH = 72;
    static constexpr bool kCustomShape = true;
    static constexpr int kPressInset = 10; // margem interna da face (world px)

   protected:
    Switch() = default;
};
} // namespace nsui
