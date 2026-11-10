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
    virtual ~Control() = default;

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
    virtual int numOut() const { return 0; }

    // ---- blob automático de estado (params + texts) ----
    // Formato compatível com os built-ins: "nome=valor" (espaço) e
    // textos como nome="texto" (escapes de \" e \\).
    virtual std::string saveBlob() const {
        std::string blob;
        for (int i = 0; i < kNumParams; ++i) {
            if (!blob.empty()) blob += ' ';
            blob += kParams[i].name;
            blob += '=';
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", paramGet(i));
            blob += buf;
        }
        for (int i = 0; i < kNumTexts; ++i) {
            const char* s = textGet(i);
            if (!blob.empty()) blob += ' ';
            blob += kTexts[i];
            blob += "=\"";
            for (const char* p = s ? s : ""; *p; ++p) {
                if (*p == '"' || *p == '\\') blob += '\\';
                blob += *p;
            }
            blob += '"';
        }
        return blob;
    }
    virtual int loadBlob(const std::string& blob) {
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
            for (int i = 0; i < kNumParams; ++i) {
                if (key == kParams[i].name) {
                    paramSet(i, value.empty() ? 0.0 : std::strtod(value.c_str(), nullptr));
                    hit = true;
                }
            }
            for (int i = 0; i < kNumTexts; ++i) {
                if (key == kTexts[i]) {
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
    static_assert(!std::is_same<typename std::remove_cv<decltype(C::kTypeId)>::type,
                                const char* const>::value ||
                      sizeof(C::kTypeId) > 4,
                  "kTypeId/kLabel должны ser inicializados na subclasse");
    NsNodeType t{};
    t.id = C::kTypeId;
    t.label = C::kLabel;
    int rgb[3] = {200, 200, 200};
    C::defColor(rgb);
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

} // namespace nsui
