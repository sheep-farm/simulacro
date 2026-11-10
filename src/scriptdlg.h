// simulacro — editor de scripts (wxDialog)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <wx/wx.h>
#include <functional>
#include <string>

#include "engine.h"

// Native script editor: multi-line text, live compile feedback and the list
// of input sources (in(n) order) for reference.
class ScriptDialog : public wxDialog {
public:
    ScriptDialog(wxWindow* parent, Engine* engine, int nodeId);

    std::function<void()> onApplied;

private:
    void checkLive();
    void apply();
    void reset();

    Engine* e_;
    int id_;
    std::string original_;
    wxTextCtrl* text_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxTextCtrl* inputs_ = nullptr;
};
