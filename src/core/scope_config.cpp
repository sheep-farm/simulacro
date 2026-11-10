// nodesim — node graph editor and simulator

// Copyright (C) 2026 Flavio de Vasconcellos Correa

// SPDX-License-Identifier: GPL-3.0-or-later



// Scope configuration parsing/serialization, independent from SDL drawing.

#include "scope.h"

#include <algorithm>

#include <cctype>

#include <cstdlib>

#include <string>

#include <utility>

#include <vector>



namespace {



void parseKV(const std::string& s, std::vector<std::pair<std::string, std::string>>& out) {

    size_t i = 0;

    while (i < s.size()) {

        while (i < s.size() && (s[i] == ',' || s[i] == ';' || s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t')) ++i;

        size_t st = i;

        while (i < s.size() && s[i] != ',' && s[i] != ';' && s[i] != '\n' && s[i] != '\r') ++i;

        std::string tok = s.substr(st, i - st);

        while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) tok.pop_back();

        if (tok.empty()) continue;

        size_t eq = tok.find('=');

        std::string k = eq == std::string::npos ? tok : tok.substr(0, eq);

        std::string v = eq == std::string::npos ? "1" : tok.substr(eq + 1);

        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();

        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());

        out.push_back({k, v});

    }

}

bool truthy(const std::string& v) {

    return !(v.empty() || v == "0" || v == "nao" || v == "off" || v == "false");

}

std::string trimListToken(std::string s) {

    size_t a = s.find_first_not_of(" \t[]\"'");

    size_t b = s.find_last_not_of(" \t[]\"'");

    if (a == std::string::npos || b == std::string::npos) return {};

    return s.substr(a, b - a + 1);

}

std::vector<std::string> splitList(const std::string& s) {

    std::vector<std::string> out;

    std::string cur;

    for (char c : s) {

        if (c == '|' || c == ',' || c == ';' || c == '\n' || c == '\r') {

            std::string t = trimListToken(cur);

            if (!t.empty()) out.push_back(std::move(t));

            cur.clear();

        } else cur.push_back(c);

    }

    std::string t = trimListToken(cur);

    if (!t.empty()) out.push_back(std::move(t));

    return out;

}



// Extracts a list key (sobrepor/camadas) directly from params, accepting

// commas and semicolons inside the value. Normal parseKV does not work because

// it would split "a.geojson,b.geojson" into two keys.

std::string extractListParam(const std::string& s, const std::string& key) {

    size_t i = 0;

    while (i < s.size()) {

        while (i < s.size() && (s[i] == ',' || s[i] == ';' || s[i] == '\n' ||

                                s[i] == '\r' || s[i] == ' ' || s[i] == '\t')) ++i;

        const size_t ks = i;

        while (i < s.size() && s[i] != '=' && s[i] != ',' && s[i] != ';' &&

               s[i] != '\n' && s[i] != '\r') ++i;

        std::string k = s.substr(ks, i - ks);

        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();

        if (i >= s.size() || s[i] != '=') continue;

        ++i;

        const size_t vs = i;

        while (i < s.size()) {

            if (s[i] == '\n' || s[i] == '\r') break;

            if (s[i] == ',' || s[i] == ';') {

                size_t j = i + 1;

                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;

                size_t t = j;

                while (t < s.size() && s[t] != '=' && s[t] != ',' && s[t] != ';' &&

                       s[t] != '\n' && s[t] != '\r') ++t;

                if (t < s.size() && s[t] == '=') break; // next k=v

            }

            ++i;

        }

        if (k == key) return s.substr(vs, i - vs);

    }

    return {};

}

ScopeType typeFromName(std::string s) {

    for (auto& ch : s) ch = (char)std::tolower((unsigned char)ch);

    if (s == "xy" || s == "x-y" || s == "lissajous") return ScopeType::XY;

    if (s == "barras" || s == "bar" || s == "bars" || s == "medidor" || s == "gauge") return ScopeType::BARS;

    if (s == "hist" || s == "histograma" || s == "histogram") return ScopeType::HIST;

    if (s == "fft" || s == "espectro" || s == "spectrum") return ScopeType::FFT;

    if (s == "mapa" || s == "map" || s == "geojson") return ScopeType::MAP;

    return ScopeType::LINE;

}



// ---- in-place radix-2 FFT ----

} // namespace



const char* scopeTypeName(ScopeType t) {

    switch (t) {

        case ScopeType::LINE: return "line";

        case ScopeType::XY:   return "xy";

        case ScopeType::BARS: return "bars";

        case ScopeType::HIST: return "histogram";

        case ScopeType::FFT:  return "fft";

        case ScopeType::MAP:  return "map";

    }

    return "line";

}

int scopeTypeCount() { return 6; }



std::string scopeJoinList(const std::vector<std::string>& v) {

    std::string out;

    for (size_t i = 0; i < v.size(); ++i) { if (i) out += "|"; out += v[i]; }

    return out;

}



std::string scopeGetParam(const std::string& params, const std::string& key, const std::string& def) {

    std::vector<std::pair<std::string, std::string>> kv;

    parseKV(params, kv);

    for (auto& p : kv) if (p.first == key) return p.second;

    return def;

}



std::string scopeSetParam(const std::string& params, const std::string& key, const std::string& val) {

    std::vector<std::pair<std::string, std::string>> kv;

    parseKV(params, kv);

    bool done = false;

    for (auto& p : kv)

        if (p.first == key) { p.second = val; done = true; }

    if (!done) kv.push_back({key, val});

    std::string out;

    for (size_t i = 0; i < kv.size(); ++i) { if (i) out += ", "; out += kv[i].first + "=" + kv[i].second; }

    return out;

}



ScopeCfg parseScopeCfg(const std::string& params) {

    ScopeCfg c;

    std::vector<std::pair<std::string, std::string>> kv;

    parseKV(params, kv);

    for (auto& p : kv) {

        const std::string& k = p.first;

        const std::string& v = p.second;

        if (k == "tipo" || k == "type") c.type = typeFromName(v);

        else if (k == "janela" || k == "window") c.window = (float)std::atof(v.c_str());

        else if (k == "y" || k == "yscale") c.yauto = !(v == "fixo" || v == "fix" || v == "fixed" || v == "0");

        else if (k == "ymin") c.ymin = (float)std::atof(v.c_str());

        else if (k == "ymax") c.ymax = (float)std::atof(v.c_str());

        else if (k == "escala" || k == "scale") c.scale = (float)std::atof(v.c_str());

        else if (k == "dec" || k == "decimacao" || k == "decimation") c.dec = std::atoi(v.c_str());

        else if (k == "grade" || k == "grid") c.grid = truthy(v);

        else if (k == "legenda" || k == "legend") c.legend = truthy(v);

        else if (k == "zero") c.zero = truthy(v);

        else if (k == "xc") c.xc = std::atoi(v.c_str());

        else if (k == "yc") c.yc = std::atoi(v.c_str());

        else if (k == "bins") c.bins = std::atoi(v.c_str());

        else if (k == "arquivo" || k == "file") c.mapFile = v;

        else if (k == "idcampo" || k == "idfield") c.mapId = v;

        else if (k == "rotulo" || k == "label") c.mapLabel = v;

        else if (k == "projecao" || k == "proj" || k == "projection") c.mapProj = v;

        else if (k == "paleta" || k == "palette") c.mapPalette = std::atoi(v.c_str());

        else if (k == "vauto" || k == "valueauto") c.mapValueAuto = truthy(v);

        else if (k == "vmin") c.mapVMin = (float)std::atof(v.c_str());

        else if (k == "vmax") c.mapVMax = (float)std::atof(v.c_str());

        else if (k == "rotulos" || k == "labels") c.mapLabels = truthy(v);

        else if (k == "idcol" || k == "idcolumn") c.mapIdCol = std::atoi(v.c_str());

        else if (k == "valcol" || k == "valorcol" || k == "valuecolumn") c.mapValCol = std::atoi(v.c_str());

        else if (k == "corcampo" || k == "colorfield") c.mapColorField = v;

        else if (k == "sobrepor" || k == "overlay" || k == "camadas")

            c.mapOverlayFiles = splitList(v);

        else if (k == "projecao2" || k == "proj2" || k == "projection2") c.mapOverlayProj = v;

        else if (k == "idcampo2" || k == "idfield2") c.mapOverlayId = v;

        else if (k == "rotulo2" || k == "label2") c.mapOverlayLabel = v;

    }

    std::string overlayList = extractListParam(params, "sobrepor");

    if (overlayList.empty()) overlayList = extractListParam(params, "camadas");

    if (overlayList.empty()) overlayList = extractListParam(params, "overlay");

    if (!overlayList.empty()) c.mapOverlayFiles = splitList(overlayList);

    if (c.window < 0.1f) c.window = 0.1f;

    if (c.window > 600.0f) c.window = 600.0f;

    if (c.dec < 1) c.dec = 1;

    if (c.dec > 100) c.dec = 100;

    if (c.scale == 0) c.scale = 1.0f;

    if (c.bins < 4) c.bins = 4;

    if (c.bins > 256) c.bins = 256;

    c.xc = std::max(0, std::min(7, c.xc));

    c.yc = std::max(0, std::min(7, c.yc));

    for (auto& ch : c.mapProj) ch = (char)std::tolower((unsigned char)ch);

    if (c.mapProj != "auto" && c.mapProj != "mercator" && c.mapProj != "linear")

        c.mapProj = "auto";

    for (auto& ch : c.mapOverlayProj) ch = (char)std::tolower((unsigned char)ch);

    if (c.mapOverlayProj != "auto" && c.mapOverlayProj != "mercator" &&

        c.mapOverlayProj != "linear")

        c.mapOverlayProj = "auto";

    c.mapPalette = std::max(0, std::min(2, c.mapPalette));

    c.mapIdCol = std::max(0, std::min(7, c.mapIdCol));

    c.mapValCol = std::max(0, std::min(7, c.mapValCol));

    if (c.mapIdCol == c.mapValCol) c.mapValCol = c.mapIdCol == 0 ? 1 : 0;

    if (!c.mapValueAuto && c.mapVMax <= c.mapVMin) c.mapVMax = c.mapVMin + 1;

    return c;

}