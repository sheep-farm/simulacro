// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// Graph step: topological evaluation, delays, plugins, events, and convergence.

#include "graph_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

void Graph::step(double t, double dt) { step(t, dt, StepOptions{}); }

void Graph::step(double t, double dt, const StepOptions& opt) {
    // The plan (topological order, edges per node, and buffers) is rebuilt only when the
    // graph changes: the step is O(V+E) and does not allocate per node.
    if (syncPluginOuts()) invalidatePlan(); // nOut may depend on parameters
    const size_t n = nodes.size();
    if (n == 0) {
        if (plan_) plan_->pending.clear();
        return;
    }
    if (!plan_) plan_.reset(new Plan());
    Plan& pl = *plan_;
    if (pl.noStream.size() != n) pl.valid = false; // safety net (external change)
    const bool planRedone = !pl.valid;
    if (planRedone) buildPlan(pl);

    // Discrete events: configure effective sources and the baseline BEFORE
    // evaluation. Delivery of events generated in the previous step occurs just
    // below, before any node runs.
    const bool evt = opt.events;
    std::vector<EventSource> sources;
    if (evt) sources = eventSources();
    if (planRedone) {
        // topology changed: the old queue and baseline are no longer valid
        pl.pending.clear();
        pl.evtPrevOk.clear();
        pl.eventsActiveBefore = false;
    }
    if (evt && (!pl.eventsActiveBefore || pl.evtPrev.size() != n)) {
        pl.pending.clear();
        pl.evtPrev.assign(n, std::array<double, Node::MAX_PORTS>{});
        pl.evtPrevOk.assign(n, std::array<char, Node::MAX_PORTS>{});
    }
    if (!evt) {
        pl.pending.clear();
        pl.eventsActiveBefore = false;
        pl.evtCount.clear();
        pl.evtKind.clear();
        pl.evtT.clear();
    } else {
        pl.evtCount.assign(n, std::array<int, Node::MAX_PORTS>{});
        pl.evtKind.assign(n, std::array<int, Node::MAX_PORTS>{});
        pl.evtT.assign(n, std::array<double, Node::MAX_PORTS>{});
    }

    // Opt-in service: in a graph with a cycle, resolves the loop in the same step instead
    // of using the unit delay. Off (default), nothing changes.
    const bool conv = opt.converge && pl.cycle;
    const int maxIter = std::max(1, std::min(opt.maxIterations, 10000));
    const double tol = (std::isfinite(opt.tolerance) && opt.tolerance > 0)
                           ? opt.tolerance
                           : 1e-9;

    pl.old.resize(n);
    pl.oldPv.resize(n);
    pl.oldTxt.resize(n);
    for (size_t i = 0; i < n; ++i) pl.old[i] = nodes[i].out; // xout for scripts
    if (pl.cycle) { // only the "delay" of cycles needs the old pv/text
        for (size_t i = 0; i < n; ++i) if (!pl.noStream[i]) {
            pl.oldPv[i] = nodes[i].pv;
            pl.oldTxt[i] = nodes[i].outTxt;
        }
    }

    // Baseline: on the first step of a source (or after a topology/configuration
    // change), the current value is the reference; in the following ones it is the
    // output from the start of the previous step.
    if (evt) {
        for (const EventSource& f : sources) {
            if (f.port < 0 || f.port >= Node::MAX_PORTS) continue;
            auto it = pl.idx.find(f.node);
            if (it == pl.idx.end()) continue;
            size_t i = it->second;
            int p = f.port;
            if (p >= nodes[i].nOut) continue;
            if (!pl.evtPrevOk[i][(size_t)p]) {
                pl.evtPrev[i][(size_t)p] = nodes[i].out[(size_t)p];
                pl.evtPrevOk[i][(size_t)p] = 1;
            }
        }
    }

    // Delivers the previous step's queue, once per edge, before evaluation.
    auto deliverEvents = [&]() {
        if (!evt) return;
        for (const Plan::PendEvent& e : pl.pending) {
            auto ia = pl.idx.find(e.from);
            auto ib = pl.idx.find(e.to);
            if (ia == pl.idx.end() || ib == pl.idx.end()) { ++eventsIgnored; continue; }
            bool exists = false;
            for (const Edge& ed : edges)
                if (ed.from == e.from && ed.port == e.port && ed.to == e.to &&
                    ed.inPort == e.inPort) { exists = true; break; }
            if (!exists) { ++eventsIgnored; continue; }
            Node& dst = nodes[ib->second];
            NsEvent ev{};
            ev.kind = e.kind;
            ev.source = e.from;
            ev.source_port = e.port;
            ev.socket = e.inPort;
            ev.t = e.t;
            ev.value = e.value;
            ev.previous = e.previous;
            if (dst.type == NodeType::SCRIPT) {
                if (e.inPort >= 0 && e.inPort < Node::MAX_PORTS &&
                    ib->second < pl.evtCount.size()) {
                    size_t s = (size_t)e.inPort;
                    ++pl.evtCount[ib->second][s];
                    pl.evtKind[ib->second][s] = e.kind;
                    pl.evtT[ib->second][s] = e.t;
                    ++eventsDelivered;
                } else {
                    ++eventsIgnored;
                }
            } else if (dst.type == NodeType::PLUGIN && dst.pinst && dst.pinst->onEventFn) {
                dst.pinst->callEvent(dst.pinst->handle, &ev);
                if (dst.pinst->failed) ++eventsFailures;
                else ++eventsDelivered;
            } else {
                ++eventsIgnored;
            }
        }
        pl.pending.clear();
    };
    deliverEvents();

    // Detects transitions on the FINAL output of the step and queues the events
    // for delivery at the start of the next step (no cascade in the same step).
    auto detectEvents = [&]() {
        if (!evt) return;
        pl.pending.clear();
        for (const EventSource& f : sources) {
            if (f.port < 0 || f.port >= Node::MAX_PORTS) continue;
            auto it = pl.idx.find(f.node);
            if (it == pl.idx.end()) continue;
            size_t i = it->second;
            Node& nd = nodes[i];
            int p = f.port;
            if (p >= nd.nOut) {
                // missing port: no event, but the baseline follows the current value
                pl.evtPrev[i][(size_t)p] = nd.out[(size_t)p];
                pl.evtPrevOk[i][(size_t)p] = 1;
                continue;
            }
            const bool numeric = nd.pv[(size_t)p].kind == NS_VAL_NUM &&
                                  nd.outTxt[(size_t)p].empty();
            const double current = nd.out[(size_t)p];
            if (!numeric || !std::isfinite(current)) {
                // no event, but the baseline follows the final value
                pl.evtPrev[i][(size_t)p] = current;
                pl.evtPrevOk[i][(size_t)p] = 1;
                continue;
            }
            if (!pl.evtPrevOk[i][(size_t)p]) {
                pl.evtPrev[i][(size_t)p] = current;
                pl.evtPrevOk[i][(size_t)p] = 1;
                continue;
            }
            const double previous = pl.evtPrev[i][(size_t)p];
            bool occurred = false;
            if (std::isfinite(previous)) {
                if (f.kind == NS_EVENT_RISE) occurred = previous < f.threshold && current >= f.threshold;
                else if (f.kind == NS_EVENT_FALL) occurred = previous >= f.threshold && current < f.threshold;
                else if (f.kind == NS_EVENT_CHANGE) occurred = current != previous;
            }
            if (occurred) {
                ++eventsGenerated;
                bool hasEdge = false;
                for (const Edge& ed : edges) {
                    if (ed.from != nd.id || ed.port != p) continue;
                    hasEdge = true;
                    Plan::PendEvent pe;
                    pe.from = nd.id; pe.port = p;
                    pe.to = ed.to; pe.inPort = ed.inPort;
                    pe.kind = f.kind;
                    pe.t = t; pe.value = current; pe.previous = previous;
                    pl.pending.push_back(pe);
                }
                if (!hasEdge) ++eventsIgnored;
            }
            pl.evtPrev[i][(size_t)p] = current;
            pl.evtPrevOk[i][(size_t)p] = 1;
        }
        pl.eventsActiveBefore = true;
    };
    auto inval = [&](size_t j, int port) -> double {
        if (port < 0) port = 0;
        if (port >= Node::MAX_PORTS) port = Node::MAX_PORTS - 1;
        return (conv || pl.noStream[j]) ? nodes[j].out[port] : pl.old[j][port];
    };
    auto evalOf = [&](size_t j, int port) -> EdgeVal { // wire value (text/list/matrix)
        EdgeVal r;
        if (port < 0) port = 0;
        if (port >= Node::MAX_PORTS) port = Node::MAX_PORTS - 1;
        bool cur = conv || pl.noStream[j] != 0;
        r.pv = cur ? nodes[j].pv[port] : pl.oldPv[j][port];
        r.txt = cur ? &nodes[j].outTxt[port] : &pl.oldTxt[j][port];
        return r;
    };
    // Single divergence policy: non-finite -> previous value (from the snapshot),
    // clamp to ±NS_LIMITE -> limit. Both cases are counted per node and in total.
    auto markNaN = [&](Node& nd) {
        ++nd.nonFiniteCount;
        ++nonFiniteCount;
        if (firstNonFiniteNode < 0) { firstNonFiniteNode = nd.id; firstNonFiniteTime = t; }
    };
    auto markLimit = [&](Node& nd) { ++nd.limits; ++limits; };
    auto mirror = [&](Node& nd) { // extra ports = copy of port 0
        for (int p = 1; p < nd.nOut && p < Node::MAX_PORTS; ++p) {
            nd.out[p] = nd.out[0];
            nd.outTxt[p] = nd.outTxt[0];
            nd.outValTxt[p] = nd.outValTxt[0];
            nd.pv[p] = nd.pv[0]; // text/list/matrix also mirror (shared pointer)
        }
    };
    auto evalNode = [&](size_t i) {
        Node& nd = nodes[i]; // nd.value is still the previous one (single write below)
        double sum = 0;
        const int nin = nd.nIn < 1 ? 1 : nd.nIn;

        // buffers reused between nodes and steps
        std::vector<double>& ins = pl.ins;
        std::vector<double>& sockSum = pl.sockSum;
        std::vector<int>& sockOff = pl.sockOff;
        auto& pick = pl.pick;
        auto& hasPick = pl.hasPick;
        ins.clear();
        sockOff.assign((size_t)nin + 1, 0);
        if ((int)sockSum.size() < nin) sockSum.resize((size_t)nin, 0.0);
        std::fill(sockSum.begin(), sockSum.begin() + nin, 0.0);
        if ((int)pick.size() < nin) { pick.resize((size_t)nin); hasPick.resize((size_t)nin, 0); }
        std::fill(hasPick.begin(), hasPick.begin() + nin, 0);
        bool portsDone = false; // SCRIPT/PLUGIN can fill the extra ports themselves
        { // input list: sockets in order; within a socket, connection order
            size_t e = pl.inOff[i], inEnd = pl.inOff[i + 1];
            for (int p = 0; p < nin; ++p) {
                sockOff[(size_t)p] = (int)ins.size();
                while (e < inEnd && pl.inInPort[e] == p) {
                    double v = inval(pl.inFrom[e], pl.inSrcPort[e]);
                    ins.push_back(v); sum += v; sockSum[(size_t)p] += v;
                    if (!hasPick[(size_t)p]) { // text/list/matrix: first wins
                        EdgeVal ev = evalOf(pl.inFrom[e], pl.inSrcPort[e]);
                        if (ev.pv.kind != NS_VAL_NUM) { pick[(size_t)p] = ev; hasPick[(size_t)p] = 1; }
                    }
                    ++e;
                }
            }
            sockOff[nin] = (int)ins.size();
            for (; e < inEnd; ++e) { // defensive: sockets above the declared number
                double v = inval(pl.inFrom[e], pl.inSrcPort[e]);
                ins.push_back(v); sum += v;
            }
        }
        // value per socket for scripts and plugins (number = sum; otherwise first non-numeric)
        if ((int)pl.nsv.size() < nin) pl.nsv.resize((size_t)nin);
        for (int s = 0; s < nin; ++s) pl.nsv[(size_t)s] = NsValue{};
        std::vector<NsValue>& nsv = pl.nsv;
        for (int s = 0; s < nin; ++s) {
            NsValue& v = nsv[(size_t)s];
            v.num = sockSum[(size_t)s];
            if (!hasPick[(size_t)s]) continue;
            const EdgeVal& ev = pick[(size_t)s];
            v.kind = ev.pv.kind;
            if (v.kind == NS_VAL_TXT) {
                v.txt = (ev.txt && !ev.txt->empty()) ? ev.txt->c_str() : nullptr;
                if (!v.txt) v.kind = NS_VAL_NUM;
            } else if (v.kind == NS_VAL_LIST || v.kind == NS_VAL_MAT) {
                if (!ev.pv.vec) { v.kind = NS_VAL_NUM; }
                else {
                    v.data = ev.pv.vec->data();
                    v.rows = ev.pv.rows;
                    v.cols = ev.pv.cols;
                    if (v.rows <= 0 || v.cols <= 0) { v.rows = 1; v.cols = (int)ev.pv.vec->size(); }
                }
            }
        }
        switch (nd.type) {
            case NodeType::SCRIPT: {
                if (!nd.prog) break; // invalid: hold
                ExprCtx cx;
                cx.t = t; cx.dt = dt; cx.x = nd.value; cx.sum = sum;
                cx.n = (double)ins.size(); cx.in = ins.data(); cx.nin = (int)ins.size();
                // x0,x1,... = previous output per port. In a convergent loop the
                // "previous" is the previous iteration, not the previous step; thus
                // the fixed point also holds for scripts that read xout.
                cx.xout = conv ? nd.out.data() : pl.old[i].data(); cx.nxout = Node::MAX_PORTS;
                cx.sockOff = sockOff.data(); cx.nsock = (int)sockOff.size() - 1;
                cx.xval = nsv.data(); cx.nval = nin; // values per socket (text/list/matrix)
                if (evt && i < pl.evtCount.size()) {
                    cx.evtCount = pl.evtCount[i].data();
                    cx.evtKind = pl.evtKind[i].data();
                    cx.evtT = pl.evtT[i].data();
                    cx.nevt = Node::MAX_PORTS;
                }
                ExprVal outs[Node::MAX_PORTS];
                std::string err;
                int cnt = nd.prog->evalAll(cx, err, outs, Node::MAX_PORTS);
                if (!err.empty()) { nd.scriptError = err; break; } // ports mirror (below)
                if (cnt < 1) cnt = 1;
                for (int p = 0; p < nd.nOut && p < Node::MAX_PORTS; ++p) {
                    nd.pv[(size_t)p] = PortVal{};
                    if (p >= cnt) { nd.out[p] = nd.out[0]; nd.outTxt[(size_t)p].clear(); continue; } // no expr: mirror
                    if (outs[p].isStr) { nd.outTxt[(size_t)p] = outs[p].str; nd.out[p] = 0.0; nd.pv[(size_t)p].kind = NS_VAL_TXT; }
                    else if (outs[p].vec) { // list/matrix on the wire
                        nd.outTxt[(size_t)p].clear();
                        nd.out[p] = 0.0;
                        nd.pv[(size_t)p].kind = (unsigned char)(outs[p].rows > 1 ? NS_VAL_MAT : NS_VAL_LIST);
                        nd.pv[(size_t)p].vec = outs[p].vec;
                        nd.pv[(size_t)p].rows = outs[p].rows;
                        nd.pv[(size_t)p].cols = outs[p].cols;
                    } else {
                        nd.outTxt[(size_t)p].clear();
                        double r = outs[p].num;
                        if (!std::isfinite(r)) {
                            markNaN(nd);
                            r = conv ? nd.out[(size_t)p] : pl.old[i][(size_t)p];
                        }
                        if (r > NS_LIMIT) { r = NS_LIMIT; markLimit(nd); }
                        if (r < -NS_LIMIT) { r = -NS_LIMIT; markLimit(nd); }
                        nd.out[p] = r;
                    }
                }
                nd.value = nd.out[0];
                portsDone = true;
                break;
            }
            case NodeType::PLUGIN: {
                if (!nd.pinst) break;
                int sub = nd.pinst->substeps;
                if (sub < 1) sub = 1;
                if (sub > 1024) sub = 1024;
                const double subdt = dt / (double)sub;
                const double t0 = t - dt;
                NsContext cx;
                cx.sum = sum;
                cx.n = (double)ins.size(); cx.in = ins.data(); cx.nin = (int)ins.size();
                cx.sockOff = sockOff.data(); cx.nsock = (int)sockOff.size() - 1; // operating per socket
                cx.xval = nsv.data(); // values per socket (ABI 10)
                auto prepare = [&](int k) {
                    if (sub == 1) { cx.t = t; cx.dt = dt; }
                    else { cx.t = t0 + subdt * (double)(k + 1); cx.dt = subdt; }
                    cx.x = nd.value; // value returned by the previous call
                };
                if (nd.nOut > 1 && nd.pinst->stepMultiFn) { // plugin defines each port
                    bool multiDone = false;
                    for (int k = 0; k < sub; ++k) {
                        prepare(k);
                        double buf[Node::MAX_PORTS] = {0};
                        if (sub > 1) ++substeps;
                        if (!nd.pinst->callStepMulti(nd.pinst->handle, &cx, buf, nd.nOut)) break;
                        multiDone = true;
                        bool notFinite = false;
                        for (int p = 0; p < nd.nOut && p < Node::MAX_PORTS; ++p) {
                            double v = buf[p];
                            if (!std::isfinite(v)) {
                                markNaN(nd);
                                v = (conv || k > 0) ? nd.out[(size_t)p] : pl.old[i][(size_t)p];
                                notFinite = true;
                            }
                            if (v > NS_LIMIT) { v = NS_LIMIT; markLimit(nd); }
                            if (v < -NS_LIMIT) { v = -NS_LIMIT; markLimit(nd); }
                            nd.out[(size_t)p] = v;
                        }
                        nd.value = nd.out[0];
                        if (notFinite) break; // does not propagate an invalid substep
                    }
                    if (multiDone) portsDone = true;
                    break;
                }
                if (!nd.pinst->stepFn) break;
                for (int k = 0; k < sub; ++k) {
                    prepare(k);
                    if (sub > 1) ++substeps;
                    double r = nd.pinst->callStep(nd.pinst->handle, &cx);
                    if (nd.pinst->failed) break; // exception: keeps the last valid value
                    if (!std::isfinite(r)) { markNaN(nd); break; } // stays at the previous value
                    if (r > NS_LIMIT) { r = NS_LIMIT; markLimit(nd); }
                    if (r < -NS_LIMIT) { r = -NS_LIMIT; markLimit(nd); }
                    nd.value = r;
                }
                break;
            }
            case NodeType::MUX: { // joins/splits: numeric sum or passes text/list through
                int pickSock = -1;
                for (int s = 0; s < nin; ++s)
                    if (hasPick[(size_t)s]) { pickSock = s; break; }
                if (pickSock >= 0) { // list/text wins over the sum; mirror mirrors the 1st port
                    nd.pv[0] = pick[(size_t)pickSock].pv;
                    const std::string* t = pick[(size_t)pickSock].txt;
                    nd.outTxt[0] = t ? *t : "";
                    nd.value = 0;
                } else {
                    nd.outTxt[0].clear();
                    nd.pv[0] = PortVal{};
                    nd.value = sum;
                }
                break;
            }
            case NodeType::SCOPE:
                break; // sink: history sampling is done by the host (App::sampleScopes)
        }
        nd.out[0] = nd.value;             // port 0 = state
        if (!portsDone) mirror(nd);       // the rest mirror port 0
        if (nd.type == NodeType::PLUGIN && nd.pinst) {
            // text per port (ABI 9): display in the node and, now, a value that travels on the wire
            std::array<std::string, Node::MAX_PORTS> txt;
            if (nd.pinst->outTextFn)
                for (int p = 0; p < nd.nOut && p < Node::MAX_PORTS; ++p)
                    if (char* s = nd.pinst->callOutText(nd.pinst->handle, p)) { txt[(size_t)p] = s; ::free(s); }
            for (auto& s : nd.outTxt) s.clear();
            for (int p = 0; p < nd.nOut && p < Node::MAX_PORTS; ++p) nd.outTxt[(size_t)p] = txt[(size_t)p];
            // ABI 10: value per port (1 = publishes; 0 = keeps the previous one).
            // Text published by outValue stays in outValTxt and is restored when the
            // plugin returns 0 on the next step (outTxt is rebuilt from scratch).
            for (int p = 0; p < nd.nOut && p < Node::MAX_PORTS; ++p) {
                bool published = false;
                if (nd.pinst->outValueFn) {
                    NsValue v;
                    if (nd.pinst->callOutValue(nd.pinst->handle, p, &v)) {
                        published = true;
                        if (v.kind == NS_VAL_TXT && v.txt) {
                            nd.outTxt[(size_t)p] = v.txt;
                            nd.outValTxt[(size_t)p] = v.txt;
                            nd.pv[(size_t)p] = PortVal{};
                            nd.pv[(size_t)p].kind = NS_VAL_TXT;
                        } else if ((v.kind == NS_VAL_LIST || v.kind == NS_VAL_MAT) && v.data &&
                                   v.rows > 0 && v.cols > 0 &&
                                   (long long)v.rows * (long long)v.cols <= 8000000LL) {
                            auto vec = std::make_shared<std::vector<double>>(v.data, v.data + (size_t)v.rows * (size_t)v.cols);
                            nd.outTxt[(size_t)p].clear();
                            nd.outValTxt[(size_t)p].clear();
                            nd.pv[(size_t)p] = PortVal{};
                            nd.pv[(size_t)p].kind = (unsigned char)v.kind;
                            nd.pv[(size_t)p].vec = vec;
                            nd.pv[(size_t)p].rows = v.rows;
                            nd.pv[(size_t)p].cols = v.cols;
                        } else { // number (clears previous text/list)
                            double r = v.num;
                            if (!std::isfinite(r)) {
                                markNaN(nd);
                                r = conv ? nd.out[(size_t)p] : pl.old[i][(size_t)p];
                            }
                            if (r > NS_LIMIT) { r = NS_LIMIT; markLimit(nd); }
                            if (r < -NS_LIMIT) { r = -NS_LIMIT; markLimit(nd); }
                            nd.out[(size_t)p] = r;
                            if (p == 0) {
                                nd.value = r;
                                if (!portsDone) // step() feeds only port 0
                                    for (int q = 1; q < nd.nOut; ++q) nd.out[(size_t)q] = r;
                            }
                            nd.outTxt[(size_t)p].clear();
                            nd.outValTxt[(size_t)p].clear();
                            nd.pv[(size_t)p] = PortVal{};
                        }
                    }
                }
                if (published) continue;
                // no new value: text from outTextFn (ABI 9) travels as NS_VAL_TXT;
                // text published earlier by outValue is restored; list/matrix/number
                // remain in pv/out (the contract is "0 = keeps the previous one").
                if (!nd.outTxt[(size_t)p].empty()) {
                    nd.pv[(size_t)p] = PortVal{};
                    nd.pv[(size_t)p].kind = NS_VAL_TXT;
                } else if (nd.pv[(size_t)p].kind == NS_VAL_TXT && !nd.outValTxt[(size_t)p].empty()) {
                    nd.outTxt[(size_t)p] = nd.outValTxt[(size_t)p];
                    nd.pv[(size_t)p] = PortVal{};
                    nd.pv[(size_t)p].kind = NS_VAL_TXT;
                } else if (!nd.pinst->outValueFn) {
                    nd.pv[(size_t)p] = PortVal{};
                }
            }
            // ports that no longer exist do not keep an old value
            for (int p = nd.nOut; p < Node::MAX_PORTS; ++p) {
                nd.pv[(size_t)p] = PortVal{};
                nd.outValTxt[(size_t)p].clear();
            }
            nd.out[0] = nd.value; // numeric outValue on port 0 also updates the state
        }
    };
    for (size_t i : pl.order) evalNode(i);
    if (!conv) {
        for (size_t i = 0; i < n; ++i) if (!pl.noStream[i]) evalNode(i); // cycles last
        detectEvents();
        return;
    }

    // ---- opt-in convergence of cycles (algebraic loop) ----
    // The partial topological order above already evaluated the acyclic prefix. The rest
    // (nodes in the cycle and downstream of it) is iterated in index order, reading the
    // CURRENT wire values. The iteration is Gauss-Seidel: an edge from a node
    // already evaluated in this round sees the new value; another sees the value from the previous round.
    // The fixed point is order-independent, but index order keeps the step
    // deterministic and close to what the editor already does.
    const double inf = std::numeric_limits<double>::infinity();

    // Plugins with internal state: store the state from the START of the step and restore
    // before each iteration. Thus the loop resolves ONE step (state advances once,
    // in the last iteration), instead of accumulating one call per iteration.
    // Plugins without save/load are treated as stateless (contract declared in
    // README); plugins with external effects (files, etc.) must not enter
    // a convergent loop, because the call repeats.
    pl.convState.assign(n, std::string());
    pl.convStateOk.assign(n, 0);
    nodesWithoutSnapshot = 0;
    for (size_t i = 0; i < n; ++i) {
        if (pl.noStream[i] || nodes[i].type != NodeType::PLUGIN || !nodes[i].pinst) continue;
        PluginInstance& pi = *nodes[i].pinst;
        if (pi.failed || !pi.saveFn || !pi.loadFn) { ++nodesWithoutSnapshot; continue; }
        char* s = pi.callSave(pi.handle);
        if (!s) { ++nodesWithoutSnapshot; ++failuresSnapshot; continue; }
        pl.convState[i].assign(s);
        ::free(s);
        pl.convStateOk[i] = 1;
    }

    auto deltaNo = [&](size_t i) -> double {
        const Node& nd = nodes[i];
        double d = 0;
        for (int p = 0; p < Node::MAX_PORTS; ++p) {
            const double a = nd.out[(size_t)p], b = pl.old[i][(size_t)p];
            if (!std::isfinite(a) || !std::isfinite(b)) return inf;
            d = std::max(d, std::fabs(a - b));
            // text/list/matrix: only counts as converged when it stabilizes.
            if (nd.outTxt[(size_t)p] != pl.oldTxt[i][(size_t)p]) return inf;
            const PortVal& x = nd.pv[(size_t)p];
            const PortVal& y = pl.oldPv[i][(size_t)p];
            if (x.kind != y.kind || x.rows != y.rows || x.cols != y.cols) return inf;
            if ((x.vec == nullptr) != (y.vec == nullptr)) return inf;
            if (x.vec && y.vec && *x.vec != *y.vec) return inf;
        }
        return d;
    };
    auto savePrevious = [&](size_t i) {
        pl.old[i] = nodes[i].out;
        pl.oldPv[i] = nodes[i].pv;
        pl.oldTxt[i] = nodes[i].outTxt;
    };

    ++convergenceSteps;
    bool converged = false;
    bool restoreOk = true;
    double delta = inf;
    int noDelta = -1;
    for (int it = 0; it < maxIter; ++it) {
        if (it > 0) { // returns plugins to the state at the start of the step
            for (size_t i = 0; i < n; ++i) {
                if (pl.noStream[i] || !pl.convStateOk[i]) continue;
                PluginInstance& pi = *nodes[i].pinst;
                if (!pi.callLoad(pi.handle, pl.convState[i].c_str())) {
                    ++failuresRestore;
                    restoreOk = false;
                    break;
                }
            }
            if (!restoreOk) break;
        }
        for (size_t i = 0; i < n; ++i) if (!pl.noStream[i]) evalNode(i);
        ++iterationsConvergence;

        delta = 0;
        noDelta = -1;
        for (size_t i = 0; i < n; ++i) {
            if (pl.noStream[i]) continue;
            double d = deltaNo(i);
            if (d > delta) { delta = d; noDelta = nodes[i].id; }
        }
        lastConvergenceDelta = delta;
        noLastDelta = noDelta;
        if (delta <= tol) { converged = true; break; }
        for (size_t i = 0; i < n; ++i) if (!pl.noStream[i]) savePrevious(i);
    }
    if (!converged) {
        ++stepsNotConverged;
        lastNonConvergenceTime = t;
        if (!restoreOk) { lastConvergenceDelta = inf; noLastDelta = -1; }
    }
    pl.convState.clear();
    pl.convStateOk.clear();
    detectEvents();
}
