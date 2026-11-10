// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// SCRIPT public API: parameters, compilation, and program construction.

#include "expr.h"
#include "expr_internal.h"
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
bool isIdent(const std::string& s) {
    if (s.empty()) return false;
    if (!(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}

// ---------- node parameters ----------
bool parseParamsImpl(const std::string& src, std::vector<ExprParam>& out, std::string& err) {
    auto trim = [](const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace((unsigned char)s[a])) ++a;
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    };
    size_t i = 0;
    while (i <= src.size()) {
        size_t j = i;
        bool inStr = false;
        while (j < src.size()) {
            char c = src[j];
            if (c == '"') inStr = !inStr;
            else if (!inStr && (c == ',' || c == ';' || c == '\n')) break;
            ++j;
        }
        std::string item = trim(src.substr(i, j - i));
        i = j + 1;
        if (item.empty()) continue;
        if (item[0] == '#') continue;
        if (item.size() >= 2 && item[0] == '/' && item[1] == '/') continue;
        size_t eq = item.find('=');
        if (eq == std::string::npos) { err = "expected name=value in \"" + item + "\""; return false; }
        std::string name = trim(item.substr(0, eq));
        std::string val = trim(item.substr(eq + 1));
        if (!isIdent(name)) { err = "invalid name: \"" + name + "\""; return false; }
        ExprParam pr;
        pr.name = name;
        if (!val.empty() && val[0] == '"') {
            std::string s;
            size_t k = 1;
            while (k < val.size() && val[k] != '"') {
                if (val[k] == '\\' && k + 1 < val.size()) {
                    ++k;
                    char e = val[k];
                    if (e == 'n') s += '\n';
                    else if (e == 't') s += '\t';
                    else s += e;
                } else s += val[k];
                ++k;
            }
            if (k >= val.size() || val[k] != '"') { err = "unterminated string in \"" + item + "\""; return false; }
            pr.isText = true;
            pr.str = s;
        } else {
            char* e = nullptr;
            double dd = std::strtod(val.c_str(), &e);
            while (e && (*e == ' ' || *e == '\t')) ++e;
            if (e && e != val.c_str() && *e == '\0') { pr.num = dd; pr.isText = false; }
            else { pr.isText = true; pr.str = val; } // text without quotes
        }
        bool found = false;
        for (auto& q : out) if (q.name == name) { q = pr; found = true; break; }
        if (!found) out.push_back(pr);
    }
    return true;
}
} // namespace

Expr::Expr(std::vector<Stmt> s, std::vector<std::string> names, std::vector<Func> f)
    : stmts(std::move(s)), varNames(std::move(names)), funcs(std::move(f)) {
    for (int i = (int)stmts.size() - 1; i >= 0; --i) {
        const Stmt& st = stmts[(size_t)i];
        if ((st.k == Stmt::EXPR || st.k == Stmt::ASSIGN) && !st.e.empty()) {
            outStmt = i;
            outCount = (int)st.e.size();
            break;
        }
    }
}
Expr::~Expr() = default;

bool Expr::parseParams(const std::string& src, std::vector<ExprParam>& out, std::string& err) {
    out.clear();
    err.clear();
    return parseParamsImpl(src, out, err);
}

std::unique_ptr<Expr> Expr::parseList(const std::string& src, std::string& err, const std::string& params) {
    err.clear();
    std::vector<ExprParam> pv;
    if (!parseParamsImpl(params, pv, err)) { err = "parameters: " + err; return nullptr; }
    ParsedProgram pp;
    if (!parseProgram(src, pv, pp, err)) return nullptr;
    auto e = std::unique_ptr<Expr>(new Expr(std::move(pp.stmts), std::move(pp.varNames), std::move(pp.funcs)));
    if (e->count() == 0) { err = "program with no output (end with expressions)"; return nullptr; }
    return e;
}

std::unique_ptr<Expr> Expr::parse(const std::string& src, std::string& err, const std::string& params) {
    auto e = parseList(src, err, params);
    if (!e) return nullptr;
    if (e->count() != 1) { err = "expected a single expression (commas separate outputs)"; return nullptr; }
    return e;
}
