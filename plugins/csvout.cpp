// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: exports the series arriving on the sockets to a CSV file,
// one column per socket. Useful for moving data computed in the graph (e.g. the
// Kalman-filtered series) to R/Python/spreadsheet without going through headless
// mode.
//
// Inputs: socket 0..N-1 = series. Output: number of rows already written.
// Parameters: `taxa` (samples/s, 0 = one per step), `cabecalho` (0/1).
// Text: `arquivo` (path; default "saida.csv").
//
// Lists and matrices become SEVERAL columns: a list `[a,b,c]` on socket 0
// becomes `c0_0,c0_1,c0_2`; a 2×3 matrix becomes 6 columns in row-major order
// (`c0_0..c0_5`). The width is fixed at the first sample (the header is written
// there); if the shape changes during the run, the extra columns stay empty
// (NaN in the spreadsheet) and the plugin warns once on stderr. Text values are
// written in quotes (double quotes escaped, line breaks become spaces).
//
// Each run REWRITES the file (opens with "w" on the first sample) and flushes
// on every line; changing the path closes the previous one and starts over. The
// file is not stored in the .nodesim (it is external): the saved state is just
// the configuration.
// Build: make plugins | Usage: menu Add -> Data -> Export CSV
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

struct CsvOut {
    std::string fileArg = "saida.csv";
    double rate = 0, header = 1;
    double key = -1e308;
    long n = 0;          // rows written
    FILE* f = nullptr;
    bool warned = false;       // already warned that it could not open
    bool warnedShape = false;  // already warned that the shape of a list/matrix changed
    std::vector<int> larg;     // columns per socket (fixed at open)
    ~CsvOut() { if (f) std::fclose(f); }
};

// how many columns the socket occupies in this sample
int width(const NsContext* c, int s) {
    if (!c || !c->xval || s < 0 || s >= c->nsock) return 1;
    const NsValue& v = c->xval[s];
    if (v.kind == NS_VAL_LIST) return v.cols > 0 ? v.cols : 1;
    if (v.kind == NS_VAL_MAT) return v.rows * v.cols > 0 ? v.rows * v.cols : 1;
    return 1;
}

void textField(FILE* f, const char* t) {
    std::fputc(',', f);
    std::fputc('"', f);
    for (const char* p = t; p && *p; ++p) {
        if (*p == '"') std::fputs("\"\"", f);             // doubled quotes (CSV)
        else if (*p == '\n' || *p == '\r') std::fputc(' ', f);
        else std::fputc(*p, f);
    }
    std::fputc('"', f);
}

bool open(CsvOut* d, const NsContext* c) {
    if (d->f) return true;
    d->f = std::fopen(d->fileArg.c_str(), "w");
    if (!d->f) {
        if (!d->warned) {
            std::fprintf(stderr, "csvout: could not open '%s'\n", d->fileArg.c_str());
            d->warned = true;
        }
        return false;
    }
    d->warned = false;
    d->warnedShape = false;
    const int ns = c ? c->nsock : 0;
    d->larg.assign(ns, 1);
    for (int s = 0; s < ns; ++s) d->larg[s] = width(c, s);
    if (d->header > 0.5) {
        std::fputs("t", d->f);
        for (int s = 0; s < ns; ++s) {
            if (d->larg[s] <= 1) {
                std::fprintf(d->f, ",c%d", s);
            } else {
                for (int j = 0; j < d->larg[s]; ++j) std::fprintf(d->f, ",c%d_%d", s, j);
            }
        }
        std::fputc('\n', d->f);
    }
    return true;
}

void write(CsvOut* d, const NsContext* c) {
    if (!d || !c) return;
    bool newValue = true;
    if (d->rate > 0) {
        double k = std::floor((double)c->t * d->rate);
        newValue = (k != d->key);
        if (newValue) d->key = k;
    }
    if (!newValue) return;
    if (!open(d, c)) return;
    const int ns = c->nsock;
    std::fprintf(d->f, "%.10g", (double)c->t);
    for (int s = 0; s < ns; ++s) {
        const int larg = s < (int)d->larg.size() ? d->larg[s] : 1;
        const NsValue* v = (c->xval && s < c->nsock) ? &c->xval[s] : nullptr;
        if (v && v->kind == NS_VAL_TXT) {
            textField(d->f, v->txt ? v->txt : "");
        } else if (v && (v->kind == NS_VAL_LIST || v->kind == NS_VAL_MAT)) {
            const int w = width(c, s);
            const int data = v->kind == NS_VAL_LIST ? v->cols : v->rows * v->cols;
            const int use = data < larg ? data : larg;
            for (int j = 0; j < use; ++j) std::fprintf(d->f, ",%.9g", v->data ? v->data[j] : 0.0);
            for (int j = use; j < larg; ++j) std::fputc(',', d->f); // empty column
            if (w != larg && !d->warnedShape) {
                std::fprintf(stderr, "csvout: socket %d changed shape (%d -> %d columns); "
                                     "keeping the header columns\n", s, larg, w);
                d->warnedShape = true;
            }
        } else {
            std::fprintf(d->f, ",%.9g", sockValue(c, s));
        }
    }
    std::fputc('\n', d->f);
    std::fflush(d->f);
    ++d->n;
}

void* newValue() { return new (std::nothrow) CsvOut(); }
void dispose(void* h) {
    CsvOut* d = (CsvOut*)h;
    if (!d) return;
    if (d->f) std::fclose(d->f);
    d->f = nullptr;
    delete d;
}

double step(void* h, const NsContext* c) {
    CsvOut* d = (CsvOut*)h;
    if (!d) return 0;
    write(d, c);
    return (double)d->n;
}

// param 0=taxa 1=cabecalho
NsParam params[] = {
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "rows written per second (0 = one per step)", 0, nullptr},
    {"header", 0, 1, 1, NS_PARAM_BOOL, 1, nullptr,
     "write the header with column names", 0, nullptr},
};
double getParam(void* h, int i) {
    CsvOut* d = (CsvOut*)h;
    if (!d) return 0;
    return i == 0 ? d->rate : d->header;
}
int setParam(void* h, int i, double v) {
    CsvOut* d = (CsvOut*)h;
    if (!d) return 0;
    if (i == 0) d->rate = v < 0 ? 0 : v;
    else d->header = v > 0.5 ? 1 : 0;
    return 1;
}

const char* const texts[] = {"file"};
char* getText(void* h, int i) {
    CsvOut* d = (CsvOut*)h;
    if (!d || i != 0) return nullptr;
    char* r = (char*)std::malloc(d->fileArg.size() + 1);
    if (r) std::memcpy(r, d->fileArg.c_str(), d->fileArg.size() + 1);
    return r;
}
int setText(void* h, int i, const char* v) {
    CsvOut* d = (CsvOut*)h;
    if (!d || i != 0 || !v || !*v) return 0;
    if (d->fileArg != v) {
        if (d->f) { std::fclose(d->f); d->f = nullptr; } // next sample opens the new one
        d->fileArg = v;
        d->n = 0;
        d->key = -1e308;
        d->larg.clear();
    }
    return 1;
}

char* portLabel(void* h, int p) {
    (void)h;
    if (p != 0) return nullptr;
    const char* s = "rows";
    char* r = (char*)std::malloc(std::strlen(s) + 1);
    if (r) std::memcpy(r, s, std::strlen(s) + 1);
    return r;
}

// quoted path (with escapes) in the blob
std::string escape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '\\' || c == '"') r += '\\';
        r += c;
    }
    return r;
}
std::string readValue(const char*& p) { // value up to a space; accepts "..." with escapes
    std::string out;
    if (*p == '"') {
        ++p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) ++p;
            out += *p++;
        }
        if (*p == '"') ++p;
    } else {
        while (*p && *p != ' ') out += *p++;
    }
    return out;
}

char* save(void* h) {
    CsvOut* d = (CsvOut*)h;
    if (!d) return nullptr;
    char b[256];
    std::snprintf(b, sizeof b, "\" rate=%.10g header=%.10g", d->rate, d->header);
    std::string s = "file=\"" + escape(d->fileArg) + b;
    char* out = (char*)std::malloc(s.size() + 1);
    if (out) std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

int load(void* h, const char* blob) {
    CsvOut* d = (CsvOut*)h;
    if (!d || !blob) return 0;
    if (d->f) { std::fclose(d->f); d->f = nullptr; }
    std::string fileArg = d->fileArg;
    double rate = d->rate, cab = d->header;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string name(p, e - p);
        p = e + 1;
        if (name == "file") {
            fileArg = readValue(p);
        } else {
            char* end = nullptr;
            double v = std::strtod(p, &end);
            if (end == p) break;
            p = end;
            if (name == "rate") rate = v;
            else if (name == "header") cab = v;
        }
    }
    d->fileArg = fileArg.empty() ? "saida.csv" : fileArg;
    d->rate = rate < 0 ? 0 : rate;
    d->header = cab > 0.5 ? 1 : 0;
    d->n = 0;
    d->key = -1e308; // each run restarts the file
    d->larg.clear();
    d->warnedShape = false;
    return 1;
}

NsNodeType types[] = {
    {"csvout", "Export CSV", {170, 210, 160}, newValue, dispose, step, save, load,
     2, params, getParam, setParam, nullptr,
     1, texts, getText, setText,
     false, nullptr, nullptr, portLabel,
     2, 0, 0, nullptr},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("csvout", "2.0", 1, types, "Data");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
