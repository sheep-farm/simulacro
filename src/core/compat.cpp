// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compat.h"
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace compat {
namespace {

struct Pair { const char* legacy; const char* canonical; };

const Pair kTypeAliases[] = {
    {"coletor", "collector"}, {"degrau", "step"}, {"impulso", "impulse"},
    {"rampa", "ramp"}, {"seno", "sine"}, {"quadrada", "square"},
    {"triangular", "triangle"}, {"dif", "diff"}, {"integ", "integrator"},
    {"deriv", "derivative"}, {"domfreq", "dominant_freq"},
    {"pausa", "stop"}, {"pausams", "pause_ms"}, {"parat", "timed_stop"},
    {"pingms", "ping_ms"}, {"pulsems", "pulse_ms"},
};

const char* const kBuiltinTypes[] = {
    "acf", "adf", "and", "ar", "binomial", "buf", "butter_bp", "butter_hp",
    "butter_lp", "chi2", "chirp", "clock", "collector", "csv", "csvout",
    "deadband", "delay30", "derivative", "det", "dff", "diff",
    "dominant_freq", "ema", "fdist", "fft", "holt", "holt_winters", "hp",
    "impulse", "infer", "integrator", "interval", "inv", "json", "kalman",
    "lag", "led", "logret", "matmul", "median", "movavg", "mux", "nand",
    "nor", "normal", "not", "ols", "or", "pacf", "pause_ms", "pgain", "pid",
    "ping_ms", "poisson", "prbs", "proc", "psd", "pulse_ms", "quad", "ramp",
    "ratelimit", "rng", "sat", "savgol", "seg7", "sine", "solve", "square",
    "sr", "stats", "step", "stop", "student_t", "tff", "timed_stop",
    "transpose", "triangle", "xnor", "xor",
};

const Pair kStateKeys[] = {
    {"acumulado", "accumulated"}, {"alto", "high"}, {"arquivo", "file"},
    {"borda", "edge"}, {"cabecalho", "header"}, {"caminho", "path"},
    {"chave", "key"}, {"ciclo", "cycle"}, {"coluna", "column"},
    {"colunas", "columns"}, {"comando", "command"}, {"constante", "constant"},
    {"defasagens", "lags"}, {"descida", "fall"}, {"desloc", "offset"},
    {"disparou", "fired"}, {"domfreq", "dominant_freq"}, {"duracao", "duration"},
    {"estado", "state"}, {"fase", "phase"}, {"ganho", "gain"},
    {"habilitar", "enable"}, {"hwNivel", "hwLevel"}, {"hwPronto", "hwReady"},
    {"inclinacao", "slope"}, {"inferencia", "inference"}, {"iniciado", "started"},
    {"inicio", "start"}, {"intervalo", "interval"}, {"janela", "window"},
    {"largura", "width"}, {"limiar", "threshold"}, {"limite", "limit"},
    {"maximo", "max"}, {"media", "mean"}, {"minimo", "min"},
    {"modelo", "model"}, {"modo", "mode"}, {"nivel", "level"},
    {"ordem", "order"}, {"passo", "step"}, {"periodo", "period"},
    {"permitidos", "allowed"}, {"previsao", "forecast"}, {"prox", "next"},
    {"salto", "hop"}, {"semente", "seed"}, {"subida", "rise"},
    {"taxa", "rate"}, {"temEp", "hasEp"}, {"temY", "hasY"},
    {"tempo", "time"}, {"tendencia", "trend"}, {"tipo", "type"},
    {"ultimaMle", "lastMle"}, {"ultimo", "last"}, {"unidade", "unit"},
    {"zona", "zone"}, {"mle", "mle"},
};

const Pair kCliAliases[] = {
    {"--ate", "--until"}, {"--passo", "--step"}, {"--nodo", "--node"},
    {"--saida", "--output"}, {"--listas", "--lists"}, {"--resumo", "--summary"},
    {"--sem-cabecalho", "--no-header"}, {"--listar", "--list"}, {"--zerar", "--zero"},
    {"--varre", "--scan"}, {"--semente", "--seed"}, {"--agregado", "--aggregate"},
    {"--sem-manifesto", "--no-manifest"}, {"--falhar-em-nan", "--fail-on-nan"},
    {"--convergir", "--converge"}, {"--tolerancia", "--tolerance"},
    {"--max-iteracoes", "--max-iterations"},
    {"--falhar-em-nao-convergencia", "--fail-on-no-convergence"},
    {"--eventos", "--events"}, {"--evento", "--event"}, {"--relatorio", "--report"},
    {"--orcamento", "--budget"}, {"--silencioso", "--quiet"},
    {"--tipos", "--types"}, {"--checar-plugins", "--check-plugins"},
    {"--conformidade", "--conformance"}, {"--ajuda", "--help"},
};

const Pair kEnvAliases[] = {
    {"NODESIM_TEMA", "NODESIM_THEME"},
    {"NODESIM_TEMA_EXPORT", "NODESIM_THEME_EXPORT"},
};

const Pair kConfigAliases[] = {
    {"salvarInicial", "saveInitial"}, {"iteracoes", "iterations"},
    {"convergir", "converge"}, {"tolerancia", "tolerance"},
    {"max_iteracoes", "maxIterations"}, {"eventos", "events"},
    {"tema_export", "themeExport"},
};

template <size_t N>
std::string mapName(const Pair (&table)[N], const std::string& name) {
    for (const auto& p : table)
        if (name == p.legacy) return p.canonical;
    return name;
}

std::string translateState(const std::string& blob) {
    std::string out;
    out.reserve(blob.size() + 16);
    size_t i = 0;
    while (i < blob.size()) {
        const bool boundary = i == 0 || blob[i - 1] == ' ' || blob[i - 1] == '\t' ||
                              blob[i - 1] == ',' || blob[i - 1] == ';' || blob[i - 1] == '|';
        if (boundary) {
            bool matched = false;
            for (const auto& k : kStateKeys) {
                const size_t n = std::char_traits<char>::length(k.legacy);
                if (blob.compare(i, n, k.legacy) == 0 && i + n < blob.size() &&
                    blob[i + n] == '=') {
                    out += k.canonical;
                    out += '=';
                    i += n + 1;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        out += blob[i++];
    }
    return out;
}

std::string trim(std::string t) {
    const size_t a = t.find_first_not_of(" \t");
    const size_t b = t.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

} // namespace

std::string canonicalTypeId(const std::string& id) {
    for (const auto& p : kTypeAliases)
        if (id == p.legacy) return p.canonical;
    return id;
}

bool isBuiltinTypeId(const std::string& id) {
    const std::string canon = canonicalTypeId(id);
    for (const char* t : kBuiltinTypes)
        if (canon == t) return true;
    return false;
}

std::string canonicalStateBlob(const std::string& typeId, const std::string& blob) {
    return isBuiltinTypeId(typeId) ? translateState(blob) : blob;
}

std::string canonicalScopeParams(const std::string& params) {
    std::vector<std::pair<std::string, std::string>> kv;
    size_t i = 0;
    while (i < params.size()) {
        const size_t comma = params.find(',', i);
        std::string item = params.substr(i, comma == std::string::npos
                                                ? std::string::npos : comma - i);
        const size_t eq = item.find('=');
        if (eq != std::string::npos) {
            std::string k = trim(item.substr(0, eq));
            std::string v = trim(item.substr(eq + 1));
            if (k == "tipo") k = "type";
            else if (k == "janela") k = "window";
            else if (k == "escala") k = "scale";
            else if (k == "dec" || k == "decimacao") k = "decimation";
            else if (k == "grade") k = "grid";
            else if (k == "legenda") k = "legend";
            else if (k == "arquivo") k = "file";
            else if (k == "idcampo") k = "idfield";
            else if (k == "rotulo") k = "label";
            else if (k == "projecao" || k == "proj") k = "projection";
            else if (k == "paleta") k = "palette";
            else if (k == "vauto") k = "valueauto";
            else if (k == "rotulos") k = "labels";
            else if (k == "idcol") k = "idcolumn";
            else if (k == "valcol" || k == "valorcol") k = "valuecolumn";
            else if (k == "corcampo") k = "colorfield";
            else if (k == "sobrepor" || k == "camadas") k = "overlay";
            else if (k == "projecao2" || k == "proj2") k = "projection2";
            else if (k == "idcampo2") k = "idfield2";
            else if (k == "rotulo2") k = "label2";
            if (k == "type") {
                if (v == "linha") v = "line";
                else if (v == "mapa") v = "map";
                else if (v == "barras") v = "bars";
                else if (v == "histograma") v = "histogram";
            }
            if (k == "y" && (v == "fixo" || v == "fix" || v == "0")) v = "fixed";
            kv.push_back({k, v});
        }
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    std::string out;
    for (size_t k = 0; k < kv.size(); ++k) {
        if (k) out += ", ";
        out += kv[k].first + "=" + kv[k].second;
    }
    return out;
}

std::string canonicalCliOption(const std::string& option) {
    return mapName(kCliAliases, option);
}

const char* getenvCompat(const char* name) {
    for (const auto& p : kEnvAliases) {
        if (std::strcmp(name, p.canonical) == 0) {
            if (const char* v = std::getenv(p.canonical)) return v;
            return std::getenv(p.legacy);
        }
        if (std::strcmp(name, p.legacy) == 0) {
            if (const char* v = std::getenv(p.legacy)) return v;
            return std::getenv(p.canonical);
        }
    }
    return std::getenv(name);
}

std::string canonicalConfigKey(const std::string& key) {
    return mapName(kConfigAliases, key);
}

} // namespace compat
