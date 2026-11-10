// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Simulation core — no UI/SDL dependency. Compiles standalone and fast.
#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "expr.h"
#include "plugin.h"

// Built-in nodes: Script (computation), Plugin (.so), Scope (graphical sink), and MUX
// (multiplexer: N inputs -> sum/value -> M identical outputs; joins and splits
// without needing a script). The old SOURCE/SUM/GAIN/INTEGRATOR were
// removed; v1-6 files are converted to SCRIPT on load. SCOPE has no
// output: it only observes the inputs.
enum class NodeType { SCRIPT = 0, PLUGIN = 1, SCOPE = 2, MUX = 3 };

// Safety limit for numeric ports (prevents integrator explosions and
// keeps display/scale sane). High enough for economic series (indices,
// prices, counts) — it used to be ±1000, which truncated real data.
static constexpr double NS_LIMIT = 1e9;

struct Vec2 { float x = 0, y = 0; };

// Port value (ABI 10). kind == NS_VAL_NUM => number in Node::out[];
// NS_VAL_TXT => text in Node::outTxt[]; NS_VAL_LIST/MAT => vector in vec
// (shared and immutable: large lists are not copied per step).
struct PortVal {
    unsigned char kind = NS_VAL_NUM;
    std::shared_ptr<const std::vector<double>> vec;
    int rows = 0, cols = 0;
};

struct Node {
    static constexpr int MAX_PORTS = NS_MAX_PORTS; // ports per node (input and output)
    int id = -1;
    Vec2 pos{0, 0};
    float r = 34.0f;
    double value = 0.0;               // state x[n] = output of port 0 (= out[0])
    NodeType type = NodeType::SCRIPT;
    float gain = 0.5f;                // legacy: converts GAIN (v1-6) to script
    float freq = 2.0f;                // legacy: converts SOURCE (v1-6) to script
    float angleDeg = 0;               // legacy (rotation removed; kept to load v1-5)
    int nIn = 1;                    // number of input ports (1..8)
    int nOut = 1;                   // number of output ports (1..8)
    std::array<double, MAX_PORTS> out{}; // output per port; out[0] == value
    std::array<std::string, MAX_PORTS> outTxt; // text per port (empty = numeric value)
    // text published by outValue that survives when it returns 0 (outTxt
    // is rebuilt every step by outText; this buffer is the "previous value")
    std::array<std::string, MAX_PORTS> outValTxt;
    std::array<PortVal, MAX_PORTS> pv;         // value per port (text/list/matrix on the wire)
    std::string title;              // title displayed in the box (empty = type name)
    std::string disp;               // plugin type label for display (transient; from the registry)
    float szW = 0, szH = 0;         // preferred node size (transient; from the plugin)
    int shape = 0;                  // node shape (transient; from the plugin: 0=box)
    int inSide = 0;                 // input side: 0=left 1=top 2=right 3=bottom
    int outSide = 2;                // output side: 0=left 1=top 2=right 3=bottom
    std::string script;               // used by SCRIPT (program; comma = ports)
    std::string params;               // SCRIPT: "k=0.5, nome=\"abc\"" (script constants)
    mutable std::string scriptError;  // last compilation/evaluation error
    std::shared_ptr<Expr> prog;       // compiled expressions (null = invalid)
    std::string plugin;               // used by PLUGIN: type id ("delay30")
    std::string pstate;               // INITIAL state (plugin or map Scope), serialized
    std::string pstateLive;           // transient CURRENT state; used in "Save current"
    std::shared_ptr<PluginInstance> pinst; // live instance (shared by copy)
    // diagnostics (memory only; not written to the file): how many times the output
    // came out non-finite (and stayed at the previous value) or was clamped to ±NS_LIMITE
    unsigned nonFiniteCount = 0;
    unsigned limits = 0;
};

struct Edge {
    int from = -1; // output (node)
    int port = 0;  // output (port)
    int to = -1;   // input (node)
    int inPort = 0; // input (port)
};

// Visual group: a titled frame around nodes. Does not affect simulation;
// when collapsed, hides the nodes and internal wires, and external wires
// enter/exit through the frame. A node belongs to at most one group.
struct Group {
    int id = -1;
    std::string title;
    std::vector<int> nodes;
    bool collapsed = false;
    // Group frame in world coordinates. Set on creation/adjustment and
    // moved only by the bar (along with the members) or resized by the
    // handle; moving a node inside it does NOT change the frame. When collapsed, the height is
    // automatic (for the ports), but x/y/w are preserved.
    float x = 0, y = 0, w = 0, h = 0;
};

// Event source configured at runtime (plugin or --evento). The host resolves
// the node and delivers the occurrence through existing wires; the configuration is not
// written to the .nodesim.
struct EventSource {
    int node = -1;
    int port = 0;
    int kind = NS_EVENT_RISE;
    double threshold = 0.0;
    bool cli = false; // origin: --evento (replaces the plugin declaration)
};

// Optional execution services. When off, the step keeps exactly the
// unit-delay semantics of cycles (the goldens do not change).
struct StepOptions {
    bool converge = false;    // resolves algebraic loops by iteration
    double tolerance = 1e-9;  // maximum accepted delta (in output units)
    int maxIterations = 64;     // 1..10000; on reaching it, the step is marked
    bool events = false;      // detects/delivers discrete events (opt-in)
};

struct Graph {
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Group> groups;
    int nextId = 0;
    int nextGroupId = 0;

    // The compiled plan (topological order, edges per node, and step buffers) is
    // DERIVED state: graph copies (undo, paste) do not carry it.
    Graph();
    ~Graph();
    Graph(const Graph&);
    Graph& operator=(const Graph&);
    Graph(Graph&&) noexcept;
    Graph& operator=(Graph&&) noexcept;

    Node* find(int id);
    const Node* find(int id) const;
    Group* findGroup(int id);
    const Group* findGroup(int id) const;
    const Group* groupOf(int nodeId) const; // group containing the node (or nullptr)
    void pruneGroups(); // removes dead members and empty groups
    int addNode(float x, float y, NodeType t = NodeType::SCRIPT);
    bool connected(int from, int port, int to, int inPort) const;
    bool connect(int from, int port, int to, int inPort = 0); // multiple inputs; only blocks duplicates
    bool connect(int from, int to) { return connect(from, 0, to, 0); }
    bool connected(int from, int port, int to) const; // any input port
    void removeNode(int id);
    void removeInputEdge(int to);              // removes the most recent (any socket)
    void removeInputEdge(int to, int inPort);  // removes the most recent of that socket
    double inputSum(int to) const;    // sum of the current values of the sources
    double outVal(int id, int port) const; // port output (missing node = 0)
    const PortVal* portValue(int id, int port) const; // value (list/matrix/text) or null
    std::vector<int> inputIds(int to) const; // source ids of the inputs, in wire order (= in(n) order)
    std::vector<std::pair<int, int>> inputSources(int to) const; // (id, port) in in(n) order
    int edgeFan(int from, int port, int to, int inPort, int& total) const; // wire index among those of the same socket
    void removeEdge(int from, int port, int to);             // removes all sockets of the pair
    void removeEdge(int from, int port, int to, int inPort); // removes exactly
    void removeEdge(int from, int to); // removes all wires of the pair
    void step(double t, double dt);   // x[n+1] = f(x[n], neighbors)
    void step(double t, double dt, const StepOptions& opt); // same, with opt-in services
    // number of outputs driven by the plugin (numOut) + pruning. Returns true if
    // something changed (nOut or edges) — the compiled plan must be rebuilt.
    bool syncPluginOuts();
    bool hasCycle() const;            // any node outside the topological order?
    // reads the previous value (node in a cycle or downstream of one): the wire arrives "late"
    bool noDelay(int id) const;
    void touch() { invalidatePlan(); } // topology changed externally: rebuild the plan

    // Discrete events (P9.4): extra --evento sources. Sources declared
    // by plugins come from PluginInstance::eventSources and are merged on the fly;
    // a CLI source for the same (node, port) replaces the plugin's. The effective list
    // is exposed for manifest/report.
    void setEventSources(const std::vector<EventSource>& sources);
    std::vector<EventSource> eventSources() const;

    // simulation diagnostics (single policy: non-finite -> previous value)
    std::size_t nonFiniteCount = 0;       // non-finite outputs corrected (total)
    std::size_t limits = 0;          // outputs clamped to ±NS_LIMITE (total)
    std::size_t substeps = 0;        // internal plugin calls with substeps > 1
    // discrete events: occurrences generated, effective deliveries, without sink, and failures
    std::size_t eventsGenerated = 0;
    std::size_t eventsDelivered = 0;
    std::size_t eventsIgnored = 0;
    std::size_t eventsFailures = 0;
    // opt-in convergence (StepOptions::convergir): iterations and failures
    std::size_t iterationsConvergence = 0; // internal iterations executed (total)
    std::size_t convergenceSteps = 0;    // steps in which convergence was attempted
    std::size_t stepsNotConverged = 0;    // steps that reached maxIteracoes
    std::size_t nodesWithoutSnapshot = 0;        // plugins in the last step without a useful save/load
    std::size_t failuresSnapshot = 0;        // save failed (total)
    std::size_t failuresRestore = 0;        // snapshot load failed (total)
    double lastConvergenceDelta = 0;    // largest delta of the last converged step
    int noLastDelta = -1;                // node with the largest delta (or -1)
    double lastNonConvergenceTime = -1;    // time of the last non-converged step
    int firstNonFiniteNode = -1;     // id of the first node that produced a non-finite value
    double firstNonFiniteTime = -1;   // time of the first non-finite value
    void clearCounters();           // zeroes counters (reset/new file)
    bool setScript(int id, const std::string& code); // compiles; false = error in scriptError
    // estadoInicial: writes the STARTING state (plugins' pstate and node
    // values as on load, via 'base') instead of the execution's accumulated state.
    bool save(const std::string& path, bool initialState = true, const Graph* base = nullptr) const;
    // INITIAL-state blob for a plugin node: starts from 'pstate' (file) or a
    // new instance and applies the instance's live parameters/texts on top.
    // This is what 'save' writes and what "Reiniciar" (F5) reloads.
    std::string initialBlob(const Node& n, PluginManager& pm) const;
    // Zeroes the accumulated STATE (plugins and node x value), keeping parameters,
    // texts, layout, and topology. Regenerates 'pstate' from new instances
    // + the live parameters (use before saving; the live instance is not replaced).
    void resetState(PluginManager& pm);
    bool load(const std::string& path); // clears current and loads; false on failure

private:
    struct Plan;                       // compiled plan + buffers (graph.cpp)
    mutable std::unique_ptr<Plan> plan_;
    std::vector<EventSource> eventSources_; // extras from --evento (no plugins)
    void invalidatePlan() const;
    void buildPlan(Plan& p) const;
};
