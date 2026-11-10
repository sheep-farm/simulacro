// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated PluginInstance calls: exceptions become an instance failure.

#include "manager.h"
#include <cstdio>

void PluginInstance::fail(const char* what) {
    if (failed) return;
    failed = true;
    failure = what ? what : "exception";
    std::fprintf(stderr, "plugins: %s: %s threw an exception; instance disabled\n",
                 pluginName.empty() ? "?" : pluginName.c_str(), failure.c_str());
}

double PluginInstance::callStep(void* h, const NsContext* c) {
    if (failed || !stepFn || !h) return 0.0;
    try { return stepFn(h, c); }
    catch (...) { fail("step"); return 0.0; }
}

int PluginInstance::callStepMulti(void* h, const NsContext* c, double* out, int n) {
    if (failed || !stepMultiFn || !h) return 0;
    try { return stepMultiFn(h, c, out, n); }
    catch (...) { fail("stepMulti"); return 0; }
}

char* PluginInstance::callSave(void* h) {
    if (failed || !saveFn || !h) return nullptr;
    try { return saveFn(h); }
    catch (...) { fail("save"); return nullptr; }
}

int PluginInstance::callLoad(void* h, const char* blob) {
    if (failed || !loadFn || !h) return 0;
    try { return loadFn(h, blob); }
    catch (...) { fail("load"); return 0; }
}

double PluginInstance::callGetParam(void* h, int i) {
    if (failed || !getParamFn || !h) return 0.0;
    try { return getParamFn(h, i); }
    catch (...) { fail("getParam"); return 0.0; }
}

int PluginInstance::callSetParam(void* h, int i, double v) {
    if (failed || !setParamFn || !h) return 0;
    try { return setParamFn(h, i, v); }
    catch (...) { fail("setParam"); return 0; }
}

char* PluginInstance::callGetText(void* h, int i) {
    if (failed || !getTextFn || !h) return nullptr;
    try { return getTextFn(h, i); }
    catch (...) { fail("getText"); return nullptr; }
}

int PluginInstance::callSetText(void* h, int i, const char* s) {
    if (failed || !setTextFn || !h) return 0;
    try { return setTextFn(h, i, s); }
    catch (...) { fail("setText"); return 0; }
}

int PluginInstance::callNumOut(void* h) {
    if (failed || !numOutFn || !h) return 0;
    try { return numOutFn(h); }
    catch (...) { fail("numOut"); return 0; }
}

char* PluginInstance::callOutText(void* h, int p) {
    if (failed || !outTextFn || !h) return nullptr;
    try { return outTextFn(h, p); }
    catch (...) { fail("outText"); return nullptr; }
}

char* PluginInstance::callPortLabel(void* h, int p) {
    if (failed || !portLabelFn || !h) return nullptr;
    try { return portLabelFn(h, p); }
    catch (...) { fail("portLabel"); return nullptr; }
}

char* PluginInstance::callPortLabelIn(void* h, int p) {
    if (failed || !portLabelInFn || !h) return nullptr;
    try { return portLabelInFn(h, p); }
    catch (...) { fail("portLabelIn"); return nullptr; }
}

int PluginInstance::callOutValue(void* h, int p, NsValue* v) {
    if (failed || !outValueFn || !h) return 0;
    try { return outValueFn(h, p, v); }
    catch (...) { fail("outValue"); return 0; }
}

void PluginInstance::callDraw(void* h, const NsDraw* g, const NsRect* area) {
    if (failed || !drawFn || !h) return;
    try { drawFn(h, g, area); }
    catch (...) { fail("draw"); }
}

void PluginInstance::callEvent(void* h, const NsEvent* ev) {
    if (failed || !onEventFn || !h || !ev) return;
    try { onEventFn(h, ev); }
    catch (...) { fail("on_event"); }
}

std::shared_ptr<PluginInstance> PluginManager::createInstance(const std::string& id) const {
    const RegType* t = find(id);
    if (!t) return nullptr;
    void* h = t->create();
    if (!h) return nullptr;
    auto destroy = [d = t->destroy](PluginInstance* p) {
        if (p) { if (d && p->handle) d(p->handle); delete p; }
    };
    std::shared_ptr<PluginInstance> pi(new PluginInstance(), destroy);
    pi->handle = h;
    pi->stepFn = t->step;
    pi->stepMultiFn = t->stepMulti;
    pi->destroyFn = t->destroy;
    pi->saveFn = t->save;
    pi->loadFn = t->load;
    pi->getParamFn = t->getParam;
    pi->setParamFn = t->setParam;
    pi->getTextFn = t->getText;
    pi->setTextFn = t->setText;
    pi->selfOut = t->selfOut;
    pi->numOutFn = t->numOut;
    pi->outTextFn = t->outText;
    pi->portLabelFn = t->portLabel;
    pi->portLabelInFn = t->portLabelIn;
    pi->outValueFn = t->outValue;
    pi->prefW = t->prefW;
    pi->prefH = t->prefH;
    pi->drawFn = t->draw;
    pi->shape = t->shape;
    pi->substeps = t->substeps;
    pi->eventSources = t->events;
    pi->onEventFn = t->onEvent;
    pi->pluginName = (t->lib >= 0 && t->lib < (int)libs.size()) ? libs[(size_t)t->lib].name : t->id;
    return pi;
}

