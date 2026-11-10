// simulacro — native wxWidgets property editor
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "properties.h"
#include "i18n.h"
#include "numfmt.h"

#include <wx/propgrid/advprops.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
wxString tr(const char* s) { return wxString::FromUTF8(i18n::text(s)); }
}

PropertiesPanel::PropertiesPanel(wxWindow* parent, Engine* engine)
    : wxPanel(parent), e_(engine) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    pg_ = new wxPropertyGrid(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxPG_DEFAULT_STYLE | wxPG_SPLITTER_AUTO_CENTER);
    pg_->SetExtraStyle(wxPG_EX_HELP_AS_TOOLTIPS | wxPG_EX_MODE_BUTTONS);
    sizer->Add(pg_, 1, wxEXPAND);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    editBtn_ = new wxButton(this, wxID_ANY, tr("Edit script…"));
    scopeBtn_ = new wxButton(this, wxID_ANY, tr("Open Scope"));
    row->Add(editBtn_, 1, wxRIGHT, 4);
    row->Add(scopeBtn_, 1);
    sizer->Add(row, 0, wxEXPAND | wxALL, 4);
    SetSizer(sizer);

    editBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (nodeId_ >= 0 && onEditScript) onEditScript(nodeId_);
    });
    scopeBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (nodeId_ >= 0 && onOpenScope) onOpenScope(nodeId_);
    });
    pg_->Bind(wxEVT_PG_CHANGED, &PropertiesPanel::onChangedEvent, this);

    SetBackgroundColour(wxColour(kUi.panel.r, kUi.panel.g, kUi.panel.b));
    pg_->SetBackgroundColour(wxColour(kUi.panel.r, kUi.panel.g, kUi.panel.b));
    editBtn_->Enable(false);
    scopeBtn_->Enable(false);
    rebuild();
}

// Internal, always-unique property names. The visible label stays as the
// user's text (which can repeat across node and plugin parameters, e.g.
// "type", "x"); only the hidden name must be unique or wxPropertyGrid warns.
wxString PropertiesPanel::pgName() {
    return wxString::Format("#p%d", ++pgSeq_);
}

void PropertiesPanel::addProp(wxPGProperty* p, std::function<void()> apply,
                              const wxString& help) {
    // p must have been added with pg_->Append() already — appending it here
    // again would register the same object twice and corrupt the grid.
    if (!help.empty()) p->SetHelpString(help);
    apply_[p] = std::move(apply);
}

void PropertiesPanel::setNode(int id) {
    nodeId_ = id;
    rebuild();
}

void PropertiesPanel::applyTheme() {
    SetBackgroundColour(wxColour(kUi.panel.r, kUi.panel.g, kUi.panel.b));
    SetForegroundColour(wxColour(kUi.text.r, kUi.text.g, kUi.text.b));
    if (pg_) {
        pg_->SetBackgroundColour(wxColour(kUi.panel.r, kUi.panel.g, kUi.panel.b));
        pg_->SetForegroundColour(wxColour(kUi.text.r, kUi.text.g, kUi.text.b));
        pg_->Refresh();
    }
    for (wxWindow* w : GetChildren()) {
        w->SetBackgroundColour(wxColour(kUi.panel.r, kUi.panel.g, kUi.panel.b));
        w->SetForegroundColour(wxColour(kUi.text.r, kUi.text.g, kUi.text.b));
        w->Refresh();
    }
    Refresh();
}

void PropertiesPanel::rebuild() {
    updating_ = true;
    apply_.clear();
    pg_->Clear();
    Node* n = nodeId_ >= 0 ? e_->g.find(nodeId_) : nullptr;
    if (!n) {
        nodeId_ = -1;
        pg_->Append(new wxPropertyCategory(tr("SCENE")));
        auto* hint = pg_->Append(new wxStringProperty(tr("no selection"), pgName(), ""));
        hint->ChangeFlag(wxPG_PROP_READONLY, true);
        editBtn_->Enable(false);
        scopeBtn_->Enable(false);
        updating_ = false;
        return;
    }
    addNodeCategory();
    if (n->type == NodeType::SCRIPT) addScriptCategory();
    if (n->type == NodeType::PLUGIN) addPluginCategory();
    if (n->type == NodeType::SCOPE) addScopeCategory();
    editBtn_->Enable(n->type == NodeType::SCRIPT);
    scopeBtn_->Enable(n->type == NodeType::SCOPE);
    updating_ = false;
}

void PropertiesPanel::commit() {
    e_->dirty = true;
    if (onChanged) onChanged();
}

void PropertiesPanel::onChangedEvent(wxPropertyGridEvent& ev) {
    if (updating_) return;
    wxPGProperty* p = ev.GetProperty();
    auto it = apply_.find(p);
    if (it == apply_.end()) return;
    it->second();
}

void PropertiesPanel::addNodeCategory() {
    Node* n = e_->g.find(nodeId_);
    if (!n) return;
    pg_->Append(new wxPropertyCategory(tr("IDENTITY")));
    auto* idp = pg_->Append(new wxStringProperty(tr("id"), pgName(),
                                                 wxString::Format("%d", n->id)));
    idp->ChangeFlag(wxPG_PROP_READONLY, true);
    auto* typep = pg_->Append(new wxStringProperty(
        tr("type"), pgName(), wxString::FromUTF8(nodeTypeName(n->type))));
    typep->ChangeFlag(wxPG_PROP_READONLY, true);

    auto* title = pg_->Append(new wxStringProperty(tr("title"), pgName(),
                                                   wxString::FromUTF8(n->title)));
    addProp(title, [this, title] {
        Node* nd = e_->g.find(nodeId_);
        if (!nd) return;
        nd->title = pg_->GetPropertyValueAsString(title).utf8_string();
        commit();
    });

    pg_->Append(new wxPropertyCategory(tr("TRANSFORMATION")));
    auto* x = pg_->Append(new wxFloatProperty(tr("x"), pgName(), n->pos.x));
    addProp(x, [this, x] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->pos.x = (float)pg_->GetPropertyValueAsDouble(x);
        commit();
    });
    auto* y = pg_->Append(new wxFloatProperty(tr("y"), pgName(), n->pos.y));
    addProp(y, [this, y] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->pos.y = (float)pg_->GetPropertyValueAsDouble(y);
        commit();
    });

    pg_->Append(new wxPropertyCategory(tr("INPUT/OUTPUT")));
    auto* nin = pg_->Append(new wxIntProperty(tr("inputs"), pgName(), n->nIn));
    nin->SetAttribute(wxPG_ATTR_MIN, 1);
    nin->SetAttribute(wxPG_ATTR_MAX, 8);
    nin->SetHelpString(tr("number of input sockets"));
    addProp(nin, [this, nin] {
        e_->setInputs(nodeId_, (int)pg_->GetPropertyValueAsLong(nin));
        commit();
        CallAfter([this] { rebuild(); });
    });
    if (n->type != NodeType::SCOPE) {
        auto* nout = pg_->Append(new wxIntProperty(tr("outputs"), pgName(), n->nOut));
        nout->SetAttribute(wxPG_ATTR_MIN, 1);
        nout->SetAttribute(wxPG_ATTR_MAX, 8);
        addProp(nout, [this, nout] {
            e_->setOutputs(nodeId_, (int)pg_->GetPropertyValueAsLong(nout));
            commit();
            CallAfter([this] { rebuild(); });
        });
    }

    wxArrayString sides;
    sides.Add(tr("Left"));
    sides.Add(tr("Top"));
    sides.Add(tr("Right"));
    sides.Add(tr("Bottom"));
    auto* inSide = pg_->Append(new wxEnumProperty(tr("input side"), pgName(), sides, wxArrayInt(), n->inSide));
    addProp(inSide, [this, inSide] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->inSide = (int)pg_->GetPropertyValueAsLong(inSide);
        commit();
    });
    auto* outSide = pg_->Append(new wxEnumProperty(tr("output side"), pgName(), sides, wxArrayInt(), n->outSide));
    addProp(outSide, [this, outSide] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->outSide = (int)pg_->GetPropertyValueAsLong(outSide);
        commit();
    });
}

void PropertiesPanel::addScriptCategory() {
    Node* n = e_->g.find(nodeId_);
    if (!n) return;
    pg_->Append(new wxPropertyCategory(tr("PARAMETERS")));
    auto* params = pg_->Append(new wxStringProperty(tr("params"), pgName(),
                                                    wxString::FromUTF8(n->params)));
    params->SetHelpString(tr("k=value, name=\"text\" — constants available to the script"));
    addProp(params, [this, params] {
        Node* nd = e_->g.find(nodeId_);
        if (!nd) return;
        nd->params = pg_->GetPropertyValueAsString(params).utf8_string();
        const bool ok = e_->g.setScript(nd->id, nd->script);
        if (!ok) e_->warn(nd->scriptError);
        commit();
        CallAfter([this] { rebuild(); });
    });

    auto* script = pg_->Append(new wxStringProperty(tr("script"), pgName(),
                                                    wxString::FromUTF8(n->script)));
    script->ChangeFlag(wxPG_PROP_READONLY, true);
    if (!n->scriptError.empty()) {
        auto* err = pg_->Append(new wxStringProperty(tr("error"), pgName(),
                                                     wxString::FromUTF8(n->scriptError)));
        err->ChangeFlag(wxPG_PROP_READONLY, true);
    }
    editBtn_->Enable(true);
}

void PropertiesPanel::addPluginCategory() {
    Node* n = e_->g.find(nodeId_);
    if (!n) return;
    const auto* rt = e_->typeOf(*n);
    pg_->Append(new wxPropertyCategory(tr("PARAMETERS")));
    auto* type = pg_->Append(new wxStringProperty(tr("plugin"), pgName(),
                                                  wxString::FromUTF8(n->plugin)));
    type->ChangeFlag(wxPG_PROP_READONLY, true);
    if (!rt || !n->pinst) {
        auto* missing = pg_->Append(new wxStringProperty(tr("(missing)"), pgName(), ""));
        missing->ChangeFlag(wxPG_PROP_READONLY, true);
        return;
    }
    for (int i = 0; i < (int)rt->params.size(); ++i) {
        const auto& pr = rt->params[i];
        const double cur = n->pinst->callGetParam(n->pinst->handle, i);
        wxString label = wxString::FromUTF8(pr.name);
        if (!pr.unit.empty()) label += " (" + wxString::FromUTF8(pr.unit) + ")";
        wxPGProperty* p = nullptr;
        if (pr.kind == NS_PARAM_BOOL) {
            p = pg_->Append(new wxBoolProperty(label, pgName(), cur != 0.0));
        } else if (pr.kind == NS_PARAM_ENUM && !pr.choices.empty()) {
            wxArrayString choices;
            for (const auto& c : pr.choices) choices.Add(wxString::FromUTF8(c));
            p = pg_->Append(new wxEnumProperty(label, pgName(), choices, wxArrayInt(), (int)cur));
        } else if (pr.kind == NS_PARAM_INT) {
            p = pg_->Append(new wxIntProperty(label, pgName(), (long)std::lround(cur)));
        } else {
            p = pg_->Append(new wxFloatProperty(label, pgName(), cur));
        }
        if (pr.kind != NS_PARAM_BOOL && pr.kind != NS_PARAM_ENUM) {
            p->SetAttribute(wxPG_ATTR_MIN, pr.min);
            p->SetAttribute(wxPG_ATTR_MAX, pr.max);
        }
        if (!pr.help.empty()) p->SetHelpString(wxString::FromUTF8(pr.help));
        else if (!pr.unit.empty()) p->SetHelpString(label);
        addProp(p, [this, p, i, kind = pr.kind, lo = pr.min, hi = pr.max] {
            Node* nd = e_->g.find(nodeId_);
            if (!nd || !nd->pinst) return;
            double v = 0;
            if (kind == NS_PARAM_BOOL) v = pg_->GetPropertyValueAsBool(p) ? 1.0 : 0.0;
            else if (kind == NS_PARAM_ENUM) v = (double)pg_->GetPropertyValueAsLong(p);
            else if (kind == NS_PARAM_INT)
                v = std::floor(pg_->GetPropertyValueAsDouble(p) + 0.5);
            else v = pg_->GetPropertyValueAsDouble(p);
            if (kind != NS_PARAM_ENUM) v = std::max(lo, std::min(hi, v));
            nd->pinst->callSetParam(nd->pinst->handle, i, v);
            e_->refreshPstate(*nd);
            commit();
        });
    }
    if (!rt->texts.empty()) {
        pg_->Append(new wxPropertyCategory(tr("TEXT")));
        for (int i = 0; i < (int)rt->texts.size(); ++i) {
            std::string cur;
            if (char* s = n->pinst->callGetText(n->pinst->handle, i)) {
                cur = s;
                ::free(s);
            }
            auto* p = pg_->Append(new wxStringProperty(wxString::FromUTF8(rt->texts[i]),
                                                       pgName(), wxString::FromUTF8(cur)));
            addProp(p, [this, p, i] {
                Node* nd = e_->g.find(nodeId_);
                if (!nd || !nd->pinst) return;
                const std::string s = pg_->GetPropertyValueAsString(p).utf8_string();
                nd->pinst->callSetText(nd->pinst->handle, i, s.c_str());
                e_->refreshPstate(*nd);
                commit();
            });
        }
    }
}

void PropertiesPanel::addScopeCategory() {
    Node* n = e_->g.find(nodeId_);
    if (!n) return;
    const ScopeCfg c = parseScopeCfg(n->params);
    pg_->Append(new wxPropertyCategory(tr("GRAPH")));

    wxArrayString types;
    types.Add("line");
    types.Add("xy");
    types.Add("bars");
    types.Add("hist");
    types.Add("fft");
    types.Add("map");
    auto* type = pg_->Append(new wxEnumProperty(tr("type"), pgName(), types, wxArrayInt(), (int)c.type));
    addProp(type, [this, type, types] {
        Node* nd = e_->g.find(nodeId_);
        if (!nd) return;
        const int idx = (int)pg_->GetPropertyValueAsLong(type);
        const std::string name = types[idx].utf8_string();
        nd->params = scopeSetParam(nd->params, "tipo", name);
        commit();
        CallAfter([this] { rebuild(); });
    });

    auto* window = pg_->Append(new wxFloatProperty(tr("window"), pgName(), c.window));
    window->SetHelpString(tr("visible seconds"));
    addProp(window, [this, window] {
        Node* nd = e_->g.find(nodeId_);
        if (!nd) return;
        double v = pg_->GetPropertyValueAsDouble(window);
        v = std::max(0.1, std::min(600.0, v));
        nd->params = scopeSetParam(nd->params, "janela", formatNumExact(v));
        commit();
    });

    auto* yauto = pg_->Append(new wxBoolProperty(tr("auto Y"), pgName(), c.yauto));
    addProp(yauto, [this, yauto] {
        Node* nd = e_->g.find(nodeId_);
        if (!nd) return;
        nd->params = scopeSetParam(nd->params, "y",
                                   pg_->GetPropertyValueAsBool(yauto) ? "auto" : "fixo");
        commit();
        CallAfter([this] { rebuild(); });
    });
    if (!c.yauto) {
        auto* ymin = pg_->Append(new wxFloatProperty(tr("y min"), pgName(), c.ymin));
        addProp(ymin, [this, ymin] {
            Node* nd = e_->g.find(nodeId_);
            if (nd) nd->params = scopeSetParam(nd->params, "ymin",
                                               formatNumExact(pg_->GetPropertyValueAsDouble(ymin)));
            commit();
        });
        auto* ymax = pg_->Append(new wxFloatProperty(tr("y max"), pgName(), c.ymax));
        addProp(ymax, [this, ymax] {
            Node* nd = e_->g.find(nodeId_);
            if (nd) nd->params = scopeSetParam(nd->params, "ymax",
                                               formatNumExact(pg_->GetPropertyValueAsDouble(ymax)));
            commit();
        });
    }

    auto* scale = pg_->Append(new wxFloatProperty(tr("scale"), pgName(), c.scale));
    addProp(scale, [this, scale] {
        Node* nd = e_->g.find(nodeId_);
        if (!nd) return;
        double v = pg_->GetPropertyValueAsDouble(scale);
        if (std::fabs(v) < 1e-12) v = 1.0;
        nd->params = scopeSetParam(nd->params, "escala", formatNumExact(v));
        commit();
    });

    auto* dec = pg_->Append(new wxIntProperty(tr("decimation"), pgName(), c.dec));
    dec->SetAttribute(wxPG_ATTR_MIN, 1);
    dec->SetAttribute(wxPG_ATTR_MAX, 100);
    addProp(dec, [this, dec] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->params = scopeSetParam(nd->params, "dec",
                                           std::to_string(pg_->GetPropertyValueAsLong(dec)));
        commit();
    });

    auto* grid = pg_->Append(new wxBoolProperty(tr("Grid"), pgName(), c.grid));
    addProp(grid, [this, grid] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->params = scopeSetParam(nd->params, "grade",
                                           pg_->GetPropertyValueAsBool(grid) ? "1" : "0");
        commit();
    });
    auto* legend = pg_->Append(new wxBoolProperty(tr("legend"), pgName(), c.legend));
    addProp(legend, [this, legend] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->params = scopeSetParam(nd->params, "legenda",
                                           pg_->GetPropertyValueAsBool(legend) ? "1" : "0");
        commit();
    });
    auto* zero = pg_->Append(new wxBoolProperty(tr("zero"), pgName(), c.zero));
    addProp(zero, [this, zero] {
        Node* nd = e_->g.find(nodeId_);
        if (nd) nd->params = scopeSetParam(nd->params, "zero",
                                           pg_->GetPropertyValueAsBool(zero) ? "1" : "0");
        commit();
    });

    if (c.type == ScopeType::MAP) {
        pg_->Append(new wxPropertyCategory(tr("MAP")));
        auto* file = pg_->Append(new wxFileProperty(tr("file"), pgName(),
                                                    wxString::FromUTF8(c.mapFile)));
        addProp(file, [this, file] {
            Node* nd = e_->g.find(nodeId_);
            if (nd) nd->params = scopeSetParam(nd->params, "arquivo",
                                               pg_->GetPropertyValueAsString(file).utf8_string());
            commit();
        });
        auto* idf = pg_->Append(new wxStringProperty(tr("id field"), pgName(),
                                                     wxString::FromUTF8(c.mapId)));
        addProp(idf, [this, idf] {
            Node* nd = e_->g.find(nodeId_);
            if (nd) nd->params = scopeSetParam(nd->params, "idcampo",
                                               pg_->GetPropertyValueAsString(idf).utf8_string());
            commit();
        });
        auto* lab = pg_->Append(new wxStringProperty(tr("label field"), pgName(),
                                                     wxString::FromUTF8(c.mapLabel)));
        addProp(lab, [this, lab] {
            Node* nd = e_->g.find(nodeId_);
            if (nd) nd->params = scopeSetParam(nd->params, "rotulo",
                                               pg_->GetPropertyValueAsString(lab).utf8_string());
            commit();
        });
        wxArrayString projs;
        projs.Add("auto");
        projs.Add("mercator");
        projs.Add("linear");
        int pidx = c.mapProj == "mercator" ? 1 : (c.mapProj == "linear" ? 2 : 0);
        auto* proj = pg_->Append(new wxEnumProperty(tr("projection"), pgName(), projs, wxArrayInt(), pidx));
        addProp(proj, [this, proj, projs] {
            Node* nd = e_->g.find(nodeId_);
            if (!nd) return;
            const int idx = (int)pg_->GetPropertyValueAsLong(proj);
            nd->params = scopeSetParam(nd->params, "projecao", projs[idx].utf8_string());
            commit();
        });
        auto* overlay = pg_->Append(new wxStringProperty(
            tr("overlay"), pgName(), wxString::FromUTF8(scopeJoinList(c.mapOverlayFiles))));
        overlay->SetHelpString(tr("GeoJSON paths separated by |"));
        addProp(overlay, [this, overlay] {
            Node* nd = e_->g.find(nodeId_);
            if (!nd) return;
            const std::string v = pg_->GetPropertyValueAsString(overlay).utf8_string();
            nd->params = scopeSetParam(nd->params, "sobrepor", v);
            commit();
        });
    }
}
