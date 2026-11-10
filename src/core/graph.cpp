// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "graph.h"
#include "graph_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace {
// wire value: number + text/list/matrix that travels with it (ABI 10)
struct EdgeVal { PortVal pv; const std::string* txt = nullptr; };
} // namespace

Graph::Graph() = default;
Graph::~Graph() = default;
Graph::Graph(const Graph& o)
    : nodes(o.nodes), edges(o.edges), nextId(o.nextId), eventSources_(o.eventSources_) {}
Graph& Graph::operator=(const Graph& o) {
    if (this != &o) {
        nodes = o.nodes; edges = o.edges; nextId = o.nextId;
        eventSources_ = o.eventSources_;
        plan_.reset();
    }
    return *this;
}
Graph::Graph(Graph&&) noexcept = default;
Graph& Graph::operator=(Graph&&) noexcept = default;

void Graph::invalidatePlan() const { if (plan_) plan_->valid = false; }

void Graph::buildPlan(Plan& p) const {
    const size_t n = nodes.size();
    p.order.clear();
    p.noStream.assign(n, 0);
    p.outOff.assign(n + 1, 0);
    p.outTo.clear();
    p.inOff.assign(n + 1, 0);
    p.inFrom.clear(); p.inSrcPort.clear(); p.inInPort.clear();

    std::unordered_map<int, size_t>& idx = p.idx;
    idx.clear();
    idx.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) idx[nodes[i].id] = i;

    // output (one entry per edge, including parallel ones)
    for (auto& e : edges) {
        auto a = idx.find(e.from), b = idx.find(e.to);
        if (a == idx.end() || b == idx.end()) continue;
        ++p.outOff[a->second + 1];
    }
    for (size_t i = 0; i < n; ++i) p.outOff[i + 1] += p.outOff[i];
    p.outTo.assign(p.outOff[n], 0);
    {
        std::vector<size_t> cur(p.outOff.begin(), p.outOff.end() - 1);
        for (auto& e : edges) {
            auto a = idx.find(e.from), b = idx.find(e.to);
            if (a == idx.end() || b == idx.end()) continue;
            p.outTo[cur[a->second]++] = b->second;
        }
    }
    // input: per node and, within the node, per socket (connection order preserved)
    for (auto& e : edges) {
        auto a = idx.find(e.from), b = idx.find(e.to);
        if (a == idx.end() || b == idx.end()) continue;
        ++p.inOff[b->second + 1];
    }
    for (size_t i = 0; i < n; ++i) p.inOff[i + 1] += p.inOff[i];
    const size_t ni = p.inOff[n];
    p.inFrom.assign(ni, 0); p.inSrcPort.assign(ni, 0); p.inInPort.assign(ni, 0);
    {
        std::vector<size_t> cur(p.inOff.begin(), p.inOff.end() - 1);
        std::vector<size_t> tmpFrom(ni, 0), tmpPort(ni, 0);
        std::vector<int> tmpSrc(ni, 0), tmpIn(ni, 0);
        for (auto& e : edges) {
            auto a = idx.find(e.from), b = idx.find(e.to);
            if (a == idx.end() || b == idx.end()) continue;
            size_t k = cur[b->second]++;
            tmpFrom[k] = a->second; tmpSrc[k] = e.port; tmpIn[k] = e.inPort;
        }
        // sorts each slice by socket, stable (within a socket: wire order)
        for (size_t i = 0; i < n; ++i) {
            size_t a = p.inOff[i], b = p.inOff[i + 1];
            std::vector<size_t> ord(b - a);
            for (size_t k = 0; k < ord.size(); ++k) ord[k] = a + k;
            std::stable_sort(ord.begin(), ord.end(), [&](size_t x, size_t y) {
                return tmpIn[x] < tmpIn[y];
            });
            for (size_t k = 0; k < ord.size(); ++k) {
                p.inFrom[a + k] = tmpFrom[ord[k]];
                p.inSrcPort[a + k] = tmpSrc[ord[k]];
                p.inInPort[a + k] = tmpIn[ord[k]];
            }
        }
    }
    // Kahn (identical to the previous one: sources in index order, LIFO stack)
    std::vector<int> indeg(n, 0);
    for (auto& e : edges) {
        auto a = idx.find(e.from), b = idx.find(e.to);
        if (a == idx.end() || b == idx.end()) continue;
        ++indeg[b->second];
    }
    std::vector<size_t> stack;
    stack.reserve(n);
    for (size_t i = 0; i < n; ++i) if (indeg[i] == 0) stack.push_back(i);
    p.order.reserve(n);
    while (!stack.empty()) {
        size_t u = stack.back(); stack.pop_back();
        p.order.push_back(u);
        p.noStream[u] = 1;
        for (size_t k = p.outOff[u]; k < p.outOff[u + 1]; ++k) {
            size_t v = p.outTo[k];
            if (--indeg[v] == 0) stack.push_back(v);
        }
    }
    p.cycle = p.order.size() < n;
    p.valid = true;
}

bool Graph::hasCycle() const {
    if (!plan_) plan_.reset(new Plan());
    if (!plan_->valid) buildPlan(*plan_);
    return plan_->cycle;
}

bool Graph::noDelay(int id) const {
    if (!plan_) plan_.reset(new Plan());
    if (!plan_->valid) buildPlan(*plan_);
    auto it = plan_->idx.find(id);
    return it != plan_->idx.end() && !plan_->noStream[it->second];
}

void Graph::setEventSources(const std::vector<EventSource>& sources) {
    eventSources_ = sources;
    if (!plan_) return;
    plan_->pending.clear();
    plan_->evtPrevOk.clear();
    plan_->eventsActiveBefore = false;
}

std::vector<EventSource> Graph::eventSources() const {
    std::vector<EventSource> out;
    // sources declared by plugins, one per instantiated node of the type
    for (const Node& nd : nodes) {
        if (nd.type != NodeType::PLUGIN || !nd.pinst) continue;
        for (const EventDecl& d : nd.pinst->eventSources) {
            EventSource f;
            f.node = nd.id;
            f.port = d.port;
            f.kind = d.kind;
            f.threshold = d.threshold;
            f.cli = false;
            out.push_back(f);
        }
    }
    // --evento replaces the plugin declaration for the same (node, port)
    for (const EventSource& extra : eventSources_) {
        auto it = std::find_if(out.begin(), out.end(), [&](const EventSource& f) {
            return f.node == extra.node && f.port == extra.port;
        });
        if (it != out.end()) *it = extra;
        else out.push_back(extra);
    }
    // stable order regardless of load/--evento order: node id and port
    std::sort(out.begin(), out.end(), [](const EventSource& a, const EventSource& b) {
        return a.node != b.node ? a.node < b.node : a.port < b.port;
    });
    return out;
}

void Graph::clearCounters() {
    nonFiniteCount = 0;
    limits = 0;
    substeps = 0;
    eventsGenerated = 0;
    eventsDelivered = 0;
    eventsIgnored = 0;
    eventsFailures = 0;
    iterationsConvergence = 0;
    convergenceSteps = 0;
    stepsNotConverged = 0;
    nodesWithoutSnapshot = 0;
    failuresSnapshot = 0;
    failuresRestore = 0;
    lastConvergenceDelta = 0;
    noLastDelta = -1;
    lastNonConvergenceTime = -1;
    firstNonFiniteNode = -1;
    firstNonFiniteTime = -1;
    if (plan_) {
        plan_->pending.clear();
        plan_->evtPrevOk.clear();
        plan_->eventsActiveBefore = false;
    }
    for (auto& n : nodes) { n.nonFiniteCount = 0; n.limits = 0; }
}

Node* Graph::find(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const Node* Graph::find(int id) const {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
int Graph::addNode(float x, float y, NodeType t) {
    invalidatePlan();
    Node n;
    n.id = nextId++;
    n.pos = {x, y};
    n.type = t;
    if (t == NodeType::SCOPE) { // sink: inputs only (channels); no outputs
        n.nIn = 2;
        n.nOut = 0;
        n.params = "type=line, window=5";
    } else if (t == NodeType::MUX) { // joins (N->1) by default; the panel switches it to splitter
        n.nIn = 2;
        n.nOut = 1;
    }
    nodes.push_back(n);
    if (t == NodeType::SCRIPT) setScript(n.id, "sin(t*2)");
    return n.id;
}
bool Graph::connected(int from, int port, int to, int inPort) const {
    for (auto& e : edges) if (e.from == from && e.port == port && e.to == to && e.inPort == inPort) return true;
    return false;
}
bool Graph::connected(int from, int port, int to) const {
    for (auto& e : edges) if (e.from == from && e.port == port && e.to == to) return true;
    return false;
}
bool Graph::connect(int from, int port, int to, int inPort) {
    invalidatePlan();
    if (from == to || connected(from, port, to, inPort)) return false;
    const Node* a = find(from);
    const Node* b = find(to);
    if (!a || !b) return false;
    if (port < 0 || port >= a->nOut) return false;
    if (inPort < 0 || inPort >= (b->nIn < 1 ? 1 : b->nIn)) return false;
    edges.push_back({from, port, to, inPort});
    return true;
}
void Graph::removeNode(int id) {
    invalidatePlan();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
        [&](const Node& n){ return n.id == id; }), nodes.end());
    edges.erase(std::remove_if(edges.begin(), edges.end(),
        [&](const Edge& e){ return e.from == id || e.to == id; }), edges.end());
    pruneGroups();
}
Group* Graph::findGroup(int id) {
    for (auto& gr : groups) if (gr.id == id) return &gr;
    return nullptr;
}
const Group* Graph::findGroup(int id) const {
    for (auto& gr : groups) if (gr.id == id) return &gr;
    return nullptr;
}
const Group* Graph::groupOf(int nodeId) const {
    for (auto& gr : groups)
        if (std::find(gr.nodes.begin(), gr.nodes.end(), nodeId) != gr.nodes.end()) return &gr;
    return nullptr;
}
void Graph::pruneGroups() {
    for (auto& gr : groups)
        gr.nodes.erase(std::remove_if(gr.nodes.begin(), gr.nodes.end(),
            [&](int id){ return find(id) == nullptr; }), gr.nodes.end());
    groups.erase(std::remove_if(groups.begin(), groups.end(),
        [](const Group& gr){ return gr.nodes.empty(); }), groups.end());
}
void Graph::removeInputEdge(int to) {
    invalidatePlan();
    for (int i = (int)edges.size() - 1; i >= 0; --i)
        if (edges[i].to == to) { edges.erase(edges.begin() + i); break; }
}
void Graph::removeInputEdge(int to, int inPort) {
    invalidatePlan();
    for (int i = (int)edges.size() - 1; i >= 0; --i)
        if (edges[i].to == to && edges[i].inPort == inPort) { edges.erase(edges.begin() + i); break; }
}
double Graph::inputSum(int to) const {
    double sum = 0;
    for (auto& e : edges) if (e.to == to) sum += outVal(e.from, e.port);
    return sum;
}
double Graph::outVal(int id, int port) const {
    const Node* n = find(id);
    if (!n) return 0;
    if (port < 0) port = 0;
    if (port >= Node::MAX_PORTS) port = Node::MAX_PORTS - 1;
    return n->out[port];
}
const PortVal* Graph::portValue(int id, int port) const {
    const Node* n = find(id);
    if (!n) return nullptr;
    if (port < 0) port = 0;
    if (port >= Node::MAX_PORTS) port = Node::MAX_PORTS - 1;
    return &n->pv[port];
}
std::vector<int> Graph::inputIds(int to) const {
    std::vector<int> ids;
    for (auto& e : edges) if (e.to == to) ids.push_back(e.from);
    return ids;
}
std::vector<std::pair<int, int>> Graph::inputSources(int to) const {
    std::vector<std::pair<int, int>> v;
    const Node* n = find(to);
    int nin = n ? (n->nIn < 1 ? 1 : n->nIn) : 1;
    for (int p = 0; p < nin; ++p) // in(i) order: sockets first, then the wires of each socket
        for (auto& e : edges) if (e.to == to && e.inPort == p) v.push_back({e.from, e.port});
    for (auto& e : edges) if (e.to == to && e.inPort >= nin) v.push_back({e.from, e.port});
    return v;
}
int Graph::edgeFan(int from, int port, int to, int inPort, int& total) const {
    int order = 0;
    total = 0;
    for (auto& e : edges) if (e.to == to && e.inPort == inPort) {
        if (e.from == from && e.port == port) order = total; // unique triple in the socket
        ++total;
    }
    return order;
}
void Graph::removeEdge(int from, int port, int to) {
    invalidatePlan();
    edges.erase(std::remove_if(edges.begin(), edges.end(),
        [&](const Edge& e){ return e.from == from && e.port == port && e.to == to; }), edges.end());
}
void Graph::removeEdge(int from, int port, int to, int inPort) {
    invalidatePlan();
    edges.erase(std::remove_if(edges.begin(), edges.end(),
        [&](const Edge& e){ return e.from == from && e.port == port && e.to == to && e.inPort == inPort; }), edges.end());
}
void Graph::removeEdge(int from, int to) {
    invalidatePlan();
    edges.erase(std::remove_if(edges.begin(), edges.end(),
        [&](const Edge& e){ return e.from == from && e.to == to; }), edges.end());
}
bool Graph::syncPluginOuts() {
    // Plugins that drive their own number of outputs (e.g. CSV with a column list):
    // queries numOut() and adjusts node.nOut, pruning edges from removed ports.
    bool changed = false;
    for (auto& n : nodes) {
        if (n.type != NodeType::PLUGIN || !n.pinst || !n.pinst->selfOut || !n.pinst->numOutFn) continue;
        int want = n.pinst->callNumOut(n.pinst->handle);
        if (want <= 0) continue;                       // 0 = host controls via the 'saídas' field
        if (want > Node::MAX_PORTS) want = Node::MAX_PORTS;
        if (want == n.nOut) continue;
        n.nOut = want;
        edges.erase(std::remove_if(edges.begin(), edges.end(),
            [id = n.id, want](const Edge& e){ return e.from == id && e.port >= want; }), edges.end());
        changed = true;
    }
    return changed;
}
