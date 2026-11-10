// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Live plugin instance in the host: function pointers + exception isolation.
// Snapshots share the instance and do NOT clone the internal buffer.
#pragma once
#include "../../sdk/nodesim_plugin.h"
#include <string>
#include <vector>

struct EventDecl { // event source declared by a plugin type
    int port = 0;
    int kind = NS_EVENT_RISE;
    double threshold = 0.0;
};

struct PluginInstance {
    void* handle = nullptr;
    double (*stepFn)(void*, const NsContext*) = nullptr;
    int (*stepMultiFn)(void*, const NsContext*, double*, int) = nullptr; // may be null
    void (*destroyFn)(void*) = nullptr;
    char* (*saveFn)(void*) = nullptr;          // malloc'd, host frees with ::free (may be null)
    int (*loadFn)(void*, const char*) = nullptr; // 1 = ok (may be null)
    double (*getParamFn)(void*, int) = nullptr;  // may be null (= no params)
    int (*setParamFn)(void*, int, double) = nullptr; // 1 = ok (host already clamps)
    char* (*getTextFn)(void*, int) = nullptr;        // malloc'd string (host frees); may be null
    int (*setTextFn)(void*, int, const char*) = nullptr; // 1 = ok
    // Number of outputs controlled by the plugin (optional): numOutFn > 0 tells the host
    // to adjust node.nOut and prune edges; 0 = the host controls via the 'saídas' field.
    bool selfOut = false;
    int (*numOutFn)(void*) = nullptr;
    // TEXT output per port (optional): malloc'd string (host frees) when
    // port k contains text in this iteration, or null if the value is numeric.
    char* (*outTextFn)(void*, int) = nullptr;
    // Label per output port (optional): malloc'd string (host frees) with the name
    // of port k (e.g. CSV column name) or null (= host uses "p<k>").
    char* (*portLabelFn)(void*, int) = nullptr;
    // Label per INPUT port (optional, ABI 12): malloc'd string (host frees)
    // with the name of socket k (e.g. "y", "x1", "A") or null (= no label on the canvas).
    char* (*portLabelInFn)(void*, int) = nullptr;
    // Value published per port (ABI 10, optional): returns 1 and fills 'v'
    // (pointers valid until the next step; the host copies) or 0 = keeps the
    // previous value of that port (avoids copying large lists every step).
    // Text/list/matrix persist between steps; the numeric port continues to be
    // fed by step/stepMulti (an outValue NUM=1 overrides only in that step).
    int (*outValueFn)(void*, int, NsValue*) = nullptr;
    // Preferred node size + custom body drawing (optional).
    int prefW = 0, prefH = 0;
    void (*drawFn)(void*, const NsDraw*, const NsRect*) = nullptr;
    // Node shape (NsShape): BOX = standard box; CUSTOM = the plugin draws everything.
    int shape = NS_SHAPE_BOX;
    // Internal substeps declared by the type (optional ABI 14 extension).
    // 1 = one call per step; >1 = the host calls step/stepMulti N times with
    // dt/N (constant inputs). See NsExec in the SDK.
    int substeps = 1;
    // Discrete events (optional ABI 14 extension): sources declared by the
    // type and sink callback. The host uses them only with the service enabled.
    std::vector<EventDecl> eventSources;
    void (*onEventFn)(void*, const NsEvent*) = nullptr;

    // Diagnostics/exception isolation (ABI 13 in the host). If a call
    // throws, the instance becomes "failed" and subsequent calls are ignored.
    bool failed = false;
    std::string failure;
    std::string pluginName;

    void fail(const char* what);
    double callStep(void* h, const NsContext* c);
    int callStepMulti(void* h, const NsContext* c, double* out, int n);
    char* callSave(void* h);
    int callLoad(void* h, const char* blob);
    double callGetParam(void* h, int i);
    int callSetParam(void* h, int i, double v);
    char* callGetText(void* h, int i);
    int callSetText(void* h, int i, const char* s);
    int callNumOut(void* h);
    char* callOutText(void* h, int p);
    char* callPortLabel(void* h, int p);
    char* callPortLabelIn(void* h, int p);
    int callOutValue(void* h, int p, NsValue* v);
    void callDraw(void* h, const NsDraw* g, const NsRect* area);
    void callEvent(void* h, const NsEvent* ev);
};
