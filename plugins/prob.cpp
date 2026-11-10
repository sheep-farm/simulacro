// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: probability distributions and their quantiles.
//
// Types:
//   normal      N(mu, sigma²)
//   student_t   Student's t with df degrees of freedom
//   chi2        chi-square with df degrees of freedom
//   fdist       F(d1, d2)
//   binomial    Bin(n, p), x = number of successes
//   poisson     Poisson(lambda), x = count
//
// Each type has the parameter `func`: 0 = pdf/pmf, 1 = cdf, 2 = quantile.
// For the quantile, `x` is the probability (0..1) and the output is the quantile. In the other
// cases `x` is the observed value. Socket 0, when connected, replaces `x`
// with the value arriving on the wire.
//
// The continuous ones use incomplete erfc/gamma/beta; the discrete ones use the incomplete
// beta (binomial) and the incomplete gamma (Poisson). The quantile is obtained by
// bisection over the CDF, with no table and no external dependency.
//
// Build: make plugins | Usage: Add menu -> Math -> Probability
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

const double PI = 3.14159265358979323846264338327950288;

enum { NORMAL = 0, STUDENT = 1, CHI2 = 2, FDIST = 3, BINOM = 4, POISSON = 5 };

struct Prob {
    int kind = NORMAL;
    double func = 1; // 0=pdf 1=cdf 2=quantile
    double x = 0;
    double mu = 0, sigma = 1;
    double df = 5, d1 = 5, d2 = 5;
    double n = 20, p = 0.5, lambda = 5;
};

double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------- special functions ----------

double betacf(double a, double b, double x) {
    const int MAXIT = 300;
    const double EPS = 3e-14, FPMIN = 1e-300;
    const double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < FPMIN) d = FPMIN;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= MAXIT; ++m) {
        const int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = 1.0 + aa / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return h;
}

double betai(double a, double b, double x) {
    if (a <= 0 || b <= 0) return 0.0;
    if (x <= 0) return 0.0;
    if (x >= 1) return 1.0;
    const double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
                               a * std::log(x) + b * std::log1p(-x));
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}

double gser(double a, double x) {
    const int MAXIT = 1000;
    const double EPS = 3e-14;
    double ap = a, del = 1.0 / a, sum = del;
    for (int n = 1; n <= MAXIT; ++n) {
        ap += 1.0;
        del *= x / ap;
        sum += del;
        if (std::fabs(del) < std::fabs(sum) * EPS) break;
    }
    return sum * std::exp(-x + a * std::log(x) - std::lgamma(a));
}

double gcf(double a, double x) {
    const int MAXIT = 1000;
    const double EPS = 3e-14, FPMIN = 1e-300;
    double b = x + 1.0 - a;
    double c = 1.0 / FPMIN;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= MAXIT; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < FPMIN) d = FPMIN;
        c = b + an / c;
        if (std::fabs(c) < FPMIN) c = FPMIN;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < EPS) break;
    }
    return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
}

double gammap(double a, double x) {
    if (a <= 0) return 0.0;
    if (x <= 0) return 0.0;
    if (x < a + 1.0) return gser(a, x);
    return 1.0 - gcf(a, x);
}

double gammq(double a, double x) { return 1.0 - gammap(a, x); }

// ---------- continuous ----------

double normPdf(double z) { return std::exp(-0.5 * z * z) / std::sqrt(2.0 * PI); }
double normCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

template <typename F>
double bisection(F f, double lo, double hi, double target) {
    for (int i = 0; i < 220; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) < target) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

double normQuant(double p) {
    p = clampD(p, 1e-15, 1.0 - 1e-15);
    return bisection(normCdf, -40.0, 40.0, p);
}

double tPdf(double t, double df) {
    if (df <= 0) return 0.0;
    const double lg = std::lgamma((df + 1.0) / 2.0) - std::lgamma(df / 2.0) -
                      0.5 * std::log(df * PI);
    return std::exp(lg - ((df + 1.0) / 2.0) * std::log1p(t * t / df));
}

double tCdf(double t, double df) {
    if (df <= 0) return 0.0;
    if (t == 0) return 0.5;
    const double xx = df / (df + t * t);
    const double ib = betai(df / 2.0, 0.5, xx);
    return t > 0 ? 1.0 - 0.5 * ib : 0.5 * ib;
}

double tQuant(double p, double df) {
    p = clampD(p, 1e-15, 1.0 - 1e-15);
    if (p == 0.5) return 0.0;
    auto f = [df](double t) { return tCdf(t, df); };
    return bisection(f, -1e6, 1e6, p);
}

double chi2Pdf(double x, double df) {
    if (x <= 0 || df <= 0) return x == 0 && df == 2 ? 0.5 : 0.0;
    const double lg = (df / 2.0 - 1.0) * std::log(x) - x / 2.0 -
                      (df / 2.0) * std::log(2.0) - std::lgamma(df / 2.0);
    return std::exp(lg);
}

double chi2Cdf(double x, double df) {
    if (x <= 0 || df <= 0) return 0.0;
    return gammap(df / 2.0, x / 2.0);
}

double chi2Quant(double p, double df) {
    p = clampD(p, 1e-15, 1.0 - 1e-15);
    if (df <= 0) return 0.0;
    double hi = std::max(1.0, df);
    while (chi2Cdf(hi, df) < p && hi < 1e12) hi *= 2.0;
    auto f = [df](double x) { return chi2Cdf(x, df); };
    return bisection(f, 0.0, hi, p);
}

double fPdf(double x, double d1, double d2) {
    if (x <= 0 || d1 <= 0 || d2 <= 0) return 0.0;
    const double lg = 0.5 * (d1 * std::log(d1 * x) + d2 * std::log(d2) -
                             (d1 + d2) * std::log(d1 * x + d2)) -
                      std::log(x) - std::lgamma(d1 / 2.0) - std::lgamma(d2 / 2.0) +
                      std::lgamma((d1 + d2) / 2.0);
    return std::exp(lg);
}

double fCdf(double x, double d1, double d2) {
    if (x <= 0 || d1 <= 0 || d2 <= 0) return 0.0;
    return betai(d1 / 2.0, d2 / 2.0, d1 * x / (d1 * x + d2));
}

double fQuant(double p, double d1, double d2) {
    p = clampD(p, 1e-15, 1.0 - 1e-15);
    if (d1 <= 0 || d2 <= 0) return 0.0;
    double hi = 1.0;
    while (fCdf(hi, d1, d2) < p && hi < 1e12) hi *= 2.0;
    auto f = [d1, d2](double x) { return fCdf(x, d1, d2); };
    return bisection(f, 0.0, hi, p);
}

// ---------- discrete ----------

double binomPdf(double k, double n, double p) {
    if (n < 0 || k < 0 || k > n || p < 0 || p > 1) return 0.0;
    const double kk = std::floor(k + 0.5), nn = std::floor(n + 0.5);
    if (p == 0) return kk == 0 ? 1.0 : 0.0;
    if (p == 1) return kk == nn ? 1.0 : 0.0;
    const double lg = std::lgamma(nn + 1.0) - std::lgamma(kk + 1.0) -
                      std::lgamma(nn - kk + 1.0) + kk * std::log(p) +
                      (nn - kk) * std::log1p(-p);
    return std::exp(lg);
}

double binomCdf(double k, double n, double p) {
    if (n < 0 || p < 0 || p > 1) return 0.0;
    const double kk = std::floor(k + 1e-12), nn = std::floor(n + 0.5);
    if (kk < 0) return 0.0;
    if (kk >= nn) return 1.0;
    if (p <= 0) return 1.0;
    if (p >= 1) return 0.0;
    return betai(nn - kk, kk + 1.0, 1.0 - p);
}

double binomQuant(double q, double n, double p) {
    const double nn = std::floor(n + 0.5);
    int lo = 0, hi = (int)std::max(0.0, nn);
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (binomCdf(mid, nn, p) >= q) hi = mid;
        else lo = mid + 1;
    }
    return (double)lo;
}

double poissonPdf(double k, double lambda) {
    if (k < 0 || lambda < 0) return 0.0;
    const double kk = std::floor(k + 0.5);
    if (lambda == 0) return kk == 0 ? 1.0 : 0.0;
    const double lg = -lambda + kk * std::log(lambda) - std::lgamma(kk + 1.0);
    return std::exp(lg);
}

double poissonCdf(double k, double lambda) {
    if (k < 0 || lambda < 0) return 0.0;
    const double kk = std::floor(k + 1e-12);
    if (lambda == 0) return 1.0;
    return gammq(kk + 1.0, lambda);
}

double poissonQuant(double q, double lambda) {
    if (lambda <= 0) return 0.0;
    int hi = 1;
    while (poissonCdf(hi, lambda) < q && hi < 1000000000) hi *= 2;
    int lo = 0;
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (poissonCdf(mid, lambda) >= q) hi = mid;
        else lo = mid + 1;
    }
    return (double)lo;
}

// ---------- computation and parameters ----------

double compute(Prob* d, double input) {
    const int f = (int)std::floor(d->func + 0.5);
    const double v = std::isfinite(input) ? input : d->x;
    if (f == 2) {
        const double q = clampD(v, 1e-15, 1.0 - 1e-15);
        switch (d->kind) {
            case NORMAL: return d->mu + d->sigma * normQuant(q);
            case STUDENT: return tQuant(q, d->df);
            case CHI2: return chi2Quant(q, d->df);
            case FDIST: return fQuant(q, d->d1, d->d2);
            case BINOM: return binomQuant(q, d->n, d->p);
            default: return poissonQuant(q, d->lambda);
        }
    }
    switch (d->kind) {
        case NORMAL:
            if (d->sigma <= 0) return 0.0;
            return f == 0 ? normPdf((v - d->mu) / d->sigma) / d->sigma
                          : normCdf((v - d->mu) / d->sigma);
        case STUDENT: return f == 0 ? tPdf(v, d->df) : tCdf(v, d->df);
        case CHI2: return f == 0 ? chi2Pdf(v, d->df) : chi2Cdf(v, d->df);
        case FDIST: return f == 0 ? fPdf(v, d->d1, d->d2) : fCdf(v, d->d1, d->d2);
        case BINOM: return f == 0 ? binomPdf(v, d->n, d->p) : binomCdf(v, d->n, d->p);
        default: return f == 0 ? poissonPdf(v, d->lambda) : poissonCdf(v, d->lambda);
    }
}

void normalize(Prob* d) {
    if (!d) return;
    int f = (int)std::floor(d->func + 0.5);
    if (f < 0) f = 0;
    if (f > 2) f = 2;
    d->func = f;
    d->sigma = clampD(d->sigma, 1e-9, 1e12);
    d->df = clampD(d->df, 1e-6, 1e7);
    d->d1 = clampD(d->d1, 1e-6, 1e7);
    d->d2 = clampD(d->d2, 1e-6, 1e7);
    d->n = clampD(std::floor(d->n + 0.5), 0.0, 1000000.0);
    d->p = clampD(d->p, 0.0, 1.0);
    d->lambda = clampD(d->lambda, 0.0, 1e9);
    if (d->kind == BINOM) {
        if (f == 2) d->x = clampD(d->x, 0.0, 1.0);
        else d->x = clampD(std::floor(d->x + 0.5), 0.0, d->n);
    } else if (d->kind == POISSON) {
        if (f == 2) d->x = clampD(d->x, 0.0, 1.0);
        else d->x = std::max(0.0, std::floor(d->x + 0.5));
    }
}

template <int T>
void* newValue() {
    Prob* d = new (std::nothrow) Prob();
    if (d) {
        d->kind = T;
        normalize(d);
    }
    return d;
}
void dispose(void* h) { delete (Prob*)h; }

double step(void* h, const NsContext* c) {
    Prob* d = (Prob*)h;
    if (!d) return 0.0;
    double v = d->x;
    if (c && c->nin > 0) {
        const double s = sockValue(c, 0);
        if (std::isfinite(s)) v = s;
    }
    return compute(d, v);
}

// indices: 0=func; then they depend on the type
const char* const funcChoices[] = {"pdf/pmf", "cdf", "quantile"};
NsParam paramsNormal[] = {
    {"func", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr, "computed function", 3, funcChoices},
    {"mu", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr, "mean", 0, nullptr},
    {"sigma", 1e-9, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr, "standard deviation", 0, nullptr},
    {"x", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr, "value (or probability in the quantile)", 0, nullptr},
};
NsParam paramsStudent[] = {
    {"func", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr, "computed function", 3, funcChoices},
    {"df", 1e-6, 1e7, 5, NS_PARAM_NUM, 1, "degrees of freedom", "degrees of freedom", 0, nullptr},
    {"x", -1e12, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr, "value (or probability in the quantile)", 0, nullptr},
};
NsParam paramsChi2[] = {
    {"func", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr, "computed function", 3, funcChoices},
    {"df", 1e-6, 1e7, 5, NS_PARAM_NUM, 1, "degrees of freedom", "degrees of freedom", 0, nullptr},
    {"x", 0, 1e12, 0, NS_PARAM_NUM, 0.1, nullptr, "value (or probability in the quantile)", 0, nullptr},
};
NsParam paramsF[] = {
    {"func", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr, "computed function", 3, funcChoices},
    {"d1", 1e-6, 1e7, 5, NS_PARAM_NUM, 1, "degrees of freedom", "numerator degrees of freedom", 0, nullptr},
    {"d2", 1e-6, 1e7, 5, NS_PARAM_NUM, 1, "degrees of freedom", "denominator degrees of freedom", 0, nullptr},
    {"x", 0, 1e12, 1, NS_PARAM_NUM, 0.1, nullptr, "value (or probability in the quantile)", 0, nullptr},
};
NsParam paramsBinom[] = {
    {"func", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr, "computed function", 3, funcChoices},
    {"n", 0, 1000000, 20, NS_PARAM_INT, 1, "trials", "number of trials", 0, nullptr},
    {"p", 0, 1, 0.5, NS_PARAM_NUM, 0.01, nullptr, "success probability", 0, nullptr},
    {"x", 0, 1e9, 0, NS_PARAM_NUM, 1, nullptr, "number of successes (or probability in the quantile)", 0, nullptr},
};
NsParam paramsPoisson[] = {
    {"func", 0, 2, 1, NS_PARAM_ENUM, 1, nullptr, "computed function", 3, funcChoices},
    {"lambda", 0, 1e9, 5, NS_PARAM_NUM, 0.1, "events", "average event rate", 0, nullptr},
    {"x", 0, 1e9, 0, NS_PARAM_NUM, 1, nullptr, "count (or probability in the quantile)", 0, nullptr},
};

double getParam(void* h, int i) {
    Prob* d = (Prob*)h;
    if (!d) return 0.0;
    if (i == 0) return d->func;
    switch (d->kind) {
        case NORMAL:
            if (i == 1) return d->mu;
            if (i == 2) return d->sigma;
            if (i == 3) return d->x;
            break;
        case STUDENT:
        case CHI2:
            if (i == 1) return d->df;
            if (i == 2) return d->x;
            break;
        case FDIST:
            if (i == 1) return d->d1;
            if (i == 2) return d->d2;
            if (i == 3) return d->x;
            break;
        case BINOM:
            if (i == 1) return d->n;
            if (i == 2) return d->p;
            if (i == 3) return d->x;
            break;
        default:
            if (i == 1) return d->lambda;
            if (i == 2) return d->x;
            break;
    }
    return 0.0;
}

int setParam(void* h, int i, double v) {
    Prob* d = (Prob*)h;
    if (!d) return 0;
    if (i == 0) d->func = v;
    else if (d->kind == NORMAL) {
        if (i == 1) d->mu = v;
        else if (i == 2) d->sigma = v;
        else if (i == 3) d->x = v;
        else return 0;
    } else if (d->kind == STUDENT || d->kind == CHI2) {
        if (i == 1) d->df = v;
        else if (i == 2) d->x = v;
        else return 0;
    } else if (d->kind == FDIST) {
        if (i == 1) d->d1 = v;
        else if (i == 2) d->d2 = v;
        else if (i == 3) d->x = v;
        else return 0;
    } else if (d->kind == BINOM) {
        if (i == 1) d->n = v;
        else if (i == 2) d->p = v;
        else if (i == 3) d->x = v;
        else return 0;
    } else {
        if (i == 1) d->lambda = v;
        else if (i == 2) d->x = v;
        else return 0;
    }
    normalize(d);
    return 1;
}

char* save(void* h) {
    Prob* d = (Prob*)h;
    if (!d) return nullptr;
    char b[512];
    std::snprintf(b, sizeof b,
                  "func=%.17g x=%.17g mu=%.17g sigma=%.17g df=%.17g d1=%.17g d2=%.17g n=%.17g p=%.17g lambda=%.17g",
                  d->func, d->x, d->mu, d->sigma, d->df, d->d1, d->d2, d->n, d->p, d->lambda);
    const size_t n = std::strlen(b) + 1;
    char* r = (char*)std::malloc(n);
    if (r) std::memcpy(r, b, n);
    return r;
}

int load(void* h, const char* blob) {
    Prob* d = (Prob*)h;
    if (!d || !blob) return 0;
    double func = d->func, x = d->x, mu = d->mu, sigma = d->sigma, df = d->df;
    double d1 = d->d1, d2 = d->d2, n = d->n, p = d->p, lambda = d->lambda;
    const char* q = blob;
    while (*q) {
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') ++q;
        const char* e = q;
        while (*e && *e != '=' && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r') ++e;
        if (!*e || *e != '=') { while (*e && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r') ++e; q = e; continue; }
        std::string k(q, e - q);
        q = e + 1;
        char* end = nullptr;
        const double v = std::strtod(q, &end);
        if (end == q) { while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') ++q; continue; }
        q = end;
        if (k == "func") func = v;
        else if (k == "x") x = v;
        else if (k == "mu") mu = v;
        else if (k == "sigma") sigma = v;
        else if (k == "df") df = v;
        else if (k == "d1") d1 = v;
        else if (k == "d2") d2 = v;
        else if (k == "n") n = v;
        else if (k == "p") p = v;
        else if (k == "lambda") lambda = v;
    }
    d->func = func; d->x = x; d->mu = mu; d->sigma = sigma; d->df = df;
    d->d1 = d1; d->d2 = d2; d->n = n; d->p = p; d->lambda = lambda;
    normalize(d);
    return 1;
}

char* labelIn(void*, int p) {
    if (p != 0) return nullptr;
    char* r = (char*)std::malloc(2);
    if (r) std::memcpy(r, "x", 2);
    return r;
}

NsNodeType kind(const char* id, const char* label, float r, float g, float b,
                double (*stepFn)(void*, const NsContext*), void* (*newFn)(),
                NsParam* params, int nparams) {
    NsNodeType t{};
    t.id = id;
    t.label = label;
    t.color[0] = r;
    t.color[1] = g;
    t.color[2] = b;
    t.create = newFn;
    t.destroy = dispose;
    t.step = stepFn;
    t.save = save;
    t.load = load;
    t.params = params;
    t.num_params = nparams;
    t.getParam = getParam;
    t.setParam = setParam;
    t.def_in = 1;
    t.portLabelIn = labelIn;
    t.capabilities = NS_CAP_INPUT_LABELS;
    return t;
}

NsNodeType types[] = {
    kind("normal", "Normal", 120, 180, 240, step, newValue<NORMAL>, paramsNormal, 4),
    kind("student_t", "Student-t", 140, 170, 235, step, newValue<STUDENT>, paramsStudent, 3),
    kind("chi2", "Chi-square", 125, 195, 230, step, newValue<CHI2>, paramsChi2, 3),
    kind("fdist", "Fisher F", 135, 200, 220, step, newValue<FDIST>, paramsF, 4),
    kind("binomial", "Binomial", 170, 185, 245, step, newValue<BINOM>, paramsBinom, 4),
    kind("poisson", "Poisson", 185, 170, 240, step, newValue<POISSON>, paramsPoisson, 3),
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("prob", "1.0", 6, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
