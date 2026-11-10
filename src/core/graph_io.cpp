// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// Graph persistence, initial plugin blob, and script compilation.

#include "graph.h"
#include "compat.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <iterator>
#include <sstream>

bool Graph::setScript(int id, const std::string& code) {
    Node* n = find(id);
    if (!n) return false;
    std::string err;
    auto p = Expr::parseList(code, err, n->params);
    n->script = code;
    if (!p) { n->prog.reset(); n->scriptError = err; return false; }
    n->prog = std::shared_ptr<Expr>(std::move(p));
    n->scriptError.clear();
    return true;
}



namespace {
std::string escScript(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '"') o += "\\\"";
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o + "\"";
}
// reads a quoted string starting at p (which points to '"'); advances p to after the
// closing '"'. returns false if malformed.
bool unescScript(const char*& p, std::string& out) {
    if (*p != '"') return false;
    ++p;
    out.clear();
    while (*p && *p != '"') {
        if (*p == '\\') {
            ++p;
            if (*p == 'n') out += '\n';
            else if (*p == 't') out += '\t';
            else if (*p == 'r') out += '\r';
            else if (*p == '\\' || *p == '"') out += *p;
            else return false;
            if (*p) ++p;
        } else {
            out += *p++;
        }
    }
    if (*p != '"') return false;
    ++p;
    return true;
}
} // namespace

std::string Graph::initialBlob(const Node& n, PluginManager& pm) const {
    if (n.type != NodeType::PLUGIN || n.plugin.empty()) return n.pstate;
    auto base = pm.createInstance(n.plugin);
    if (!base) return n.pstate; // plugin missing: keep the blob from the file
    if (!n.pstate.empty()) {
        if (!base->loadFn) return n.pstate; // no load: there is no way to rebuild it
        if (!base->callLoad(base->handle, n.pstate.c_str())) return n.pstate;
    }
    const PluginManager::RegType* rt = pm.find(n.plugin);
    if (rt && n.pinst) { // live parameters and texts take precedence over those in the blob
        if (base->setParamFn && n.pinst->getParamFn)
            for (int i = 0; i < (int)rt->params.size(); ++i)
                base->callSetParam(base->handle, i, n.pinst->callGetParam(n.pinst->handle, i));
        if (base->setTextFn && n.pinst->getTextFn)
            for (int i = 0; i < (int)rt->texts.size(); ++i)
                if (char* s = n.pinst->callGetText(n.pinst->handle, i)) {
                    base->callSetText(base->handle, i, s);
                    ::free(s);
                }
    }
    std::string out;
    if (base->saveFn)
        if (char* s = base->callSave(base->handle)) { out = s; ::free(s); }
    return out.empty() ? n.pstate : out;
}

void Graph::resetState(PluginManager& pm) {
    for (auto& n : nodes) {
        n.value = 0;
        n.out = {};
        for (auto& s : n.outTxt) s.clear();
        n.outValTxt = {};
        n.pv = {};
        if (n.type != NodeType::PLUGIN || n.plugin.empty() || !n.pinst) continue;
        std::string old = n.pstate;
        n.pstate.clear();                       // base = new instance (zeroed state)
        std::string newValue = initialBlob(n, pm);  // + parameters/texts as they are
        n.pstate = newValue.empty() ? old : newValue;
    }
    clearCounters();
}

bool Graph::save(const std::string& path, bool initialState, const Graph* base) const {
    { // safeguard: stores the current file (if not empty) in <path>.bak before overwriting
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            if (!old.empty()) {
                std::ofstream bak(path + ".bak", std::ios::binary | std::ios::trunc);
                if (bak) bak.write(old.data(), (std::streamsize)old.size());
            }
        }
    }
    std::ofstream f(path);
    if (!f) return false;
    f << "nodesim 10\n";
    for (auto& n : nodes) {
        // Plugin state: by default the INITIAL one is written (from the file, or from a
        // newly created node) — the execution's accumulated state is not written. 'pstate' is kept
        // up to date by the host with the current parameters (see App::refreshPstate); with
        // estadoInicial=0 it writes the live state (a snapshot from mid-execution).
        std::string blob;
        bool vivo = !initialState || n.pstate.empty();
        if (vivo && n.pinst && n.pinst->saveFn) {
            if (char* s = n.pinst->callSave(n.pinst->handle)) { blob = s; ::free(s); }
        }
        // Map Scope: the current state lives outside the plugin; the App keeps it in
        // pstateLive before saving. In "initial state", pstate is used.
        if (blob.empty() && n.type == NodeType::SCOPE && !initialState &&
            !n.pstateLive.empty())
            blob = n.pstateLive;
        if (blob.empty()) blob = n.pstate;
        double value = n.value; // node x state: starting (as on load) or live
        if (initialState && base) {
            const Node* b = base->find(n.id);
            value = b ? b->value : 0.0f;
        }
        char px[32], py[32], vb[40];
        std::snprintf(px, sizeof px, "%.9g", (double)n.pos.x);
        std::snprintf(py, sizeof py, "%.9g", (double)n.pos.y);
        std::snprintf(vb, sizeof vb, "%.17g", value);
        f << "node " << n.id << ' ' << (int)n.type << ' '
          << px << ' ' << py << ' ' << vb << ' '
          << escScript(n.script) << ' '
          << escScript(n.plugin) << ' ' << escScript(blob) << ' ' << n.nOut << ' '
          << n.nIn << ' ' << n.inSide << ' ' << n.outSide << ' '
          << escScript(n.title) << ' ' << escScript(n.type == NodeType::SCOPE ? compat::canonicalScopeParams(n.params) : n.params) << '\n';
    }
    for (auto& gr : groups) {
        f << "group " << gr.id << ' ' << (gr.collapsed ? 1 : 0) << ' '
          << gr.x << ' ' << gr.y << ' ' << gr.w << ' ' << gr.h << ' '
          << escScript(gr.title);
        for (int id : gr.nodes) f << ' ' << id;
        f << '\n';
    }
    for (auto& e : edges) f << "edge " << e.from << ' ' << e.port << ' ' << e.to << ' ' << e.inPort << '\n';
    return (bool)f;
}

bool Graph::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    std::getline(f, line);
    bool v1 = (line == "nodesim 1");
    bool v2 = (line == "nodesim 2");
    bool v3 = (line == "nodesim 3");
    bool v4 = (line == "nodesim 4");
    bool v5 = (line == "nodesim 5");
    bool v6 = (line == "nodesim 6");
    bool v7 = (line == "nodesim 7");
    bool v8 = (line == "nodesim 8");
    bool v9 = (line == "nodesim 9");
    bool v10 = (line == "nodesim 10");
    if (!v1 && !v2 && !v3 && !v4 && !v5 && !v6 && !v7 && !v8 && !v9 && !v10) return false; // magic
    Graph tmp;
    int maxId = -1, maxGroupId = -1;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::string kind; s >> kind;
        if (kind == "node") {
            Node n;
            if (v7 || v8 || v9 || v10) { // v7+: type 0=SCRIPT 1=PLUGIN 2=SCOPE (v8+: + params)
                int ty = 0;
                if (!(s >> n.id >> ty >> n.pos.x >> n.pos.y >> n.value)) return false;
                if (ty < 0 || ty > 3) return false;
                n.type = (NodeType)ty;
                std::string rest;
                std::getline(s, rest);
                size_t q = rest.find('"');
                if (q == std::string::npos) return false;
                const char* p = rest.c_str() + q;
                auto str = [&](std::string& out) -> bool {
                    while (*p == ' ' || *p == '\t') ++p;
                    return unescScript(p, out);
                };
                auto num = [&](int lo, int hi, int& out) -> bool {
                    while (*p == ' ' || *p == '\t') ++p;
                    char* e3 = nullptr;
                    long v = std::strtol(p, &e3, 10);
                    if (e3 == p || v < lo || v > hi) return false;
                    p = e3; out = (int)v; return true;
                };
                if (!str(n.script)) return false;
                if (!str(n.plugin)) return false;
                n.plugin = compat::canonicalTypeId(n.plugin);
                if (!str(n.pstate)) return false;
                n.pstate = compat::canonicalStateBlob(n.plugin, n.pstate);
                if (!num(0, Node::MAX_PORTS, n.nOut)) return false;
                if (!num(1, Node::MAX_PORTS, n.nIn)) return false;
                if (!num(0, 3, n.inSide)) return false;
                if (!num(0, 3, n.outSide)) return false;
                if (!str(n.title)) return false;
                if ((v8 || v9 || v10) && !str(n.params)) return false;
                if (n.type == NodeType::SCOPE) n.params = compat::canonicalScopeParams(n.params);
                if (n.type == NodeType::SCOPE) n.nOut = 0;        // sink: never has an output
                else if (n.nOut < 1) n.nOut = 1;                  // others: at least 1
                while (*p == ' ' || *p == '\t') ++p;
                if (*p != '\0') return false;
                tmp.nodes.push_back(n);
                if (n.id > maxId) maxId = n.id;
                continue;
            }
            int ty = 0;
            if (!(s >> n.id >> ty >> n.pos.x >> n.pos.y >> n.gain >> n.freq >> n.value)) return false;
            if (ty < 0 || ty > 5) return false;
            n.r = 34.0f;
            if (!v1) { // v2+ has the script quoted in the rest of the line
                std::string rest;
                std::getline(s, rest);
                size_t q = rest.find('"');
                if (q == std::string::npos) return false;
                const char* p = rest.c_str() + q;
                std::string sc;
                if (!unescScript(p, sc)) return false;
                n.script = sc;
                while (*p == ' ' || *p == '\t') ++p;
                if (!v2) { // v3+ has the angle after the script
                    char* end = nullptr;
                    float ang = std::strtof(p, &end);
                    if (end == p) return false;
                    p = end;
                    n.angleDeg = ang;
                    if (!v3) { // v4+ has the plugin id + state blob
                        std::string pid, blob;
                        while (*p == ' ' || *p == '\t') ++p;
                        if (!unescScript(p, pid)) return false;
                        while (*p == ' ' || *p == '\t') ++p;
                        if (!unescScript(p, blob)) return false;
                        n.plugin = pid;
                        n.pstate = compat::canonicalStateBlob(n.plugin, blob);
                        if (!v4) { // v5 has the number of outputs
                            while (*p == ' ' || *p == '\t') ++p;
                            char* e2 = nullptr;
                            long no = std::strtol(p, &e2, 10);
                            if (e2 == p || no < 1 || no > Node::MAX_PORTS) return false;
                            p = e2;
                            n.nOut = (int)no;
                            if (!v5) { // v6: number of inputs, sides, and title
                                auto nextInt = [&](int lo, int hi, int& out) -> bool {
                                    while (*p == ' ' || *p == '\t') ++p;
                                    char* e3 = nullptr;
                                    long v = std::strtol(p, &e3, 10);
                                    if (e3 == p || v < lo || v > hi) return false;
                                    p = e3;
                                    out = (int)v;
                                    return true;
                                };
                                if (!nextInt(1, Node::MAX_PORTS, n.nIn)) return false;
                                if (!nextInt(0, 3, n.inSide)) return false;
                                if (!nextInt(0, 3, n.outSide)) return false;
                                while (*p == ' ' || *p == '\t') ++p;
                                std::string ti;
                                if (!unescScript(p, ti)) return false;
                                n.title = ti;
                            }
                        }
                    }
                }
                while (*p == ' ' || *p == '\t') ++p;
                if (*p != '\0') return false;
            }
            // v1-6: converts the removed types to equivalent SCRIPT
            char nb[32];
            switch (ty) {
                case 0: // FONTE: sin(freq*t + id)
                    std::snprintf(nb, sizeof nb, "%g", n.freq);
                    n.type = NodeType::SCRIPT;
                    n.script = "sin(" + std::string(nb) + "*t + " + std::to_string(n.id) + ")";
                    break;
                case 1: // SOMA
                    n.type = NodeType::SCRIPT;
                    n.script = "sum";
                    break;
                case 2: // GANHO: sum*gain
                    std::snprintf(nb, sizeof nb, "%g", n.gain);
                    n.type = NodeType::SCRIPT;
                    n.script = "sum*" + std::string(nb);
                    break;
                case 3: // INTEGRADOR
                    n.type = NodeType::SCRIPT;
                    n.script = "x + dt*sum";
                    break;
                case 4:
                    n.type = NodeType::SCRIPT;
                    break;
                default:
                    n.type = NodeType::PLUGIN;
                    break;
            }
            tmp.nodes.push_back(n);
            if (n.id > maxId) maxId = n.id;
        } else if (kind == "edge") {
            Edge e;
            e.port = 0;
            if (!(s >> e.from)) return false;
            if (!v1 && !v2 && !v3 && !v4) { // v5+: from port to [inPort]
                if (!(s >> e.port >> e.to) || e.port < 0 || e.port > 7) return false;
                if (!v5) { // v6 has the input port
                    if (!(s >> e.inPort) || e.inPort < 0 || e.inPort > 7) return false;
                }
            } else { // v1-4: from to (= port 0)
                if (!(s >> e.to)) return false;
            }
            tmp.edges.push_back(e);
        } else if (kind == "group") { // group frame (format 9+, optional)
            Group gr;
            int col = 0;
            if (!(s >> gr.id >> col) || gr.id < 0) return false;
            gr.collapsed = col != 0;
            std::string rest;
            std::getline(s, rest);
            size_t q = rest.find('"');
            if (q == std::string::npos) return false;
            { // optional x/y/w/h before the title (old files do not have them)
                std::istringstream pre(rest.substr(0, q));
                float x = 0, y = 0, w = 0, h = 0;
                if (pre >> x) {
                    gr.x = std::isfinite(x) ? x : 0.0f;
                    if (!(pre >> y)) y = 0.0f;
                    gr.y = std::isfinite(y) ? y : 0.0f;
                    if (pre >> w)
                        gr.w = (w > 0 && std::isfinite(w)) ? std::min(w, 10000.0f) : 0.0f;
                    if (pre >> h)
                        gr.h = (h > 0 && std::isfinite(h)) ? std::min(h, 10000.0f) : 0.0f;
                }
            }
            const char* p = rest.c_str() + q;
            if (!unescScript(p, gr.title)) return false;
            for (;;) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p) break;
                char* e = nullptr;
                long v = std::strtol(p, &e, 10);
                if (e == p || v < 0) return false;
                p = e;
                gr.nodes.push_back((int)v);
            }
            if (gr.id > maxGroupId) maxGroupId = gr.id;
            tmp.groups.push_back(std::move(gr));
        } else {
            return false;
        }
    }
    // validates references
    for (auto& e : tmp.edges) {
        const Node* a = nullptr;
        const Node* b = nullptr;
        for (auto& n : tmp.nodes) {
            if (n.id == e.from) a = &n;
            if (n.id == e.to) b = &n;
        }
        if (!a || !b || e.from == e.to) return false;
        if (e.port < 0 || e.port >= a->nOut) return false;
        if (e.inPort < 0 || e.inPort >= (b->nIn < 1 ? 1 : b->nIn)) return false;
    }
    // removes exact duplicates
    std::vector<Edge> uniq;
    for (auto& e : tmp.edges) {
        bool dup = false;
        for (auto& u : uniq)
            if (u.from == e.from && u.port == e.port && u.to == e.to && u.inPort == e.inPort) { dup = true; break; }
        if (!dup) uniq.push_back(e);
    }
    tmp.edges.swap(uniq);
    tmp.nextId = maxId + 1;
    // groups: unique ids, existing members, and one group per node; empty ones are dropped
    {
        std::set<int> ids, members;
        for (auto& gr : tmp.groups) {
            if (!ids.insert(gr.id).second) return false;
            std::vector<int> stays;
            for (int id : gr.nodes)
                if (tmp.find(id) && members.insert(id).second) stays.push_back(id);
            gr.nodes.swap(stays);
        }
        tmp.groups.erase(std::remove_if(tmp.groups.begin(), tmp.groups.end(),
            [](const Group& gr){ return gr.nodes.empty(); }), tmp.groups.end());
        // keeps creation order (id) stable in the drawing
        std::sort(tmp.groups.begin(), tmp.groups.end(),
                  [](const Group& a, const Group& b){ return a.id < b.id; });
        tmp.nextGroupId = maxGroupId + 1;
    }
    for (auto& n : tmp.nodes) { // compiles scripts; an error does not abort the load, it stays on the node
        n.out[0] = n.value; // port 0 reflects the loaded state (before the 1st step)
        if (n.type == NodeType::SCRIPT && !n.script.empty()) {
            std::string err;
            auto p = Expr::parseList(n.script, err, n.params);
            if (p) { n.prog = std::shared_ptr<Expr>(std::move(p)); n.scriptError.clear(); }
            else { n.prog.reset(); n.scriptError = err; }
        }
    }
    *this = std::move(tmp);
    invalidatePlan();
    return true;
}
