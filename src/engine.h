// simulacro — wxWidgets front-end for the nodesim engine
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Engine: the headless model. It owns the graph, the plugin registry, the
// simulation clock, scope history and undo. It knows nothing about wxWidgets
// or SDL windows; the views observe it.
#pragma once

#include "graph.h"
#include "plugin.h"
#include "scope.h"
#include "draw.h"
#include <map>
#include <set>
#include <string>
#include <vector>

struct Engine {
    // plugin manager must outlive the graph (dlclose only after the instances die)
    PluginManager plugins;
    Graph g;
    Graph base;             // state as loaded, for "Reset"
    std::string path;       // empty = untitled
    bool saveInitial = true;

    // clock / transport
    double t = 0;
    long long stepIdx = 0;
    double dt = 1.0 / 60.0;
    bool running = false;
    int rate = 60;
    int iterations = -1;    // -1 = infinite
    int steps = 0;

    // opt-in services
    bool converge = false;
    double tolerance = 1e-9;
    int maxIterations = 64;
    bool events = false;

    // scope history (transient, keyed by Scope node id)
    std::map<int, ScopeData> scopeData;

    // editing
    bool dirty = false;
    std::vector<Graph> undoStack;
    std::vector<Graph> redoStack;
    std::string lastError;

    // lifecycle
    int scanPlugins(const std::string& dir = {}); // empty = default discovery
    bool load(const std::string& file);
    bool save();                                  // writes path (or fails)
    bool saveAs(const std::string& file);
    void newGraph();
    void resolvePlugins();
    void captureBase();
    void reloadPluginNodes();
    void refreshPluginLabels();
    std::string initialBlob(const Node& n);

    // editing helpers
    void checkpoint();
    void undo();
    void redo();
    Node* addScript(float x, float y);
    Node* addScope(float x, float y);
    Node* addMux(float x, float y, int nin = 2, int nout = 2);
    Node* addPlugin(const std::string& typeId, float x, float y);
    void removeSelection(const std::set<int>& nodes,
                         const std::set<std::tuple<int, int, int, int>>& wires);
    void setInputs(int id, int n);
    void setOutputs(int id, int n);
    void refreshPstate(Node& n);

    // simulation
    void stepOnce();
    void resetSimulation();
    void clearScopes();
    void sampleScopes();
    std::string nodeLabel(const Node& n) const;
    const PluginManager::RegType* typeOf(const Node& n) const;
    // Canvas-click do plugin (additive ABI 14): devolve o ato declarado pelo
    // tipo (kind + params[] index) ou {}. `kind` NS_ACT_HOLD (aperta/solta) ou
    // NS_ACT_TOGGLE (cada clique alterna). Registrado por NS_PLUGIN_ACTS.
    struct ClickAct { int kind = 0; int param = -1; };
    ClickAct clickAct(const Node& n) const;
    // Executa a transição do ato (usado pelo canvas em cliques):
    //   HOLD: value=1 na pressão, 0 na soltura; TOGGLE: vira !value.
    // Não gera checkpoint de undo (é interação de painel, não edição).
    void runClickAct(const Node& n, const ClickAct& act, bool pressValue);
    void toggleClickAct(const Node& n, const ClickAct& act);

    // scope maps (same semantics as nodesim)
    void ensureMapState(const Node& n, ScopeData& d);
    void ensureScopeMap(ScopeData& d, const ScopeCfg& c);
    void syncMapStates(bool live);
    void resetMapStates();
    std::string scopeMapPath(const ScopeCfg& c) const;
    std::string scopeOverlayPath(const std::string& file) const;
    bool exportScopeSvg(int id, const std::string& file, std::string& error);

    // diagnostics
    std::string statusLine() const;
    void warn(const std::string& s) { lastError = s; }
};
