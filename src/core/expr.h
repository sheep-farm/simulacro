// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Mini procedural language for SCRIPT nodes — no external dependencies.
// Usage: auto e = Expr::parse("x + dt*(in(0)-x)", err);
//      e->evalAll(ctx, err, outs, MAX_PORTS);   // 1 value per port (number or text)
//
// PROGRAM: statements separated by ';' or newline; '#' and '//' comment.
//   The last top-level expression list defines the OUTPUTS (comma =
//   ports). Blocks, control flow, and functions are additive:
//     if (cond) { ... } else if (cond) { ... } else { ... }
//     while (cond) { ... }
//     for (i = 0; i < n; i += 1) { ... }
//     break | continue | return expr
//     fun nome(a, b) { ... }   (pure, recursive functions at the top level)
//   x = clamp(dt*2, 0, 1)     # assignment (local variable)
//   a = x + k*(sum - x)
//   a, 1 - a                  # port 0 = a, port 1 = 1-a
//
// VARS: t dt x sum n pi e | x0 x1 ... x15 = previous output of each port
// Node PARAMS (field "params", e.g.: k=0.5, mode="fast") become constants.
// INPUTS: in(i) = i-th (socket→wire order) | in(socket, j) | nin(socket) | ssum(socket)
//   in(i) out of range = 0 (no connection = 0).
// FUNCS 1-arg: sin cos tan asin acos atan exp log log10 log2 sqrt abs floor ceil
//   round trunc sign sinh cosh tanh deg rad noise(x) len(x) num(s) str(x)
//   rows(v) cols(v) mean(v) sumv(v)   (v = list/matrix)
//   evt(i) evt_kind(i) evt_t(i) = discrete events delivered on socket i
// FUNCS 2-arg: min max pow atan2 hypot fmt(x,dec)  |  0-arg: rand()
// FUNCS 3-arg: clamp(v,lo,hi) lerp(a,b,u) wrap(x,lo,hi) smoothstep(e0,e1,x) div(a,b,def)
// WIRE VALUES (ABI 10): sval(i) = value of socket i (number/text/list/matrix)
//   at(v, i) element (row by row in the matrix) | mget(m, i, j) matrix element
//   col(m, j) / row(m, i) = list | vec(a, b, ...) = list | mat(r, c, ...) = matrix
// TEXT: "..." literals and '+' concatenates (number becomes text). str()/fmt() produce text.
// OPS: + - * / % ^  < <= > >= == !=  ! && ||  ?:   (^ = power, right-assoc)
// Multi-output: parseList("sin(t), cos(t)") = one expression per output port.
#pragma once
#include <memory>
#include <string>
#include <vector>

struct NsValue; // value per socket (plugin.h, ABI 10) — only the pointer is used here

// Language value: number (default), text, list (1×n), or matrix (rows×cols).
// The vector is shared (immutable): copying an ExprVal with a list is cheap.
struct ExprVal {
    bool isStr = false;
    double num = 0.0;
    std::string str;
    std::shared_ptr<const std::vector<double>> vec; // NS_VAL_LIST/NS_VAL_MAT
    int rows = 0, cols = 0;                         // list: rows=1
    ExprVal() = default;
    ExprVal(double v) : num(v) {}
    ExprVal(const char* s) : isStr(true), str(s) {}
    ExprVal(std::string s) : isStr(true), str(std::move(s)) {}
    bool isVec() const { return (bool)vec; }
};

// Named node parameter: numeric or textual constant available to the script.
struct ExprParam {
    std::string name;
    bool isText = false;
    double num = 0.0;
    std::string str;
};

struct ExprCtx {
    double t = 0, dt = 0, x = 0, sum = 0, n = 0;
    const double* in = nullptr; // flattened inputs (socket 0, socket 1, ...)
    int nin = 0;
    const double* xout = nullptr; // previous output of each port (x0,x1,...)
    int nxout = 0;
    const int* sockOff = nullptr; // sockOff[s]..sockOff[s+1] = wires of socket s
    int nsock = 0;
    // value per socket (text/list/matrix on the wire); number = socket sum
    const NsValue* xval = nullptr;
    int nval = 0;
    // discrete events delivered in this step, per socket (nullptr = service off)
    const int* evtCount = nullptr;
    const int* evtKind = nullptr;
    const double* evtT = nullptr;
    int nevt = 0;
};

struct Expr {
    // compiles a program (comma separates ports). params = "k=0.5, nome=\"x\"".
    static std::unique_ptr<Expr> parse(const std::string& src, std::string& err,
                                       const std::string& params = std::string());
    static std::unique_ptr<Expr> parseList(const std::string& src, std::string& err,
                                           const std::string& params = std::string());
    // reads the node's parameter block (comma/;/newline separate "name=value")
    static bool parseParams(const std::string& src, std::vector<ExprParam>& out, std::string& err);

    double eval(const ExprCtx& c, std::string& err) const;               // port 0 (numeric)
    double evalPort(const ExprCtx& c, int port, std::string& err) const; // port k (clamps)
    ExprVal evalVal(const ExprCtx& c, int port, std::string& err) const; // port k (number or text)
    // runs the program once (assignments happen only here); returns the number of ports
    int evalAll(const ExprCtx& c, std::string& err, ExprVal* outs, int maxOut) const;
    int count() const { return outCount; }
    ~Expr();

    // Implementation detail (AST node). Use only via parse()/eval().
    struct Node {
        enum K { NUM, VAR, LOCAL, PNUM, PSTR, IN, CALL, UCALL, UN, BIN, TER, STR } k = NUM;
        double num = 0;
        std::string sval; // STR / PSTR / user function name
        int var = -1;     // VAR: 0=t 1=dt 2=x 3=sum 4=n 5=pi 6=e | 100+k = xk
                          // LOCAL: slot index
        int fn = -1, op = -1;
        int line = 1, col = 1; // diagnostics
        std::vector<std::unique_ptr<Node>> ch;
    };
    // Procedural language statements. EXPR/ASSIGN can also define the
    // outputs when they are the last top-level statement with expressions.
    struct Stmt {
        enum K { EXPR, ASSIGN, IF, WHILE, FOR, BREAK, CONTINUE, RETURN } k = EXPR;
        int slot = -1;   // ASSIGN: local slot
        int op = 0;      // ASSIGN: 0 =, 1 +=, 2 -=, 3 *=, 4 /=, 5 %=
        int line = 1, col = 1;
        std::vector<std::unique_ptr<Node>> e; // EXPR/ASSIGN/RETURN (return: 0 or 1)
        std::unique_ptr<Node> cond;           // IF/WHILE/FOR
        std::unique_ptr<Stmt> init, step;     // FOR
        std::vector<Stmt> body, elseBody;     // blocks
    };
    // Function defined in the script itself; body shares the immutable AST.
    struct Func {
        std::string name;
        std::vector<std::string> params;
        std::vector<std::string> vars; // params first, then locals
        std::vector<Stmt> body;
        int line = 1, col = 1;
    };
 private:
    std::vector<Stmt> stmts;
    std::vector<std::string> varNames;
    std::vector<Func> funcs;
    int outStmt = -1, outCount = 0;
    Expr(std::vector<Stmt> s, std::vector<std::string> names, std::vector<Func> f);
};
