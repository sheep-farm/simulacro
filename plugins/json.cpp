// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: JSON reading and extraction without external dependencies.
//
// The JSON source is, in priority order:
//   1. text arriving on socket 0 (NS_VAL_TXT);
//   2. file from the `arquivo` text parameter (re-read when it changes);
//   3. text from the `json` parameter.
//
// The `caminho` parameter selects a value: `a.b[2].c`, `$[0]`, etc.
// `modo` decides the output:
//   0 number | 1 text | 2 list | 3 JSON | 4 exists | 5 size
//
// The text/JSON modes publish via outText; list mode publishes via
// outValue/NS_VAL_LIST; the others use the normal numeric value. Invalid JSON,
// a missing path or an unreadable file return 0 (and empty text/list), without
// poisoning the state.
//
// Build: make plugins | Usage: menu Add -> Data -> JSON
#include <plugin.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace {

struct J {
    enum Kind { NULLVALUE, BOOL, NUM, STR, ARR, OBJ } t = NULLVALUE;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;
};

void utf8(std::string& out, unsigned cp) {
    if (cp <= 0x7F) out.push_back((char)cp);
    else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

struct Parser {
    const char* p = nullptr;
    const char* e = nullptr;
    int depth = 0;

    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool hex4(unsigned& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= e) return false;
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e) {
            const unsigned char c = (unsigned char)*p++;
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out.push_back((char)c); continue; }
            if (p >= e) return false;
            const char esc = *p++;
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (p + 1 < e && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo = 0;
                            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else return false;
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) return false;
                    utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(J& v) {
        ws();
        if (p >= e) return false;
        const char c = *p;
        if (c == '{') {
            if (++depth > 64) return false;
            ++p;
            J obj;
            obj.t = J::OBJ;
            ws();
            if (p < e && *p == '}') { ++p; --depth; v = std::move(obj); return true; }
            while (p < e) {
                ws();
                std::string k;
                if (!str(k)) return false;
                ws();
                if (p >= e || *p != ':') return false;
                ++p;
                J x;
                if (!value(x)) return false;
                obj.o.emplace_back(std::move(k), std::move(x));
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; --depth; v = std::move(obj); return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            if (++depth > 64) return false;
            ++p;
            J arr;
            arr.t = J::ARR;
            ws();
            if (p < e && *p == ']') { ++p; --depth; v = std::move(arr); return true; }
            while (p < e) {
                J x;
                if (!value(x)) return false;
                arr.a.push_back(std::move(x));
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; --depth; v = std::move(arr); return true; }
                return false;
            }
            return false;
        }
        if (c == '"') {
            std::string s;
            if (!str(s)) return false;
            v.t = J::STR;
            v.s = std::move(s);
            return true;
        }
        if (p + 4 <= e && std::strncmp(p, "true", 4) == 0) {
            p += 4;
            v.t = J::BOOL;
            v.b = true;
            return true;
        }
        if (p + 5 <= e && std::strncmp(p, "false", 5) == 0) {
            p += 5;
            v.t = J::BOOL;
            v.b = false;
            return true;
        }
        if (p + 4 <= e && std::strncmp(p, "null", 4) == 0) {
            p += 4;
            v.t = J::NULLVALUE;
            return true;
        }
        char* end = nullptr;
        const double n = std::strtod(p, &end);
        if (end == p || end > e || !std::isfinite(n)) return false;
        p = end;
        v.t = J::NUM;
        v.n = n;
        return true;
    }
    bool root(J& v) {
        if (!value(v)) return false;
        ws();
        return p == e;
    }
};

void escape(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", (unsigned)c);
                    out += b;
                } else out.push_back((char)c);
        }
    }
    out.push_back('"');
}

void writeJson(const J& v, std::string& out, int prof = 0) {
    if (out.size() > 2000000 || prof > 64) return;
    switch (v.t) {
        case J::NULLVALUE: out += "null"; break;
        case J::BOOL: out += v.b ? "true" : "false"; break;
        case J::NUM: {
            char b[64];
            std::snprintf(b, sizeof b, "%.17g", v.n);
            out += b;
            break;
        }
        case J::STR: escape(out, v.s); break;
        case J::ARR:
            out.push_back('[');
            for (size_t i = 0; i < v.a.size(); ++i) {
                if (i) out.push_back(',');
                writeJson(v.a[i], out, prof + 1);
            }
            out.push_back(']');
            break;
        case J::OBJ:
            out.push_back('{');
            for (size_t i = 0; i < v.o.size(); ++i) {
                if (i) out.push_back(',');
                escape(out, v.o[i].first);
                out.push_back(':');
                writeJson(v.o[i].second, out, prof + 1);
            }
            out.push_back('}');
            break;
    }
}

double number(const J& v) {
    switch (v.t) {
        case J::NUM: return v.n;
        case J::BOOL: return v.b ? 1.0 : 0.0;
        case J::STR: {
            char* end = nullptr;
            const double n = std::strtod(v.s.c_str(), &end);
            if (end == v.s.c_str()) return 0.0;
            while (*end && std::isspace((unsigned char)*end)) ++end;
            return *end ? 0.0 : (std::isfinite(n) ? n : 0.0);
        }
        case J::ARR: return v.a.empty() ? 0.0 : number(v.a[0]);
        default: return 0.0;
    }
}

std::string text(const J& v) {
    switch (v.t) {
        case J::STR: return v.s;
        case J::NUM: {
            char b[64];
            std::snprintf(b, sizeof b, "%.17g", v.n);
            return b;
        }
        case J::BOOL: return v.b ? "true" : "false";
        case J::NULLVALUE: return "null";
        default: {
            std::string s;
            writeJson(v, s);
            return s;
        }
    }
}

size_t size(const J& v) {
    switch (v.t) {
        case J::STR: return v.s.size();
        case J::ARR: return v.a.size();
        case J::OBJ: return v.o.size();
        case J::NULLVALUE: return 0;
        default: return 1;
    }
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

J* find(J& root, const std::string& path) {
    if (path.empty() || path == "$") return &root;
    size_t i = 0;
    if (path[0] == '$') i = 1;
    J* cur = &root;
    while (i < path.size()) {
        if (path[i] == '.') {
            ++i;
            if (i >= path.size()) return nullptr;
            continue;
        }
        if (path[i] == '[') {
            const size_t j = path.find(']', i);
            if (j == std::string::npos) return nullptr;
            std::string tok = trim(path.substr(i + 1, j - i - 1));
            if (!tok.empty() && (tok[0] == '"' || tok[0] == '\'')) {
                const char q = tok[0];
                if (tok.size() < 2 || tok.back() != q) return nullptr;
                tok = tok.substr(1, tok.size() - 2);
                if (cur->t != J::OBJ) return nullptr;
                J* finding = nullptr;
                for (auto& kv : cur->o) if (kv.first == tok) { finding = &kv.second; break; }
                if (!finding) return nullptr;
                cur = finding;
            } else {
                char* end = nullptr;
                const long idx = std::strtol(tok.c_str(), &end, 10);
                if (end == tok.c_str() || *end || cur->t != J::ARR || idx < 0 ||
                    (size_t)idx >= cur->a.size()) return nullptr;
                cur = &cur->a[(size_t)idx];
            }
            i = j + 1;
        } else {
            size_t j = i;
            while (j < path.size() && path[j] != '.' && path[j] != '[') ++j;
            const std::string key = path.substr(i, j - i);
            if (cur->t != J::OBJ) return nullptr;
            J* finding = nullptr;
            for (auto& kv : cur->o) if (kv.first == key) { finding = &kv.second; break; }
            if (!finding) return nullptr;
            cur = finding;
            i = j;
        }
    }
    return cur;
}

struct Json {
    double mode = 0; // 0 num, 1 text, 2 list, 3 json, 4 exists, 5 size
    std::string path, inlineJson, file;
    std::string text;
    std::vector<double> list;
    std::string textCache;
    J cache;
    bool cacheOk = false;
    struct stat st{};
    bool hasSt = false;
    bool fileOk = false;
    std::string fileText;
};

void* create() { return new (std::nothrow) Json(); }
void dispose(void* h) { delete (Json*)h; }

bool readFile(Json* d) {
    if (!d || d->file.empty()) return false;
    struct stat st{};
    if (stat(d->file.c_str(), &st) != 0) return false;
    if (d->hasSt && st.st_mtime == d->st.st_mtime && st.st_size == d->st.st_size &&
        d->fileOk) return true;
    std::ifstream f(d->file, std::ios::binary);
    if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    d->fileText = std::move(s);
    d->st = st;
    d->hasSt = true;
    d->fileOk = true;
    return true;
}

void clear(Json* d) {
    d->text.clear();
    d->list.clear();
}

double step(void* h, const NsContext* c) {
    Json* d = (Json*)h;
    if (!d) return 0.0;
    clear(d);
    std::string source;
    bool hasSource = false;
    if (c && c->xval && c->nsock > 0 && c->xval[0].kind == NS_VAL_TXT && c->xval[0].txt) {
        source = c->xval[0].txt;
        hasSource = true;
    } else if (!d->file.empty()) {
        if (readFile(d)) {
            source = d->fileText;
            hasSource = true;
        }
    } else {
        source = d->inlineJson;
        hasSource = true;
    }
    if (!hasSource) return 0.0;
    if (!d->cacheOk || source != d->textCache) {
        Parser p;
        p.p = source.data();
        p.e = source.data() + source.size();
        J root;
        d->cacheOk = p.root(root);
        if (d->cacheOk) d->cache = std::move(root);
        else d->cache = J{};
        d->textCache = source;
    }
    if (!d->cacheOk) return 0.0;
    J* v = find(d->cache, d->path);
    if (!v) return 0.0;
    const int mode = (int)std::floor(d->mode + 0.5);
    switch (mode) {
        case 1:
            d->text = text(*v);
            return 0.0;
        case 2:
            if (v->t == J::ARR) {
                d->list.reserve(v->a.size());
                for (const J& x : v->a) d->list.push_back(number(x));
            } else {
                d->list.push_back(number(*v));
            }
            return d->list.empty() ? 0.0 : d->list[0];
        case 3:
            writeJson(*v, d->text);
            return 0.0;
        case 4:
            return 1.0;
        case 5:
            return (double)size(*v);
        default:
            return number(*v);
    }
}

int outValue(void* h, int port, NsValue* v) {
    Json* d = (Json*)h;
    if (!d || !v || port != 0 || (int)std::floor(d->mode + 0.5) != 2 || d->list.empty())
        return 0;
    v->kind = NS_VAL_LIST;
    v->num = d->list[0];
    v->txt = nullptr;
    v->data = d->list.data();
    v->rows = 1;
    v->cols = (int)d->list.size();
    return 1;
}

char* outText(void* h, int port) {
    Json* d = (Json*)h;
    if (!d || port != 0) return nullptr;
    const int mode = (int)std::floor(d->mode + 0.5);
    if (mode != 1 && mode != 3) return nullptr;
    char* r = (char*)std::malloc(d->text.size() + 1);
    if (r) std::memcpy(r, d->text.c_str(), d->text.size() + 1);
    return r;
}

const char* const modeChoices[] = {"number", "text", "list", "json", "exists", "size"};
NsParam params[] = {
    {"mode", 0, 5, 0, NS_PARAM_ENUM, 1, nullptr, "type of value extracted", 6, modeChoices},
};
const char* textNames[] = {"path", "json", "file"};

double getParam(void* h, int i) {
    Json* d = (Json*)h;
    return d && i == 0 ? d->mode : 0.0;
}
int setParam(void* h, int i, double v) {
    Json* d = (Json*)h;
    if (!d || i != 0) return 0;
    int m = (int)std::floor(v + 0.5);
    if (m < 0) m = 0;
    if (m > 5) m = 5;
    d->mode = m;
    return 1;
}

char* getText(void* h, int i) {
    Json* d = (Json*)h;
    if (!d) return nullptr;
    const std::string* s = nullptr;
    if (i == 0) s = &d->path;
    else if (i == 1) s = &d->inlineJson;
    else if (i == 2) s = &d->file;
    else return nullptr;
    char* r = (char*)std::malloc(s->size() + 1);
    if (r) std::memcpy(r, s->c_str(), s->size() + 1);
    return r;
}

int setText(void* h, int i, const char* s) {
    Json* d = (Json*)h;
    if (!d || !s) return 0;
    if (i == 0) d->path = s;
    else if (i == 1) { d->inlineJson = s; d->cacheOk = false; }
    else if (i == 2) {
        d->file = s;
        d->hasSt = false;
        d->fileOk = false;
        d->cacheOk = false;
    } else return 0;
    return 1;
}

void addField(std::string& s, const char* k, const std::string& v) {
    s += k;
    s += '=';
    s += std::to_string(v.size());
    s += ':';
    s += v;
    s += ';';
}

char* save(void* h) {
    Json* d = (Json*)h;
    if (!d) return nullptr;
    std::string s;
    addField(s, "mode", std::to_string((int)std::floor(d->mode + 0.5)));
    addField(s, "path", d->path);
    addField(s, "json", d->inlineJson);
    addField(s, "file", d->file);
    char* r = (char*)std::malloc(s.size() + 1);
    if (r) std::memcpy(r, s.c_str(), s.size() + 1);
    return r;
}

bool readField(const char*& p, std::string& k, std::string& v) {
    if (!p || !*p) return false;
    const char* e = p;
    while (*e && *e != '=') ++e;
    if (*e != '=') return false;
    k.assign(p, e - p);
    p = e + 1;
    char* end = nullptr;
    const unsigned long n = std::strtoul(p, &end, 10);
    if (end == p || *end != ':') return false;
    p = end + 1;
    if (std::strlen(p) < n) return false;
    v.assign(p, n);
    p += n;
    if (*p != ';') return false;
    ++p;
    return true;
}

int load(void* h, const char* blob) {
    Json* d = (Json*)h;
    if (!d || !blob) return 0;
    int mode = (int)std::floor(d->mode + 0.5);
    std::string path = d->path, json = d->inlineJson, file = d->file;
    const char* p = blob;
    std::string k, v;
    while (readField(p, k, v)) {
        if (k == "mode") {
            char* end = nullptr;
            const long m = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str()) return 0;
            mode = (int)m;
        } else if (k == "path") path = v;
        else if (k == "json") json = v;
        else if (k == "file") file = v;
    }
    if (mode < 0) mode = 0;
    if (mode > 5) mode = 5;
    d->mode = mode;
    d->path = std::move(path);
    d->inlineJson = std::move(json);
    d->file = std::move(file);
    d->cacheOk = false;
    d->hasSt = false;
    d->fileOk = false;
    return 1;
}

char* labelIn(void*, int p) {
    if (p != 0) return nullptr;
    char* r = (char*)std::malloc(5);
    if (r) std::memcpy(r, "json", 5);
    return r;
}

char* labelOut(void*, int p) {
    if (p != 0) return nullptr;
    char* r = (char*)std::malloc(7);
    if (r) std::memcpy(r, "output", 7);
    return r;
}

NsNodeType types[] = {
    [] {
        NsNodeType t{};
        t.id = "json";
        t.label = "JSON";
        t.color[0] = 160;
        t.color[1] = 210;
        t.color[2] = 180;
        t.create = create;
        t.destroy = dispose;
        t.step = step;
        t.save = save;
        t.load = load;
        t.params = params;
        t.num_params = 1;
        t.getParam = getParam;
        t.setParam = setParam;
        t.text_names = textNames;
        t.num_text = 3;
        t.getText = getText;
        t.setText = setText;
        t.def_in = 1;
        t.outText = outText;
        t.portLabel = labelOut;
        t.outValue = outValue;
        t.portLabelIn = labelIn;
        t.capabilities = NS_CAP_INPUT_LABELS | NS_CAP_TEXT_OUT | NS_CAP_VALUE_OUT;
        return t;
    }(),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("json", "1.0", 1, types, "Data");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
