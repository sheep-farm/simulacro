// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// SCRIPT evaluator: values, native functions, control flow, and budget.

#include "expr.h"
#include "plugin.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
// ---------- value utilities ----------
std::string fmtNum(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        char b[32];
        std::snprintf(b, sizeof b, "%lld", (long long)v);
        return b;
    }
    char b[40];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}
std::string toStrVal(const ExprVal& v) { return v.isStr ? v.str : fmtNum(v.num); }
double toNumVal(const ExprVal& v) {
    if (!v.isStr) return v.num;
    char* e = nullptr;
    double d = std::strtod(v.str.c_str(), &e);
    return (e == v.str.c_str()) ? 0.0 : d;
}
bool truthy(const ExprVal& v) { return v.isStr ? !v.str.empty() : v.num != 0.0; }
int utf8Len(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}
double hash1(int i) { // deterministic per-cell noise
    uint32_t x = (uint32_t)i * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return x / 4294967296.0;
}
double noise1(double x) { // 1D value noise in [0,1)
    double fi = std::floor(x);
    int i = (int)fi;
    double f = x - fi;
    double u = f * f * (3 - 2 * f);
    return hash1(i) * (1 - u) + hash1(i + 1) * u;
}
double rnd() { // xorshift64*; fixed sequence (reproducible across runs)
    static uint64_t s = 0x9E3779B97F4A7C15ull;
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return (double)((s * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0;
}
// ---------- procedural execution ----------
enum class Flow { NORMAL, BREAK, CONTINUE, RETURN };

struct Exec {
    const ExprCtx& c;
    const std::vector<Expr::Func>& funcs;
    std::string& err;
    uint64_t ops = 0;
    uint64_t maxOps = 100000; // deterministic budget per step
    int loop = 0;
    int maxLoop = 10000;
    int depth = 0;
    int maxDepth = 64;
    std::vector<std::string> stack;

    Exec(const ExprCtx& cc, const std::vector<Expr::Func>& ff, std::string& ee)
        : c(cc), funcs(ff), err(ee) {}
};

void errorExec(Exec& x, int line, int col, const std::string& msg) {
    if (!x.err.empty()) return;
    x.err = msg + " (line " + std::to_string(line) + ", column " + std::to_string(col) + ")";
    if (!x.stack.empty()) {
        x.err += " em ";
        for (size_t i = 0; i < x.stack.size(); ++i) {
            if (i) x.err += " > ";
            x.err += x.stack[i];
        }
    }
}

bool tick(Exec& x, int line, int col) {
    if (!x.err.empty()) return false;
    if (++x.ops > x.maxOps) {
        errorExec(x, line, col, "script execution limit exceeded");
        return false;
    }
    return true;
}

ExprVal ev(const Expr::Node* n, Exec& x, const std::vector<ExprVal>& vars);
Flow execBlock(const std::vector<Expr::Stmt>& body, Exec& x,
               std::vector<ExprVal>& vars, ExprVal& ret);
ExprVal callFunc(const Expr::Node* n, Exec& x, const std::vector<ExprVal>& args);

ExprVal ev(const Expr::Node* n, Exec& x, const std::vector<ExprVal>& vars) {
    if (!x.err.empty()) return ExprVal();
    if (!tick(x, n->line, n->col)) return ExprVal();
    if (!x.err.empty()) return ExprVal();
    switch (n->k) {
        case Expr::Node::NUM: return ExprVal(n->num);
        case Expr::Node::STR: return ExprVal(n->sval);
        case Expr::Node::PSTR: return ExprVal(n->sval);
        case Expr::Node::PNUM: return ExprVal(n->num);
        case Expr::Node::LOCAL:
            return (n->var >= 0 && n->var < (int)vars.size()) ? vars[n->var] : ExprVal();
        case Expr::Node::VAR:
            switch (n->var) {
                case 0: return ExprVal(x.c.t);
                case 1: return ExprVal(x.c.dt);
                case 2: return ExprVal((x.c.xout && x.c.nxout > 0) ? (double)x.c.xout[0] : x.c.x);
                case 3: return ExprVal(x.c.sum);
                case 4: return ExprVal(x.c.n);
                case 5: return ExprVal(3.141592653589793);
                case 6: return ExprVal(2.718281828459045);
            }
            if (n->var >= 100) { int k = n->var - 100; if (x.c.xout && k < x.c.nxout) return ExprVal((double)x.c.xout[k]); }
            return ExprVal(0.0);
        case Expr::Node::IN: {
            if (n->ch.size() == 1) { // in(i): flattened list
                double d = toNumVal(ev(n->ch[0].get(), x, vars));
                if (!x.err.empty()) return ExprVal();
                long k = (long)d;
                if (k < 0 || k >= x.c.nin || !x.c.in) return ExprVal(0.0);
                return ExprVal((double)x.c.in[k]);
            }
            double ds = toNumVal(ev(n->ch[0].get(), x, vars));
            double dj = toNumVal(ev(n->ch[1].get(), x, vars));
            if (!x.err.empty()) return ExprVal();
            long s = (long)ds, j = (long)dj;
            if (!x.c.sockOff || s < 0 || s >= x.c.nsock) return ExprVal(0.0);
            int base = x.c.sockOff[s], cnt = x.c.sockOff[s + 1] - base;
            if (j < 0 || j >= cnt || !x.c.in) return ExprVal(0.0);
            return ExprVal((double)x.c.in[base + j]);
        }
        case Expr::Node::UN: {
            ExprVal a = ev(n->ch[0].get(), x, vars);
            if (!x.err.empty()) return ExprVal();
            if (n->op == 0) return ExprVal(-toNumVal(a));
            return ExprVal(truthy(a) ? 0.0 : 1.0);
        }
        case Expr::Node::BIN: {
            if (n->op == 12) { // && short-circuit
                ExprVal a = ev(n->ch[0].get(), x, vars);
                if (!x.err.empty()) return ExprVal();
                if (!truthy(a)) return ExprVal(0.0);
                ExprVal b = ev(n->ch[1].get(), x, vars);
                if (!x.err.empty()) return ExprVal();
                return ExprVal(truthy(b) ? 1.0 : 0.0);
            }
            if (n->op == 13) { // || short-circuit
                ExprVal a = ev(n->ch[0].get(), x, vars);
                if (!x.err.empty()) return ExprVal();
                if (truthy(a)) return ExprVal(1.0);
                ExprVal b = ev(n->ch[1].get(), x, vars);
                if (!x.err.empty()) return ExprVal();
                return ExprVal(truthy(b) ? 1.0 : 0.0);
            }
            ExprVal A = ev(n->ch[0].get(), x, vars);
            ExprVal B = ev(n->ch[1].get(), x, vars);
            if (!x.err.empty()) return ExprVal();
            if (n->op == 0 && (A.isStr || B.isStr)) return ExprVal(toStrVal(A) + toStrVal(B)); // '+' concatenates
            if ((n->op == 10 || n->op == 11) && (A.isStr || B.isStr)) {
                bool eqs = (toStrVal(A) == toStrVal(B));
                return ExprVal(((n->op == 10) == eqs) ? 1.0 : 0.0);
            }
            double a = toNumVal(A), b = toNumVal(B);
            switch (n->op) {
                case 0: return ExprVal(a + b);
                case 1: return ExprVal(a - b);
                case 2: return ExprVal(a * b);
                case 3: return ExprVal(b == 0 ? 0.0 : a / b); // /0 = 0 (does not stall the node)
                case 4: return ExprVal(b == 0 ? 0.0 : std::fmod(a, b));
                case 5: return ExprVal(std::pow(a, b));
                case 6: return ExprVal(a < b ? 1.0 : 0.0);
                case 7: return ExprVal(a <= b ? 1.0 : 0.0);
                case 8: return ExprVal(a > b ? 1.0 : 0.0);
                case 9: return ExprVal(a >= b ? 1.0 : 0.0);
                case 10: return ExprVal(a == b ? 1.0 : 0.0);
                case 11: return ExprVal(a != b ? 1.0 : 0.0);
            }
            return ExprVal(0.0);
        }
        case Expr::Node::TER: {
            ExprVal k = ev(n->ch[0].get(), x, vars);
            if (!x.err.empty()) return ExprVal();
            return ev(n->ch[truthy(k) ? 1 : 2].get(), x, vars);
        }
        case Expr::Node::UCALL:
            {
                std::vector<ExprVal> args;
                args.reserve(n->ch.size());
                for (const auto& a : n->ch) {
                    args.push_back(ev(a.get(), x, vars));
                    if (!x.err.empty()) return ExprVal();
                }
                return callFunc(n, x, args);
            }
        case Expr::Node::CALL: {
            ExprVal A, B, D;
            if (n->ch.size() > 0) A = ev(n->ch[0].get(), x, vars);
            if (n->ch.size() > 1) B = ev(n->ch[1].get(), x, vars);
            if (n->ch.size() > 2) D = ev(n->ch[2].get(), x, vars);
            if (!x.err.empty()) return ExprVal();
            double a = toNumVal(A), b = toNumVal(B), d = toNumVal(D);
            switch (n->fn) {
                case 0: return ExprVal(std::sin(a));
                case 1: return ExprVal(std::cos(a));
                case 2: return ExprVal(std::tan(a));
                case 3: return ExprVal(std::asin(a));
                case 4: return ExprVal(std::acos(a));
                case 5: return ExprVal(std::atan(a));
                case 6: return ExprVal(std::exp(a));
                case 7: return ExprVal(std::log(a));
                case 8: return ExprVal(std::sqrt(a));
                case 9: return ExprVal(std::fabs(a));
                case 10: return ExprVal(std::floor(a));
                case 11: return ExprVal(std::ceil(a));
                case 12: return ExprVal(std::round(a));
                case 13: return ExprVal(a < b ? a : b);
                case 14: return ExprVal(a > b ? a : b);
                case 15: return ExprVal(std::pow(a, b));
                case 16: return ExprVal(std::atan2(a, b));
                case 17: return ExprVal(std::hypot(a, b));
                case 18: return ExprVal(a < b ? b : (a > d ? d : a)); // clamp(v,lo,hi)
                case 19: return ExprVal(std::log10(a));
                case 20: return ExprVal(std::log2(a));
                case 21: return ExprVal(std::trunc(a));
                case 22: return ExprVal((double)((a > 0) - (a < 0))); // sign
                case 23: return ExprVal(std::sinh(a));
                case 24: return ExprVal(std::cosh(a));
                case 25: return ExprVal(std::tanh(a));
                case 26: return ExprVal(a * 180.0 / 3.141592653589793); // deg
                case 27: return ExprVal(a * 3.141592653589793 / 180.0); // rad
                case 28: return ExprVal(noise1(a));
                case 29: // len(x): text = characters; list/matrix = elements
                    if (A.vec) return ExprVal((double)A.vec->size());
                    return ExprVal((double)utf8Len(toStrVal(A)));
                case 30: return ExprVal(std::strtod(toStrVal(A).c_str(), nullptr)); // num(s)
                case 31: return ExprVal(toStrVal(A));                                // str(x)
                case 32: return ExprVal(a + b * (d - a)); // lerp(a,b,u)
                case 33: { // wrap(x,lo,hi)
                    if (d <= b) return ExprVal(a);
                    double r = d - b, v = std::fmod(a - b, r);
                    if (v < 0) v += r;
                    return ExprVal(b + v);
                }
                case 34: { // smoothstep(e0,e1,x): a=e0, b=e1, d=x
                    if (b == a) return ExprVal(0.0);
                    double u = (d - a) / (b - a);
                    if (u < 0) u = 0;
                    if (u > 1) u = 1;
                    return ExprVal(u * u * (3 - 2 * u));
                }
                case 35: return ExprVal(b == 0 ? d : a / b); // div(a,b,def)
                case 36: { // fmt(x, dec)
                    int dec = (int)b;
                    if (dec < 0) dec = 0;
                    if (dec > 12) dec = 12;
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "%.*f", dec, a);
                    return ExprVal(std::string(buf));
                }
                case 37: return ExprVal(rnd()); // rand()
                case 38: { // nin(socket)
                    long s = (long)a;
                    if (!x.c.sockOff || s < 0 || s >= x.c.nsock) return ExprVal(0.0);
                    return ExprVal((double)(x.c.sockOff[s + 1] - x.c.sockOff[s]));
                }
                case 39: { // ssum(socket)
                    long s = (long)a;
                    if (!x.c.sockOff || !x.c.in || s < 0 || s >= x.c.nsock) return ExprVal(0.0);
                    double acc = 0;
                    for (int k = x.c.sockOff[s]; k < x.c.sockOff[s + 1]; ++k) acc += x.c.in[k];
                    return ExprVal(acc);
                }
                case 40: { // sval(socket): socket value (number/text/list/matrix)
                    long s = (long)a;
                    if (!x.c.xval || s < 0 || s >= x.c.nval) return ExprVal(0.0);
                    const NsValue& v = x.c.xval[s];
                    if (v.kind == NS_VAL_TXT && v.txt) return ExprVal(std::string(v.txt));
                    if ((v.kind == NS_VAL_LIST || v.kind == NS_VAL_MAT) && v.data && v.rows > 0 && v.cols > 0) {
                        ExprVal r;
                        r.vec = std::make_shared<std::vector<double>>(v.data, v.data + (size_t)v.rows * (size_t)v.cols);
                        r.rows = v.rows;
                        r.cols = v.cols;
                        return r;
                    }
                    return ExprVal(v.num);
                }
                case 41: { // at(v, i): element (in a matrix, row by row)
                    if (!A.vec) return ExprVal(0.0);
                    long k = (long)b;
                    if (k < 0 || k >= (long)A.vec->size()) return ExprVal(0.0);
                    return ExprVal((*A.vec)[(size_t)k]);
                }
                case 42: { // mget(m, i, j)
                    if (!A.vec || A.cols <= 0) return ExprVal(0.0);
                    long i = (long)b, j = (long)d;
                    if (i < 0 || j < 0 || i >= A.rows || j >= A.cols) return ExprVal(0.0);
                    long k = i * A.cols + j;
                    if (k >= (long)A.vec->size()) return ExprVal(0.0);
                    return ExprVal((*A.vec)[(size_t)k]);
                }
                case 43: return ExprVal(A.vec ? (double)A.rows : (A.isStr ? 1.0 : 1.0)); // rows(v)
                case 44: return ExprVal(A.vec ? (double)A.cols : (A.isStr ? (double)utf8Len(A.str) : 1.0)); // cols(v)
                case 45: { // mean(v)
                    if (!A.vec || A.vec->empty()) return ExprVal(0.0);
                    double s2 = 0;
                    for (double x : *A.vec) s2 += x;
                    return ExprVal(s2 / (double)A.vec->size());
                }
                case 46: { // sumv(v)
                    if (!A.vec) return ExprVal(0.0);
                    double s2 = 0;
                    for (double x : *A.vec) s2 += x;
                    return ExprVal(s2);
                }
                case 47: { // col(m, j) = list
                    if (!A.vec || A.cols <= 0) return ExprVal(0.0);
                    long j = (long)b;
                    if (j < 0 || j >= A.cols) return ExprVal(0.0);
                    auto out = std::make_shared<std::vector<double>>();
                    out->reserve((size_t)A.rows);
                    for (int i = 0; i < A.rows; ++i) {
                        long k = (long)i * A.cols + j;
                        out->push_back(k < (long)A.vec->size() ? (*A.vec)[(size_t)k] : 0.0);
                    }
                    ExprVal r; r.vec = out; r.rows = 1; r.cols = (int)out->size();
                    return r;
                }
                case 48: { // row(m, i) = list
                    if (!A.vec || A.cols <= 0) return ExprVal(0.0);
                    long i = (long)b;
                    if (i < 0 || i >= A.rows) return ExprVal(0.0);
                    auto out = std::make_shared<std::vector<double>>();
                    for (int j = 0; j < A.cols; ++j) {
                        long k = (long)i * A.cols + j;
                        out->push_back(k < (long)A.vec->size() ? (*A.vec)[(size_t)k] : 0.0);
                    }
                    ExprVal r; r.vec = out; r.rows = 1; r.cols = (int)out->size();
                    return r;
                }
                case 49: { // vec(a, b, ...) = list
                    auto out = std::make_shared<std::vector<double>>();
                    out->reserve(n->ch.size());
                    for (size_t k = 0; k < n->ch.size(); ++k) {
                        ExprVal v = k == 0 ? A : (k == 1 ? B : (k == 2 ? D : ev(n->ch[k].get(), x, vars)));
                        if (!x.err.empty()) return ExprVal();
                        out->push_back(toNumVal(v));
                    }
                    ExprVal r; r.vec = out; r.rows = 1; r.cols = (int)out->size();
                    return r;
                }
                case 50: { // mat(r, c, v0, v1, ...) = r×c matrix (missing = 0)
                    long rr = (long)a, cc = (long)b;
                    auto out = std::make_shared<std::vector<double>>();
                    if (rr > 0 && cc > 0 && rr * cc <= 8000000L) {
                        out->assign((size_t)(rr * cc), 0.0);
                        long k = 0;
                        for (size_t i = 2; i < n->ch.size() && k < rr * cc; ++i, ++k) {
                            ExprVal v = i == 2 ? D : ev(n->ch[i].get(), x, vars);
                            if (!x.err.empty()) return ExprVal();
                            (*out)[(size_t)k] = toNumVal(v);
                        }
                    }
                    ExprVal r; r.vec = out; r.rows = (int)rr; r.cols = (int)cc;
                    return r;
                }
                case 51: { // evt(socket): number of events delivered in this step
                    if (!x.c.evtCount || !std::isfinite(a) || a < 0.0 || a >= x.c.nevt)
                        return ExprVal(0.0);
                    return ExprVal((double)x.c.evtCount[(int)a]);
                }
                case 52: { // evt_kind(socket): 0/1/2/3 of the last event
                    if (!x.c.evtKind || !std::isfinite(a) || a < 0.0 || a >= x.c.nevt)
                        return ExprVal(0.0);
                    return ExprVal((double)x.c.evtKind[(int)a]);
                }
                case 53: { // evt_t(socket): time of the last event, or -1
                    if (!x.c.evtT || !x.c.evtCount || !std::isfinite(a) || a < 0.0 || a >= x.c.nevt)
                        return ExprVal(-1.0);
                    int s = (int)a;
                    if (x.c.evtCount[s] == 0) return ExprVal(-1.0);
                    return ExprVal(x.c.evtT[s]);
                }
            }
            return ExprVal(0.0);
        }
    }
    return ExprVal(0.0);
}


Flow execStmt(const Expr::Stmt& s, Exec& x, std::vector<ExprVal>& vars,
              ExprVal& ret, std::vector<ExprVal>* capture) {
    if (!tick(x, s.line, s.col)) return Flow::NORMAL;
    switch (s.k) {
        case Expr::Stmt::EXPR:
        case Expr::Stmt::ASSIGN: {
            ExprVal last;
            for (const auto& e : s.e) {
                last = ev(e.get(), x, vars);
                if (!x.err.empty()) return Flow::NORMAL;
                if (capture) capture->push_back(last);
            }
            if (s.k == Expr::Stmt::ASSIGN && s.slot >= 0 && s.slot < (int)vars.size()) {
                if (s.op == 0) vars[(size_t)s.slot] = last;
                else {
                    const ExprVal ant = vars[(size_t)s.slot];
                    if (s.op == 1 && (ant.isStr || last.isStr)) {
                        vars[(size_t)s.slot] = ExprVal(toStrVal(ant) + toStrVal(last));
                    } else {
                        const double a = toNumVal(ant), b = toNumVal(last);
                        double r = 0.0;
                        switch (s.op) {
                            case 1: r = a + b; break;
                            case 2: r = a - b; break;
                            case 3: r = a * b; break;
                            case 4: r = b == 0 ? 0.0 : a / b; break;
                            case 5: r = b == 0 ? 0.0 : std::fmod(a, b); break;
                        }
                        vars[(size_t)s.slot] = ExprVal(r);
                    }
                }
            }
            return Flow::NORMAL;
        }
        case Expr::Stmt::IF: {
            ExprVal c = ev(s.cond.get(), x, vars);
            if (!x.err.empty()) return Flow::NORMAL;
            return execBlock(truthy(c) ? s.body : s.elseBody, x, vars, ret);
        }
        case Expr::Stmt::WHILE: {
            int it = 0;
            for (;;) {
                ExprVal c = ev(s.cond.get(), x, vars);
                if (!x.err.empty()) return Flow::NORMAL;
                if (!truthy(c)) break;
                if (++it > x.maxLoop) {
                    errorExec(x, s.line, s.col, "while iteration limit exceeded");
                    return Flow::NORMAL;
                }
                const Flow f = execBlock(s.body, x, vars, ret);
                if (!x.err.empty()) return Flow::NORMAL;
                if (f == Flow::BREAK) break;
                if (f == Flow::RETURN) return f;
                // CONTINUE and NORMAL proceed to the next iteration
            }
            return Flow::NORMAL;
        }
        case Expr::Stmt::FOR: {
            if (s.init) {
                const Flow fi = execStmt(*s.init, x, vars, ret, nullptr);
                if (!x.err.empty()) return Flow::NORMAL;
                if (fi != Flow::NORMAL) return fi;
            }
            int it = 0;
            for (;;) {
                ExprVal c = ev(s.cond.get(), x, vars);
                if (!x.err.empty()) return Flow::NORMAL;
                if (!truthy(c)) break;
                if (++it > x.maxLoop) {
                    errorExec(x, s.line, s.col, "for iteration limit exceeded");
                    return Flow::NORMAL;
                }
                const Flow f = execBlock(s.body, x, vars, ret);
                if (!x.err.empty()) return Flow::NORMAL;
                if (f == Flow::BREAK) break;
                if (f == Flow::RETURN) return f;
                if (s.step) {
                    const Flow fs = execStmt(*s.step, x, vars, ret, nullptr);
                    if (!x.err.empty()) return Flow::NORMAL;
                    if (fs == Flow::RETURN) return fs;
                    if (fs == Flow::BREAK) break;
                }
            }
            return Flow::NORMAL;
        }
        case Expr::Stmt::BREAK: return Flow::BREAK;
        case Expr::Stmt::CONTINUE: return Flow::CONTINUE;
        case Expr::Stmt::RETURN:
            ret = ExprVal(0.0);
            if (!s.e.empty()) ret = ev(s.e[0].get(), x, vars);
            return x.err.empty() ? Flow::RETURN : Flow::NORMAL;
    }
    return Flow::NORMAL;
}

Flow execBlock(const std::vector<Expr::Stmt>& body, Exec& x,
               std::vector<ExprVal>& vars, ExprVal& ret) {
    for (const auto& s : body) {
        const Flow f = execStmt(s, x, vars, ret, nullptr);
        if (!x.err.empty()) return Flow::NORMAL;
        if (f != Flow::NORMAL) return f;
    }
    return Flow::NORMAL;
}

ExprVal callFunc(const Expr::Node* n, Exec& x, const std::vector<ExprVal>& args) {
    if (!n || n->fn < 0 || n->fn >= (int)x.funcs.size()) {
        errorExec(x, n ? n->line : 1, n ? n->col : 1, "invalid user function");
        return ExprVal();
    }
    if (x.depth >= x.maxDepth) {
        errorExec(x, n->line, n->col, "recursion limit exceeded");
        return ExprVal();
    }
    const Expr::Func& f = x.funcs[(size_t)n->fn];
    if (args.size() != f.params.size()) {
        errorExec(x, n->line, n->col, "invalid arity in function '" + f.name + "'");
        return ExprVal();
    }
    std::vector<ExprVal> lv(f.vars.size());
    for (size_t i = 0; i < args.size(); ++i) lv[i] = args[i];
    ++x.depth;
    x.stack.push_back(f.name);
    ExprVal ret;
    const Flow fl = execBlock(f.body, x, lv, ret);
    x.stack.pop_back();
    --x.depth;
    if (!x.err.empty()) return ExprVal();
    if (fl == Flow::RETURN) return ret;
    if (fl == Flow::BREAK || fl == Flow::CONTINUE) {
        errorExec(x, n->line, n->col, "invalid flow inside function '" + f.name + "'");
        return ExprVal();
    }
    return ExprVal(0.0);
}

} // namespace

int Expr::evalAll(const ExprCtx& c, std::string& err, ExprVal* outs, int maxOut) const {
    err.clear();
    if (stmts.empty()) { err = "empty program"; return 0; }
    if (outStmt < 0 || outStmt >= (int)stmts.size()) { err = "program with no output"; return 0; }
    Exec x(c, funcs, err);
    std::vector<ExprVal> vars(varNames.size());
    std::vector<ExprVal> captured;
    for (size_t si = 0; si < stmts.size(); ++si) {
        ExprVal ret;
        std::vector<ExprVal>* cap = ((int)si == outStmt) ? &captured : nullptr;
        const Flow f = execStmt(stmts[si], x, vars, ret, cap);
        if (!err.empty()) return 0;
        if (f != Flow::NORMAL) { err = "break/continue/return outside context"; return 0; }
    }
    if ((int)captured.size() != outCount) {
        err = "inconsistent output in script";
        return 0;
    }
    if (outs) {
        for (int k = 0; k < outCount && k < maxOut; ++k) outs[k] = captured[(size_t)k];
    }
    return outCount;
}

ExprVal Expr::evalVal(const ExprCtx& c, int port, std::string& err) const {
    int n = count();
    if (n < 1) { err = "empty program"; return ExprVal(); }
    if (port < 0) port = 0;
    if (port >= n) port = n - 1;
    std::vector<ExprVal> outs(n);
    evalAll(c, err, outs.data(), n);
    if (!err.empty()) return ExprVal();
    return outs[port];
}

double Expr::eval(const ExprCtx& c, std::string& err) const {
    ExprVal v = evalVal(c, 0, err);
    if (!err.empty()) return 0;
    if (v.isStr) return 0;
    if (!std::isfinite(v.num)) { err = "non-numeric result"; return 0; }
    return v.num;
}

double Expr::evalPort(const ExprCtx& c, int port, std::string& err) const {
    ExprVal v = evalVal(c, port, err);
    if (!err.empty()) return 0;
    if (v.isStr) return 0;
    if (!std::isfinite(v.num)) { err = "non-numeric result"; return 0; }
    return v.num;
}
