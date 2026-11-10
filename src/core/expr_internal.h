// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal structures shared by the SCRIPT lexer, parser, and evaluator.
#pragma once
#include "expr.h"
#include <string>
#include <vector>

enum class T { End, Num, Str, Id, Op, LP, RP, Comma, Q, C, Assign, Sep, Semi, LB, RB };
struct Tok { T type = T::End; double num = 0; std::string s; size_t off = 0; int line = 1, col = 1; };
bool tokenize(const std::string& src, std::vector<Tok>& out, std::string& err);

struct ParsedProgram {
    std::vector<Expr::Stmt> stmts;
    std::vector<std::string> varNames;
    std::vector<Expr::Func> funcs;
};
bool parseProgram(const std::string& src, const std::vector<ExprParam>& params,
                  ParsedProgram& out, std::string& err);
