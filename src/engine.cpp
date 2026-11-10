// simulacro — wxWidgets front-end for the nodesim engine
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "compat.h"
#include "numfmt.h"
#include "tema.h"
#include "scopesvg.h"
#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

int Engine::scanPlugins(const std::string& dir) {
    if (dir.empty()) return plugins.scanDirs(defaultPluginDirs());
    return plugins.scan(dir.c_str());
}

bool Engine::load(const std::string& file) {
    Graph tmp;
    if (!tmp.load(file)) return false;
    g = std::move(tmp);
    path = file;
    dirty = false;
    scopeData.clear();
    undoStack.clear();
    redoStack.clear();
    t = 0;
    stepIdx = 0;
    steps = 0;
    running = false;
    resolvePlugins();
    captureBase();
    g.clearCounters();
    return true;
}

bool Engine::save() {
    if (path.empty()) return false;
    syncMapStates(!saveInitial);
    // The loaded state is the reset reference. Save-current writes the live
    // plugin state; save-initial keeps the reference untouched.
    const Graph* b = saveInitial ? &base : nullptr;
    if (!g.save(path, saveInitial, b)) {
        lastError = "could not write " + path;
        return false;
    }
    dirty = false;
    return true;
}

bool Engine::saveAs(const std::string& file) {
    path = file;
    return save();
}

void Engine::newGraph() {
    g = Graph();
    base = Graph();
    path.clear();
    dirty = false;
    scopeData.clear();
    undoStack.clear();
    redoStack.clear();
    t = stepIdx = steps = 0;
    running = false;
    lastError.clear();
}

void Engine::resolvePlugins() {
    for (auto& n : g.nodes) {
        if (n.type != NodeType::PLUGIN || n.pinst || n.plugin.empty()) continue;
        n.plugin = compat::canonicalTypeId(n.plugin);
        auto inst = plugins.createInstance(n.plugin);
        if (!inst) {
            printf("simulacro: missing plugin: %s (node %d has no behaviour)\n",
                   n.plugin.c_str(), n.id);
            continue;
        }
        n.pinst = std::move(inst);
        if (!n.pstate.empty() && n.pinst->loadFn &&
            !n.pinst->callLoad(n.pinst->handle, n.pstate.c_str()))
            printf("simulacro: plugin %s rejected state of node %d\n", n.plugin.c_str(), n.id);
    }
    refreshPluginLabels();
    g.syncPluginOuts();
}

void Engine::refreshPluginLabels() {
    // The declarative in-node UI (nodesim_plugin_ui) needs vertical room: when
    // the type has a layout, reserve rows in the body height so the strip
    // never covers the ports. szH is the transient preferred size already used
    // by the engine; UI rows only INCREASE it (never shrink existing prefs).
    for (auto& n : g.nodes) {
        if (n.type != NodeType::PLUGIN) {
            n.disp.clear();
            n.szW = n.szH = 0;
            n.shape = 0;
            continue;
        }
        const PluginManager::RegType* rt = plugins.find(n.plugin);
        n.disp = rt ? rt->label : n.plugin;
        n.szW = rt ? (float)rt->prefW : 0;
        n.szH = rt ? (float)rt->prefH : 0;
        n.shape = rt ? rt->shape : 0;
        if (!rt || rt->ui.empty()) continue;
        // rows needed by the declared layout (kinds that occupy one row each)
        float rows = 0;
        for (const auto& it : rt->ui) {
            if (it.kind == NS_UI_END) break;
            if (it.kind == NS_UI_VSPACE) continue; // empty line (already counted)
            rows += 1.0f;
        }
        const float needH = 18.0f * rows + 10.0f;   // NODE_ROW_H ~ world units
        if (n.szH < needH) n.szH = needH;
        if (n.szW < 200.0f) n.szW = 200.0f;         // roomy inline widgets
    }
}
std::string Engine::initialBlob(const Node& n) { return g.initialBlob(n, plugins); }

void Engine::refreshPstate(Node& n) {
    if (n.type != NodeType::PLUGIN || n.plugin.empty()) return;
    std::string b = initialBlob(n);
    if (!b.empty()) n.pstate = b;
}

// After an undo/redo restores a graph snapshot, plugin nodes must go back to
// the state captured in the snapshot (params/texts/config). Reapplies the
// snapshot blob over the live instances; a missing plugin keeps its blob.
void Engine::reloadPluginNodes() {
    for (auto& n : g.nodes) {
        if (n.type != NodeType::PLUGIN || !n.pinst) continue;
        if (n.pstate.empty()) continue;
        if (n.pinst->loadFn && !n.pinst->callLoad(n.pinst->handle, n.pstate.c_str()))
            std::printf("simulacro: plugin %s: state of node %d rejected\n",
                        n.plugin.c_str(), n.id);
    }
}

void Engine::captureBase() { base = g; }

void Engine::checkpoint() {
    for (auto& n : g.nodes)
        if (n.type == NodeType::PLUGIN && n.pinst) refreshPstate(n);
    undoStack.push_back(g);
    if (undoStack.size() > 100) undoStack.erase(undoStack.begin());
    redoStack.clear();
    dirty = true;
}

void Engine::undo() {
    if (undoStack.empty()) return;
    redoStack.push_back(g);
    g = std::move(undoStack.back());
    undoStack.pop_back();
    g.touch();
    reloadPluginNodes();
    refreshPluginLabels();
    dirty = true;
}

void Engine::redo() {
    if (redoStack.empty()) return;
    undoStack.push_back(g);
    g = std::move(redoStack.back());
    redoStack.pop_back();
    g.touch();
    reloadPluginNodes();
    refreshPluginLabels();
    dirty = true;
}

Node* Engine::addScript(float x, float y) {
    checkpoint();
    int id = g.addNode(x, y, NodeType::SCRIPT);
    return g.find(id);
}

Node* Engine::addScope(float x, float y) {
    checkpoint();
    int id = g.addNode(x, y, NodeType::SCOPE);
    return g.find(id);
}

Node* Engine::addMux(float x, float y, int nin, int nout) {
    checkpoint();
    int id = g.addNode(x, y, NodeType::MUX);
    Node* n = g.find(id);
    if (n) {
        n->nIn = std::max(1, std::min(8, nin));
        n->nOut = std::max(1, std::min(8, nout));
        n->title = nin >= nout ? "Join" : "Split";
    }
    g.touch();
    return n;
}

Node* Engine::addPlugin(const std::string& typeId, float x, float y) {
    const PluginManager::RegType* rt = plugins.find(typeId);
    auto inst = plugins.createInstance(typeId);
    if (!inst) {
        lastError = "missing plugin: " + typeId;
        return nullptr;
    }
    checkpoint();
    Node n;
    n.id = g.nextId++;
    n.pos = {x, y};
    n.type = NodeType::PLUGIN;
    n.plugin = typeId;
    n.pinst = std::move(inst);
    if (rt) {
        n.nIn = rt->defIn > 0 ? rt->defIn : 1;
        n.disp = rt->label;
        n.szW = (float)rt->prefW;
        n.szH = (float)rt->prefH;
        n.shape = rt->shape;
    }
    g.nodes.push_back(n);
    g.syncPluginOuts();
    g.touch();
    Node& nn = g.nodes.back();
    nn.pstate = initialBlob(nn);
    return &nn;
}

void Engine::removeSelection(const std::set<int>& nodes,
                             const std::set<std::tuple<int, int, int, int>>& wires) {
    if (nodes.empty() && wires.empty()) return;
    checkpoint();
    for (const auto& w : wires)
        g.removeEdge(std::get<0>(w), std::get<1>(w), std::get<2>(w), std::get<3>(w));
    for (int id : nodes) {
        g.removeNode(id);
        scopeData.erase(id);
    }
    g.pruneGroups();
    g.touch();
}

void Engine::setInputs(int id, int n) {
    Node* nd = g.find(id);
    if (!nd || nd->type == NodeType::SCOPE) return;
    n = std::max(1, std::min(8, n));
    if (nd->nIn == n) return;
    checkpoint();
    nd = g.find(id);
    nd->nIn = n;
    for (auto it = g.edges.begin(); it != g.edges.end();) {
        if (it->to == id && it->inPort >= n) it = g.edges.erase(it);
        else ++it;
    }
    g.touch();
}

void Engine::setOutputs(int id, int n) {
    Node* nd = g.find(id);
    if (!nd || nd->type == NodeType::SCOPE) return;
    n = std::max(1, std::min(8, n));
    if (nd->nOut == n) return;
    checkpoint();
    nd = g.find(id);
    nd->nOut = n;
    for (auto it = g.edges.begin(); it != g.edges.end();) {
        if (it->from == id && it->port >= n) it = g.edges.erase(it);
        else ++it;
    }
    g.touch();
}

void Engine::stepOnce() {
    t = (double)(++stepIdx) * dt;
    StepOptions opt;
    opt.converge = converge;
    opt.tolerance = tolerance;
    opt.maxIterations = maxIterations;
    opt.events = events;
    g.step(t, dt, opt);
    sampleScopes();
    ++steps;
    if (running && iterations > 0 && steps >= iterations) running = false;
}

void Engine::resetSimulation() {
    checkpoint();
    running = false;
    t = 0;
    stepIdx = 0;
    steps = 0;
    for (auto& kv : scopeData) {
        for (auto& q : kv.second.ch) q.clear();
        for (auto& r : kv.second.rows) r = 0;
        for (auto& c : kv.second.cols) c = 0;
        for (auto& id : kv.second.srcId) id = nullptr;
        kv.second.tick = 0;
    }
    resetMapStates();
    for (auto& n : g.nodes) {
        const Node* b = base.find(n.id);
        if (b) {
            n.value = b->value;
            n.out = b->out;
            n.outTxt = b->outTxt;
            n.outValTxt = b->outValTxt;
        } else {
            n.value = 0;
            n.out = {};
            for (auto& s : n.outTxt) s.clear();
            n.outValTxt = {};
        }
        if (n.type == NodeType::PLUGIN && !n.plugin.empty()) {
            n.pv = {};
            n.pinst = plugins.createInstance(n.plugin);
            if (n.pinst && !n.pstate.empty() && n.pinst->loadFn)
                n.pinst->callLoad(n.pinst->handle, n.pstate.c_str());
        }
    }
    g.syncPluginOuts();
    g.clearCounters();
}

void Engine::clearScopes() {
    for (auto& kv : scopeData) {
        for (auto& q : kv.second.ch) q.clear();
        kv.second.tick = 0;
    }
}

std::string Engine::nodeLabel(const Node& n) const {
    if (!n.title.empty()) return n.title;
    if (n.type == NodeType::PLUGIN)
        return n.disp.empty() ? n.plugin : n.disp;
    return nodeTypeName(n.type);
}

const PluginManager::RegType* Engine::typeOf(const Node& n) const {
    if (n.type != NodeType::PLUGIN) return nullptr;
    return plugins.find(n.plugin);
}

void Engine::sampleScopes() {
    for (auto& n : g.nodes) {
        if (n.type != NodeType::SCOPE) continue;
        const ScopeCfg c = parseScopeCfg(n.params);
        ScopeData& d = scopeData[n.id];
        const int nin = n.nIn < 1 ? 1 : n.nIn;
        if ((int)d.ch.size() != nin) d.ch.assign((size_t)nin, std::deque<double>());
        if ((int)d.names.size() != nin) d.names.assign((size_t)nin, std::string());
        if ((int)d.colors.size() != nin) d.colors.assign((size_t)nin, SDL_Color{0, 0, 0, 0});
        if ((int)d.srcId.size() != nin) d.srcId.assign((size_t)nin, nullptr);
        if ((int)d.rows.size() != nin) d.rows.assign((size_t)nin, 0);
        if ((int)d.cols.size() != nin) d.cols.assign((size_t)nin, 0);
        for (int p = 0; p < nin; ++p) {
            d.colors[p] = socketColor(true, p);
            std::vector<std::pair<int, int>> src;
            for (const auto& e : g.edges)
                if (e.to == n.id && e.inPort == p) src.push_back({e.from, e.port});
            if (src.empty()) {
                d.names[p] = "ch" + std::to_string(p);
            } else if (src.size() == 1) {
                const Node* s = g.find(src[0].first);
                std::string tn = s ? (s->title.empty() ? std::string(nodeTypeName(s->type)) : s->title)
                                   : std::string("?");
                if (src[0].second) tn += "p" + std::to_string(src[0].second);
                const PortVal* sv = g.portValue(src[0].first, src[0].second);
                if (sv && (sv->kind == NS_VAL_LIST || sv->kind == NS_VAL_MAT) && sv->vec)
                    tn += "[" + std::to_string(sv->vec->size()) + "]";
                d.names[p] = tn;
            } else {
                d.names[p] = "sum" + std::to_string(p);
            }
        }
        if (d.frozen) continue;
        if (c.dec > 1 && (++d.tick % c.dec) != 0) continue;
        int cap = (int)(c.window / dt);
        if (cap < 32) cap = 32;
        if (cap > 8000) cap = 8000;
        for (int p = 0; p < nin; ++p) {
            const PortVal* pv = nullptr;
            int nw = 0;
            for (const auto& e : g.edges)
                if (e.to == n.id && e.inPort == p) {
                    ++nw;
                    pv = g.portValue(e.from, e.port);
                }
            if (nw == 1 && pv && (pv->kind == NS_VAL_LIST || pv->kind == NS_VAL_MAT) && pv->vec) {
                const void* idv = (const void*)pv->vec.get();
                if (d.srcId[p] != idv) {
                    d.srcId[p] = idv;
                    d.ch[p].clear();
                    d.rows[p] = pv->rows > 0 ? pv->rows : 1;
                    d.cols[p] = pv->cols > 0 ? pv->cols : (int)pv->vec->size();
                    const size_t MAXL = 20000;
                    size_t k = 0;
                    for (double v : *pv->vec) {
                        if (k++ >= MAXL) break;
                        d.ch[p].push_back(v);
                    }
                }
                continue;
            }
            if (d.srcId[p]) {
                d.srcId[p] = nullptr;
                d.ch[p].clear();
                d.rows[p] = d.cols[p] = 0;
            }
            double sum = 0;
            for (const auto& e : g.edges)
                if (e.to == n.id && e.inPort == p) sum += g.outVal(e.from, e.port);
            d.ch[p].push_back(sum);
            while ((int)d.ch[p].size() > cap) d.ch[p].pop_front();
        }
    }
    for (auto it = scopeData.begin(); it != scopeData.end();)
        if (!g.find(it->first)) it = scopeData.erase(it);
        else ++it;
}

void Engine::ensureMapState(const Node& n, ScopeData& d) {
    if (d.mapStateLoaded || n.type != NodeType::SCOPE) return;
    if (parseScopeCfg(n.params).type != ScopeType::MAP) return;
    MapState st;
    if (!n.pstate.empty()) st.deserialize(n.pstate);
    d.mapState = std::move(st);
    d.mapStateLoaded = true;
}

void Engine::ensureScopeMap(ScopeData& d, const ScopeCfg& c) {
    if (c.type != ScopeType::MAP) return;
    if (!d.map) d.map = std::make_shared<MapCache>();
    d.map->ensure(scopeMapPath(c), c);
    d.overlays.resize(c.mapOverlayFiles.size());
    for (size_t i = 0; i < c.mapOverlayFiles.size(); ++i) {
        if (!d.overlays[i]) d.overlays[i] = std::make_shared<MapCache>();
        ScopeCfg oc = c;
        oc.mapId = c.mapOverlayId;
        oc.mapLabel = c.mapOverlayLabel;
        oc.mapProj = c.mapOverlayProj;
        d.overlays[i]->ensure(scopeOverlayPath(c.mapOverlayFiles[i]), oc);
    }
}

void Engine::syncMapStates(bool live) {
    for (auto& n : g.nodes) {
        if (n.type != NodeType::SCOPE) continue;
        if (parseScopeCfg(n.params).type != ScopeType::MAP) continue;
        auto it = scopeData.find(n.id);
        std::string blob;
        if (it != scopeData.end() && it->second.mapStateLoaded)
            blob = it->second.mapState.serialize();
        if (live) n.pstateLive = blob;
        else if (n.pstate.empty() && !blob.empty()) n.pstate = blob;
    }
}

void Engine::resetMapStates() {
    for (auto& n : g.nodes) {
        if (n.type != NodeType::SCOPE) continue;
        if (parseScopeCfg(n.params).type != ScopeType::MAP) continue;
        auto it = scopeData.find(n.id);
        if (it == scopeData.end()) continue;
        it->second.mapState = MapState{};
        it->second.mapStateLoaded = false;
    }
}

std::string Engine::scopeMapPath(const ScopeCfg& c) const {
    if (c.mapFile.empty()) return {};
    if (c.mapFile[0] == '/') return c.mapFile;
    const size_t bar = path.find_last_of('/');
    if (bar == std::string::npos) return c.mapFile;
    return path.substr(0, bar + 1) + c.mapFile;
}

std::string Engine::scopeOverlayPath(const std::string& file) const {
    if (file.empty()) return {};
    if (file[0] == '/') return file;
    const size_t bar = path.find_last_of('/');
    if (bar == std::string::npos) return file;
    return path.substr(0, bar + 1) + file;
}

bool Engine::exportScopeSvg(int id, const std::string& file, std::string& error) {
    const Node* n = g.find(id);
    if (!n || n->type != NodeType::SCOPE) {
        error = "not a Scope node";
        return false;
    }
    const ScopeCfg c = parseScopeCfg(n->params);
    ScopeData& d = scopeData[id];
    ensureMapState(*n, d);
    ensureScopeMap(d, c);
    const std::string title = n->title.empty() ? ("Scope #" + std::to_string(id)) : n->title;
    const double dtStep = dt * (c.dec < 1 ? 1.0 : (double)c.dec);
    const ThemeColors tExp = themeExport();

    if (c.type == ScopeType::MAP) {
        const ThemeColors before = theme();
        themeApplyColors(tExp);
        const std::string svg = mapSvg(c, d, 900, 480, title);
        themeApplyColors(before);
        if (svg.empty()) {
            error = "map: load a GeoJSON before exporting";
            return false;
        }
        std::ofstream f(file);
        if (!f) {
            error = "could not write " + file;
            return false;
        }
        f << svg;
        return true;
    }

    std::vector<ScopeSvgChannel> channels;
    channels.reserve(d.ch.size());
    for (size_t k = 0; k < d.ch.size(); ++k) {
        ScopeSvgChannel ch;
        ch.name = k < d.names.size() && !d.names[k].empty() ? d.names[k]
                                                           : ("ch" + std::to_string(k));
        if (k < d.colors.size()) {
            ch.r = d.colors[k].r;
            ch.g = d.colors[k].g;
            ch.b = d.colors[k].b;
        } else {
            scopeSvgColor((int)k, ch.r, ch.g, ch.b);
        }
        ch.values.assign(d.ch[k].begin(), d.ch[k].end());
        channels.push_back(std::move(ch));
    }
    ScopeSvgCfg sc;
    sc.window = c.window;
    sc.dt = dtStep;
    sc.grid = c.grid;
    sc.legend = c.legend;
    sc.zero = c.zero;
    sc.yauto = c.yauto;
    sc.ymin = c.ymin;
    sc.ymax = c.ymax;
    sc.scale = c.scale;
    sc.theme.background[0] = tExp.background.r;
    sc.theme.background[1] = tExp.background.g;
    sc.theme.background[2] = tExp.background.b;
    sc.theme.panel[0] = tExp.panel.r;
    sc.theme.panel[1] = tExp.panel.g;
    sc.theme.panel[2] = tExp.panel.b;
    sc.theme.grid[0] = tExp.grid.r;
    sc.theme.grid[1] = tExp.grid.g;
    sc.theme.grid[2] = tExp.grid.b;
    sc.theme.text[0] = tExp.text.r;
    sc.theme.text[1] = tExp.text.g;
    sc.theme.text[2] = tExp.text.b;
    sc.theme.weak[0] = tExp.weak.r;
    sc.theme.weak[1] = tExp.weak.g;
    sc.theme.weak[2] = tExp.weak.b;
    sc.theme.highlight[0] = tExp.highlight.r;
    sc.theme.highlight[1] = tExp.highlight.g;
    sc.theme.highlight[2] = tExp.highlight.b;
    const std::string svg = scopeSvg(channels, title, sc);
    std::ofstream f(file);
    if (!f) {
        error = "could not write " + file;
        return false;
    }
    f << svg;
    return true;
}

std::string Engine::statusLine() const {
    std::string s = i18n::text(running ? "RUNNING" : "PAUSED");
    s += "  t=" + formatNumShort(t) + "s";
    s += "  step " + std::to_string(stepIdx);
    return s;
}
