// simulacro — fase 1..3: widgets de parâmetro dentro do nó (ImGui)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Camada AUTORAL de edição no canvas (SIMULACRO_NODE_UI, default off): o nó
// selecionado recebe a strip de widgets declarada pelo plugin
// (nodesim_plugin_ui, fase 3) ou a lista genérica de parâmetros (fase 2).
// Modos (nodeui::mode()): inner (default) ancora a strip no corpo; below
// mantém o trilho da fase 1 sob o nó; off desliga a camada (o property dock
// continua sendo o editor canônico). Charts/mapas nunca tocam ImGui.


#if defined(SIMULACRO_NODE_UI)
// ---- ImGui widgets over/in-node (parameter editors of the selected node) ----
// Drawn on top of the custom renderer; the UI that moves nodes/wires remains
// custom. On scope/map views this layer never runs.
//
// Layout precedence (fase 3): the declarative in-node layout exported by the
// plugin via `nodesim_plugin_ui` when present; otherwise the generic parameter
// list (fase 2). Modes come from nodeui::mode(): `inner` (default) anchors the
// strip inside the node body below the title, clipped to the node box; `below`
// keeps the fase-1 rail under the node; `off` disables canvas editing.
void GraphCanvas::drawNodeWidgets() {
    if (!imui::enabled() || !ImGui::GetCurrentContext()) return;
    if (nodeui::mode() == nodeui::Mode::Off) return;
    const int id = singleSelection();
    const Node* n = id >= 0 ? e_->g.find(id) : nullptr;
    if (!n) return;
    if (n->type != NodeType::PLUGIN) return;
    auto* inst = n->pinst.get();
    if (!inst) return;
    const auto* rt = e_->typeOf(*n);
    if (!rt) return;
    const bool authored = !rt->ui.empty();
    if (!authored && rt->params.empty()) return;
    if (zoom_ < 0.45f) return;   // window tiny: editing is for the property grid

    char win[40];
    std::snprintf(win, sizeof win, "##params%d", id);
    const FRect b = nodeBox(*n);
    const float pad = 6.0f, labelW = 60.0f;
    const float rowH = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y * 0.5f;
    const int rows = authored ? (int)rt->ui.size() : (int)rt->params.size();
    // LED rows are visual chips, not widgets; they still take one row each.
    float railH = (float)rows * rowH + 2 * pad + 4;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    Vec2 pos = w2s({b.x, b.y + b.h});
    float width = 196.0f;
    float height = railH;
    bool inner = false;
    if (nodeui::mode() == nodeui::Mode::Inner) {
        const Vec2 boxTl = w2s({b.x, b.y});
        const float boxW = b.w * zoom_;
        const float boxH = b.h * zoom_;
        const float titleH = NODE_TITLE_H * zoom_;
        const bool fits = boxW >= 118.0f && (boxH - titleH) >= rowH &&
                          (authored || (int)rt->params.size() <= 3);
        if (fits) {
            inner = true;
            pos = {boxTl.x, boxTl.y + titleH};
            width = boxW;
            height = boxH - titleH;
        }
    }

    widgetBoxes_.clear();
    widgetBoxes_.push_back({(int)pos.x, (int)pos.y, (int)width, (int)height});
    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    if (!ImGui::Begin(win, nullptr, flags)) {
        ImGui::End();
        return;
    }
    const bool compact = inner && width < 150.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

    auto displayName = [&](const PluginRegistry::Param* pr,
                           const std::string& overrideName) {
        if (!overrideName.empty()) return overrideName;
        return pr ? pr->name : std::string();
    };

    // Generic/declarative row body: one widget per param (or Uint item).
    auto rowStart = [&](const std::string& shownName) {
        ImGui::SetCursorPosX(pad);
        if (compact) {
            ImGui::PushItemWidth(width - 2 * pad);
        } else {
            ImGui::TextUnformatted(shownName.c_str());
            ImGui::SameLine(pad + labelW);
            ImGui::PushItemWidth(width - labelW - 2 * pad);
        }
    };
    auto rowHover = [&](const std::string& shownName) {
        if (compact && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shownName.c_str());
    };

    for (int row = 0; row < rows; ++row) {
        // Declarative-only leafs that open with no parameter binding.
        if (authored) {
            const auto& it = rt->ui[(size_t)row];
            if (it.kind == NS_UI_END) break;
            if (it.kind == NS_UI_LABEL) {
                ImGui::SetCursorPosX(pad);
                ImGui::TextUnformatted(it.name.c_str());
                continue;
            }
            if (it.kind == NS_UI_VSPACE) {
                ImGui::Dummy(ImVec2(0.0f, rowH));
                continue;
            }
            if (it.kind == NS_UI_LED) {
                const int idx = it.index;
                const bool okBound =
                    idx >= 0 && idx < (int)rt->params.size() &&
                    rt->params[(size_t)idx].kind == NS_PARAM_BOOL;
                const double v = okBound ? inst->callGetParam(inst->handle, idx) : 0.0;
                const bool on = v > 0.5;
                const ImVec2 c = ImGui::GetCursorScreenPos();
                const float r = 5.0f;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(c.x + pad + r, c.y + rowH * 0.5f), r,
                    on ? IM_COL32(kUi.ok.r, kUi.ok.g, kUi.ok.b, 255)
                       : IM_COL32(kUi.weak.r, kUi.weak.g, kUi.weak.b, 255));
                ImGui::Dummy(ImVec2(width - pad - r, rowH));
                continue;
            }
        }

        ImGui::PushID(row);
        if (authored) {
            // Declarative item with binding.
            const auto& it = rt->ui[(size_t)row];
            const bool isText = it.kind == NS_UI_TEXT;
            if (isText) {
                const std::string shown = it.name.empty()
                                              ? (it.index < (int)rt->texts.size()
                                                     ? rt->texts[(size_t)it.index]
                                                     : std::string())
                                              : it.name;
                rowStart(shown);
                char cur[512] = {0};
                if (char* s = inst->callGetText(inst->handle, it.index)) {
                    strncpy(cur, s, sizeof(cur) - 1);
                    ::free(s);
                }
                ImGui::InputText("##tbl", cur, sizeof(cur));
                const bool entered =
                    ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemEdited();
                if (entered) {
                    widgetNode_ = id;
                    applyText((Node*)n, it.index, cur);
                    Refresh(false);
                }
                rowHover(shown);
                ImGui::PopItemWidth();
                ImGui::PopID();
                continue;
            }
            if (it.index < 0 || it.index >= (int)rt->params.size()) {
                ImGui::PopID();
                continue;  // loader validated but be defensive
            }
            const auto& pr = rt->params[(size_t)it.index];
            const double cur = inst->callGetParam(inst->handle, it.index);
            const std::string shownName = displayName(&pr, it.name);
            switch (pr.kind) {
                case NS_PARAM_BOOL: {
                    rowStart(shownName);
                    bool v = cur != 0.0;
                    if (ImGui::Checkbox("##sw", &v)) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, v ? 1.0 : 0.0);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
                case NS_PARAM_ENUM: {
                    std::vector<const char*> items;
                    items.reserve(pr.choices.size());
                    for (const auto& s : pr.choices) items.push_back(s.c_str());
                    int idx = (int)std::lround(cur);
                    if (idx < 0 || idx >= (int)items.size()) idx = 0;
                    rowStart(shownName);
                    if (ImGui::Combo("##combo", &idx, items.data(), (int)items.size())) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, (double)idx);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
                case NS_PARAM_INT: {
                    int v = (int)std::lround(cur);
                    rowStart(shownName);
                    const double lo = it.min != 0 ? it.min : pr.min;
                    const double hi = it.max != 0 ? it.max : pr.max;
                    const bool rangeOk =
                        std::isfinite(lo) && std::isfinite(hi) && hi > lo &&
                        (hi - lo) <= 100000.0;
                    bool changed = false;
                    if (rangeOk) {
                        changed = ImGui::SliderInt("##int", &v, (int)lo, (int)hi);
                    } else {
                        changed = ImGui::InputInt("##int", &v, 1, 100);
                        if (std::isfinite(lo)) v = std::max(v, (int)lo);
                        if (std::isfinite(hi)) v = std::min(v, (int)hi);
                    }
                    if (changed) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, (double)v);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
                default: {
                    rowStart(shownName);
                    const double lo = it.min != 0 ? it.min : pr.min;
                    const double hi = it.max != 0 ? it.max : pr.max;
                    const bool rangeOk =
                        std::isfinite(lo) && std::isfinite(hi) && hi > lo &&
                        (hi - lo) <= 1e6;
                    bool changed = false;
                    double out = cur;
                    if (rangeOk) {
                        float f = (float)cur;
                        changed =
                            ImGui::SliderFloat("##f", &f, (float)lo, (float)hi, "%.4f");
                        out = (double)f;
                    } else {
                        ImGui::InputDouble("##num", &out, 0.0, 0.0, "%.6g");
                        changed = ImGui::IsItemDeactivatedAfterEdit();
                        if (std::isfinite(lo)) out = std::max(out, lo);
                        if (std::isfinite(hi)) out = std::min(out, hi);
                    }
                    if (changed) {
                        widgetNode_ = id;
                        applyParam((Node*)n, it.index, out);
                        Refresh(false);
                    }
                    rowHover(shownName);
                    ImGui::PopItemWidth();
                    break;
                }
            }
            ImGui::PopID();
            continue;
        }

        // Generic path (fase 2): one row per parameter.
        const auto& pr = rt->params[(size_t)row];
        const double cur = inst->callGetParam(inst->handle, row);
        const std::string shownName = pr.name;
        switch (pr.kind) {
            case NS_PARAM_BOOL: {
                rowStart(shownName);
                bool v = cur != 0.0;
                if (ImGui::Checkbox("##sw", &v)) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, v ? 1.0 : 0.0);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
            case NS_PARAM_ENUM: {
                std::vector<const char*> items;
                items.reserve(pr.choices.size());
                for (const auto& s : pr.choices) items.push_back(s.c_str());
                int idx = (int)std::lround(cur);
                if (idx < 0 || idx >= (int)items.size()) idx = 0;
                rowStart(shownName);
                if (ImGui::Combo("##combo", &idx, items.data(), (int)items.size())) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, (double)idx);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
            case NS_PARAM_INT: {
                int v = (int)std::lround(cur);
                rowStart(shownName);
                const double lo = pr.min, hi = pr.max;
                const bool rangeOk =
                    std::isfinite(lo) && std::isfinite(hi) && hi > lo &&
                    (hi - lo) <= 100000.0;
                bool changed = false;
                if (rangeOk) {
                    changed = ImGui::SliderInt("##int", &v, (int)lo, (int)hi);
                } else {
                    changed = ImGui::InputInt("##int", &v, 1, 100);
                    if (std::isfinite(lo)) v = std::max(v, (int)lo);
                    if (std::isfinite(hi)) v = std::min(v, (int)hi);
                }
                if (changed) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, (double)v);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
            default: {
                rowStart(shownName);
                const double lo = pr.min, hi = pr.max;
                const bool rangeOk =
                    std::isfinite(lo) && std::isfinite(hi) && hi > lo && (hi - lo) <= 1e6;
                bool changed = false;
                double out = cur;
                if (rangeOk) {
                    float f = (float)cur;
                    changed =
                        ImGui::SliderFloat("##f", &f, (float)lo, (float)hi, "%.4f");
                    out = (double)f;
                } else {
                    ImGui::InputDouble("##num", &out, 0.0, 0.0, "%.6g");
                    changed = ImGui::IsItemDeactivatedAfterEdit();
                    if (std::isfinite(lo)) out = std::max(out, lo);
                    if (std::isfinite(hi)) out = std::min(out, hi);
                }
                if (changed) {
                    widgetNode_ = id;
                    applyParam((Node*)n, row, out);
                    Refresh(false);
                }
                rowHover(shownName);
                ImGui::PopItemWidth();
                break;
            }
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

#endif // SIMULACRO_NODE_UI
