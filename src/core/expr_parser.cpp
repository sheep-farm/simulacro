// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// SCRIPT parser: expressions, statements, blocks, and functions.

#include "expr_internal.h"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
// ---------- ids ----------
int varId(const std::string& n) {
    if (n == "t") return 0;
    if (n == "dt") return 1;
    if (n == "x") return 2;
    if (n == "sum") return 3;
    if (n == "n") return 4;
    if (n == "pi") return 5;
    if (n == "e") return 6;
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'x') {
        int k = 0;
        for (size_t j = 1; j < n.size(); ++j) {
            if (n[j] < '0' || n[j] > '9') return -1;
            k = k * 10 + (n[j] - '0');
        }
        if (k <= 15) return 100 + k;
    }
    return -1;
}
int binOp(const std::string& s) {
    if (s == "+") return 0;
    if (s == "-") return 1;
    if (s == "*") return 2;
    if (s == "/") return 3;
    if (s == "%") return 4;
    if (s == "^") return 5;
    if (s == "<") return 6;
    if (s == "<=") return 7;
    if (s == ">") return 8;
    if (s == ">=") return 9;
    if (s == "==") return 10;
    if (s == "!=") return 11;
    if (s == "&&") return 12;
    if (s == "||") return 13;
    return -1;
}
struct FnDef { const char* name; int arity; int id; };
const FnDef kFns[] = {
    {"sin",1,0},{"cos",1,1},{"tan",1,2},{"asin",1,3},{"acos",1,4},{"atan",1,5},
    {"exp",1,6},{"log",1,7},{"sqrt",1,8},{"abs",1,9},{"floor",1,10},{"ceil",1,11},{"round",1,12},
    {"min",2,13},{"max",2,14},{"pow",2,15},{"atan2",2,16},{"hypot",2,17},
    {"clamp",3,18},
    {"log10",1,19},{"log2",1,20},{"trunc",1,21},{"sign",1,22},
    {"sinh",1,23},{"cosh",1,24},{"tanh",1,25},{"deg",1,26},{"rad",1,27},
    {"noise",1,28},{"len",1,29},{"num",1,30},{"str",1,31},
    {"lerp",3,32},{"wrap",3,33},{"smoothstep",3,34},{"div",3,35},
    {"fmt",2,36},{"rand",0,37},{"nin",1,38},{"ssum",1,39},
    {"sval",1,40},{"at",2,41},{"mget",3,42},{"rows",1,43},{"cols",1,44},
    {"mean",1,45},{"sumv",1,46},{"col",2,47},{"row",2,48},
    {"evt",1,51},{"evt_kind",1,52},{"evt_t",1,53},
};
int fnLookup(const std::string& n, int& arity) {
    for (auto& f : kFns) if (n == f.name) { arity = f.arity; return f.id; }
    return -1;
}

// ---------- parser ----------
struct P {
    const std::vector<Tok>& t;
    size_t i = 0;
    std::string err;
    std::map<std::string, const ExprParam*> params;
    std::map<std::string, int> locals;
    std::vector<std::string> varNames;
    std::vector<Expr::Stmt> stmts;
    std::vector<Expr::Func> funcs;
    std::map<std::string, int> funcIndex;
    bool inFunc = false;
    int loopDepth = 0;

    explicit P(const std::vector<Tok>& toks) : t(toks) {}
    const Tok& cur() const { return t[i]; }
    const Tok& peek(size_t k = 1) const { return t[i + k < t.size() ? i + k : t.size() - 1]; }
    bool fail(const std::string& m) {
        if (err.empty())
            err = m + " (line " + std::to_string(cur().line) + ", column " +
                  std::to_string(cur().col) + ")";
        return false;
    }
    bool failNode(const Expr::Node* n, const std::string& m) {
        if (err.empty())
            err = m + " (line " + std::to_string(n ? n->line : cur().line) + ", column " +
                  std::to_string(n ? n->col : cur().col) + ")";
        return false;
    }
    bool eat(T ty) { if (cur().type == ty) { ++i; return true; } return false; }
    bool eatId(const char* s) {
        if (cur().type == T::Id && cur().s == s) { ++i; return true; }
        return false;
    }
    bool expect(T ty, const char* what) {
        if (cur().type == ty) { ++i; return true; }
        return fail(std::string("esperado ") + what);
    }
    void skipSeps() { while (cur().type == T::Sep) ++i; }
    std::unique_ptr<Expr::Node> mk(Expr::Node::K k) {
        auto n = std::unique_ptr<Expr::Node>(new Expr::Node());
        n->k = k;
        n->line = cur().line;
        n->col = cur().col;
        return n;
    }
    std::unique_ptr<Expr::Node> expr() { return ternary(); }

    std::unique_ptr<Expr::Node> ternary() {
        auto c = oor();
        if (!c || !eat(T::Q)) return c;
        auto a = expr();
        if (!a || !eat(T::C)) { fail("expected ':' in ternary"); return nullptr; }
        auto b = ternary();
        if (!b) return nullptr;
        auto n = mk(Expr::Node::TER);
        n->ch.push_back(std::move(c)); n->ch.push_back(std::move(a)); n->ch.push_back(std::move(b));
        return n;
    }
    std::unique_ptr<Expr::Node> oor() {
        auto l = aand();
        while (l && cur().type == T::Op && cur().s == "||") { ++i; auto r = aand(); if (!r) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = 13; n->ch.push_back(std::move(l)); n->ch.push_back(std::move(r)); l = std::move(n); }
        return l;
    }
    std::unique_ptr<Expr::Node> aand() {
        auto l = eq();
        while (l && cur().type == T::Op && cur().s == "&&") { ++i; auto r = eq(); if (!r) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = 12; n->ch.push_back(std::move(l)); n->ch.push_back(std::move(r)); l = std::move(n); }
        return l;
    }
    std::unique_ptr<Expr::Node> eq() {
        auto l = rel();
        while (l && cur().type == T::Op && (cur().s == "==" || cur().s == "!=")) {
            int op = binOp(cur().s); ++i; auto r = rel(); if (!r) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = op; n->ch.push_back(std::move(l)); n->ch.push_back(std::move(r)); l = std::move(n); }
        return l;
    }
    std::unique_ptr<Expr::Node> rel() {
        auto l = add();
        while (l && cur().type == T::Op && (cur().s == "<" || cur().s == "<=" || cur().s == ">" || cur().s == ">=")) {
            int op = binOp(cur().s); ++i; auto r = add(); if (!r) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = op; n->ch.push_back(std::move(l)); n->ch.push_back(std::move(r)); l = std::move(n); }
        return l;
    }
    std::unique_ptr<Expr::Node> add() {
        auto l = mul();
        while (l && cur().type == T::Op && (cur().s == "+" || cur().s == "-")) {
            int op = binOp(cur().s); ++i; auto r = mul(); if (!r) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = op; n->ch.push_back(std::move(l)); n->ch.push_back(std::move(r)); l = std::move(n); }
        return l;
    }
    std::unique_ptr<Expr::Node> mul() {
        auto l = unary();
        while (l && cur().type == T::Op && (cur().s == "*" || cur().s == "/" || cur().s == "%")) {
            int op = binOp(cur().s); ++i; auto r = unary(); if (!r) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = op; n->ch.push_back(std::move(l)); n->ch.push_back(std::move(r)); l = std::move(n); }
        return l;
    }
    std::unique_ptr<Expr::Node> unary() {
        if (cur().type == T::Op && (cur().s == "-" || cur().s == "!")) {
            std::string o = cur().s; ++i;
            auto c = unary();
            if (!c) return nullptr;
            auto n = mk(Expr::Node::UN); n->op = (o == "-") ? 0 : 1; n->ch.push_back(std::move(c));
            return n;
        }
        return pw();
    }
    std::unique_ptr<Expr::Node> pw() { // power: right-associative, exponent accepts unary
        auto b = primary();
        if (!b) return nullptr;
        if (cur().type == T::Op && cur().s == "^") {
            ++i;
            auto e = unary();
            if (!e) return nullptr;
            auto n = mk(Expr::Node::BIN); n->op = 5; n->ch.push_back(std::move(b)); n->ch.push_back(std::move(e));
            return n;
        }
        return b;
    }
    std::unique_ptr<Expr::Node> call(const std::string& name) {
        const int ln = cur().line, cl = cur().col;
        ++i; // id
        ++i; // '('
        if (name == "in") {
            if (inFunc) { fail("pure function cannot access inputs; pass the value as an argument"); return nullptr; }
            auto n = mk(Expr::Node::NIN);
            auto a = expr(); if (!a) return nullptr;
            n->ch.push_back(std::move(a));
            if (eat(T::Comma)) {
                auto b = expr(); if (!b) return nullptr;
                n->ch.push_back(std::move(b));
            }
            if (!eat(T::RP)) { fail("usage: in(i) or in(socket, j)"); return nullptr; }
            return n;
        }
        if (name == "vec" || name == "mat") { // variadic: vec(a,b,...) / mat(r,c,...)
            auto n = mk(Expr::Node::CALL);
            n->fn = name == "vec" ? 49 : 50;
            if (cur().type != T::RP) {
                for (;;) {
                    auto a = expr(); if (!a) return nullptr;
                    n->ch.push_back(std::move(a));
                    if (!eat(T::Comma)) break;
                }
            }
            if (!eat(T::RP)) { fail("expected ')'"); return nullptr; }
            return n;
        }
        int arity = 0, id = fnLookup(name, arity);
        if (id < 0) { // user function: arity and existence are resolved at the end
            auto n = mk(Expr::Node::UCALL);
            n->sval = name;
            n->line = ln; n->col = cl;
            if (cur().type != T::RP) {
                for (;;) {
                    auto a = expr(); if (!a) return nullptr;
                    n->ch.push_back(std::move(a));
                    if (!eat(T::Comma)) break;
                }
            }
            if (!eat(T::RP)) { fail("expected ')'"); return nullptr; }
            return n;
        }
        if (inFunc && (id == 37 || id == 38 || id == 39 || id == 40 ||
                       id == 51 || id == 52 || id == 53)) {
            fail("pure function cannot call '" + name + "'");
            return nullptr;
        }
        auto n = mk(Expr::Node::CALL);
        n->fn = id;
        if (arity >= 1) {
            auto a = expr(); if (!a) return nullptr;
            n->ch.push_back(std::move(a));
            for (int k = 1; k < arity; ++k) {
                if (!eat(T::Comma)) { fail("function '" + name + "' expects " + std::to_string(arity) + " arguments"); return nullptr; }
                auto b = expr(); if (!b) return nullptr;
                n->ch.push_back(std::move(b));
            }
        }
        if (!eat(T::RP)) { fail("expected ')'"); return nullptr; }
        return n;
    }
    std::unique_ptr<Expr::Node> primary() {
        if (cur().type == T::Num) { auto n = mk(Expr::Node::NUM); n->num = cur().num; ++i; return n; }
        if (cur().type == T::Str) { auto n = mk(Expr::Node::STR); n->sval = cur().s; ++i; return n; }
        if (cur().type == T::Id) {
            std::string name = cur().s;
            if (peek().type == T::LP) return call(name);
            auto lo = locals.find(name);
            if (lo != locals.end()) { ++i; auto n = mk(Expr::Node::LOCAL); n->var = lo->second; return n; }
            auto pa = params.find(name);
            if (pa != params.end()) {
                ++i;
                const ExprParam* pr = pa->second;
                auto n = mk(pr->isText ? Expr::Node::PSTR : Expr::Node::PNUM);
                n->sval = pr->str; n->num = pr->num;
                return n;
            }
            int v = varId(name);
            if (v >= 0) {
                if (inFunc && (v == 2 || v == 3 || v == 4 || v >= 100)) {
                    fail("pure function cannot access state/inputs; pass the value as an argument");
                    return nullptr;
                }
                ++i; auto n = mk(Expr::Node::VAR); n->var = v; return n;
            }
            fail("unknown variable '" + name + "'");
            return nullptr;
        }
        if (eat(T::LP)) {
            auto e = expr();
            if (!e || !eat(T::RP)) { fail("expected ')'"); return nullptr; }
            return e;
        }
        fail("invalid expression here");
        return nullptr;
    }

    int assignOp(const std::string& s) {
        if (s == "=") return 0;
        if (s == "+=") return 1;
        if (s == "-=") return 2;
        if (s == "*=") return 3;
        if (s == "/=") return 4;
        if (s == "%=") return 5;
        return -1;
    }
    bool simple(Expr::Stmt& st) {
        st.line = cur().line;
        st.col = cur().col;
        if (cur().type == T::Id && peek().type == T::Assign) {
            const std::string name = cur().s;
            const int op = assignOp(peek().s);
            if (op < 0) return fail("invalid assignment operator");
            i += 2;
            auto it = locals.find(name);
            int slot = -1;
            if (it == locals.end()) {
                if (varId(name) >= 0 || params.count(name))
                    return fail("reserved name: '" + name + "'");
                if (op != 0) return fail("undeclared variable: '" + name + "'");
                slot = (int)varNames.size();
                locals[name] = slot;
                varNames.push_back(name);
            } else slot = it->second;
            st.k = Expr::Stmt::ASSIGN;
            st.slot = slot;
            st.op = op;
            for (;;) {
                auto e = expr(); if (!e) return false;
                st.e.push_back(std::move(e));
                if (eat(T::Comma)) continue;
                break;
            }
            return true;
        }
        st.k = Expr::Stmt::EXPR;
        for (;;) {
            auto e = expr(); if (!e) return false;
            st.e.push_back(std::move(e));
            if (eat(T::Comma)) continue;
            break;
        }
        return true;
    }
    bool block(std::vector<Expr::Stmt>& out) {
        if (!eat(T::LB)) return fail("expected '{'");
        skipSeps();
        while (cur().type != T::RB && cur().type != T::End) {
            Expr::Stmt st;
            if (!statement(st)) return false;
            out.push_back(std::move(st));
            skipSeps();
        }
        if (!eat(T::RB)) return fail("expected '}'");
        return true;
    }
    bool scopedBlock(std::vector<Expr::Stmt>& out) {
        auto saved = locals;
        if (!block(out)) return false;
        locals = std::move(saved);
        return true;
    }
    bool parseIf(Expr::Stmt& st) {
        st.k = Expr::Stmt::IF;
        st.line = cur().line;
        st.col = cur().col;
        ++i; // if
        if (!expect(T::LP, "'('")) return false;
        st.cond = expr();
        if (!st.cond || !expect(T::RP, "')'")) return false;
        skipSeps();
        if (!scopedBlock(st.body)) return false;
        skipSeps();
        if (eatId("else")) {
            skipSeps();
            if (cur().type == T::Id && cur().s == "if") {
                Expr::Stmt nested;
                if (!parseIf(nested)) return false;
                st.elseBody.push_back(std::move(nested));
            } else if (!scopedBlock(st.elseBody)) return false;
        }
        return true;
    }
    bool parseWhile(Expr::Stmt& st) {
        st.k = Expr::Stmt::WHILE;
        st.line = cur().line;
        st.col = cur().col;
        ++i; // while
        if (!expect(T::LP, "'('")) return false;
        st.cond = expr();
        if (!st.cond || !expect(T::RP, "')'")) return false;
        skipSeps();
        ++loopDepth;
        const bool ok = scopedBlock(st.body);
        --loopDepth;
        return ok;
    }
    bool parseFor(Expr::Stmt& st) {
        st.k = Expr::Stmt::FOR;
        st.line = cur().line;
        st.col = cur().col;
        ++i; // for
        if (!expect(T::LP, "'('")) return false;
        auto saved = locals;
        if (cur().type != T::Semi) {
            st.init = std::unique_ptr<Expr::Stmt>(new Expr::Stmt());
            if (!simple(*st.init)) return false;
        }
        if (!expect(T::Semi, "';'")) return false;
        st.cond = expr();
        if (!st.cond || !expect(T::Semi, "';'")) return false;
        if (cur().type != T::RP) {
            st.step = std::unique_ptr<Expr::Stmt>(new Expr::Stmt());
            if (!simple(*st.step)) return false;
        }
        if (!expect(T::RP, "')'")) return false;
        skipSeps();
        ++loopDepth;
        const bool ok = block(st.body);
        --loopDepth;
        locals = std::move(saved);
        return ok;
    }
    bool parseJump(Expr::Stmt& st, Expr::Stmt::K k) {
        st.k = k;
        st.line = cur().line;
        st.col = cur().col;
        ++i;
        return true;
    }
    bool parseReturn(Expr::Stmt& st) {
        if (!inFunc) return fail("'return' outside a function");
        st.k = Expr::Stmt::RETURN;
        st.line = cur().line;
        st.col = cur().col;
        ++i;
        if (cur().type != T::Sep && cur().type != T::RB && cur().type != T::End) {
            auto e = expr();
            if (!e) return false;
            st.e.push_back(std::move(e));
        }
        return true;
    }
    bool statement(Expr::Stmt& st) {
        skipSeps();
        if (cur().type == T::Id) {
            if (cur().s == "if") return parseIf(st);
            if (cur().s == "while") return parseWhile(st);
            if (cur().s == "for") return parseFor(st);
            if (cur().s == "break") {
                if (loopDepth <= 0) return fail("'break' outside a loop");
                return parseJump(st, Expr::Stmt::BREAK);
            }
            if (cur().s == "continue") {
                if (loopDepth <= 0) return fail("'continue' outside a loop");
                return parseJump(st, Expr::Stmt::CONTINUE);
            }
            if (cur().s == "return") return parseReturn(st);
            if (cur().s == "fun") return fail("'fun' only at top level");
        }
        return simple(st);
    }
    bool parseFun() {
        const int ln = cur().line, cl = cur().col;
        ++i; // fun
        if (cur().type != T::Id) return fail("expected function name");
        const std::string name = cur().s;
        int arity = 0;
        if (fnLookup(name, arity) >= 0 || name == "in" || name == "vec" || name == "mat" ||
            name == "if" || name == "while" || name == "for" || name == "break" ||
            name == "continue" || name == "return" || name == "fun")
            return fail("reserved function name: '" + name + "'");
        if (params.count(name)) return fail("parameter name already used: '" + name + "'");
        if (funcIndex.count(name)) return fail("duplicate function: '" + name + "'");
        ++i;
        if (!expect(T::LP, "'('")) return false;
        std::vector<std::string> ps;
        if (cur().type != T::RP) {
            for (;;) {
                if (cur().type != T::Id) return fail("expected parameter name");
                const std::string p = cur().s;
                if (p == "if" || p == "while" || p == "for" || p == "break" ||
                    p == "continue" || p == "return" || p == "fun" ||
                    p == "in" || p == "vec" || p == "mat")
                    return fail("reserved parameter name: '" + p + "'");
                for (const auto& q : ps) if (q == p) return fail("duplicate parameter: '" + p + "'");
                ps.push_back(p);
                ++i;
                if (!eat(T::Comma)) break;
            }
        }
        if (!expect(T::RP, "')'")) return false;
        skipSeps();
        if (!eat(T::LB)) return fail("expected '{' in function body");

        const int idx = (int)funcs.size();
        funcs.emplace_back();
        funcs[(size_t)idx].name = name;
        funcs[(size_t)idx].params = ps;
        funcs[(size_t)idx].line = ln;
        funcs[(size_t)idx].col = cl;
        funcIndex[name] = idx;

        const auto savedLocals = locals;
        const auto savedVars = varNames;
        auto savedStmts = std::move(stmts);
        const bool savedInFunc = inFunc;
        const int savedLoop = loopDepth;
        locals.clear();
        varNames.clear();
        inFunc = true;
        loopDepth = 0;
        for (const auto& p : ps) {
            locals[p] = (int)varNames.size();
            varNames.push_back(p);
        }
        skipSeps();
        bool ok = true;
        while (cur().type != T::RB && cur().type != T::End) {
            Expr::Stmt st;
            if (!statement(st)) { ok = false; break; }
            funcs[(size_t)idx].body.push_back(std::move(st));
            skipSeps();
        }
        if (ok && !eat(T::RB)) { ok = fail("expected '}' at end of function"); }
        funcs[(size_t)idx].vars = varNames;
        stmts = std::move(savedStmts);
        locals = savedLocals;
        varNames = savedVars;
        inFunc = savedInFunc;
        loopDepth = savedLoop;
        return ok;
    }
    bool resolveNode(Expr::Node* n) {
        if (!n || !err.empty()) return false;
        if (n->k == Expr::Node::UCALL) {
            auto it = funcIndex.find(n->sval);
            if (it == funcIndex.end()) return failNode(n, "unknown function '" + n->sval + "'");
            const Expr::Func& f = funcs[(size_t)it->second];
            if (n->ch.size() != f.params.size())
                return failNode(n, "function '" + n->sval + "' expects " +
                                    std::to_string(f.params.size()) + " argumentos");
            n->fn = it->second;
        }
        for (auto& c : n->ch) if (!resolveNode(c.get())) return false;
        return true;
    }
    bool resolveStmt(Expr::Stmt& s) {
        for (auto& e : s.e) if (!resolveNode(e.get())) return false;
        if (s.cond && !resolveNode(s.cond.get())) return false;
        if (s.init && !resolveStmt(*s.init)) return false;
        if (s.step && !resolveStmt(*s.step)) return false;
        for (auto& b : s.body) if (!resolveStmt(b)) return false;
        for (auto& b : s.elseBody) if (!resolveStmt(b)) return false;
        return true;
    }
    bool resolveAll() {
        for (auto& s : stmts) if (!resolveStmt(s)) return false;
        for (auto& f : funcs) {
            for (auto& s : f.body) if (!resolveStmt(s)) return false;
        }
        return true;
    }
    bool program() {
        skipSeps();
        if (cur().type == T::End) { fail("empty program"); return false; }
        while (cur().type != T::End) {
            if (cur().type == T::Id && cur().s == "fun") {
                if (!parseFun()) return false;
            } else {
                Expr::Stmt st;
                if (!statement(st)) return false;
                stmts.push_back(std::move(st));
            }
            skipSeps();
        }
        if (stmts.empty()) { fail("program with no statements"); return false; }
        return resolveAll();
    }
};

} // namespace

bool parseProgram(const std::string& src, const std::vector<ExprParam>& params,
                  ParsedProgram& out, std::string& err) {
    std::vector<Tok> toks;
    if (!tokenize(src, toks, err)) return false;
    P p(toks);
    for (const auto& pr : params) p.params[pr.name] = &pr;
    if (!p.program()) {
        err = p.err.empty() ? "invalid expression" : p.err;
        return false;
    }
    out.stmts = std::move(p.stmts);
    out.varNames = std::move(p.varNames);
    out.funcs = std::move(p.funcs);
    return true;
}
