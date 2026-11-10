// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: CSV reader — feeds the simulation with tabular data.
// Columns can be numeric OR textual: each port yields a number when the cell
// is numeric, and text when it is not (the node bar/value shows the text).
//
// Panel: texts "file", "sep" and "columns"; numerics col/rate/loop/header/watch.
//   arquivo : .csv path (accepts spaces)
//   sep     : separator (default ','); use ';' for semicolon, '\t' for TAB
//   colunas : desired columns, comma-separated. Each selector is:
//               - the column NAME (if there is a header, header=1), or
//               - "colN" (0-based index) when the column has no name, or
//               - a bare number (same as colN).
//             List order = port order p0,p1,...
//             Empty = simple mode: port 0 = column 'col' (and the count comes
//             from the node's 'saídas'/'nOut' field, as before).
//   col     : base column of simple mode (when 'columns' is empty)
//   rate    : rows per simulation second (default 10)
//   loop    : 1 = restart at the end (default), 0 = hold the last row
//   header  : 1 = the first row has the column names
//   watch   : 1 = reload if the file changes on disk (default); 0 = never
// Current row = floor(t * rate): restarting (F5) goes back to the beginning.
// When 'columns' is declared, the node controls its own number of outputs (numOut).
// Legacy config (single line): path col=N rate=R loop=0/1 sep=C header=0/1 watch=0/1
// Build: make plugins | Usage: menu Add -> CSV (plugin)
#include <plugin.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
constexpr size_t MAX_ROWS = 2000000; // safety limit

struct Csv {
    std::string path;
    std::string sel;      // column selectors (comma-separated list); empty = simple mode
    int col = 0;          // base column of simple mode
    double rate = 10.0;
    bool loop = true;
    char sep = ',';
    bool header = true;
    bool watch = true;    // reload if the file changes on disk
    long long mtimeNs = 0;
    long long fsize = -1;
    std::vector<std::vector<std::string>> rows; // raw cells (without the header)
    std::vector<std::string> names;             // column names (header or colN)
    std::vector<int> selCols;                   // resolved columns (-1 = not found)
    int ncols = 0;
    bool loaded = false;
    std::vector<std::string> txt;               // text per port from the last step
    // mode 1 (batch): the whole file becomes ONE matrix (rows × columns) on the wire
    double mode = 0;
    std::vector<double> mat;                    // rows × columns, row by row
    int matRows = 0, matCols = 0;
    bool matDirty = true, newMatrix = false;
};

void* create() { return new (std::nothrow) Csv(); }
void destroy(void* h) { delete (Csv*)h; }

// file stamp (mtime in ns + size); false if inaccessible
bool fileStamp(const std::string& p, long long& mt, long long& sz) {
    struct stat st;
    if (p.empty() || ::stat(p.c_str(), &st) != 0) return false;
    mt = (long long)st.st_mtim.tv_sec * 1000000000LL + (long long)st.st_mtim.tv_nsec;
    sz = (long long)st.st_size;
    return true;
}

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// bare number? (integer) -> true and value
bool asInt(const std::string& s, int& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    long x = std::strtol(s.c_str(), &e, 10);
    if (e == s.c_str() || *e != '\0') return false;
    v = (int)x;
    return true;
}

// "colN" (integer N >= 0)? -> true and N
bool asColN(const std::string& s, int& v) {
    if (s.size() < 4) return false;
    if (std::tolower((unsigned char)s[0]) != 'c' || std::tolower((unsigned char)s[1]) != 'o' ||
        std::tolower((unsigned char)s[2]) != 'l') return false;
    return asInt(s.substr(3), v);
}

// numeric cell? (tolerates surrounding spaces)
bool parseNum(const std::string& s, double& v) {
    if (s.empty()) return false;
    const char* p = s.c_str();
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') return false;
    char* e = nullptr;
    double x = std::strtod(p, &e);
    if (e == p) return false;
    while (*e == ' ' || *e == '\t') ++e;
    if (*e != '\0') return false;
    v = x;
    return true;
}

// split a line into cells respecting quotes ("a,b" = 1 cell; "" = ")
std::vector<std::string> splitLine(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inq) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else inq = false;
            } else cur += c;
        } else if (c == '"') {
            inq = true;
        } else if (c == sep) {
            out.push_back(cur); cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

// split the selector list by comma, with trim
std::vector<std::string> splitSel(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        size_t a = 0, b = cur.size();
        while (a < b && std::isspace((unsigned char)cur[a])) ++a;
        while (b > a && std::isspace((unsigned char)cur[b - 1])) --b;
        if (b > a) out.push_back(cur.substr(a, b - a));
        cur.clear();
    };
    for (char c : s) { if (c == ',') flush(); else cur += c; }
    flush();
    return out;
}

// resolve 'sel' (names/colN/indices) into column indices; empty = {col}
void resolveSel(Csv& d) {
    d.selCols.clear();
    std::vector<std::string> toks = splitSel(d.sel);
    if (toks.empty()) { d.selCols.push_back(d.col); return; }
    for (const std::string& t : toks) {
        int idx = -1;
        for (size_t i = 0; i < d.names.size(); ++i) if (ieq(d.names[i], t)) { idx = (int)i; break; }
        if (idx < 0) asColN(t, idx);
        if (idx < 0) asInt(t, idx);
        if (idx < 0) std::printf("csv: column '%s' not found\n", t.c_str());
        d.selCols.push_back(idx);
    }
}

// split by spaces respecting double quotes (paths/selectors with spaces)
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false, has = false;
    for (char c : s) {
        if (c == '"') { inq = !inq; has = true; continue; }
        if (!inq && std::isspace((unsigned char)c)) {
            if (has) { out.push_back(cur); cur.clear(); has = false; }
            continue;
        }
        cur += c; has = true;
    }
    if (has) out.push_back(cur);
    return out;
}

std::string canon(const Csv& d) {
    char b[96];
    std::snprintf(b, sizeof b, " col=%d rate=%.6g loop=%d header=%d watch=%d mode=%d",
                  d.col, d.rate, d.loop ? 1 : 0, d.header ? 1 : 0, d.watch ? 1 : 0, d.mode > 0.5 ? 1 : 0);
    std::string s;
    if (!d.path.empty()) { s += '"'; s += d.path; s += '"'; } // quotes: accepts spaces
    s += b;
    s += " sep=";
    if (d.sep == ' ' || d.sep == '"' || d.sep == ';') { s += '"'; s += d.sep; s += '"'; }
    else s += d.sep;
    if (!d.sel.empty()) { s += " columns=\""; s += d.sel; s += '"'; }
    return s;
}

// "key=value" tokens or the path (1st token without '=')
bool applyConfig(const std::string& cfg, Csv& d) {
    d.path.clear(); d.sel.clear();
    d.col = 0; d.rate = 10.0; d.loop = true; d.sep = ','; d.header = true; d.watch = true; d.mode = 0;
    for (const std::string& tok : split(cfg)) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) { // path
            if (d.path.empty()) d.path = tok;
            else return false; // two paths: ambiguous
            continue;
        }
        std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        if (k == "col") d.col = std::atoi(v.c_str());
        else if (k == "columns") d.sel = v;
        else if (k == "rate") { d.rate = std::atof(v.c_str()); if (!(d.rate > 0)) d.rate = 10.0; }
        else if (k == "loop") d.loop = (std::atoi(v.c_str()) != 0);
        else if (k == "header") d.header = (std::atoi(v.c_str()) != 0);
        else if (k == "watch") d.watch = (std::atoi(v.c_str()) != 0);
        else if (k == "mode") d.mode = std::atoi(v.c_str()) > 0 ? 1 : 0;
        else if (k == "sep") { if (!v.empty()) d.sep = v[0]; }
        else return false; // unknown key
    }
    if (d.col < 0) d.col = 0;
    return true;
}

// read the file into memory (raw cells) and resolve the names/columns
bool readFile(Csv& d) {
    d.rows.clear(); d.names.clear(); d.ncols = 0; d.selCols.clear(); d.txt.clear();
    d.loaded = false; d.mtimeNs = 0; d.fsize = -1;
    if (d.path.empty()) { resolveSel(d); return true; } // no file: ok (output 0)
    std::ifstream f(d.path);
    if (!f) { std::printf("csv: could not open '%s'\n", d.path.c_str()); return false; }
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells = splitLine(line, d.sep);
        if (first) {
            first = false;
            if (d.header) { // first row = names (strip UTF-8 BOM from the first name)
                d.names = cells;
                if (!d.names.empty() && d.names[0].size() >= 3 &&
                    (unsigned char)d.names[0][0] == 0xEF && (unsigned char)d.names[0][1] == 0xBB &&
                    (unsigned char)d.names[0][2] == 0xBF)
                    d.names[0].erase(0, 3);
                continue;
            }
        }
        first = false;
        if ((int)cells.size() > d.ncols) d.ncols = (int)cells.size();
        d.rows.push_back(std::move(cells));
        if (d.rows.size() >= MAX_ROWS) { std::printf("csv: limit of %zu rows reached\n", MAX_ROWS); break; }
    }
    if ((int)d.names.size() > d.ncols) d.ncols = (int)d.names.size();
    if (d.names.empty()) // no header: synthetic names col0..colN
        for (int i = 0; i < d.ncols; ++i) d.names.push_back("col" + std::to_string(i));
    d.loaded = true;
    fileStamp(d.path, d.mtimeNs, d.fsize);
    resolveSel(d);
    std::printf("csv: '%s' %zu rows, %d columns%s\n", d.path.c_str(), d.rows.size(), d.ncols,
                d.names.empty() ? "" : " (nomeadas)");
    return true;
}

// reload only if the file changed (mtime/size); on failure, keep the previous one
void refresh(Csv& d) {
    if (!d.watch || d.path.empty()) return;
    long long mt, sz;
    if (!fileStamp(d.path, mt, sz)) return;
    if (mt == d.mtimeNs && sz == d.fsize) return;
    std::vector<std::vector<std::string>> keepRows = d.rows;
    std::vector<std::string> keepNames = d.names;
    std::vector<int> keepSel = d.selCols;
    int keepCols = d.ncols; bool keepLoaded = d.loaded;
    long long keepMt = d.mtimeNs, keepSz = d.fsize;
    std::printf("csv: '%s' alterado, recarregando\n", d.path.c_str());
    if (!readFile(d)) {
        d.rows = std::move(keepRows); d.names = std::move(keepNames); d.selCols = std::move(keepSel);
        d.ncols = keepCols; d.loaded = keepLoaded; d.mtimeNs = keepMt; d.fsize = keepSz;
    }
}

// current row (index) for time t; -1 = empty
long rowAt(const Csv& d, double t) {
    if (d.rows.empty()) return -1;
    long n = (long)d.rows.size();
    double f = t * d.rate;
    if (f < 0) f = 0;
    long r = (long)std::floor(f);
    if (r >= n) r = d.loop ? (r % n) : (n - 1);
    return r;
}

const std::string* cellRaw(const Csv& d, long r, int col) {
    if (r < 0 || col < 0 || (size_t)r >= d.rows.size()) return nullptr;
    const std::vector<std::string>& row = d.rows[(size_t)r];
    if ((size_t)col >= row.size()) return nullptr;
    return &row[(size_t)col];
}

// column of port k: explicit list (sel) or simple window (col + k)
int portCol(const Csv& d, int k) {
    if (!d.sel.empty()) return (k >= 0 && k < (int)d.selCols.size()) ? d.selCols[(size_t)k] : -1;
    return d.col + k;
}

// batch mode: build the rows×columns matrix (non-numeric cell = 0)
void buildMatrix(Csv& d) {
    d.mat.clear();
    d.matRows = 0;
    d.matCols = 0;
    if (!d.loaded || d.rows.empty()) return;
    std::vector<int> cols;
    if (!d.sel.empty()) cols = d.selCols;
    else cols.push_back(d.col);
    std::vector<int> ok;
    for (int c : cols) if (c >= 0) ok.push_back(c);
    if (ok.empty()) return;
    const size_t MAXE = 4000000; // limit: ~32 MB of doubles
    size_t n = d.rows.size();
    if (n * ok.size() > MAXE) n = MAXE / ok.size();
    if (n < 1) return;
    d.mat.assign(n * ok.size(), 0.0);
    for (size_t r = 0; r < n; ++r)
        for (size_t k = 0; k < ok.size(); ++k) {
            const std::string* raw = cellRaw(d, (long)r, ok[k]);
            double v = 0;
            if (raw && !raw->empty()) parseNum(*raw, v);
            d.mat[r * ok.size() + k] = v;
        }
    d.matRows = (int)n;
    d.matCols = (int)ok.size();
    d.newMatrix = true;
}

void ensureMatrix(Csv& d) { // re-read if the file changed; build only when needed
    refresh(d);
    if (!d.matDirty) return;
    buildMatrix(d);
    d.matDirty = false;
}

// fill out[] (numbers; text -> 0) and txt[] (text per port)
void sample(Csv& d, const NsContext* c, double* out, int nout) {
    refresh(d);
    d.txt.assign((size_t)nout, std::string());
    long r = rowAt(d, c->t);
    for (int k = 0; k < nout; ++k) {
        out[k] = 0.0f;
        const std::string* raw = cellRaw(d, r, portCol(d, k));
        if (!raw || raw->empty()) continue;
        double v;
        if (parseNum(*raw, v)) out[k] = v; // numeric
        else d.txt[(size_t)k] = *raw;             // text
    }
}

double step(void* h, const NsContext* c) {
    Csv* d = (Csv*)h;
    if (!d || !c) return 0;
    if (d->mode > 0.5) { ensureMatrix(*d); return (double)d->matRows; }
    double o = 0.0;
    sample(*d, c, &o, 1);
    return o;
}

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Csv* d = (Csv*)h;
    if (!d || !c || !out || nout <= 0) return 0;
    if (d->mode > 0.5) { // batch: port 0 = number of rows (the matrix goes via outValue)
        ensureMatrix(*d);
        out[0] = d->matRows;
        for (int k = 1; k < nout; ++k) out[k] = 0;
        return 1;
    }
    sample(*d, c, out, nout);
    return 1;
}

// publish the batch-mode matrix (1 = new value; 0 = keep the previous one)
int outValue(void* h, int port, NsValue* v) {
    Csv* d = (Csv*)h;
    if (!d || !v || d->mode < 0.5 || port != 0) return 0;
    ensureMatrix(*d);
    if (!d->newMatrix) return 0;
    d->newMatrix = false;
    if (d->matRows <= 0 || d->matCols <= 0) { v->kind = NS_VAL_NUM; return 1; } // clear
    v->kind = NS_VAL_MAT;
    v->num = (double)d->matRows;
    v->data = d->mat.data();
    v->rows = d->matRows;
    v->cols = d->matCols;
    return 1;
}

// number of outputs driven by the column list (0 = host controls via the 'saídas' field)
int numOut(void* h) {
    Csv* d = (Csv*)h;
    if (!d || d->mode > 0.5) return d ? 1 : 0;
    if (d->sel.empty()) return 0;
    int n = (int)d->selCols.size();
    return n < 1 ? 1 : n;
}

char* outText(void* h, int port) { // mallocked string if port k has text; otherwise null
    Csv* d = (Csv*)h;
    if (!d || port < 0 || port >= (int)d->txt.size() || d->txt[(size_t)port].empty()) return nullptr;
    const std::string& s = d->txt[(size_t)port];
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

// name of port k: column name (header) or "colN"; raw selector if unresolved
char* portLabel(void* h, int k) {
    Csv* d = (Csv*)h;
    if (!d || k < 0) return nullptr;
    if (d->mode > 0.5) { // batch: "rows×columns"
        if (k != 0) return nullptr;
        std::string s = std::to_string(d->matRows) + "×" + std::to_string(d->matCols);
        char* o = (char*)std::malloc(s.size() + 1);
        if (o) std::memcpy(o, s.c_str(), s.size() + 1);
        return o;
    }
    int ci = portCol(*d, k);
    std::string s;
    if (ci >= 0) {
        if ((size_t)ci < d->names.size() && !d->names[(size_t)ci].empty()) s = d->names[(size_t)ci];
        else s = "col" + std::to_string(ci);
    } else if (!d->sel.empty()) {
        std::vector<std::string> toks = splitSel(d->sel);
        if (k < (int)toks.size()) s = toks[(size_t)k];
    }
    if (s.empty()) return nullptr;
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

char* save(void* h) {
    Csv* d = (Csv*)h;
    if (!d) return nullptr;
    std::string s = canon(*d);
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int load(void* h, const char* s) {
    Csv* d = (Csv*)h;
    if (!d || !s) return 0;
    Csv o;
    if (!applyConfig(s, o)) return 0; // syntax error: keep the previous one
    if (!readFile(o)) return 0;       // inaccessible file: keep the previous one
    *d = std::move(o);
    return 1;
}

// ---- structured parameters (numeric) ----
constexpr int P_COL = 0, P_RATE = 1, P_LOOP = 2, P_HEADER = 3, P_WATCH = 4, P_MODE = 5;

const char* const modeChoices[] = {"stream", "batch"};
NsParam params[] = {
    {"col",    0.0,   64.0,   0.0, NS_PARAM_INT, 1, nullptr,
     "base column when 'columns' is empty", 0, nullptr},
    {"rate",   0.01, 1000.0, 10.0, NS_PARAM_NUM, 1, "Hz",
     "rows read per simulation second", 0, nullptr},
    {"loop",   0.0,    1.0,   1.0, NS_PARAM_BOOL, 1, nullptr,
     "restart at end of file", 0, nullptr},
    {"header", 0.0,    1.0,   1.0, NS_PARAM_BOOL, 1, nullptr,
     "the first row contains the column names", 0, nullptr},
    {"watch",  0.0,    1.0,   1.0, NS_PARAM_BOOL, 1, nullptr,
     "reload if the file changes on disk", 0, nullptr},
    {"mode",   0.0,    1.0,   0.0, NS_PARAM_ENUM, 1, nullptr,
     "stream (1 value/step) or batch (whole matrix)", 2, modeChoices},
};

double getParam(void* h, int i) {
    Csv* d = (Csv*)h;
    if (!d) return 0;
    switch (i) {
        case P_COL:    return d->col;
        case P_RATE:   return d->rate;
        case P_LOOP:   return d->loop ? 1 : 0;
        case P_HEADER: return d->header ? 1 : 0;
        case P_WATCH:  return d->watch ? 1 : 0;
        case P_MODE:   return d->mode;
        default:       return 0;
    }
}

int setParam(void* h, int i, double v) {
    Csv* d = (Csv*)h;
    if (!d) return 0;
    switch (i) {
        case P_COL: d->col = (int)(v + 0.5); if (d->col < 0) d->col = 0; if (d->sel.empty()) resolveSel(*d); break;
        case P_RATE: if (v > 0) d->rate = v; break;
        case P_LOOP: d->loop = (v != 0); break;
        case P_HEADER: { // 'header' changes the read: reload the file
            bool nh = (v != 0);
            if (nh != d->header) { d->header = nh; if (!d->path.empty()) readFile(*d); }
            break;
        }
        case P_WATCH: d->watch = (v != 0); break;
        case P_MODE: d->mode = v > 0.5 ? 1 : 0; d->matDirty = true; break;
        default: return 0;
    }
    return 1;
}

// ---- text parameters: file, separator and column list ----
const char* textNames[] = {"file", "sep", "columns"};

char* getText(void* h, int i) {
    Csv* d = (Csv*)h;
    if (!d) return nullptr;
    std::string s;
    if (i == 0) s = d->path;
    else if (i == 1) s = std::string(1, d->sep);
    else if (i == 2) s = d->sel;
    else return nullptr;
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int setText(void* h, int i, const char* s) {
    Csv* d = (Csv*)h;
    if (!d || !s) return 0;
    if (i == 2) { // column list: re-resolve the names (without re-reading the file)
        d->sel = s;
        resolveSel(*d);
        d->txt.assign(d->selCols.size(), std::string());
        d->matDirty = true; // batch mode rebuilds the matrix with the new columns
        return 1;
    }
    if (i == 1) { // separator: 1st character; accepts \t
        char ns = s[0];
        if (ns == '\\' && s[1] == 't') ns = '\t';
        if (ns == '\0') return 0;
        if (ns != d->sep) { d->sep = ns; if (!d->path.empty()) readFile(*d); }
        return 1;
    }
    if (i != 0) return 0;
    if (s == d->path && d->loaded) return 1; // same file: do not re-read (watch handles changes)
    std::string old = d->path;
    d->path = s;
    if (d->path.empty()) { // clear: no file (output 0)
        d->rows.clear(); d->names.clear(); d->selCols.clear(); d->ncols = 0;
        d->loaded = false; d->mtimeNs = 0; d->fsize = -1;
        resolveSel(*d);
        return 1;
    }
    if (!readFile(*d)) { // inaccessible file: restore the previous one
        d->path = old;
        readFile(*d); // best-effort to return to the old state
        return 0;
    }
    return 1;
}

NsNodeType types[] = {
    {"csv", "CSV", {150, 220, 255}, create, destroy, step, save, load,
     6, params, getParam, setParam, stepMulti,
     3, textNames, getText, setText,
     1, numOut, outText, portLabel,
     1, 0, 0, nullptr, NS_SHAPE_BOX, outValue},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("csv", "2.1", 1, types, "Data");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
