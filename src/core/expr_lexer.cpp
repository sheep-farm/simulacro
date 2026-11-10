// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// SCRIPT lexer: tokens, line continuation, comments, and strings.

#include "expr_internal.h"
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

// ';' and newline separate statements; the line continues if it ends with
// an operator/comma/'(' or if inside parentheses. '#' and '//' comment.
// ';' inside parentheses becomes T::Semi (for header). '{' and '}' group
// blocks; newlines inside a block still separate statements.
bool tokenize(const std::string& src, std::vector<Tok>& out, std::string& err) {
    const char* p = src.c_str();
    size_t off = 0;
    int depth = 0;
    int line = 1;
    size_t lineStart = 0;
    auto colOf = [&](size_t at) { return (int)(at >= lineStart ? at - lineStart + 1 : 1); };
    auto emit = [&](T ty, double num, std::string str, size_t at, int ln, int col) {
        Tok tk;
        tk.type = ty;
        tk.num = num;
        tk.s = std::move(str);
        tk.off = at;
        tk.line = ln;
        tk.col = col;
        out.push_back(std::move(tk));
    };
    auto failAt = [&](const std::string& msg, int ln, int col) {
        if (err.empty()) err = msg + " (line " + std::to_string(ln) + ", column " + std::to_string(col) + ")";
        return false;
    };
    auto incomplete = [&]() -> bool {
        if (out.empty()) return true;
        T ty = out.back().type;
        return ty == T::Op || ty == T::Comma || ty == T::Assign || ty == T::Q ||
               ty == T::C || ty == T::LP;
    };
    auto sep = [&](size_t o) {
        if (out.empty() || out.back().type != T::Sep)
            emit(T::Sep, 0, {}, o, line, colOf(o));
    };
    while (*p) {
        const char c = *p;
        if (c == '#' || (c == '/' && p[1] == '/')) { while (*p && *p != '\n') ++p; continue; }
        if (c == '\n') {
            if (depth == 0 && !incomplete()) sep(off);
            ++p; ++off;
            ++line;
            lineStart = off;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') { ++p; ++off; continue; }
        if (c == ';') {
            if (depth > 0) emit(T::Semi, 0, ";", off, line, colOf(off));
            else sep(off);
            ++p; ++off;
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)p[1]))) {
            const size_t at = off;
            char* e = nullptr;
            const double v = std::strtod(p, &e);
            if (e == p) return failAt("invalid number", line, colOf(at));
            emit(T::Num, v, {}, at, line, colOf(at));
            off += (size_t)(e - p);
            p = e;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            const size_t at = off;
            const char* st = p;
            while (std::isalnum((unsigned char)*p) || *p == '_') ++p;
            emit(T::Id, 0, std::string(st, p), at, line, colOf(at));
            off += (size_t)(p - st);
            continue;
        }
        if (c == '"') { // text literal
            const size_t at = off;
            const int ln = line, cl = colOf(at);
            ++p; ++off;
            std::string str;
            while (*p && *p != '"') {
                if (*p == '\n') { ++p; ++off; ++line; lineStart = off; str += '\n'; continue; }
                if (*p == '\\') {
                    ++p; ++off;
                    const char e = *p;
                    if (e == 'n') str += '\n';
                    else if (e == 't') str += '\t';
                    else if (e == 'r') str += '\r';
                    else if (e == '\\' || e == '"') str += e;
                    else return failAt("invalid escape in string", ln, cl);
                    if (*p) { ++p; ++off; }
                } else { str += *p++; ++off; }
            }
            if (*p != '"') return failAt("unterminated string", ln, cl);
            ++p; ++off;
            emit(T::Str, 0, std::move(str), at, ln, cl);
            continue;
        }
        std::string two;
        if (p[1]) two.assign(p, 2);
        else two.assign(1, *p);
        if (two == "<=" || two == ">=" || two == "==" || two == "!=" || two == "&&" || two == "||" ||
            two == "+=" || two == "-=" || two == "*=" || two == "/=" || two == "%=") {
            const T ty = (two[1] == '=' && two != "<=" && two != ">=" && two != "==" && two != "!=")
                             ? T::Assign : T::Op;
            emit(ty, 0, two, off, line, colOf(off));
            p += 2; off += 2; continue;
        }
        const size_t at = off;
        const int cl = colOf(at);
        const char d = *p++; ++off;
        if (d == '(') { ++depth; emit(T::LP, 0, {}, at, line, cl); }
        else if (d == ')') { if (depth > 0) --depth; emit(T::RP, 0, {}, at, line, cl); }
        else if (d == '{') emit(T::LB, 0, {}, at, line, cl);
        else if (d == '}') emit(T::RB, 0, {}, at, line, cl);
        else if (d == ',') emit(T::Comma, 0, {}, at, line, cl);
        else if (d == '?') emit(T::Q, 0, {}, at, line, cl);
        else if (d == ':') emit(T::C, 0, {}, at, line, cl);
        else if (d == '=') emit(T::Assign, 0, "=", at, line, cl);
        else if (d == '+' || d == '-' || d == '*' || d == '/' || d == '%' || d == '^' ||
                 d == '<' || d == '>' || d == '!') emit(T::Op, 0, std::string(1, d), at, line, cl);
        else return failAt(std::string("invalid character '") + d + "'", line, cl);
    }
    emit(T::End, 0, {}, off, line, colOf(off));
    return true;
}

