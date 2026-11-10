// simulacro — paleta de nós (wxTreeCtrl + busca)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "palette.h"
#include "i18n.h"
#include "themewx.h"

#include <algorithm>
#include <cctype>

namespace {
wxString tr(const char* s) { return wxString::FromUTF8(i18n::text(s)); }

std::string fold(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x80) o += (char)std::tolower(c);
        else o += (char)c; // keep UTF-8 bytes for a simple substring match
    }
    return o;
}

bool matches(const std::string& label, const std::string& id, const std::string& group,
             const std::string& q) {
    if (q.empty()) return true;
    const std::string hay = fold(label) + " " + fold(id) + " " + fold(group);
    size_t p = 0;
    while (p < q.size()) {
        while (p < q.size() && std::isspace((unsigned char)q[p])) ++p;
        size_t e = p;
        while (e < q.size() && !std::isspace((unsigned char)q[e])) ++e;
        if (e > p && hay.find(q.substr(p, e - p)) == std::string::npos) return false;
        p = e;
    }
    return true;
}

struct PaletteItemData : wxTreeItemData {
    std::string key;
    explicit PaletteItemData(std::string k) : key(std::move(k)) {}
};
} // namespace

PalettePanel::PalettePanel(wxWindow* parent, Engine* engine)
    : wxPanel(parent), e_(engine) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    search_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                             wxTE_PROCESS_ENTER);
    search_->SetHint(tr("Search node types…"));
    sizer->Add(search_, 0, wxEXPAND | wxALL, 4);

    tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE |
                               wxTR_FULL_ROW_HIGHLIGHT);
    sizer->Add(tree_, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

    hint_ = new wxStaticText(this, wxID_ANY, tr("Double-click to add at the view center."));
    hint_->SetForegroundColour(wxColourOf(kUi.weak));
    sizer->Add(hint_, 0, wxEXPAND | wxALL, 6);

    SetSizer(sizer);
    themeWindowDeep(this);

    search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        query_ = search_->GetValue();
        rebuild();
    });
    search_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        wxTreeItemId root = tree_->GetRootItem();
        if (!root.IsOk()) return;
        wxTreeItemIdValue cookie;
        wxTreeItemId cat = tree_->GetFirstChild(root, cookie);
        if (cat.IsOk() && tree_->ItemHasChildren(cat)) {
            wxTreeItemIdValue c2;
            wxTreeItemId leaf = tree_->GetFirstChild(cat, c2);
            if (leaf.IsOk()) activate(leaf);
        }
    });
    tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED,
                [this](wxTreeEvent& ev) { activate(ev.GetItem()); });

    rebuild();
}

void PalettePanel::addItem(wxTreeItemId parent, const std::string& key,
                           const std::string& label) {
    wxTreeItemId id = tree_->AppendItem(parent, wxString::FromUTF8(label));
    tree_->SetItemData(id, new PaletteItemData(key));
}

void PalettePanel::activate(wxTreeItemId id) {
    if (!id.IsOk()) return;
    if (tree_->ItemHasChildren(id)) {
        tree_->Toggle(id);
        return;
    }
    auto* data = dynamic_cast<PaletteItemData*>(tree_->GetItemData(id));
    if (data && onAdd) onAdd(data->key);
}

void PalettePanel::rebuild() {
    if (!tree_) return;
    const wxString keep = query_;
    tree_->DeleteAllItems();
    wxTreeItemId root = tree_->AddRoot("root");
    const std::string q = keep.utf8_string();

    wxTreeItemId basics = tree_->AppendItem(root, tr("BASICS"));
    struct Builtin {
        const char* key;
        const char* label;
    };
    static const Builtin builtins[] = {
        {"script", "Script"},
        {"scope", "Scope"},
        {"map", "Map scope"},
        {"join", "Join (multiple inputs)"},
        {"split", "Split (multiple outputs)"},
    };
    for (const auto& b : builtins)
        if (matches(i18n::text(b.label), b.key, "Basic", q))
            addItem(basics, b.key, i18n::text(b.label));
    if (!tree_->ItemHasChildren(basics)) tree_->Delete(basics);

    // Plugins, grouped by the registry category (empty = General).
    std::map<std::string, wxTreeItemId> cats;
    for (const auto& rt : e_->plugins.types) {
        const std::string group = rt.group.empty() ? "General" : rt.group;
        if (!matches(i18n::text(rt.label), rt.id, i18n::text(group), q)) continue;
        auto it = cats.find(group);
        if (it == cats.end()) {
            wxTreeItemId id = tree_->AppendItem(root, tr(group.c_str()));
            cats[group] = id;
            it = cats.find(group);
        }
        addItem(it->second, rt.id, i18n::text(rt.label));
    }
    for (auto it = cats.begin(); it != cats.end();)
        if (!tree_->ItemHasChildren(it->second)) {
            tree_->Delete(it->second);
            it = cats.erase(it);
        } else {
            ++it;
        }
    tree_->ExpandAll();

    if (tree_->GetChildrenCount(root, false) == 0)
        tree_->AppendItem(root, tr("(no match)"));
    themeWindowDeep(this);
}

void PalettePanel::focusSearch() {
    search_->SetFocus();
    search_->SelectAll();
}
