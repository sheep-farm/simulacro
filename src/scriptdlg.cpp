// simulacro — editor de scripts (wxDialog)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scriptdlg.h"
#include "expr.h"
#include "i18n.h"
#include "numfmt.h"
#include "themewx.h"

namespace {
wxString tr(const char* s) { return wxString::FromUTF8(i18n::text(s)); }
}

ScriptDialog::ScriptDialog(wxWindow* parent, Engine* engine, int nodeId)
    : wxDialog(parent, wxID_ANY, tr("Edit script"), wxDefaultPosition, wxSize(760, 560),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      e_(engine), id_(nodeId) {
    Node* n = e_->g.find(id_);
    original_ = n ? n->script : std::string();

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    wxString header = wxString::Format(tr("node %d"), id_);
    if (n && !n->params.empty())
        header += "   " + wxString::FromUTF8(n->params);
    sizer->Add(new wxStaticText(this, wxID_ANY, header), 0, wxEXPAND | wxALL, 8);

    text_ = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(original_),
                           wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_RICH2 | wxTE_DONTWRAP);
    wxFont mono(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE));
    text_->SetFont(mono);
    sizer->Add(text_, 3, wxEXPAND | wxLEFT | wxRIGHT, 8);

    sizer->Add(new wxStaticText(this, wxID_ANY,
                                tr("';' separates ports · '#' comments · constants from params")),
               0, wxLEFT | wxRIGHT | wxTOP, 8);

    status_ = new wxStaticText(this, wxID_ANY, "");
    sizer->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

    sizer->Add(new wxStaticText(this, wxID_ANY, tr("INPUTS")), 0, wxLEFT | wxRIGHT | wxTOP, 8);
    inputs_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 96),
                             wxTE_MULTILINE | wxTE_READONLY);
    inputs_->SetFont(mono);
    if (n) {
        const auto srcs = e_->g.inputSources(id_);
        wxString lines;
        for (size_t k = 0; k < srcs.size(); ++k) {
            const Node* s = e_->g.find(srcs[k].first);
            lines += wxString::Format("in(%d) <- %d ", (int)k, srcs[k].first) +
                     (s ? wxString::FromUTF8(e_->nodeLabel(*s)) : wxString("?")) + " = " +
                     wxString::FromUTF8(formatNumShort(
                         e_->g.outVal(srcs[k].first, srcs[k].second))) +
                     "\n";
        }
        if (lines.empty()) lines = tr("(no inputs connected)");
        inputs_->SetValue(lines);
    }
    sizer->Add(inputs_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto* buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    sizer->Add(buttons, 0, wxEXPAND | wxALL, 8);
    SetSizer(sizer);
    themeWindowDeep(this);
    CentreOnParent();

    text_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { checkLive(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent& ev) {
        if (ev.GetId() == wxID_OK) apply();
        else if (ev.GetId() == wxID_CANCEL) reset();
    });
    text_->SetFocus();
    text_->SetInsertionPointEnd();
    checkLive();
}

void ScriptDialog::checkLive() {
    if (!text_ || !status_) return;
    std::string err;
    Node* n = e_->g.find(id_);
    auto p = Expr::parseList(text_->GetValue().utf8_string(), err,
                             n ? n->params : std::string());
    if (p) {
        status_->SetLabel(tr("OK — script compiles."));
        status_->SetForegroundColour(wxColourOf(kUi.ok));
    } else {
        status_->SetLabel(wxString::FromUTF8(err));
        status_->SetForegroundColour(wxColourOf(kUi.error));
    }
    Layout();
}

void ScriptDialog::apply() {
    Node* n = e_->g.find(id_);
    if (!n) {
        EndModal(wxID_CANCEL);
        return;
    }
    const std::string code = text_->GetValue().utf8_string();
    std::string err;
    auto p = Expr::parseList(code, err, n->params);
    if (!p) {
        wxMessageBox(wxString::FromUTF8(err), tr("Script error"), wxOK | wxICON_ERROR, this);
        return;
    }
    if (code != original_) {
        e_->checkpoint();
        e_->g.setScript(id_, code);
        e_->dirty = true;
        if (onApplied) onApplied();
    }
    EndModal(wxID_OK);
}

void ScriptDialog::reset() {
    Node* n = e_->g.find(id_);
    if (n && n->script != original_) {
        e_->g.setScript(id_, original_);
        if (onApplied) onApplied();
    }
    EndModal(wxID_CANCEL);
}
