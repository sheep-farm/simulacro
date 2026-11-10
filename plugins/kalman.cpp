// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin: incremental Kalman filter (local level or local linear trend
// model) — estimates the latent state of a series arriving on socket 0.
//
// Models (`modelo` param):
//   0 = local level:       x_t = x_{t-1} + η (var q)   ;  y_t = x_t + ε (var r)
//   1 = linear trend:      level_t = level_{t-1} + incl + η1 (var q)
//                           incl_t  = incl_{t-1} + η2       (var qs)
//                           y_t     = level_t + ε           (var r)
//
// Input: socket 0 = y (the observed series).
// Parameters: modelo (0/1), q, qs, r, p0 (initial state variance), taxa
// (samples/s, 0 = each step), mle (0/1) and janela (MLE samples).
//
// MLE (mle=1, only in model 0): the variances q and r are re-estimated by
// maximum likelihood every ~janela/8 samples, over the last `janela`
// observations. The differenced local level is an MA(1):
//   Δy_t = ξ_t + ε_t − ε_{t-1} = e_t + θ·e_{t-1},  θ ∈ (−1, 0)
// and the exact likelihood (scalar Kalman filter, profiled σ²) is
// maximized by grid + refinement in θ. The relation to the variances is
//   r = −θ·σ² ,  q = (1+θ)²·σ².
// The estimate uses only samples PRIOR to the current data point (without
// peeking at y_t itself). In model 1 the MLE is not applied (single stderr
// warning) and fixed q/r apply. The q/r parameters you enter serve as the
// initial estimate while the MLE does not yet have enough window.
//
// Outputs: filtered level, slope, innovation (y − ŷ), gain K and state
// variance P (5 ports); with mle=1, plus estimated q and r (7 ports: qMLE/rMLE).
//
// The variances are PER SAMPLE (with `taxa` fixed, they amount to "by time").
// The first data point initializes the state (level = y, slope = 0, P = p0).
// Build: make plugins | Usage: menu Add -> Math -> Kalman
#include <plugin.h>
#include "common.h"

using nsplug::dupStr;
using nsplug::sockValue;
using nsplug::sockSum;
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace {

struct Kalman {
    double model = 0, q = 0.1, qs = 0.01, r = 1, p0 = 1, rate = 0;
    double mle = 0, mleWindow = 240;    // MLE: 0/1 and samples used
    double key = -1e308;
    long n = 0;                         // processed samples (0 = not initialized yet)
    double l = 0, s = 0;                // filtered level and slope
    double P00 = 1, P01 = 0, P11 = 1;   // state covariance
    double oLevel = 0, oIncl = 0, oInov = 0, oGain = 0, oVar = 0; // step outputs
    // MLE: observation window (y), last sample used and estimates
    std::deque<double> ys;
    long lastMle = -1;
    double qHat = 0.1, rHat = 1;
    bool warnedModel = false;
};

void pruneWindow(Kalman* d) {
    const size_t cap = (size_t)(d->mleWindow < 20 ? 20 : (d->mleWindow > 2000 ? 2000 : d->mleWindow));
    while (d->ys.size() > cap) d->ys.pop_front();
}

void zero(Kalman* d) {
    d->n = 0;
    d->l = d->s = 0;
    d->P00 = d->p0;
    d->P01 = 0;
    d->P11 = d->p0;
    d->oLevel = d->oIncl = d->oInov = d->oGain = 0;
    d->oVar = d->p0;
    d->key = -1e308;
    d->ys.clear();
    d->lastMle = -1;
    d->qHat = d->q;
    d->rHat = d->r;
    d->warnedModel = false;
}

// concentrated log-likelihood of the MA(1) d_t = e_t + θ·e_{t-1} (σ²=1).
// State (e_t, e_{t-1}), P1 = I; returns the log-likelihood and σ² in `s2`.
double loglikMA1(const std::vector<double>& d, double theta, double& s2) {
    s2 = 0;
    double P11 = 1, P12 = 0, P22 = 1; // P_1 = I (e_1 and e_0 independent)
    double a1 = 0, a2 = 0;
    double sumLogS = 0, sumV2S = 0;
    const double th2 = theta * theta;
    for (double x : d) {
        const double S = P11 + 2 * theta * P12 + th2 * P22;
        if (!(S > 1e-15) || !std::isfinite(S)) return -1e308;
        const double v = x - (a1 + theta * a2);
        sumLogS += std::log(S);
        sumV2S += v * v / S;
        const double K1 = (P11 + theta * P12) / S;
        const double f11 = P11 - K1 * (P11 + theta * P12); // filtered (1,1) block
        const double af1 = a1 + K1 * v; // filtered e_t
        a1 = 0;                         // prediction: x_{t+1} = (0, e_t)
        a2 = af1;
        P11 = 1;
        P12 = 0;
        P22 = f11;
    }
    const double m = (double)d.size();
    if (!(sumV2S > 0) || !std::isfinite(sumV2S)) return -1e308;
    s2 = sumV2S / m;
    if (!(s2 > 0) || !std::isfinite(s2)) return -1e308;
    return -0.5 * (m * (std::log(6.283185307179586476925286766559) + 1.0 + std::log(s2)) + sumLogS);
}

struct MleEst {
    double q = 0, r = 0;
    bool ok = false;
};

// MLE of (q, r) for the local level via the MA(1) of the differences. Grid in
// θ ∈ (−1,0) with two refinements; σ² is profiled (closed form) for each θ.
MleEst estimateMle(const std::deque<double>& ys) {
    MleEst est;
    std::vector<double> d;
    d.reserve(ys.size());
    for (size_t i = 1; i < ys.size(); ++i) {
        const double dy = ys[i] - ys[i - 1];
        if (std::isfinite(dy)) d.push_back(dy);
    }
    if (d.size() < 8) return est;

    double lo = -0.999, hi = -1e-6;
    double bestTh = lo, bestLL = -1e308, bestS2 = 0;
    for (int back = 0; back < 3; ++back) {
        const int K = back == 0 ? 80 : 40;
        for (int i = 0; i <= K; ++i) {
            const double th = lo + (hi - lo) * (double)i / (double)K;
            double s2 = 0;
            const double ll = loglikMA1(d, th, s2);
            if (std::isfinite(ll) && ll > bestLL) {
                bestLL = ll;
                bestTh = th;
                bestS2 = s2;
            }
        }
        if (back < 2) { // refine around the best point
            const double tick = (hi - lo) / (double)K;
            lo = bestTh - tick;
            hi = bestTh + tick;
            if (lo < -0.999) lo = -0.999;
            if (hi > -1e-6) hi = -1e-6;
        }
    }
    if (!(bestS2 > 0) || !std::isfinite(bestS2)) return est;
    double q = bestS2 * (1.0 + bestTh) * (1.0 + bestTh);
    double r = -bestTh * bestS2;
    if (!std::isfinite(q) || !std::isfinite(r)) return est;
    if (q < 1e-12) q = 1e-12;
    if (r < 1e-12) r = 1e-12;
    est.q = q;
    est.r = r;
    est.ok = true;
    return est;
}

// Re-estimate when the window is full and ~janela/8 samples have passed.
void estimateIfNeeded(Kalman* d) {
    if (d->ys.size() < 24) return;
    const long tick = (long)(d->mleWindow / 8) < 10 ? 10 : (long)(d->mleWindow / 8);
    if (d->lastMle >= 0 && d->n - d->lastMle < tick) return;
    const MleEst e = estimateMle(d->ys);
    d->lastMle = d->n;
    if (!e.ok) return;
    d->q = e.q;
    d->r = e.r;
    d->qHat = e.q;
    d->rHat = e.r;
}

// one prediction + update cycle (the first data point only initializes)
void process(Kalman* d, const NsContext* c) {
    if (!d) return;
    double y = sockValue(c, 0);
    bool m1 = d->model > 0.5;
    if (d->n == 0) { // initialize on the first data point (no innovation)
        d->l = y;
        d->s = 0;
        d->P00 = d->p0 > 0 ? d->p0 : 0;
        d->P01 = 0;
        d->P11 = m1 ? (d->p0 > 0 ? d->p0 : 0) : 0;
        d->n = 1;
        d->oLevel = d->l;
        d->oIncl = 0;
        d->oInov = 0;
        d->oGain = 0;
        d->oVar = d->P00;
        if (std::isfinite(y)) {
            d->ys.push_back(y);
            pruneWindow(d);
        }
        d->qHat = d->q;
        d->rHat = d->r;
        return;
    }
    if (d->mle > 0.5) {
        if (m1) { // MLE is only for the local level; warn once and keep q/r
            if (!d->warnedModel) {
                std::fprintf(stderr, "kalman: MLE only in model 0 (local level); using fixed q/r\n");
                d->warnedModel = true;
            }
            d->qHat = d->q;
            d->rHat = d->r;
        } else {
            estimateIfNeeded(d); // only with samples prior to y_t
        }
    }
    if (std::isfinite(y)) {
        d->ys.push_back(y);
        pruneWindow(d);
    }
    ++d->n;
    double q = d->q > 0 ? d->q : 0;
    double qs = m1 ? (d->qs > 0 ? d->qs : 0) : 0;
    double r = d->r > 0 ? d->r : 0;

    double lp, sp, P00p, P01p, P11p;
    if (m1) { // level + slope
        lp = d->l + d->s;
        sp = d->s;
        P00p = d->P00 + 2 * d->P01 + d->P11 + q;
        P01p = d->P01 + d->P11;
        P11p = d->P11 + qs;
    } else { // local level (slope always 0)
        lp = d->l;
        sp = 0;
        P00p = d->P00 + q;
        P01p = 0;
        P11p = 0;
    }
    double F = P00p + r;          // innovation variance
    double inov = y - lp;
    if (!(F > 1e-300)) {          // zero variance (r = q = P = 0): no update
        d->l = lp;
        d->s = sp;
        d->P00 = P00p;
        d->P01 = P01p;
        d->P11 = P11p;
        d->oLevel = lp;
        d->oIncl = sp;
        d->oInov = inov;
        d->oGain = 0;
        d->oVar = P00p;
        return;
    }
    double K0 = P00p / F;
    double K1 = m1 ? P01p / F : 0.0;
    d->l = lp + K0 * inov;
    d->s = sp + K1 * inov;
    d->P00 = P00p * (1 - K0);              // P = P' − K·H·P'
    d->P01 = P01p * (1 - K0);
    d->P11 = m1 ? (P11p - K1 * P01p) : 0;
    d->oLevel = d->l;
    d->oIncl = d->s;
    d->oInov = inov;
    d->oGain = K0;
    d->oVar = d->P00;
    if (d->mle > 0.5 && !m1) {
        d->qHat = d->q;
        d->rHat = d->r;
    }
}

void update(Kalman* d, const NsContext* c) {
    if (!d || !c) return;
    bool newValue = true;
    if (d->rate > 0) {
        double k = std::floor((double)c->t * d->rate);
        newValue = (k != d->key);
        if (newValue) d->key = k;
    }
    if (newValue) process(d, c);
}

void* newValue() { return new (std::nothrow) Kalman(); }
void dispose(void* h) { delete (Kalman*)h; }

double step(void* h, const NsContext* c) {
    Kalman* d = (Kalman*)h;
    update(d, c);
    return d ? d->oLevel : 0.0;
}

int stepMulti(void* h, const NsContext* c, double* out, int nout) {
    Kalman* d = (Kalman*)h;
    if (!d) return 0;
    update(d, c);
    const double v[7] = {d->oLevel, d->oIncl, d->oInov, d->oGain, d->oVar, d->qHat, d->rHat};
    for (int p = 0; p < nout && p < 7; ++p) out[p] = v[p];
    for (int p = 7; p < nout; ++p) out[p] = 0;
    return 1;
}

int numOut(void* h) {
    Kalman* d = (Kalman*)h;
    return d && d->mle > 0.5 ? 7 : 5;
}

char* portLabel(void* h, int p) {
    (void)h;
    static const char* names[7] = {"level", "incl", "inov", "K", "P", "qMLE", "rMLE"};
    if (p < 0 || p >= 7) return nullptr;
    const char* s = names[p];
    char* r = (char*)std::malloc(std::strlen(s) + 1);
    if (r) std::memcpy(r, s, std::strlen(s) + 1);
    return r;
}

// param 0=modelo 1=q 2=qs 3=r 4=p0 5=taxa 6=mle 7=janela
const char* const modelChoices[] = {"local level", "linear trend"};
NsParam params[] = {
    {"model", 0, 1, 0, NS_PARAM_ENUM, 1, nullptr,
     "state-space model", 2, modelChoices},
    {"q", 0, 1000, 0.1, NS_PARAM_NUM, 0.01, nullptr,
     "state noise variance", 0, nullptr},
    {"qs", 0, 1000, 0.01, NS_PARAM_NUM, 0.01, nullptr,
     "slope noise variance (model 1)", 0, nullptr},
    {"r", 0, 1000, 1, NS_PARAM_NUM, 0.01, nullptr,
     "observation noise variance", 0, nullptr},
    {"p0", 0, 1000, 1, NS_PARAM_NUM, 0.01, nullptr,
     "initial state variance", 0, nullptr},
    {"rate", 0, 1000, 0, NS_PARAM_NUM, 1, "Hz",
     "samples per second (0 = one per step)", 0, nullptr},
    {"mle", 0, 1, 0, NS_PARAM_BOOL, 1, nullptr,
     "re-estimate q and r by maximum likelihood", 0, nullptr},
    {"window", 20, 2000, 240, NS_PARAM_INT, 1, "samples",
     "MLE window", 0, nullptr},
};
double getParam(void* h, int i) {
    Kalman* d = (Kalman*)h;
    if (!d) return 0;
    switch (i) {
        case 0: return d->model;
        case 1: return d->q;
        case 2: return d->qs;
        case 3: return d->r;
        case 4: return d->p0;
        case 5: return d->rate;
        case 6: return d->mle;
        default: return d->mleWindow;
    }
}
int setParam(void* h, int i, double v) {
    Kalman* d = (Kalman*)h;
    if (!d) return 0;
    switch (i) {
        case 0: {
            double m = v > 0.5 ? 1 : 0;
            if (m != d->model) { d->model = m; zero(d); } // model changed: restart
            return 1;
        }
        case 1: d->q = v < 0 ? 0 : v; return 1;
        case 2: d->qs = v < 0 ? 0 : v; return 1;
        case 3: d->r = v < 0 ? 0 : v; return 1;
        case 4: d->p0 = v < 0 ? 0 : v; return 1;
        case 5: d->rate = v < 0 ? 0 : v; return 1;
        case 6: {
            double m = v > 0.5 ? 1 : 0;
            if (m != d->mle) { // on/off: fresh window, no stale estimate
                d->mle = m;
                d->ys.clear();
                d->lastMle = -1;
                d->qHat = d->q;
                d->rHat = d->r;
                d->warnedModel = false;
            }
            return 1;
        }
        default:
            d->mleWindow = v < 20 ? 20 : (v > 2000 ? 2000 : v);
            pruneWindow(d);
            return 1;
    }
}

char* save(void* h) {
    Kalman* d = (Kalman*)h;
    if (!d) return nullptr;
    char b[640];
    std::snprintf(b, sizeof b,
                  "model=%.10g q=%.17g qs=%.17g r=%.17g p0=%.17g rate=%.10g key=%.10g "
                  "n=%ld l=%.17g s=%.17g P00=%.17g P01=%.17g P11=%.17g "
                  "mle=%.10g window=%.10g lastMle=%ld qHat=%.17g rHat=%.17g",
                  d->model, d->q, d->qs, d->r, d->p0, d->rate, d->key,
                  d->n, d->l, d->s, d->P00, d->P01, d->P11,
                  d->mle, d->mleWindow, d->lastMle, d->qHat, d->rHat);
    std::string out(b);
    out += " ys=";
    char v[48];
    for (size_t i = 0; i < d->ys.size(); ++i) {
        std::snprintf(v, sizeof v, "%s%.17g", i ? "," : "", d->ys[i]);
        out += v;
    }
    char* r = (char*)std::malloc(out.size() + 1);
    if (r) std::memcpy(r, out.c_str(), out.size() + 1);
    return r;
}

int load(void* h, const char* blob) {
    Kalman* d = (Kalman*)h;
    if (!d || !blob) return 0;
    double model = d->model, q = d->q, qs = d->qs, r = d->r, p0 = d->p0, rate = d->rate;
    double mle = d->mle, window = d->mleWindow, qHat = d->qHat, rHat = d->rHat;
    double key = -1e308, l = 0, s = 0, P00 = d->p0, P01 = 0, P11 = d->p0;
    long n = 0, lastMle = -1;
    std::deque<double> ys;
    bool hasWindow = false;
    const char* p = blob;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != '=' && *e != ' ') ++e;
        if (!*e || *e != '=') break;
        std::string name(p, e - p);
        p = e + 1;
        if (name == "ys") { // comma-separated list: feeds the MLE window
            while (*p) {
                char* end = nullptr;
                double v = std::strtod(p, &end);
                if (end == p) break;
                if (std::isfinite(v)) ys.push_back(v);
                p = end;
                if (*p == ',') ++p;
                else break;
            }
            hasWindow = true;
            continue;
        }
        char* end = nullptr;
        if (name == "n") {
            n = std::strtol(p, &end, 10);
        } else if (name == "lastMle") {
            lastMle = std::strtol(p, &end, 10);
        } else {
            double v = std::strtod(p, &end);
            if (end == p) break;
            if (name == "model") model = v;
            else if (name == "q") q = v;
            else if (name == "qs") qs = v;
            else if (name == "r") r = v;
            else if (name == "p0") p0 = v;
            else if (name == "rate") rate = v;
            else if (name == "mle") mle = v;
            else if (name == "window") window = v;
            else if (name == "qHat") qHat = v;
            else if (name == "rHat") rHat = v;
            else if (name == "key") key = v;
            else if (name == "l") l = v;
            else if (name == "s") s = v;
            else if (name == "P00") P00 = v;
            else if (name == "P01") P01 = v;
            else if (name == "P11") P11 = v;
        }
        if (end == p) break;
        p = end;
    }
    d->model = model > 0.5 ? 1 : 0;
    d->q = q < 0 ? 0 : q;
    d->qs = qs < 0 ? 0 : qs;
    d->r = r < 0 ? 0 : r;
    d->p0 = p0 < 0 ? 0 : p0;
    d->rate = rate < 0 ? 0 : rate;
    d->mle = mle > 0.5 ? 1 : 0;
    d->mleWindow = window < 20 ? 20 : (window > 2000 ? 2000 : window);
    d->key = key;
    d->n = n;
    d->l = l;
    d->s = s;
    d->P00 = P00;
    d->P01 = P01;
    d->P11 = P11;
    d->lastMle = lastMle;
    d->qHat = qHat < 0 ? 0 : qHat;
    d->rHat = rHat < 0 ? 0 : rHat;
    if (hasWindow) {
        d->ys = std::move(ys);
        pruneWindow(d);
    }
    // outputs consistent with the restored state (the next step recomputes)
    d->oLevel = l;
    d->oIncl = s;
    d->oInov = 0;
    d->oGain = 0;
    d->oVar = P00;
    return 1;
}

NsNodeType types[] = {
    {"kalman", "Kalman", {140, 215, 180}, newValue, dispose, step, save, load,
     8, params, getParam, setParam, stepMulti,
     0, nullptr, nullptr, nullptr,
     true, numOut, nullptr, portLabel,
     1, 0, 0, nullptr},
};

NsPlugin plugin = NS_PLUGIN_BUILTIN("kalman", "1.1", 1, types, "Math");
} // namespace

extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
