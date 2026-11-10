// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// Internal structures shared by the graph plan and step.
#pragma once
#include "graph.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

// wire value: number + text/list/matrix that travels with it (ABI 10)
struct EdgeVal { PortVal pv; const std::string* txt = nullptr; };

// Holds the topological order (same discipline as the old Kahn: LIFO stack, sources
// in index order), the "in flow" set (outside a cycle), the output edges
// (CSR, for Kahn) and input edges (CSR sorted by socket, for the step), plus the
// buffers reused every step.
struct Graph::Plan {
    bool valid = false;
    bool cycle = false;
    std::vector<size_t> order;              // indices, partial topological order
    std::vector<char> noStream;              // 1 = evaluated in this step (outside a cycle)
    std::vector<size_t> outOff, outTo;      // output CSR
    std::vector<size_t> inOff, inFrom;      // input CSR (per node, per socket)
    std::vector<int> inSrcPort, inInPort;
    std::unordered_map<int, size_t> idx;    // node id -> position in nodes

    // step buffers (reused; avoid allocating per node and per step)
    std::vector<std::array<double, Node::MAX_PORTS>> old;
    std::vector<std::array<PortVal, Node::MAX_PORTS>> oldPv;
    std::vector<std::array<std::string, Node::MAX_PORTS>> oldTxt;
    std::vector<double> ins, sockSum;
    std::vector<int> sockOff;
    std::vector<EdgeVal> pick;
    std::vector<char> hasPick;
    std::vector<NsValue> nsv;
    // opt-in convergence: plugin state at the start of the step (restored
    // before each iteration, so the loop resolves a single step)
    std::vector<std::string> convState;
    std::vector<char> convStateOk;
    // discrete events (opt-in): queue generated in the previous step, baseline per
    // (node, port), and events delivered in this step per socket (for SCRIPT).
    struct PendEvent {
        int from = -1, port = 0, to = -1, inPort = 0;
        int kind = NS_EVENT_RISE;
        double t = 0.0, value = 0.0, previous = 0.0;
    };
    std::vector<PendEvent> pending;
    std::vector<std::array<double, Node::MAX_PORTS>> evtPrev;
    std::vector<std::array<char, Node::MAX_PORTS>> evtPrevOk;
    std::vector<std::array<int, Node::MAX_PORTS>> evtCount, evtKind;
    std::vector<std::array<double, Node::MAX_PORTS>> evtT;
    bool eventsActiveBefore = false;
};

