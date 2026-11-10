// nodesim — graph node editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Registry: metadata lookup and validation of the tables declared by a .so.
#include "registry.h"
#include "../compat.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr int kKnownCaps = NS_CAP_STEP_MULTI | NS_CAP_STATE | NS_CAP_PARAMS |
                            NS_CAP_TEXTS | NS_CAP_DYNAMIC_OUT | NS_CAP_TEXT_OUT |
                            NS_CAP_VALUE_OUT | NS_CAP_CUSTOM_DRAW | NS_CAP_INPUT_LABELS |
                            NS_CAP_PAUSE | NS_CAP_PAUSE_MS;

} // namespace

void PluginRegistry::addProblem(const std::string& file, const std::string& message) {
    problems.push_back({file, message});
    if (!quiet) std::printf("plugins: %s: %s\n", file.c_str(), message.c_str());
}

const PluginRegistry::RegType* PluginRegistry::find(const std::string& id) const {
    const std::string canon = compat::canonicalTypeId(id);
    for (const auto& t : types) if (t.id == canon) return &t;
    return nullptr;
}

bool PluginRegistry::colorOf(const std::string& id, float rgb[3]) const {
    if (const RegType* t = find(id)) {
        rgb[0] = t->color[0]; rgb[1] = t->color[1]; rgb[2] = t->color[2];
        return true;
    }
    return false;
}

namespace plugin_registry {

int addLibrary(PluginRegistry& reg, const std::string& path, void* handle,
               const NsPlugin* p) {
    // Only read fields beyond ABI 13 when the plugin declared a valid
    // struct_size; struct_size == 0 means an old struct.
    const bool hasMeta = p->struct_size != 0;
    PluginRegistry::Lib lib;
    lib.h = handle;
    lib.file = path;
    lib.name = p->name ? p->name : "?";
    lib.version = p->version ? p->version : "?";
    lib.api = p->api;
    lib.structSize = p->struct_size;
    lib.capabilities = hasMeta ? p->capabilities : 0;
    lib.author = hasMeta && p->author ? p->author : "";
    lib.license = hasMeta && p->license ? p->license : "";
    lib.homepage = hasMeta && p->homepage ? p->homepage : "";
    lib.description = hasMeta && p->description ? p->description : "";
    lib.build = hasMeta && p->build ? p->build : "";
    reg.libs.push_back(std::move(lib));
    return (int)reg.libs.size() - 1;
}

int registerTypes(PluginRegistry& reg, size_t libIndex, const std::string& path,
                  const NsPlugin* p) {
    if (p->num_types <= 0 || p->num_types > 1024 || !p->types) {
        reg.addProblem(path, "invalid type table");
        return 0;
    }
    int n = 0;
    for (int i = 0; i < p->num_types; ++i) {
        const NsNodeType& t = p->types[i];
        if (!t.id || !*t.id || !t.create || !t.destroy || (!t.step && !t.stepMulti)) {
            reg.addProblem(path, "invalid type ignored" +
                                     std::string(t.id ? std::string(": ") + t.id : ""));
            continue;
        }
        if (reg.find(t.id)) {
            reg.addProblem(path, std::string("duplicate id ignored: ") + t.id);
            continue;
        }
        PluginRegistry::RegType r;
        r.id = t.id;
        r.label = t.label ? t.label : t.id;
        r.lib = (int)libIndex;
        r.color[0] = t.color[0]; r.color[1] = t.color[1]; r.color[2] = t.color[2];
        r.create = t.create; r.destroy = t.destroy; r.step = t.step;
        r.stepMulti = t.stepMulti;
        r.save = t.save; r.load = t.load;
        if (t.num_params < 0 || t.num_params > 256) {
            reg.addProblem(path, std::string("'") + t.id + "' with invalid num_params");
        } else if (t.num_params > 0) { // params require a table + get/set
            if (!t.params || !t.getParam || !t.setParam) {
                reg.addProblem(path, std::string("'") + t.id +
                                         "' with incomplete params (ignored)");
            } else {
                for (int k = 0; k < t.num_params; ++k) {
                    const NsParam& np = t.params[k];
                    if (!np.name || !std::isfinite(np.min) || !std::isfinite(np.max) ||
                        np.min > np.max) {
                        reg.addProblem(path, std::string("'") + t.id + "' param " +
                                                 std::to_string(k) + " invalid (ignored)");
                        continue;
                    }
                    PluginRegistry::Param p;
                    p.name = np.name;
                    p.min = np.min;
                    p.max = np.max;
                    p.def = np.def;
                    p.kind = (np.kind >= NS_PARAM_NUM && np.kind <= NS_PARAM_INT)
                                 ? np.kind : NS_PARAM_NUM;
                    p.step = np.step > 0 && std::isfinite(np.step) ? np.step : 0;
                    if (np.unit) p.unit = np.unit;
                    if (np.help) p.help = np.help;
                    if (p.kind == NS_PARAM_ENUM) {
                        if (np.num_choices <= 0 || !np.choices) {
                            reg.addProblem(path, std::string("'") + t.id + "' param '" +
                                                     np.name + "' ENUM without choices (becomes a number)");
                            p.kind = NS_PARAM_NUM;
                        } else {
                            for (int c = 0; c < np.num_choices; ++c)
                                if (np.choices[c]) p.choices.push_back(np.choices[c]);
                            if (p.choices.empty()) {
                                reg.addProblem(path, std::string("'") + t.id + "' param '" +
                                                         np.name + "' ENUM without labels (becomes a number)");
                                p.kind = NS_PARAM_NUM;
                            } else { // ENUM is an index: the host normalizes the range
                                p.min = 0;
                                p.max = (double)p.choices.size() - 1;
                            }
                        }
                    }
                    if (p.kind == NS_PARAM_BOOL) {
                        p.min = 0;
                        p.max = 1;
                    }
                    if (p.kind == NS_PARAM_INT || p.kind == NS_PARAM_BOOL) p.step = 1;
                    if (p.def < p.min) p.def = p.min;
                    if (p.def > p.max) p.def = p.max;
                    if (p.kind == NS_PARAM_INT || p.kind == NS_PARAM_ENUM)
                        p.def = std::floor(p.def + 0.5);
                    r.params.push_back(std::move(p));
                }
                r.getParam = t.getParam;
                r.setParam = t.setParam;
            }
        }
        if (t.num_text < 0 || t.num_text > 256) {
            reg.addProblem(path, std::string("'") + t.id + "' with invalid num_text");
        } else if (t.num_text > 0) { // texts require a name table + get/set
            bool ok = t.text_names && t.getText && t.setText;
            for (int k = 0; ok && k < t.num_text; ++k)
                if (!t.text_names[k]) ok = false;
            if (!ok) {
                reg.addProblem(path, std::string("'") + t.id +
                                         "' with incomplete texts (ignored)");
            } else {
                for (int k = 0; k < t.num_text; ++k) r.texts.push_back(t.text_names[k]);
                r.getText = t.getText;
                r.setText = t.setText;
            }
        }
        if (t.self_out) { // plugin controls the number of outputs
            if (t.numOut) { r.selfOut = true; r.numOut = t.numOut; }
            else reg.addProblem(path, std::string("'") + t.id + "' self_out without numOut (ignored)");
        }
        r.outText = t.outText;
        r.portLabel = t.portLabel;
        r.portLabelIn = t.portLabelIn;
        r.outValue = t.outValue;
        r.group = p->group ? p->group : "";
        r.defIn = t.def_in > 0 ? (t.def_in > NS_MAX_PORTS ? NS_MAX_PORTS : t.def_in) : 1;
        r.prefW = t.pref_w > 0 ? (t.pref_w < 48 ? 48 : (t.pref_w > 480 ? 480 : t.pref_w)) : 0;
        r.prefH = t.pref_h > 0 ? (t.pref_h < 32 ? 32 : (t.pref_h > 480 ? 480 : t.pref_h)) : 0;
        r.draw = t.draw;
        r.shape = (t.shape == NS_SHAPE_CUSTOM && t.draw) ? NS_SHAPE_CUSTOM : NS_SHAPE_BOX;
        // Capabilities: inferred from the pointers + declared by the plugin
        // (known bits; future bits are ignored).
        int caps = t.capabilities & kKnownCaps;
        if (t.stepMulti) caps |= NS_CAP_STEP_MULTI;
        if (t.save || t.load) caps |= NS_CAP_STATE;
        if (!r.params.empty()) caps |= NS_CAP_PARAMS;
        if (!r.texts.empty()) caps |= NS_CAP_TEXTS;
        if (r.selfOut) caps |= NS_CAP_DYNAMIC_OUT;
        if (r.outText) caps |= NS_CAP_TEXT_OUT;
        if (r.outValue) caps |= NS_CAP_VALUE_OUT;
        if (r.draw) caps |= NS_CAP_CUSTOM_DRAW;
        if (r.portLabelIn) caps |= NS_CAP_INPUT_LABELS;
        r.capabilities = caps;
        reg.types.push_back(std::move(r));
        ++n;
    }
    return n;
}

} // namespace plugin_registry
