// simulacro — janela principal (wxWidgets + wxAui)
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app.h"

#include "canvas.h"
#include "i18n.h"
#include "imgui_shell.h"
#include "palette.h"
#include "properties.h"
#include "scopeview.h"
#include "scriptdlg.h"
#include "tema.h"
#include "themewx.h"
#include "version.h"

#include <wx/artprov.h>
#include <wx/filename.h>

#include <cstdlib>
#include <fstream>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/statusbr.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

extern std::string g_startFile; // set by main.cpp
extern bool g_openScopes;      // --open-scopes (GUI)

namespace {
wxString T(const char* s) { return wxString::FromUTF8(i18n::text(s)); }

// Ícones de alinhamento desenhados (wxArtProvider não tem stock "align"):
// a linha-GUIA clara é a referência e as barras são os nós encostados nela
// (larguras diferentes para lembrar que alinha a CAIXA, não o centro).
static wxBitmap alignIcon(GraphCanvas::Align mode) {
    wxBitmap bmp(20, 20, 32);
    bmp.UseAlpha();
    wxMemoryDC dc;
    dc.SelectObject(bmp);
    dc.SetBackground(wxBrush(wxColour(0, 0, 0, 0)));
    dc.Clear();
    auto bar = [&](int x, int y, int w, int h, const wxColour& c) {
        dc.SetPen(wxPen(c, 1, wxPENSTYLE_TRANSPARENT));
        dc.SetBrush(wxBrush(c));
        dc.DrawRectangle(x, y, w, h);
    };
    const wxColour guide(240, 240, 248);
    const wxColour node(150, 150, 160);
    switch (mode) {
        case GraphCanvas::Align::Left:
            bar(4, 2, 2, 16, guide); bar(7, 4, 10, 5, node); bar(7, 11, 6, 5, node);
            break;
        case GraphCanvas::Align::HCenter:
            bar(9, 2, 2, 16, guide); bar(5, 4, 10, 5, node); bar(7, 11, 6, 5, node);
            break;
        case GraphCanvas::Align::Right:
            bar(14, 2, 2, 16, guide); bar(4, 4, 10, 5, node); bar(8, 11, 6, 5, node);
            break;
        case GraphCanvas::Align::Top:
            bar(2, 4, 16, 2, guide); bar(4, 7, 5, 10, node); bar(11, 7, 6, 10, node);
            break;
        case GraphCanvas::Align::VCenter:
            bar(2, 9, 16, 2, guide); bar(4, 5, 5, 10, node); bar(11, 7, 6, 6, node);
            break;
        case GraphCanvas::Align::Bottom:
            bar(2, 14, 16, 2, guide); bar(4, 4, 5, 10, node); bar(11, 4, 6, 10, node);
            break;
        default:
            break;
    }
    dc.SelectObject(wxNullBitmap);
    return bmp;
}

enum {
    ID_NEW = wxID_HIGHEST + 1,
    ID_OPEN, ID_SAVE, ID_SAVE_AS, ID_SAVE_INITIAL, ID_EXPORT_SCOPE,
    ID_UNDO, ID_REDO, ID_CUT, ID_COPY, ID_PASTE, ID_DUPLICATE, ID_DELETE,
    ID_SELECT_ALL, ID_GROUP, ID_UNGROUP,
    ID_ALIGN_L, ID_ALIGN_R, ID_ALIGN_HC, ID_ALIGN_T, ID_ALIGN_B, ID_ALIGN_VC,
    ID_ALIGN_DH, ID_ALIGN_DV, ID_ALIGN_DD,
    ID_ADD_SCRIPT, ID_ADD_SCOPE, ID_ADD_MAP, ID_ADD_JOIN, ID_ADD_SPLIT,
    ID_PALETTE_FOCUS,
    ID_SIM_TOGGLE, ID_SIM_STEP, ID_SIM_RESET, ID_SIM_CLEAR, ID_CONVERGE, ID_EVENTS,
    ID_SHOW_VALUES, ID_GRID, ID_CURVE, ID_ZOOM_IN, ID_ZOOM_OUT, ID_ZOOM_RESET,
    ID_CENTER, ID_PANE_PALETTE, ID_PANE_PROPS, ID_PANE_SCOPES,
    ID_LANG_EN, ID_LANG_PT, ID_RELOAD_THEME,
    ID_SHORTCUTS, ID_ABOUT,
    // Faixas reservadas DEPOIS do último ID sequencial: no meio do enum elas
    // colidiam com os IDs que vêm depois (ID_ITER_BASE+1..4 caía em cima de
    // SHOW_VALUES/GRID/CURVE/PANE_PALETTE e o FindItem do menu "Stop after"
    // marcava os toggles de View — os checks do menu View se perdiam).
    ID_RATE_BASE = wxID_HIGHEST + 200,
    ID_ITER_BASE = wxID_HIGHEST + 230
};

const int kRates[] = {1, 5, 10, 20, 30, 60};
const int kIterations[] = {-1, 1, 10, 100, 1000};

wxString baseName(const std::string& path) {
    if (path.empty()) return T("untitled");
    wxFileName fn(wxString::FromUTF8(path));
    return fn.GetFullName();
}
} // namespace

// ------------------------------------------------------------------ w x A p p

class SimulacroApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) return false;
        SetAppName("simulacro");
        SetVendorName("Flavio de Vasconcellos Correa");
        i18n::initFromEnv();
        auto* frame = new MainFrame();
        frame->Show();
        return true;
    }
};

wxIMPLEMENT_APP_NO_MAIN(SimulacroApp);

// ------------------------------------------------------------------ M a i n

// ------------------------------------------------------------- sessão
// ui.ini em ~/.local/state/nodesim (contrato XDG do README): pares
// chave=valor; a perspectiva AUI é UMA linha (contém ; e = — o split é no
// PRIMEIRO '='). Salvo ao fechar; restaurado no boot.
static wxString sessionPath() {
    const char* x = std::getenv("XDG_STATE_HOME");
    wxString dir = x && *x ? wxString::FromUTF8(x) : wxGetHomeDir() + "/.local/state";
    return dir + "/nodesim/ui.ini";
}

static std::map<wxString, wxString> readSession() {
    std::map<wxString, wxString> out;
    std::ifstream f(std::string(sessionPath().ToUTF8()));
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[wxString::FromUTF8(line.substr(0, eq))] =
            wxString::FromUTF8(line.substr(eq + 1));
    }
    return out;
}

static void writeSession(const std::vector<std::pair<wxString, wxString>>& kv) {
    const wxString path = sessionPath();
    wxFileName::Mkdir(wxFileName(path).GetPath(), 0755, wxPATH_MKDIR_FULL);
    std::ofstream f(std::string(path.ToUTF8()));
    if (!f) return;
    for (const auto& [k, v] : kv)
        f << std::string(k.ToUTF8()) << "=" << std::string(v.ToUTF8()) << "\n";
}

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "simulacro", wxDefaultPosition, wxSize(1280, 780)),
      timer_(this) {
    themeInit();

    // sessão salva: o IDIOMA vem antes de buildMenu (os menus nascem no
    // idioma restaurado; --lang do CLI tem precedência sobre o ui.ini).
    const auto sess = readSession();
    if (std::getenv("NODESIM_LANG") == nullptr) {
        const auto it = sess.find("lang");
        if (it != sess.end() && it->second == "pt-BR")
            i18n::setLang(i18n::Lang::PT_BR);
    }

    CreateStatusBar(3);
    const int widths[] = {-1, 240, 280};
    SetStatusWidths(3, widths);
    SetStatusText("simulacro 0.1.0", 0);

    engine_.scanPlugins();

    buildMenu();
    buildToolbar();
    buildLayout();
    bindUi();

    // sessão salva: geometria/panes/toggles/câmera pendente (antes do
    // Update() do AUI e antes do fit — a câmera restaurada vence o fit).
    restoreSession(sess);

    aui_.Update();
    applyTheme();
    updateTitle();
    updateStatus();
    // GTK submenu items can only be checked after the menu is realized.
    // Refit once the AUI panes settle: loadFile ran before the canvas pane
    // reached its final size, so the constructor's fit used a stale rect.
    canvas_->requestFit();

    timer_.SetOwner(this);
    timer_.Start(1000 / std::max(1, engine_.rate));

    if (!g_startFile.empty()) {
        if (!loadFile(g_startFile))
            wxMessageBox(wxString::FromUTF8(engine_.lastError), T("Open failed"),
                         wxOK | wxICON_ERROR, this);
    }
    if (g_openScopes)
        for (const auto& n : engine_.g.nodes)
            if (n.type == NodeType::SCOPE) scopes_->openScope(n.id);
    canvas_->SetFocus();
}

MainFrame::~MainFrame() {
    timer_.Stop();
    aui_.UnInit();
}

// Menu de alinhamento: UMA fonte para o submenu Edit > Align e para o
// dropdown da barra (os IDs são os mesmos; um Bind só atende aos dois).
static wxMenu* makeAlignMenu() {
    wxMenu* m = new wxMenu();
    m->Append(ID_ALIGN_L, T("Align left") + "\tCtrl+Shift+L");
    m->Append(ID_ALIGN_HC, T("Align horizontal centers") + "\tCtrl+Shift+H");
    m->Append(ID_ALIGN_R, T("Align right") + "\tCtrl+Shift+R");
    m->Append(ID_ALIGN_T, T("Align top") + "\tCtrl+Shift+T");
    m->Append(ID_ALIGN_VC, T("Align vertical centers") + "\tCtrl+Shift+V");
    m->Append(ID_ALIGN_B, T("Align bottom") + "\tCtrl+Shift+B");
    m->AppendSeparator();
    m->Append(ID_ALIGN_DH, T("Distribute horizontally"));
    m->Append(ID_ALIGN_DV, T("Distribute vertically"));
    return m;
}

void MainFrame::buildMenu() {
    auto* mb = new wxMenuBar();

    auto* file = new wxMenu();
    file->Append(ID_NEW, T("New") + "\tCtrl+N");
    file->Append(ID_OPEN, T("Open…") + "\tCtrl+O");
    file->Append(ID_SAVE, T("Save") + "\tCtrl+S");
    file->Append(ID_SAVE_AS, T("Save As…") + "\tCtrl+Shift+S");
    file->AppendSeparator();
    file->AppendCheckItem(ID_SAVE_INITIAL, T("Save initial state"));
    file->Append(ID_EXPORT_SCOPE, T("Export Scope SVG…"));
    file->AppendSeparator();
    file->Append(wxID_EXIT, T("Exit") + "\tCtrl+Q");
    mb->Append(file, T("File"));

    auto* edit = new wxMenu();
    edit->Append(ID_UNDO, T("Undo") + "\tCtrl+Z");
    edit->Append(ID_REDO, T("Redo") + "\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(ID_CUT, T("Cut") + "\tCtrl+X");
    edit->Append(ID_COPY, T("Copy") + "\tCtrl+C");
    edit->Append(ID_PASTE, T("Paste") + "\tCtrl+V");
    edit->Append(ID_DUPLICATE, T("Duplicate") + "\tCtrl+D");
    edit->Append(ID_DELETE, T("Delete") + "\tDel");
    edit->Append(ID_SELECT_ALL, T("Select All") + "\tCtrl+A");
    edit->AppendSeparator();
    edit->Append(ID_GROUP, T("Group selection") + "\tCtrl+G");
    edit->Append(ID_UNGROUP, T("Ungroup") + "\tCtrl+Shift+G");
    edit->AppendSeparator();
    edit->AppendSubMenu(makeAlignMenu(), T("Align"));
    mb->Append(edit, T("Edit"));

    auto* add = new wxMenu();
    add->Append(ID_ADD_SCRIPT, T("Script"));
    add->Append(ID_ADD_SCOPE, T("Scope"));
    add->Append(ID_ADD_MAP, T("Map scope (GeoJSON)"));
    add->AppendSeparator();
    add->Append(ID_ADD_JOIN, T("Join (N→1)"));
    add->Append(ID_ADD_SPLIT, T("Split (1→N)"));
    add->AppendSeparator();
    add->Append(ID_PALETTE_FOCUS, T("Search palette…") + "\tCtrl+K");
    mb->Append(add, T("Add"));

    auto* view = new wxMenu();
    view->AppendCheckItem(ID_SHOW_VALUES, T("Show values"));
    view->AppendCheckItem(ID_GRID, T("Grid"));
    view->AppendCheckItem(ID_CURVE, T("Curved wires"));
    view->AppendSeparator();
    view->Append(ID_ZOOM_IN, T("Zoom In") + "\tCtrl+=");
    view->Append(ID_ZOOM_OUT, T("Zoom Out") + "\tCtrl+-");
    view->Append(ID_ZOOM_RESET, T("Reset zoom"));
    view->Append(ID_CENTER, T("Center on graph\tCtrl+1"));
    view->AppendSeparator();
    view->AppendCheckItem(ID_PANE_PALETTE, T("Nodes palette"));
    view->AppendCheckItem(ID_PANE_PROPS, T("Properties"));
    view->AppendCheckItem(ID_PANE_SCOPES, T("Scopes"));
    view->AppendSeparator();
    auto* lang = new wxMenu();
    lang->AppendCheckItem(ID_LANG_EN, "English");
    lang->AppendCheckItem(ID_LANG_PT, wxString::FromUTF8("Português (Brasil)"));
    view->AppendSubMenu(lang, T("Language"));
    view->Append(ID_RELOAD_THEME, T("Reload theme"));
    mb->Append(view, T("View"));

    auto* sim = new wxMenu();
    sim->Append(ID_SIM_TOGGLE, T("Start"));
    sim->Append(ID_SIM_STEP, T("Step") + "\tF6");
    sim->Append(ID_SIM_RESET, T("Reset") + "\tF5");
    sim->Append(ID_SIM_CLEAR, T("Clear scopes"));
    sim->AppendSeparator();
    auto* rate = new wxMenu();
    for (size_t i = 0; i < sizeof(kRates) / sizeof(*kRates); ++i)
        rate->AppendCheckItem(ID_RATE_BASE + (int)i,
                              wxString::Format("%d ", kRates[i]) + T("steps/s"));
    sim->AppendSubMenu(rate, T("Rate"));
    auto* iterations = new wxMenu();
    for (size_t i = 0; i < sizeof(kIterations) / sizeof(*kIterations); ++i) {
        wxString label = kIterations[i] < 0
                             ? T("Infinite")
                             : wxString::Format("%d ", kIterations[i]) + T("steps");
        iterations->AppendCheckItem(ID_ITER_BASE + (int)i, label);
    }
    sim->AppendSubMenu(iterations, T("Stop after"));
    sim->AppendSeparator();
    sim->AppendCheckItem(ID_CONVERGE, T("Converge cycles"));
    sim->AppendCheckItem(ID_EVENTS, T("Discrete events"));
    mb->Append(sim, T("Simulation"));

    auto* help = new wxMenu();
    help->Append(ID_SHORTCUTS, T("Keyboard shortcuts") + "\tF1");
    help->Append(ID_ABOUT, T("About"));
    mb->Append(help, T("Help"));

    SetMenuBar(mb);
    simItem_ = mb->FindItem(ID_SIM_TOGGLE);
}

void MainFrame::buildToolbar() {
    auto* tb = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT);
    tb->SetToolBitmapSize(wxSize(20, 20));

    auto icon = [](const wxArtID& id) {
        return wxArtProvider::GetBitmap(id, wxART_TOOLBAR, wxSize(20, 20));
    };
    tb->AddTool(ID_NEW, T("New"), icon(wxART_NEW), T("New"));
    tb->AddTool(ID_OPEN, T("Open"), icon(wxART_FILE_OPEN), T("Open"));
    tb->AddTool(ID_SAVE, T("Save"), icon(wxART_FILE_SAVE), T("Save"));
    tb->AddSeparator();
    tb->AddTool(ID_UNDO, T("Undo"), icon(wxART_UNDO), T("Undo"));
    tb->AddTool(ID_REDO, T("Redo"), icon(wxART_REDO), T("Redo"));
    tb->AddSeparator();
    // Alinhamento da seleção como DROPDOWN (um único botão na barra): a seta
    // abre o menu (SetDropdownMenu) e o clique no CORPO também abre (o Bind
    // do ID_ALIGN_DD faz PopupMenu na posição da tool). Os itens reusam os
    // IDs do menu Edit > Align.
    tb->AddTool(ID_ALIGN_DD, "", alignIcon(GraphCanvas::Align::HCenter),
                T("Align"), wxITEM_DROPDOWN);
    tb->SetDropdownMenu(ID_ALIGN_DD, makeAlignMenu());
    tb->AddSeparator();
    tb->AddTool(ID_SIM_TOGGLE, T("Start"), icon(wxART_GO_FORWARD), T("Start/Pause"));
    tb->AddTool(ID_SIM_STEP, T("Step"), icon(wxART_GO_DOWN), T("Step"));
    tb->AddTool(ID_SIM_RESET, T("Reset"), icon(wxART_REFRESH), T("Reset"));
    tb->AddSeparator();
    tb->AddTool(ID_ADD_SCRIPT, T("Script"), icon(wxART_NEW_DIR), T("Add script node"));
    tb->AddTool(ID_ADD_SCOPE, T("Scope"), icon(wxART_REPORT_VIEW), T("Add scope node"));
    tb->Realize();
    SetToolBar(tb);
}

void MainFrame::buildLayout() {
    aui_.SetManagedWindow(this);
    aui_.SetFlags(wxAUI_MGR_DEFAULT | wxAUI_MGR_TRANSPARENT_DRAG);

    palette_ = new PalettePanel(this, &engine_);
    canvas_ = new GraphCanvas(this, &engine_);
    props_ = new PropertiesPanel(this, &engine_);
    scopes_ = new ScopeNotebook(this, &engine_);

    aui_.AddPane(palette_, wxAuiPaneInfo().Name("palette").Caption(T("Nodes")).Left().
                                 BestSize(260, 560).MinSize(180, 160));
    aui_.AddPane(canvas_, wxAuiPaneInfo().Name("canvas").CenterPane().PaneBorder(false));
    aui_.AddPane(props_, wxAuiPaneInfo().Name("props").Caption(T("Properties")).Right().
                                 BestSize(330, 560).MinSize(240, 180));
    aui_.AddPane(scopes_, wxAuiPaneInfo().Name("scopes").Caption(T("Scopes")).Bottom().
                                 BestSize(-1, 260).MinSize(200, 120));
}

void MainFrame::bindUi() {
    Bind(wxEVT_MENU, &MainFrame::onNew, this, ID_NEW);
    Bind(wxEVT_MENU, &MainFrame::onOpen, this, ID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::onSave, this, ID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::onSaveAs, this, ID_SAVE_AS);
    Bind(wxEVT_MENU, &MainFrame::onSaveInitial, this, ID_SAVE_INITIAL);
    Bind(wxEVT_MENU, &MainFrame::onExportScope, this, ID_EXPORT_SCOPE);
    Bind(wxEVT_MENU, &MainFrame::onQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::onUndo, this, ID_UNDO);
    Bind(wxEVT_MENU, &MainFrame::onRedo, this, ID_REDO);
    Bind(wxEVT_MENU, &MainFrame::onCut, this, ID_CUT);
    Bind(wxEVT_MENU, &MainFrame::onCopy, this, ID_COPY);
    Bind(wxEVT_MENU, &MainFrame::onPaste, this, ID_PASTE);
    Bind(wxEVT_MENU, &MainFrame::onDuplicate, this, ID_DUPLICATE);
    Bind(wxEVT_MENU, &MainFrame::onDelete, this, ID_DELETE);
    Bind(wxEVT_MENU, &MainFrame::onSelectAll, this, ID_SELECT_ALL);
    Bind(wxEVT_MENU, &MainFrame::onGroup, this, ID_GROUP);
    Bind(wxEVT_MENU, &MainFrame::onAlign, this, ID_ALIGN_L, ID_ALIGN_DV);
    // corpo do botão dropdown: abre o menu no cursor (o usuário acabou de
    // clicar na tool). A SETA já usa o menu do SetDropdownMenu; o menu é
    // descartado após o popup síncrono.
    Bind(wxEVT_MENU, [this](wxCommandEvent& ev) {
        wxToolBar* tb = GetToolBar();
        if (!tb) return;
        wxMenu* m = makeAlignMenu();
        tb->PopupMenu(m);
        delete m;
    }, ID_ALIGN_DD);
    Bind(wxEVT_MENU, &MainFrame::onUngroup, this, ID_UNGROUP);
    Bind(wxEVT_MENU, &MainFrame::onAddScript, this, ID_ADD_SCRIPT);
    Bind(wxEVT_MENU, &MainFrame::onAddScope, this, ID_ADD_SCOPE);
    Bind(wxEVT_MENU, &MainFrame::onAddMap, this, ID_ADD_MAP);
    Bind(wxEVT_MENU, &MainFrame::onAddJoin, this, ID_ADD_JOIN);
    Bind(wxEVT_MENU, &MainFrame::onAddSplit, this, ID_ADD_SPLIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { palette_->focusSearch(); }, ID_PALETTE_FOCUS);
    Bind(wxEVT_MENU, &MainFrame::onSimToggle, this, ID_SIM_TOGGLE);
    Bind(wxEVT_MENU, &MainFrame::onSimStep, this, ID_SIM_STEP);
    Bind(wxEVT_MENU, &MainFrame::onSimReset, this, ID_SIM_RESET);
    Bind(wxEVT_MENU, &MainFrame::onSimClear, this, ID_SIM_CLEAR);
    Bind(wxEVT_MENU, &MainFrame::onConverge, this, ID_CONVERGE);
    Bind(wxEVT_MENU, &MainFrame::onEvents, this, ID_EVENTS);
    Bind(wxEVT_MENU, &MainFrame::onRate, this, ID_RATE_BASE, ID_RATE_BASE + 15);
    Bind(wxEVT_MENU, &MainFrame::onIterations, this, ID_ITER_BASE, ID_ITER_BASE + 15);
    Bind(wxEVT_MENU, &MainFrame::onShowValues, this, ID_SHOW_VALUES);
    Bind(wxEVT_MENU, &MainFrame::onGrid, this, ID_GRID);
    Bind(wxEVT_MENU, &MainFrame::onCurve, this, ID_CURVE);
    Bind(wxEVT_MENU, &MainFrame::onZoomIn, this, ID_ZOOM_IN);
    Bind(wxEVT_MENU, &MainFrame::onZoomOut, this, ID_ZOOM_OUT);
    Bind(wxEVT_MENU, &MainFrame::onZoomReset, this, ID_ZOOM_RESET);
    Bind(wxEVT_MENU, &MainFrame::onCenter, this, ID_CENTER);
    Bind(wxEVT_MENU, &MainFrame::onTogglePane, this, ID_PANE_PALETTE);
    Bind(wxEVT_MENU, &MainFrame::onTogglePane, this, ID_PANE_PROPS);
    Bind(wxEVT_MENU, &MainFrame::onTogglePane, this, ID_PANE_SCOPES);
    Bind(wxEVT_MENU, &MainFrame::onLang, this, ID_LANG_EN, ID_LANG_PT);
    Bind(wxEVT_MENU, &MainFrame::onReloadTheme, this, ID_RELOAD_THEME);
    Bind(wxEVT_MENU, &MainFrame::onShortcuts, this, ID_SHORTCUTS);
    Bind(wxEVT_MENU, &MainFrame::onAbout, this, ID_ABOUT);
    Bind(wxEVT_TIMER, &MainFrame::onTimer, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);

    // Keys that should only act when the canvas has focus (they would
    // otherwise steal space/letters from text editors).
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& ev) {
        wxWindow* focus = wxWindow::FindFocus();
        wxCommandEvent dummy;
        if (ev.GetEventType() == wxEVT_KEY_UP) { ev.Skip(); return; } // soltura
                                                                     // vai ao
                                                                     // canvas
        if (ev.GetKeyCode() == WXK_F5) { onSimReset(dummy); return; }
        if (ev.GetKeyCode() == WXK_F6) { onSimStep(dummy); return; }
        if (focus != canvas_) { ev.Skip(); return; }
        // ImGui text/nav widgets take the keyboard first: without this rule a
        // text field in a node widget would have V/G/C/O/Space stolen from it.
        if (imui::wantsTextInputNow() || imui::wantsKeyboardNow()) { ev.Skip(); return; }
        switch (ev.GetKeyCode()) {
            case WXK_SPACE:
                // Controle selecionado (NS_PLUGIN_ACTS): Space aperta o
                // controle (HOLD) — sem controle, liga/pausa a simulação.
                if (canvas_->actKey(ev, true)) return;
                onSimToggle(dummy);
                return;
            case WXK_RETURN:
            case WXK_NUMPAD_ENTER:
                // Controle selecionado: Enter alterna (TOGGLE) ou pulsa
                // (HOLD). Sem seleção de controle, nada (comportamento atual).
                canvas_->actKey(ev, true);
                return;
            case 'V': case 'v': onShowValues(dummy); return;
            case 'G': case 'g': onGrid(dummy); return;
            case 'C': case 'c': onCurve(dummy); return;
            case 'O': case 'o': {
                const int id = canvas_->singleSelection();
                if (id >= 0) {
                    const Node* n = engine_.g.find(id);
                    if (n && n->type == NodeType::SCOPE) openScope(id);
                }
                return;
            }
            default: break;
        }
        ev.Skip();
    });

    canvas_->onSelectionChanged = [this] {
        const int id = canvas_->singleSelection();
        if (props_->node() != id) props_->setNode(id);
        updateStatus();
    };
    // Widget edits (ImGui on the node): keep the property grid as the
    // canonical view and refresh scope/beat if needed.
    canvas_->onParamEdited = [this](int id) {
        const Node* n = engine_.g.find(id);
        if (n && props_->node() == id) props_->setNode(id);
        scopes_->refreshNode(id);
        updateTitle();
        updateStatus();
    };
    canvas_->onDocumentChanged = [this] { documentChanged(true); };
    canvas_->onOpenScope = [this](int id) { openScope(id); };
    canvas_->onEditScript = [this](int id) { editScript(id); };
    canvas_->onMessage = [this](const std::string& s) {
        SetStatusText(wxString::FromUTF8(s), 0);
    };

    props_->onChanged = [this] {
        engine_.dirty = true;
        canvas_->Refresh(false);
        scopes_->refreshNode(props_->node());
        updateTitle();
        updateStatus();
    };
    props_->onEditScript = [this](int id) { editScript(id); };
    props_->onOpenScope = [this](int id) { openScope(id); };

    palette_->onAdd = [this](const std::string& key) { onAddKey(key); };
}

// --------------------------------------------------------------- document

void MainFrame::newDocument() {
    engine_.newGraph();
    scopes_->closeAll();
    props_->setNode(-1);
    canvas_->clearSelection();
    canvas_->zoomReset();
    canvas_->Refresh(false);
    updateTitle();
    updateStatus();
}

bool MainFrame::loadFile(const std::string& path) {
    if (!engine_.load(path)) {
        if (engine_.lastError.empty()) engine_.lastError = "could not load " + path;
        return false;
    }
    scopes_->closeAll();
    props_->setNode(-1);
    canvas_->clearSelection();
    canvas_->centerOnGraph();
    canvas_->requestFit();
    palette_->rebuild();
    updateTitle();
    updateStatus();
    SetStatusText(T("Loaded ") + wxString::FromUTF8(path), 0);
    return true;
}

void MainFrame::documentChanged(bool scopes) {
    engine_.dirty = true;
    updateTitle();
    updateStatus();
    if (scopes) scopes_->refreshAll();
}

void MainFrame::refreshEverything() {
    canvas_->refreshFromModel();
    const int id = canvas_->singleSelection();
    props_->setNode(id);
    scopes_->refreshAll();
    updateChecks();
    updateTitle();
    updateStatus();
}

void MainFrame::updateTitle() {
    wxString name = baseName(engine_.path);
    SetTitle(wxString::FromUTF8("simulacro — ") + name + (engine_.dirty ? " *" : ""));
}

void MainFrame::updateStatus() {
    const wxString sim = wxString::FromUTF8(engine_.statusLine()) +
                         wxString::FromUTF8("  ·  ") +
                         wxString::Format("%d/s", engine_.rate);
    SetStatusText(sim, 1);
    const auto& sel = canvas_->selection();
    wxString info;
    if (sel.size() == 1) {
        const Node* n = engine_.g.find(*sel.begin());
        if (n)
            info = wxString::FromUTF8(engine_.nodeLabel(*n)) +
                   wxString::Format(" #%d", n->id);
    } else {
        info = wxString::Format("%d ", (int)engine_.g.nodes.size()) + T("nodes") +
               wxString::FromUTF8("  ·  ") +
               wxString::Format("%d ", (int)engine_.g.edges.size()) + T("wires") +
               wxString::FromUTF8("  ·  ") +
               wxString::Format("%d ", (int)engine_.g.groups.size()) + T("groups");
    }
    SetStatusText(info, 2);
    if (!engine_.lastError.empty()) SetStatusText(wxString::FromUTF8(engine_.lastError), 0);
}

void MainFrame::setSimLabel() {
    if (!simItem_) return;
    simItem_->SetItemLabel(engine_.running ? T("Pause (Space)") : T("Start (Space)"));
}

void MainFrame::updateChecks() {
    wxMenuBar* mb = GetMenuBar();
    if (!mb) return;

    if (auto* it = mb->FindItem(ID_SAVE_INITIAL)) it->Check(engine_.saveInitial);
    if (auto* it = mb->FindItem(ID_SHOW_VALUES)) it->Check(canvas_->showValues());
    if (auto* it = mb->FindItem(ID_GRID)) it->Check(canvas_->grid());
    if (auto* it = mb->FindItem(ID_CURVE)) it->Check(canvas_->curveWires());
    if (auto* it = mb->FindItem(ID_CONVERGE)) it->Check(engine_.converge);
    if (auto* it = mb->FindItem(ID_EVENTS)) it->Check(engine_.events);
    for (size_t i = 0; i < sizeof(kRates) / sizeof(*kRates); ++i)
        if (auto* it = mb->FindItem(ID_RATE_BASE + (int)i)) it->Check(kRates[i] == engine_.rate);
    for (size_t i = 0; i < sizeof(kIterations) / sizeof(*kIterations); ++i)
        if (auto* it = mb->FindItem(ID_ITER_BASE + (int)i))
            it->Check(kIterations[i] == engine_.iterations);
    if (auto* it = mb->FindItem(ID_PANE_PALETTE))
        it->Check(aui_.GetPane("palette").IsShown());
    if (auto* it = mb->FindItem(ID_PANE_PROPS)) it->Check(aui_.GetPane("props").IsShown());
    if (auto* it = mb->FindItem(ID_PANE_SCOPES)) it->Check(aui_.GetPane("scopes").IsShown());
    if (auto* it = mb->FindItem(ID_LANG_EN)) it->Check(i18n::lang() == i18n::Lang::EN);
    if (auto* it = mb->FindItem(ID_LANG_PT)) it->Check(i18n::lang() == i18n::Lang::PT_BR);
    static const bool dbg = std::getenv("NODESIM_DEBUG_UI") != nullptr;
    if (dbg)
        for (int id : {ID_SHOW_VALUES, ID_GRID, ID_CURVE, ID_PANE_PALETTE,
                       ID_PANE_PROPS, ID_PANE_SCOPES, ID_LANG_EN, ID_LANG_PT,
                       ID_CONVERGE, ID_EVENTS}) {
            wxMenuItem* it = mb->FindItem(id);
            if (it) std::fprintf(stderr, "[chk] id=%d checked=%d\n", id, (int)it->IsChecked());
        }
    setSimLabel();
}

void MainFrame::applyTheme() {
    themeApply();
    imui::theme();
    SetBackgroundColour(wxColourOf(kUi.darkBackground));
    themeWindowDeep(palette_);
    themeWindowDeep(props_);
    props_->applyTheme();
    scopes_->SetBackgroundColour(wxColourOf(kUi.darkBackground));
    for (size_t i = 0; i < scopes_->GetPageCount(); ++i)
        if (auto* v = dynamic_cast<ScopeView*>(scopes_->GetPage(i)))
            v->SetBackgroundColour(wxColourOf(kUi.background));
    canvas_->SetBackgroundColour(wxColourOf(kUi.background));
    canvas_->Refresh(false);
    scopes_->Refresh(false);
    aui_.Update();
    SetStatusText(wxString::FromUTF8(themeDescription()), 0);
}

void MainFrame::applyLanguage() {
    if (wxMenuBar* old = GetMenuBar()) {
        SetMenuBar(nullptr);
        old->Destroy();
    }
    buildMenu();
    if (wxToolBar* old = GetToolBar()) {
        SetToolBar(nullptr);
        old->Destroy();
    }
    buildToolbar();
    aui_.GetPane("palette").Caption(T("Nodes"));
    aui_.GetPane("props").Caption(T("Properties"));
    aui_.GetPane("scopes").Caption(T("Scopes"));
    palette_->rebuild();
    props_->rebuild();
    scopes_->refreshAll();
    updateChecks();
    updateTitle();
    updateStatus();
    aui_.Update();
}

// ---------------------------------------------------------------- commands

int MainFrame::askSave() {
    if (!engine_.dirty) return wxNO;
    const int r = wxMessageBox(T("Save changes before continuing?"), T("simulacro"),
                               wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (r == wxCANCEL) return wxCANCEL;
    if (r == wxNO) return wxNO;
    if (engine_.path.empty()) {
        wxFileDialog dlg(this, T("Save As"), wxEmptyString, "untitled.nodesim",
                         T("nodesim (*.nodesim)|*.nodesim|All files|*"),
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg.ShowModal() != wxID_OK) return wxCANCEL;
        wxString path = dlg.GetPath();
        if (!path.Lower().EndsWith(".nodesim")) path += ".nodesim";
        if (!engine_.saveAs(path.utf8_string())) {
            wxMessageBox(wxString::FromUTF8(engine_.lastError), T("Save failed"),
                         wxOK | wxICON_ERROR, this);
            return wxCANCEL;
        }
    } else if (!engine_.save()) {
        wxMessageBox(wxString::FromUTF8(engine_.lastError), T("Save failed"),
                     wxOK | wxICON_ERROR, this);
        return wxCANCEL;
    }
    updateTitle();
    return wxYES;
}

void MainFrame::onNew(wxCommandEvent&) {
    if (askSave() == wxCANCEL) return;
    newDocument();
}

void MainFrame::onOpen(wxCommandEvent&) {
    if (askSave() == wxCANCEL) return;
    wxFileDialog dlg(this, T("Open"), wxEmptyString, "",
                     T("nodesim (*.nodesim)|*.nodesim|All files|*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    if (!loadFile(dlg.GetPath().utf8_string()))
        wxMessageBox(wxString::FromUTF8(engine_.lastError), T("Open failed"),
                     wxOK | wxICON_ERROR, this);
}

void MainFrame::onSave(wxCommandEvent&) {
    if (engine_.path.empty()) {
        onSaveAs(*(new wxCommandEvent()));
        return;
    }
    if (!engine_.save())
        wxMessageBox(wxString::FromUTF8(engine_.lastError), T("Save failed"),
                     wxOK | wxICON_ERROR, this);
    else
        SetStatusText(T("Saved ") + wxString::FromUTF8(engine_.path), 0);
    updateTitle();
    updateStatus();
}

void MainFrame::onSaveAs(wxCommandEvent&) {
    wxString suggest = baseName(engine_.path);
    wxFileDialog dlg(this, T("Save As"), wxEmptyString, suggest,
                     T("nodesim (*.nodesim)|*.nodesim|All files|*"),
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    wxString path = dlg.GetPath();
    if (!path.Lower().EndsWith(".nodesim")) path += ".nodesim";
    if (!engine_.saveAs(path.utf8_string()))
        wxMessageBox(wxString::FromUTF8(engine_.lastError), T("Save failed"),
                     wxOK | wxICON_ERROR, this);
    else
        SetStatusText(T("Saved ") + path, 0);
    updateTitle();
    updateStatus();
}

void MainFrame::onQuit(wxCommandEvent&) { Close(false); }

void MainFrame::onClose(wxCloseEvent& ev) {
    if (ev.CanVeto() && askSave() == wxCANCEL) {
        ev.Veto();
        return;
    }
    saveSession(); // estado da sessão (panes/view/idioma) ao sair
    timer_.Stop();
    ev.Skip();
}

void MainFrame::restoreSession(const std::map<wxString, wxString>& s) {
    // geometria da janela (quando não maximizada, o par x/y/w/h vale)
    auto num = [&](const char* k, double* v) {
        const auto it = s.find(k);
        return it != s.end() && it->second.ToDouble(v);
    };
    double w, h, x, y, max;
    if (num("winw", &w) && num("winh", &h) && num("winx", &x) && num("winy", &y) &&
        num("max", &max)) {
        if (max < 0.5) SetSize((int)x, (int)y, (int)w, (int)h);
        if (max > 0.5) Maximize(true);
    }
    // perspectiva AUI (nomes de pane atuais: palette/props/scopes/canvas)
    const auto persp = s.find("persp");
    if (persp != s.end() && !persp->second.empty())
        aui_.LoadPerspective(persp->second, false); // Update() vem no ctor
    // toggles de View (só aplica o que existe no arquivo)
    if (s.count("values")) {
        double v = 0;
        if (num("values", &v)) canvas_->setShowValues(v > 0.5);
    }
    if (s.count("grid")) {
        double v = 0;
        if (num("grid", &v)) canvas_->setGrid(v > 0.5);
    }
    if (s.count("curve")) {
        double v = 0;
        if (num("curve", &v)) canvas_->setCurveWires(v > 0.5);
    }
    // câmera (pendente: vence o fit de carga uma vez)
    double z, cx, cy;
    if (num("zoom", &z) && num("camx", &cx) && num("camy", &cy))
        canvas_->setPendingView((float)z, (float)cx, (float)cy);
    updateChecks(); // checks do menu condizem com o estado restaurado
}

void MainFrame::saveSession() {
    std::vector<std::pair<wxString, wxString>> kv;
    kv.emplace_back("persp", aui_.SavePerspective());
    const bool max = IsMaximized();
    kv.emplace_back("max", max ? "1" : "0");
    if (!max) {
        kv.emplace_back("winx", wxString::Format("%d", GetPosition().x));
        kv.emplace_back("winy", wxString::Format("%d", GetPosition().y));
        kv.emplace_back("winw", wxString::Format("%d", GetSize().x));
        kv.emplace_back("winh", wxString::Format("%d", GetSize().y));
    }
    kv.emplace_back("lang",
                    i18n::lang() == i18n::Lang::PT_BR ? "pt-BR" : "en");
    float z, cx, cy;
    canvas_->viewState(z, cx, cy);
    kv.emplace_back("zoom", wxString::Format("%.6f", (double)z));
    kv.emplace_back("camx", wxString::Format("%.1f", (double)cx));
    kv.emplace_back("camy", wxString::Format("%.1f", (double)cy));
    kv.emplace_back("values", canvas_->showValues() ? "1" : "0");
    kv.emplace_back("grid", canvas_->grid() ? "1" : "0");
    kv.emplace_back("curve", canvas_->curveWires() ? "1" : "0");
    writeSession(kv);
}

void MainFrame::onUndo(wxCommandEvent&) {
    engine_.undo();
    refreshEverything();
}
void MainFrame::onRedo(wxCommandEvent&) {
    engine_.redo();
    refreshEverything();
}
void MainFrame::onCut(wxCommandEvent&) {
    canvas_->copySelection();
    canvas_->deleteSelection();
    documentChanged(true);
}
void MainFrame::onCopy(wxCommandEvent&) {
    canvas_->copySelection();
    SetStatusText(T("Copied selection"), 0);
}
void MainFrame::onPaste(wxCommandEvent&) {
    canvas_->pasteClipboard();
    documentChanged(true);
}
void MainFrame::onDuplicate(wxCommandEvent&) {
    canvas_->duplicateSelection();
    documentChanged(true);
}
void MainFrame::onDelete(wxCommandEvent&) {
    canvas_->deleteSelection();
    documentChanged(true);
}
void MainFrame::onSelectAll(wxCommandEvent&) { canvas_->selectAll(); }

void MainFrame::onGroup(wxCommandEvent&) {
    const auto& sel = canvas_->selection();
    if (sel.empty()) {
        SetStatusText(T("select one or more nodes to group"), 0);
        return;
    }
    engine_.checkpoint();
    Group gr;
    gr.id = engine_.g.nextGroupId++;
    gr.title = "Group " + std::to_string(gr.id + 1);
    for (int id : sel) {
        for (auto& other : engine_.g.groups)
            other.nodes.erase(std::remove(other.nodes.begin(), other.nodes.end(), id),
                             other.nodes.end());
        gr.nodes.push_back(id);
    }
    engine_.g.groups.erase(
        std::remove_if(engine_.g.groups.begin(), engine_.g.groups.end(),
                       [](const Group& x) { return x.nodes.empty(); }),
        engine_.g.groups.end());
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int id : gr.nodes) {
        if (const Node* n = engine_.g.find(id)) {
            const FRect b = nodeBox(*n);
            x0 = std::min(x0, b.x);
            y0 = std::min(y0, b.y);
            x1 = std::max(x1, b.x + b.w);
            y1 = std::max(y1, b.y + b.h);
        }
    }
    const float m = 24;
    gr.x = x0 - m;
    gr.y = y0 - m;
    gr.w = x1 - x0 + 2 * m;
    gr.h = y1 - y0 + 2 * m;
    engine_.g.groups.push_back(std::move(gr));
    documentChanged(true);
    canvas_->Refresh(false);
}

void MainFrame::onUngroup(wxCommandEvent&) {
    std::set<int> affected;
    for (int id : canvas_->selection())
        if (const Group* gr = engine_.g.groupOf(id)) affected.insert(gr->id);
    if (affected.empty()) {
        SetStatusText(T("select a grouped node"), 0);
        return;
    }
    engine_.checkpoint();
    engine_.g.groups.erase(
        std::remove_if(engine_.g.groups.begin(), engine_.g.groups.end(),
                       [&](const Group& gr) { return affected.count(gr.id) > 0; }),
        engine_.g.groups.end());
    documentChanged(true);
    canvas_->Refresh(false);
}

void MainFrame::onAddKey(const std::string& key) {
    wxPoint p = canvas_->spawnWorld();
    int id = -1;
    Node* n = nullptr;
    if (key == "script") n = engine_.addScript((float)p.x, (float)p.y);
    else if (key == "scope") n = engine_.addScope((float)p.x, (float)p.y);
    else if (key == "map") {
        n = engine_.addScope((float)p.x, (float)p.y);
        if (n) n->params = scopeSetParam(n->params, "tipo", "map");
    } else if (key == "join") n = engine_.addMux((float)p.x, (float)p.y, 2, 1);
    else if (key == "split") n = engine_.addMux((float)p.x, (float)p.y, 1, 2);
    else n = engine_.addPlugin(key, (float)p.x, (float)p.y);

    if (!n) {
        wxMessageBox(wxString::FromUTF8(engine_.lastError.empty() ? "could not add node"
                                                                  : engine_.lastError),
                     T("Add failed"), wxOK | wxICON_ERROR, this);
        return;
    }
    id = n->id;
    engine_.dirty = true;
    canvas_->selectNode(id);
    props_->setNode(id);
    canvas_->refreshFromModel();
    scopes_->refreshAll();
    updateTitle();
    updateStatus();
    canvas_->SetFocus();
}

void MainFrame::onAddScript(wxCommandEvent&) { onAddKey("script"); }
void MainFrame::onAddScope(wxCommandEvent&) { onAddKey("scope"); }
void MainFrame::onAddMap(wxCommandEvent&) { onAddKey("map"); }
void MainFrame::onAddJoin(wxCommandEvent&) { onAddKey("join"); }
void MainFrame::onAddSplit(wxCommandEvent&) { onAddKey("split"); }

void MainFrame::openScope(int id) {
    scopes_->openScope(id);
    aui_.GetPane("scopes").Show();
    aui_.Update();
    updateChecks();
}

void MainFrame::editScript(int id) {
    ScriptDialog dlg(this, &engine_, id);
    dlg.onApplied = [this] {
        engine_.dirty = true;
        canvas_->Refresh(false);
        props_->rebuild();
        updateTitle();
        updateStatus();
    };
    dlg.ShowModal();
}

// -------------------------------------------------------------- simulation

void MainFrame::onSimToggle(wxCommandEvent&) {
    if (engine_.running) {
        engine_.running = false;
        engine_.steps = 0;
    } else {
        engine_.running = true;
        engine_.steps = 0;
        if (!engine_.iterations) engine_.iterations = -1;
    }
    setSimLabel();
    updateStatus();
}

void MainFrame::onSimStep(wxCommandEvent&) {
    engine_.stepOnce();
    canvas_->Refresh(false);
    if (ScopeView* v = scopes_->currentView()) v->tick();
    updateStatus();
}

void MainFrame::onSimReset(wxCommandEvent&) {
    engine_.resetSimulation();
    engine_.running = false;
    canvas_->Refresh(false);
    scopes_->refreshAll();
    setSimLabel();
    updateStatus();
}

void MainFrame::onSimClear(wxCommandEvent&) {
    engine_.clearScopes();
    scopes_->refreshAll();
    updateStatus();
}

void MainFrame::onConverge(wxCommandEvent&) {
    engine_.converge = !engine_.converge;
    updateChecks();
}
void MainFrame::onEvents(wxCommandEvent&) {
    engine_.events = !engine_.events;
    updateChecks();
}

void MainFrame::onRate(wxCommandEvent& ev) {
    const int idx = ev.GetId() - ID_RATE_BASE;
    if (idx < 0 || (size_t)idx >= sizeof(kRates) / sizeof(*kRates)) return;
    engine_.rate = kRates[idx];
    timer_.Start(1000 / std::max(1, engine_.rate));
    updateChecks();
    updateStatus();
}

void MainFrame::onIterations(wxCommandEvent& ev) {
    const int idx = ev.GetId() - ID_ITER_BASE;
    if (idx < 0 || (size_t)idx >= sizeof(kIterations) / sizeof(*kIterations)) return;
    engine_.iterations = kIterations[idx];
    updateChecks();
}

void MainFrame::onTimer(wxTimerEvent&) {
    ++ticks_;
    if (engine_.running) {
        engine_.stepOnce();
        canvas_->Refresh(false);
        if (!engine_.running) setSimLabel();
        if (++lastPaintTick_ % 3 == 0)
            if (ScopeView* v = scopes_->currentView()) v->tick();
    }
    if (ticks_ % 20 == 0) updateStatus();
    if (ticks_ % 120 == 0 && themePoll()) applyTheme();
}

// ------------------------------------------------------------------- view

void MainFrame::onShowValues(wxCommandEvent&) {
    canvas_->setShowValues(!canvas_->showValues());
    updateChecks();
}
void MainFrame::onGrid(wxCommandEvent&) {
    canvas_->setGrid(!canvas_->grid());
    updateChecks();
}
void MainFrame::onCurve(wxCommandEvent&) {
    canvas_->setCurveWires(!canvas_->curveWires());
    updateChecks();
}
void MainFrame::onZoomIn(wxCommandEvent&) {
    wxSize sz = canvas_->GetClientSize();
    canvas_->zoomBy(1.2f, wxPoint(sz.x / 2, sz.y / 2));
}
void MainFrame::onZoomOut(wxCommandEvent&) {
    wxSize sz = canvas_->GetClientSize();
    canvas_->zoomBy(1.0f / 1.2f, wxPoint(sz.x / 2, sz.y / 2));
}
void MainFrame::onZoomReset(wxCommandEvent&) { canvas_->zoomReset(); }
void MainFrame::onCenter(wxCommandEvent&) { canvas_->centerOnGraph(); }

void MainFrame::onAlign(wxCommandEvent& ev) {
    using A = GraphCanvas::Align;
    A mode;
    switch (ev.GetId()) {
        case ID_ALIGN_L: mode = A::Left; break;
        case ID_ALIGN_R: mode = A::Right; break;
        case ID_ALIGN_HC: mode = A::HCenter; break;
        case ID_ALIGN_T: mode = A::Top; break;
        case ID_ALIGN_B: mode = A::Bottom; break;
        case ID_ALIGN_VC: mode = A::VCenter; break;
        case ID_ALIGN_DH: mode = A::DistH; break;
        case ID_ALIGN_DV: mode = A::DistV; break;
        default: return;
    }
    if (canvas_->selection().size() < 2) {
        SetStatusText(T("select two or more nodes to align"), 0);
        return;
    }
    canvas_->alignSelection(mode);
}

void MainFrame::onTogglePane(wxCommandEvent& ev) {
    const char* name = ev.GetId() == ID_PANE_PALETTE ? "palette"
                       : ev.GetId() == ID_PANE_PROPS ? "props"
                                                     : "scopes";
    wxAuiPaneInfo& pane = aui_.GetPane(name);
    pane.Show(!pane.IsShown());
    aui_.Update();
    updateChecks();
}

void MainFrame::onSaveInitial(wxCommandEvent&) {
    engine_.saveInitial = !engine_.saveInitial;
    engine_.dirty = true;
    updateChecks();
    updateTitle();
}

void MainFrame::onExportScope(wxCommandEvent&) {
    ScopeView* v = scopes_->currentView();
    if (!v) {
        SetStatusText(T("open a Scope first"), 0);
        return;
    }
    v->exportSvg();
}

void MainFrame::onLang(wxCommandEvent& ev) {
    i18n::setLang(ev.GetId() == ID_LANG_PT ? i18n::Lang::PT_BR : i18n::Lang::EN);
    CallAfter([this] { applyLanguage(); });
}

void MainFrame::onReloadTheme(wxCommandEvent&) {
    themePoll(true);
    applyTheme();
    updateChecks();
}

void MainFrame::onShortcuts(wxCommandEvent&) {
    const wxString text =
        T("Canvas\n"
          "  Left drag: move nodes / select\n"
          "  Left drag on a socket: create a wire\n"
          "  Middle drag: pan   ·   Scroll: pan horizontal/vertical\n"
          "  Ctrl+Scroll: zoom   ·   Ctrl+1: center on graph\n"
          "  Double-click: open script or scope\n"
          "  Space: start/pause (com controle selecionado: aperta/alterna)\n"
          "  F5: reset   ·   F6: step\n"
          "  V: values   ·   G: grid   ·   C: curved wires   ·   O: open scope\n"
          "\n"
          "Editing\n"
          "  Ctrl+Z / Ctrl+Y: undo/redo\n"
          "  Ctrl+C / Ctrl+V / Ctrl+D: copy/paste/duplicate\n"
          "  Del: delete selection   ·   Ctrl+A: select all\n"
          "  Ctrl+G / Ctrl+Shift+G: group/ungroup\n"
          "  Ctrl+K: search palette\n"
          "\n"
          "Files\n"
          "  Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S");
    wxMessageBox(text, T("Keyboard shortcuts"), wxOK | wxICON_INFORMATION, this);
}

void MainFrame::onAbout(wxCommandEvent&) {
    const wxString text =
        "simulacro 0.1.0\n\n" +
        T("Node graph editor and simulator (embedded nodesim engine).") + "\n\n" +
        wxString::FromUTF8(nodesimVersion()) + "\n\n" +
        "wxWidgets " + wxString(wxVERSION_STRING) + "\nSDL " +
        wxString::Format("%d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL) +
        "\nTTF " + wxString::FromUTF8(TTF_GetError()) + "\n\nGPL-3.0-or-later";
    wxMessageBox(text, T("About"), wxOK | wxICON_INFORMATION, this);
}
