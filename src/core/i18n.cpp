// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "i18n.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

namespace i18n {
namespace {

Lang gLang = Lang::EN;

struct Entry { const char* en; const char* pt; };

// Exact UI phrases. English is the source language; Portuguese is a
// translation. Keep entries sorted by use, not alphabetically: lookup is a
// hash map built once.
const Entry kEntries[] = {

    // ---- plugin groups and type labels (source is English; Portuguese UI) ----
    {"Logic", "Lógica"}, {"Math", "Matemática"}, {"Signal", "Sinal"},
    {"Filter", "Filtro"}, {"Data", "Dados"}, {"I/O", "E/S"},
    {"Display", "Exibição"}, {"Control", "Controle"}, {"General", "Geral"},
    {"ACF (autocorrelation)", "ACF (autocorrelação)"},
    {"PACF (partial)", "PACF (parcial)"}, {"ADF (unit root)", "ADF (raiz unitária)"},
    {"Clock", "Relógio"}, {"Window (matrix)", "Janela (matriz)"},
    {"Export CSV", "Exportar CSV"}, {"Inference (t/p/CI)", "Inferência (t/p/IC)"},
    {"Interval", "Intervalo"}, {"OLS regression", "Regressão OLS"},
    {"Gain K", "Ganho K"}, {"Random (RNG)", "Aleatório (RNG)"},
    {"7-seg display", "Display 7 seg"}, {"Δ (difference)", "Δ (diferença)"},
    {"Delay (lag)", "Atraso (lag)"}, {"Statistics", "Estatística"},
    {"Integrator", "Integrador"}, {"Derivative", "Derivativo"},
    {"Saturation", "Saturação"}, {"Dead zone", "Zona morta"},
    {"Stop", "Parada"}, {"Pause (ms)", "Pausa (ms)"},
    {"Moving average", "Média móvel"}, {"Median", "Mediana"},
    {"Matrix product", "Produto de matrizes"}, {"Transpose", "Transposta"},
    {"Inverse", "Inversa"}, {"Solve A·x=b", "Resolver A·x=b"},
    {"Determinant", "Determinante"}, {"Chi-square", "Qui-quadrado"},
    {"Fisher F", "F de Fisher"}, {"Step", "Degrau"}, {"Impulse", "Impulso"},
    {"Ramp", "Rampa"}, {"Sine", "Seno"}, {"Square", "Quadrada"},
    {"Triangle", "Triangular"}, {"Dominant frequency", "Frequência dominante"},
    {"AR(p) — forecast", "AR(p) — previsão"},
    {"Holt — level and trend", "Holt — nível e tendência"},
    {"Holt-Winters — seasonal", "Holt-Winters — sazonal"},
    {"Timed stop", "Parada por tempo"}, {"Square wave (ms)", "Onda quadrada (ms)"},
    {"Pulse generator (ms)", "Gerador de pulsos (ms)"},
    {"External process", "Processo externo"},
    {"line", "linha"}, {"xy", "xy"}, {"bars", "barras"},
    {"histogram", "histograma"}, {"fft", "fft"}, {"map", "mapa"},
    {"[FROZEN]", "[CONGELADO]"}, {"window", "janela"},
    {"re-dock", "reencaixar"},
    {"choose a GeoJSON file", "escolha um arquivo GeoJSON"},
    {"X-Y needs 2 channels (inputs)", "X-Y precisa de 2 canais (entradas)"},
    {"(click)", "(clique)"}, {"(by columns)", "(por colunas)"},
    {"(type)", "(tipo)"}, {"(empty)", "(vazio)"}, {"(none)", "(nenhum)"},
    {"(missing)", "(ausente)"}, {"(sink)", "(sink)"},
    // ---- parameter help / units ----
    {"samples", "amostras"}, {"events", "eventos"}, {"rows", "linhas"},
    {"column", "coluna"}, {"field", "campo"}, {"name", "nome"},
    {"value", "valor"}, {"output", "saída"}, {"input", "entrada"},
    // ---- menus / toolbar ----
    {"File", "Arquivo"},
    {"Sim", "Sim"},
    {"Edit", "Editar"},
    {"Align", "Alinhar"},
    {"Add", "Adicionar"},
    {"View", "Ver"},
    {"Help", "Ajuda"},
    {"Clear all", "Limpar tudo"},
    {"New (Ctrl+N)", "Novo (Ctrl+N)"},
    {"Open… (Ctrl+O)", "Abrir… (Ctrl+O)"},
    {"Save (S)", "Salvar (S)"},
    {"Save as… (Ctrl+Shift+S)", "Salvar como… (Ctrl+Shift+S)"},
    {"Save graph as (.nodesim)", "Salvar grafo como (.nodesim)"},
    {"(type a name)", "(digite um nome)"},
    {"new file — choose Save as (Ctrl+Shift+S) to set the path",
     "novo arquivo — escolha Salvar como (Ctrl+Shift+S) para definir o caminho"},
    {"Save: initial state", "Salvar: estado inicial"},
    {"Save: current state", "Salvar: estado atual"},
    {"Save (initial)", "Salvar (inicial)"},
    {"Save (current)", "Salvar (atual)"},
    {"Load (L)", "Carregar (L)"},
    {"Export diagram SVG (Ctrl+Shift+D)", "Exportar diagrama SVG (Ctrl+Shift+D)"},
    {"Export diagram PNG (Ctrl+Shift+P)", "Exportar diagrama PNG (Ctrl+Shift+P)"},
    {"Quit", "Sair"},
    {"Start/Stop (Space)", "Iniciar/Parar (Espaço)"},
    {"Start", "Iniciar"},
    {"Stop", "Parar"},
    {"Single step (.)", "Passo único (.)"},
    {"Step", "Passo"},
    {"Advance N (Shift+.)", "Avançar N (Shift+.)"},
    {"Advance N", "Avançar N"},
    {"Reset to loaded (F5)", "Reiniciar ao carregado (F5)"},
    {"Reset", "Reiniciar"},
    {"Converge cycles: off", "Convergir ciclos: desligado"},
    {"Converge cycles: on", "Convergir ciclos: ligado"},
    {"Discrete events: off", "Eventos discretos: desligados"},
    {"Discrete events: on", "Eventos discretos: ligados"},
    {"Event sources… (F2)", "Fontes de evento… (F2)"},
    {"Events (F2)", "Eventos (F2)"},
    {"Iterations… (I)", "Iterações… (I)"},
    {"Node scope (O)", "Scope do nó (O)"},
    {"View Scope (O)", "Ver Scope (O)"},
    {"Export scope SVG (Ctrl+E)", "Exportar scope SVG (Ctrl+E)"},
    {"Export scope PNG (Ctrl+Shift+E)", "Exportar scope PNG (Ctrl+Shift+E)"},
    {"Undo (Ctrl+Z)", "Desfazer (Ctrl+Z)"},
    {"Redo (Ctrl+Y)", "Refazer (Ctrl+Y)"},
    {"Select all (Ctrl+A)", "Selecionar tudo (Ctrl+A)"},
    {"Copy (Ctrl+C)", "Copiar (Ctrl+C)"},
    {"Paste (Ctrl+V)", "Colar (Ctrl+V)"},
    {"Duplicate (Ctrl+D)", "Duplicar (Ctrl+D)"},
    {"Delete (Del)", "Excluir (Del)"},
    {"Group selection (Ctrl+G)", "Agrupar seleção (Ctrl+G)"},
    {"Ungroup (Ctrl+Shift+G)", "Desagrupar (Ctrl+Shift+G)"},
    {"Rotate sockets (T)", "Girar sockets (T)"},
    {"Rotate sockets -90° (Shift+T)", "Girar sockets -90° (Shift+T)"},
    {"Rotate all sockets (Ctrl+T)", "Girar sockets de tudo (Ctrl+T)"},
    {"Rotate all sockets -90° (Ctrl+Shift+T)", "Girar sockets de tudo -90° (Ctrl+Shift+T)"},
    {"Rotate layout +45 (Alt+T)", "Girar layout +45 (Alt+T)"},
    {"Rotate layout -45 (Alt+Shift+T)", "Girar layout -45 (Alt+Shift+T)"},
    {"Left (Alt+Left)", "Esquerda (Alt+Left)"},
    {"Center X", "Centro X"},
    {"Right (Alt+Right)", "Direita (Alt+Right)"},
    {"Top (Alt+Up)", "Topo (Alt+Up)"},
    {"Center Y", "Centro Y"},
    {"Bottom (Alt+Down)", "Base (Alt+Down)"},
    {"Distribute horizontally", "Espaçar horizontal"},
    {"Distribute vertically", "Espaçar vertical"},
    {"Basic", "Básico"},
    {"Script", "Script"},
    {"Join (N→1)", "Junta (N→1)"},
    {"Split (1→N)", "Divisor (1→N)"},
    {"Scope (sink)", "Scope (sink)"},
    {"Map (GeoJSON)", "Mapa (GeoJSON)"},
    {"Find node… (Ctrl+K)", "Buscar nó… (Ctrl+K)"},
    {"Plugins", "Plugins"},
    {"Values (V)", "Valores (V)"},
    {"Values", "Valores"},
    {"Grid (G)", "Grade (G)"},
    {"Grid", "Grade"},
    {"Straight wires", "Fios retos"},
    {"Curved wires (C)", "Fios curvos (C)"},
    {"Curved wires", "Fios curvos"},
    {"Graph diagnostics (F3)", "Diagnóstico do grafo (F3)"},
    {"Export theme: current", "Tema das exportações: atual"},
    {"Export theme: default", "Tema das exportações: padrão"},
    {"Export theme: Omarchy", "Tema das exportações: Omarchy"},
    {"nodesim (default)", "nodesim (padrão)"},
    {"current", "atual"}, {"default", "padrão"}, {"Omarchy", "Omarchy"},
    {"Step (R)", "Passo (R)"},
    {"Zoom +", "Zoom +"},
    {"Zoom -", "Zoom -"},
    {"Zoom 1:1 (0)", "Zoom 1:1 (0)"},
    {"Zoom 1:1", "Zoom 1:1"},
    {"Shortcuts and first steps (F1)", "Atalhos e primeiros passos (F1)"},
    {"About", "Sobre"},
    {"Language: English", "Idioma: Inglês"},
    {"Language: Português (Brasil)", "Idioma: Português (Brasil)"},
    // ---- simulacro native front-end ----
    {"New", "Novo"},
    {"Exit", "Sair"},
    {"Undo", "Desfazer"},
    {"Redo", "Refazer"},
    {"Cut", "Recortar"},
    {"Copy", "Copiar"},
    {"Paste", "Colar"},
    {"Duplicate", "Duplicar"},
    {"Delete", "Excluir"},
    {"Select All", "Selecionar tudo"},
    {"Group selection", "Agrupar seleção"},
    {"Align", "Alinhar"},
    {"Align left", "Alinhar à esquerda"},
    {"Align right", "Alinhar à direita"},
    {"Align top", "Alinhar ao topo"},
    {"Align bottom", "Alinhar embaixo"},
    {"Align horizontal centers", "Centralizar na horizontal"},
    {"Align vertical centers", "Centralizar na vertical"},
    {"Distribute horizontally", "Distribuir na horizontal"},
    {"Distribute vertically", "Distribuir na vertical"},
    {"select two or more nodes to align", "selecione dois ou mais nós para alinhar"},
    {"Ungroup", "Desagrupar"},
    {"Map scope", "Mapa"},
    {"Scope", "Scope"},
    {"no values", "sem valores"},
    {"x", "x"},
    {"y", "y"},
    {"Map scope (GeoJSON)", "Mapa (GeoJSON)"},
    {"Join (multiple inputs)", "Junta (várias entradas)"},
    {"Split (multiple outputs)", "Divisor (várias saídas)"},
    {"Show values", "Mostrar valores"},
    {"Zoom In", "Aproximar"},
    {"Zoom Out", "Afastar"},
    {"Reset zoom", "Redefinir zoom"},
    {"Center on graph", "Centralizar no grafo"},
    {"Nodes", "Nodos"},
    {"Nodes palette", "Paleta de nodos"},
    {"Scopes", "Scopes"},
    {"Language", "Idioma"},
    {"Reload theme", "Recarregar tema"},
    {"Simulation", "Simulação"},
    {"Start/Pause", "Iniciar/Pausar"},
    {"Start (Space)", "Iniciar (Espaço)"},
    {"Pause (Space)", "Pausar (Espaço)"},
    {"Clear scopes", "Limpar scopes"},
    {"Rate", "Taxa"},
    {"Stop after", "Parar após"},
    {"Infinite", "Infinito"},
    {"Converge cycles", "Convergir ciclos"},
    {"Discrete events", "Eventos discretos"},
    {"Keyboard shortcuts", "Atalhos de teclado"},
    {"Search palette…", "Buscar na paleta…"},
    {"Export Scope SVG…", "Exportar Scope SVG…"},
    {"Export Scope as SVG", "Exportar Scope como SVG"},
    {"Export Scope as PNG", "Exportar Scope como PNG"},
    {"Export SVG…", "Exportar SVG…"},
    {"Export PNG…", "Exportar PNG…"},
    {"Fit view", "Ajustar visão"},
    {"Open Scope", "Abrir Scope"},
    {"Edit script", "Editar script"},
    {"Edit script…", "Editar script…"},
    {"Add Script here", "Adicionar Script aqui"},
    {"Add Scope here", "Adicionar Scope aqui"},
    {"Add Join here", "Adicionar Junta aqui"},
    {"Add Split here", "Adicionar Divisor aqui"},
    {"Add script node", "Adicionar nó Script"},
    {"Add scope node", "Adicionar nó Scope"},
    {"Add failed", "Falha ao adicionar"},
    {"Open failed", "Falha ao abrir"},
    {"Save failed", "Falha ao salvar"},
    {"Export failed", "Falha na exportação"},
    {"Script error", "Erro de script"},
    {"Save changes before continuing?", "Salvar alterações antes de continuar?"},
    {"Save As", "Salvar como"},
    {"Save As…", "Salvar como…"},
    {"Save initial state", "Salvar estado inicial"},
    {"Copied selection", "Seleção copiada"},
    {"select a grouped node", "selecione um nó agrupado"},
    {"open a Scope first", "abra um Scope primeiro"},
    {"could not add node", "não foi possível adicionar o nó"},
    {"untitled", "sem título"},
    {"simulacro", "simulacro"},
    {"nodesim (*.nodesim)|*.nodesim|All files|*",
     "nodesim (*.nodesim)|*.nodesim|Todos os arquivos|*"},
    {"wxWidgets front-end for the nodesim engine.",
     "Interface wxWidgets para o motor nodesim."},
    {"Node graph editor and simulator (embedded nodesim engine).",
     "Editor e simulador de grafos de nós (motor nodesim incorporado)."},
    {"RUNNING", "EXECUTANDO"},
    {"PAUSED", "PAUSADO"},
    {"groups", "grupos"},
    {"steps", "passos"},
    {"steps/s", "passos/s"},
    // ---- properties: direction choices ----
    {"Left", "Esquerda"}, {"Top", "Topo"},
    {"Right", "Direita"}, {"Bottom", "Base"},
    // ---- palette / script editor ----
    {"BASICS", "BÁSICOS"},
    {"TEXT", "TEXTO"},
    {"INPUTS", "ENTRADAS"},
    {"Search node types…", "Buscar tipos de nó…"},
    {"Double-click to add at the view center.",
     "Duplo-clique para adicionar no centro da visão."},
    {"(no match)", "(sem correspondência)"},
    {"(no inputs connected)", "(nenhuma entrada ligada)"},
    {"OK — script compiles.", "OK — script compila."},
    {"';' separates ports · '#' comments · constants from params",
     "';' separa portas · '#' comenta · constantes dos params"},
    {"number of input sockets", "número de sockets de entrada"},
    {"k=value, name=\"text\" — constants available to the script",
     "k=valor, nome=\"texto\" — constantes disponíveis para o script"},
    // ---- scope menu / properties ----
    {"Clear history", "Limpar histórico"},
    {"Freeze", "Congelar"},
    {"Unfreeze", "Descongelar"},
    {"Could not write the file.", "Não foi possível gravar o arquivo."},
    {"GeoJSON paths separated by |", "Caminhos GeoJSON separados por |"},
    {"input side", "lado in"},
    {"output side", "lado out"},
    {"label field", "campo rótulo"},
    {"node %d", "nó %d"},
    {"auto Y", "Y automático"},
    {"visible seconds", "segundos visíveis"},
    {"y min", "y mín"},
    {"y max", "y máx"},
    {"zero", "zero"},
    {"error", "erro"},
    // ---- shortcuts dialog ----
    {"Canvas\n"
     "  Left drag: move nodes / select\n"
     "  Left drag on a socket: create a wire\n"
     "  Middle drag: pan   ·   Wheel: zoom\n"
     "  Double-click: open script or scope\n"
     "  Space: start/pause   ·   F5: reset   ·   F6: step\n"
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
     "  Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S",
     "Canvas\n"
     "  Arrastar com o esquerdo: mover nós / selecionar\n"
     "  Arrastar com o esquerdo num socket: criar fio\n"
     "  Arrastar com o meio: mover   ·   Roda: zoom\n"
     "  Duplo-clique: abrir script ou scope\n"
     "  Espaço: iniciar/pausar   ·   F5: reiniciar   ·   F6: passo\n"
     "  V: valores   ·   G: grade   ·   C: fios curvos   ·   O: abrir scope\n"
     "\n"
     "Edição\n"
     "  Ctrl+Z / Ctrl+Y: desfazer/refazer\n"
     "  Ctrl+C / Ctrl+V / Ctrl+D: copiar/colar/duplicar\n"
     "  Del: excluir seleção   ·   Ctrl+A: selecionar tudo\n"
     "  Ctrl+G / Ctrl+Shift+G: agrupar/desagrupar\n"
     "  Ctrl+K: buscar na paleta\n"
     "\n"
     "Arquivos\n"
     "  Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S"},
    // ---- toolbar / sections ----
    {"New Scope", "Novo Scope"},
    {"ADD", "ADICIONAR"},
    {"SIMULATION", "SIMULAÇÃO"},
    {"FILE", "ARQUIVO"},
    {"SHOW", "EXIBIR"},
    {"ZOOM", "ZOOM"},
    {"EDIT", "EDIÇÃO"},
    {"Open…", "Abrir…"},
    {"Open graph (.nodesim)", "Abrir grafo (.nodesim)"},
    {"Choose file", "Escolher arquivo"},
    {"Save", "Salvar"},
    {"Clear", "Limpar"},
    {"Rate: 60 Hz", "Taxa: 60 Hz"},
    // ---- tool status ----
    {"PAUSED ", "PAUSADO "},
    {"autosave ✓", "autosave ✓"},
    {"F1 help · Ctrl+K search · F2 events · autosave pending",
     "F1 ajuda · Ctrl+K buscar · F2 eventos · autosave pendente"},
    {"F1 help · Ctrl+K search · F2 events", "F1 ajuda · Ctrl+K buscar · F2 eventos"},
    // ---- help overlay ----
    {"First steps", "Primeiros passos"},
    {"Shortcuts", "Atalhos"},
    {"1. Ctrl+K searches and adds a node; N creates a Script.",
     "1. Ctrl+K busca e adiciona um nó; N cria um Script."},
    {"2. Select a node: the right panel edits title, script and parameters.",
     "2. Selecione um nó: o painel direito edita título, script e parâmetros."},
    {"3. Space starts/pauses; . steps once; Shift+. advances N steps.",
     "3. Espaço inicia/pausa; . dá um passo; Shift+. avança N passos."},
    {"4. Ctrl+S saves; autosave recovers edits if the app crashes.",
     "4. Ctrl+S salva; o autosave recupera edições se o app cair."},
    {"5. F2 configures event sources; O opens the node Scope.",
     "5. F2 configura fontes de evento; O abre o Scope do nó."},
    {"6. Drag the background to select; wheel = zoom; middle button = pan.",
     "6. Arraste o fundo para selecionar; roda = zoom; botão do meio = mover."},
    {"7. Connect output→input or input→output; double-click/drag a wire to branch.",
     "7. Ligue saída→entrada ou entrada→saída; duplo-clique/arraste num fio p/ ramificar."},
    {"8. A group has ports on its edges (collapsed or expanded): connect like a node.",
     "8. Grupo tem portas nas bordas (recolhido ou expandido): ligue como num nó."},
    {"9. Drag a node inside the box to group it; ports connect internal nodes.",
     "9. Arraste um nó para dentro do box para agrupá-lo; portas ligam nós internos."},
    {"Ctrl+N new · Ctrl+O open · Ctrl+S save · Ctrl+Shift+S save as · Ctrl+Z/Ctrl+Y undo/redo",
     "Ctrl+N novo · Ctrl+O abrir · Ctrl+S salvar · Ctrl+Shift+S salvar como · Ctrl+Z/Ctrl+Y desfazer/refazer"},
    {"Ctrl+C/V/D copy/paste/duplicate · Del delete",
     "Ctrl+C/V/D copiar/colar/duplicar · Del excluir"},
    {"Ctrl+K find node · N Script · F2 events · O Scope",
     "Ctrl+K buscar nó · N Script · F2 eventos · O Scope"},
    {"Space run · . step · Shift+. advance · F5 reset",
     "Espaço rodar · . passo · Shift+. avançar · F5 reiniciar"},
    {"V values · G grid · C straight/curved wires · F3 diagnostics",
     "V valores · G grade · C fios retos/curvos · F3 diagnóstico"},
    {"R rate · I iterations · Ctrl+G group · Ctrl+Shift+G ungroup",
     "R taxa · I iterações · Ctrl+G agrupar · Ctrl+Shift+G desagrupar"},
    {"Alt+arrows align · T rotate sockets · Alt+T rotate layout",
     "Alt+setas alinhar · T girar sockets · Alt+T girar layout"},
    {"Ctrl+E export SVG · Ctrl+Shift+E export PNG",
     "Ctrl+E exportar SVG · Ctrl+Shift+E exportar PNG"},
    {"Ctrl+Shift+D/P exports the graph diagram (SVG/PNG).",
     "Ctrl+Shift+D/P exporta o diagrama do grafo (SVG/PNG)."},
    {"F1 help · F3 diagnostics · Esc closes/cancels",
     "F1 ajuda · F3 diagnóstico · Esc fecha/cancela"},
    {"Theme: follows Omarchy automatically (NODESIM_THEME=off disables).",
     "Tema: segue o Omarchy automaticamente (NODESIM_THEME=off desliga)."},
    {"Figures: View ▸ Export theme (current/default/Omarchy).",
     "Figuras: Ver ▸ Tema das exportações (atual/padrão/Omarchy)."},
    {"GPL-3.0-or-later · documentation in docs/ · F1 closes this window",
     "GPL-3.0-or-later · documentação em docs/ · F1 fecha esta janela"},
    {"Help — nodesim", "Ajuda — nodesim"},
    {"first steps, shortcuts and a view of the current contract",
     "primeiros passos, atalhos e uma visão do contrato atual"},
    {"Close (Esc)", "Fechar (Esc)"},
    {"wheel/↑↓ scroll · F1 or Esc closes · docs/README.md has the index",
     "roda/↑↓ rola · F1 ou Esc fecha · docs/README.md tem o índice"},
    // ---- properties panel ----
    {"Properties", "Propriedades"},
    {"Properties panel (Ctrl+B)", "Painel de propriedades (Ctrl+B)"},
    {"Detach active view", "Destacar aba ativa"},
    {"Dock floating views", "Encaixar janelas flutuantes"},
    {"no selection", "nenhuma seleção"},
    {"GROUP", "GRUPO"},
    {"SELECTION", "SELEÇÃO"},
    {"SCENE", "CENA"},
    {"IDENTITY", "IDENTIDADE"},
    {"TRANSFORMATION", "TRANSFORMAÇÃO"},
    {"INPUT/OUTPUT", "ENTRADA/SAÍDA"},
    {"MAP", "MAPA"},
    {"GRAPH", "GRÁFICO"},
    {"ACTIONS", "AÇÕES"},
    {"PARAMETERS", "PARÂMETROS"},
    {"title", "título"},
    {"members", "membros"},
    {"collapsed", "recolhido"},
    {"fit frame", "ajustar quadro"},
    {"ungroup", "desagrupar"},
    {"nodes", "nodos"}, {"finding(s)", "achado(s)"},
    {"error(s)", "erro(s)"}, {"warning(s)", "aviso(s)"},
    {"wires", "fios"},
    {"time", "tempo"},
    {"iterations", "iterações"},
    {"type", "tipo"},
    {"id", "id"},
    {"plugin", "plugin"},
    {"group", "grupo"},
    {"shape", "forma"},
    {"own", "própria"},
    {"(type)", "(tipo)"},
    {"in side", "lado in"},
    {"out side", "lado out"},
    {"channels", "canais"},
    {"inputs", "entradas"},
    {"outputs", "saídas"},
    {"0 (sink)", "0 (sink)"},
    {"(by columns)", "(por colunas)"},
    {"value", "valor"},
    {"file", "arquivo"},
    {"(none)", "(nenhum)"},
    {"overlay", "sobrepor"},
    {"id field", "campo id"},
    {"label", "rótulo"},
    {"projection", "projeção"},
    {"overlay proj.", "proj. sobrepor"},
    {"overlay label", "rótulo sobrepor"},
    {"color field", "campo de cor"},
    {"palette", "paleta"},
    {"scale", "escala"},
    {"auto", "auto"},
    {"fixed", "fixo"},
    {"id column", "coluna id"},
    {"value column", "coluna valor"},
    {"labels", "rótulos"},
    {"legend", "legenda"},
    {"fit map", "ajustar mapa"},
    {"window (s)", "janela (s)"},
    {"Y scale", "escala Y"},
    {"decimation", "decimação"},
    {"zero line", "linha zero"},
    {"x channel", "canal x"},
    {"y channel", "canal y"},
    {"bins", "faixas"},
    {"freeze", "congelar"},
    {"clear history", "limpar histórico"},
    {"script", "script"},
    {"params", "params"},
    {"(empty)", "(vazio)"},
    {"!error", "!erro"},
    {"separate paths with | (or comma/semicolon)",
     "separe caminhos com | (ou vírgula/ponto e vírgula)"},
    {"yes", "sim"},
    {"no", "não"},
    {"on", "on"},
    {"off", "off"},
    {"gray", "cinza"},
    {"blue-yellow-red", "azul-amarelo-vermelho"},
    // ---- diagnostics ----
    {"Graph diagnostics", "Diagnóstico do grafo"},
    {"Close (Esc) ", "Fechar (Esc) "},
    {"click/Enter selects the node · ↑↓ scrolls · F3 or Esc closes",
     "clique/Enter seleciona o nó · ↑↓ rola · F3 ou Esc fecha"},
    {"No problem found. Scripts compile, inputs are connected and there are no cycles.",
     "Nenhum problema encontrado. Scripts compilam, entradas ligadas e sem ciclos."},
    {"non-finite outputs fixed in total: ", "saídas não finitas corrigidas no total: "},
    {"outputs stuck at ±1e9: ", "saídas presas em ±1e9: "},
    {"steps that did not converge: ", "passos que não convergiram: "},
    {"plugin snapshot failures: ", "falhas de snapshot de plugin: "},
    {"script: ", "script: "},
    {"empty script", "script vazio"},
    {"plugin without type", "plugin sem tipo"},
    {"plugin not loaded: ", "plugin não carregado: "},
    {"multiplexer without inputs", "multiplexador sem entradas"},
    {"input without wire", "entrada sem fio"},
    {"none of the inputs has a wire", "nenhuma das entradas tem fio"},
    {"unused output", "saída sem uso"},
    {"non-finite outputs: ", "saídas não finitas: "},
    {"outputs stuck at the limit: ", "saídas presas no limite: "},
    {"reads the previous step (cycle or downstream of it)",
     "lê o passo anterior (ciclo ou a jusante dele)"},
    {"orphan wire: ", "fio órfão: "},
    {"node ", "nó "},
    {"findings", "achados"},
    {"errors", "erros"},
    {"warnings", "avisos"},
    // ---- event sources ----
    {"Event sources (F2)", "Fontes de evento (F2)"},
    {"Discrete events: ", "Eventos discretos: "},
    {" · execution configuration (not written to the .nodesim)",
     " · configuração de execução (não vai para o .nodesim)"},
    {"node", "nó"},
    {"port", "porta"},
    {"edge", "borda"},
    {"threshold", "limiar"},
    {"source", "origem"},
    {"host", "host"},
    {"(missing)", "(ausente)"},
    {"no source — press A to add an output port",
     "nenhuma fonte — pressione A para adicionar uma porta de saída"},
    {"Add", "Adicionar"},
    {"Remove", "Remover"},
    {"Close", "Fechar"},
    {"↑↓ select · Enter/E threshold · K edge · A add · Del remove · Space toggle · Esc close",
     "↑↓ seleciona · Enter/E limiar · K borda · A adiciona · Del remove · Espaço liga · Esc fecha"},
    {"all output ports already have an event source",
     "todas as portas de saída já têm fonte de evento"},
    {"event source added — discrete events enabled",
     "fonte de evento adicionada — eventos discretos ligados"},
    {"source declared by the plugin: read-only; add the same port to override",
     "fonte declarada pelo plugin: somente leitura; adicione a mesma porta para sobrescrever"},
    {"event source removed", "fonte de evento removida"},
    {"change does not use a threshold; K switches to rise/fall",
     "change não usa limiar; K alterna para rise/fall"},
    {"invalid threshold: use a finite number (e.g. 0.5)",
     "limiar inválido: use um número finito (ex.: 0.5)"},
    // ---- palette ----
    {"Add node", "Adicionar nó"},
    {"type to filter…", "digite para filtrar…"},
    {"no node found", "nenhum nó encontrado"},
    {"↑↓ navigate · Enter insert · Esc close", "↑↓ navegar · Enter inserir · Esc fechar"},
    // ---- file dialog ----
    {"File", "Arquivo"},
    {"Enter opens · Esc cancels", "Enter abre · Esc cancela"},
    {"Open", "Abrir"},
    {"Cancel", "Cancelar"},
    // ---- script editor ----
    {"no inputs — in(i) is 0", "sem entradas — in(i) vale 0"},
    {"Ctrl+Enter applies · Esc closes · ';' separates · '#' comments · Ctrl+Up/Down history",
     "Ctrl+Enter aplica · Esc fecha · ';' separa · '#' comenta · Ctrl+Up/Down histórico"},
    {"valid expression", "expressão válida"},
    {"Apply", "Aplicar"},
    {"script — node ", "script — nó "},
    {"current output: ", "saída atual: "},
    // ---- app messages ----
    {" — save with Ctrl+S and check space/permissions",
     " — salve com Ctrl+S e verifique espaço/permissões"},
    {" — keeping the saved file; see docs/solucao-de-problemas.md",
     " — mantendo o arquivo salvo; veja docs/solucao-de-problemas.md"},
    {"autosave recovered (not saved)", "autosave recuperado (não salvo)"},
    {" could not be opened '", " não consegui abrir '"},
    {"' — using the startup example. ", "' — usando o exemplo inicial. "},
    {" — simulation stopped", " — simulação parada"},
    {" — the previous graph was kept; ", " — o grafo anterior foi mantido; "},
    {"exports: theme ", "exportações: tema "},
    {"saved: ", "salvo: "},
    {"loaded: ", "carregado: "},
    {"cleared", "limpo"},
    {"autosave failed: ", "autosave falhou: "},
    {"events enabled but no sources — F2 adds an output port",
     "eventos ligados, mas sem fontes — F2 adiciona uma porta de saída"},
    {"scope: select a SCOPE node (or double-click it)",
     "scope: selecione um nó SCOPE (ou dê duplo-clique nele)"},
    {"scope: select a SCOPE node (or open the window)",
     "scope: selecione um nó SCOPE (ou abra a janela)"},
    {"scope: node %d open\n", "scope: nó %d aberto\n"},
    {"select one or more nodes to group", "selecione um ou mais nós para agrupar"},
    {"no group in the selection (or under the mouse)",
     "nenhum grupo na seleção (ou sob o mouse)"},
    {"group removed; nodes stay in place", "grupo desfeito; os nós continuam no lugar"},
    {"internal connection to a collapsed group: expand the group to connect those nodes",
     "conexão interna a um grupo recolhido: expanda o grupo para ligar esses nós"},
    {"a box output does not receive wires; connect internal outputs -> output port",
     "uma saída do box não recebe fios; ligue saídas internas -> porta de saída"},
    {"plugin node: no editor (behavior comes from the .so)",
     "nó plugin: sem editor (comportamento vem do .so)"},
    {"simulation reset (load state, t=0)", "simulação reiniciada (estado da carga, t=0)"},
    {"missing plugin: %s (node %d has no behavior)",
     "plugin ausente: %s (nó %d sem comportamento)"},
    {"plugin %s: state of node %d rejected", "plugin %s: estado do nó %d rejeitado"},
    {"simulation paused by ", "simulação pausada por "},
    {"simulation: paused for %u ms by control #%d (%s)",
     "simulação: pausada por %u ms pelo controle #%d (%s)"},
    {"simulation resumed after a pause of ", "simulação retomada após pausa de "},
    {"simulation: resumed after a pause of %u ms", "simulação: retomada após pausa de %u ms"},
    {"simulation paused by control #", "simulação pausada pelo controle #"},
    {"simulation: paused by control #%d (%s)", "simulação: pausada pelo controle #%d (%s)"},
    {"simulation: paused (%d iterations)", "simulação: pausada (%d iterações)"},
    {"autosave", "autosave"},
    {"select one or more nodes to group", "selecione um ou mais nós para agrupar"},
    {"no group in the selection (or under the mouse)", "nenhum grupo na seleção (ou sob o mouse)"},
    {"group removed; nodes stay in place", "grupo desfeito; os nós continuam no lugar"},
    {"group created: drag the bar to move, double-click to collapse", "grupo criado: arraste a barra para mover, duplo-clique recolhe"},
    {"internal connection to a collapsed group: expand the group to connect those nodes", "conexão interna a um grupo recolhido: expanda o grupo para ligar esses nós"},
    {"a box output does not receive wires; connect internal outputs -> output port", "uma saída do box não recebe fios; ligue saídas internas -> porta de saída"},
    {"plugin node: no editor (behavior comes from the .so)", "nó plugin: sem editor (comportamento vem do .so)"},
    {"added: ", "adicionado: "},
    {"autosave recovered", "autosave recuperado"},
    {"node graph panel for simulation", "painel de nodos para simulação"},
    {"autosave recovered (not saved)", "autosave recuperado (não salvo)"},
    {"script — node ", "script — nó "},
    {"edit", "edição"},
};

const std::unordered_map<std::string, std::string>& exactMap() {
    static const std::unordered_map<std::string, std::string> m = [] {
        std::unordered_map<std::string, std::string> out;
        out.reserve(sizeof kEntries / sizeof *kEntries * 2);
        for (const auto& e : kEntries) out.emplace(e.en, e.pt);
        return out;
    }();
    return m;
}

// Dynamic strings that start with a known English phrase. The longest prefix
// wins, so specific rules must stay before shorter ones.
struct Prefix { const char* en; const char* pt; };
const Prefix kPrefixes[] = {
    {"Iterations: ", "Iterações: "},
    {"Rate: ", "Taxa: "},
    {"Language: ", "Idioma: "},
    {"Name: ", "Nome: "},
    {"PAUSED t=", "PAUSADO t="},
    {"PAUSED ", "PAUSADO "},
    {"RUNNING ", "EXECUTANDO "},
    {"Converge cycles: ", "Convergir ciclos: "},
    {"Discrete events: ", "Eventos discretos: "},
    {"Export theme: ", "Tema das exportações: "},
    {"Saved: ", "Salvo: "},
    {"Saved ", "Salvo "},
    {"Loaded ", "Carregado "},
    {"saved: ", "salvo: "},
    {"loaded: ", "carregado: "},
    {"exports: theme ", "exportações: tema "},
    {"simulation paused by control #", "simulação pausada pelo controle #"},
    {"simulation paused by ", "simulação pausada por "},
    {"simulation resumed after a pause of ", "simulação retomada após pausa de "},
    {"simulation: paused for ", "simulação: pausada por "},
    {"simulation: resumed after a pause of ", "simulação: retomada após pausa de "},
    {"simulation: paused by control #", "simulação: pausada pelo controle #"},
    {"simulation: paused (", "simulação: pausada ("},
    {"simulation: ", "simulação: "},
    {"scope: node ", "scope: nó "},
    {"scope: ", "scope: "},
    {"event source added", "fonte de evento adicionada"},
    {"event source removed", "fonte de evento removida"},
    {"no group", "nenhum grupo"},
    {"group removed", "grupo desfeito"},
    {"autosave failed: ", "autosave falhou: "},
    {"autosave ", "autosave "},
    {"nodes: ", "nodos: "},
    {"wires: ", "fios: "},
    {"node ", "nó "},
    {"map: ", "mapa: "},
    {"diagram: ", "diagrama: "},
    {"theme: ", "tema: "},
    {"language: ", "idioma: "},
    {"added: ", "adicionado: "},
    {"could not save '", "não foi possível salvar '"},
    {"could not load '", "não foi possível carregar '"},
    {"could not open '", "não foi possível abrir '"},
};


const Entry kFragments[] = {
    {" — simulation stopped", " — simulação parada"},
    {" — check permissions/space; autosave continues in ~/.local/state/nodesim",
     " — verifique permissões/espaço; o autosave continua em ~/.local/state/nodesim"},
    {" — check permissions", " — verifique permissões"},
    {" — check directory permissions", " — verifique permissões do diretório"},
    {" — the previous graph was kept; see docs/solucao-de-problemas.md",
     " — o grafo anterior foi mantido; veja docs/solucao-de-problemas.md"},
    {" — save with Ctrl+S and check space/permissions",
     " — salve com Ctrl+S e verifique espaço/permissões"},
    {" — keeping the saved file; see docs/solucao-de-problemas.md",
     " — mantendo o arquivo salvo; veja docs/solucao-de-problemas.md"},
    {" — using the startup example. Check the format/backup and see docs/solucao-de-problemas.md",
     " — usando o exemplo inicial. Confira o formato/backup e veja docs/solucao-de-problemas.md"},
    {" (not saved)", " (não salvo)"},
};

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Serialized plugin/scope keys that are not English. The keys stay stable in
// files and in the ABI; only the label shown by the UI is translated.
const std::unordered_map<std::string, std::string>& paramMap() {
    static const std::unordered_map<std::string, std::string> m = {
        {"alpha", "alpha"}, {"ampl", "gain"}, {"antiwindup", "anti-windup"},
        {"b", "b"}, {"beta", "beta"}, {"borda", "edge"}, {"cabecalho", "header"},
        {"ciclo", "cycle"}, {"col", "col"}, {"coluna", "column"},
        {"constante", "constant"}, {"cor", "color"}, {"defasagens", "lags"},
        {"descida", "fall"}, {"desloc", "offset"}, {"dist", "dist"},
        {"duracao", "duration"}, {"escala", "scale"}, {"f", "f"}, {"f0", "f0"},
        {"f1", "f1"}, {"fase", "phase"}, {"fc", "fc"}, {"freq", "freq"},
        {"func", "func"}, {"g", "g"}, {"gamma", "gamma"}, {"ganho", "gain"},
        {"habilitar", "enable"}, {"header", "header"}, {"inclinacao", "slope"},
        {"inferencia", "inference"}, {"inicio", "start"}, {"janela", "window"},
        {"k", "k"}, {"kd", "kd"}, {"ki", "ki"}, {"kp", "kp"}, {"lambda", "lambda"},
        {"largura", "width"}, {"limiar", "threshold"}, {"limite", "limit"},
        {"loop", "loop"}, {"maximo", "max"}, {"media", "mean"}, {"minimo", "min"},
        {"mle", "mle"}, {"modelo", "model"}, {"modo", "mode"}, {"ms", "ms"},
        {"mu", "mu"}, {"n", "n"}, {"nivel", "level"}, {"ordem", "order"},
        {"p", "p"}, {"p0", "p0"}, {"periodo", "period"}, {"q", "q"},
        {"qs", "qs"}, {"r", "r"}, {"rate", "rate"}, {"salto", "jump"},
        {"semente", "seed"}, {"subida", "rise"}, {"t0", "t0"}, {"taxa", "rate"},
        {"window", "janela"}, {"rate", "taxa"}, {"seed", "semente"},
    {"model", "modelo"}, {"mode", "modo"}, {"constant", "constante"}, {"state", "estado"},
    {"tempo", "time"}, {"tf", "tf"}, {"timeout_ms", "timeout (ms)"},
        {"tipo", "type"}, {"unidade", "unit"}, {"watch", "watch"}, {"x", "x"},
        {"ycol", "y column"}, {"zona", "zone"}, {"arquivo", "file"}, {"caminho", "path"}, {"json", "json"},
        {"xcols", "x columns"}, {"hist", "history"}, {"eventos", "events"},
        {"estado", "state"}, {"formato", "format"}, {"sep", "sep"}, {"colunas", "columns"},
        {"hex", "hex"}, {"sobrepor", "overlay"}, {"idcampo", "id field"},
        {"rotulo", "label"}, {"projecao", "projection"}, {"paleta", "palette"},
        {"vmin", "vmin"}, {"vmax", "vmax"}, {"rotulos", "labels"},
        {"idcol", "id column"}, {"valcol", "value column"}, {"corcampo", "color field"},
        {"rotulo2", "overlay label"}, {"projecao2", "overlay projection"},
        {"idcampo2", "overlay id field"},
        {"linha", "line"}, {"xy", "xy"}, {"barras", "bars"},
        {"histograma", "histogram"}, {"mapa", "map"}, {"fft", "fft"},
        {"sim", "yes"}, {"não", "no"}, {"fixo", "fixed"}, {"auto", "auto"},
        {"ligado", "on"}, {"desligado", "off"}, {"padrão", "default"},
    };
    return m;
}

} // namespace

Lang lang() { return gLang; }
void setLang(Lang l) { gLang = l; }

void initFromEnv() {
    const char* env = std::getenv("NODESIM_LANG");
    if (!env || !*env) { gLang = Lang::EN; return; }
    std::string v = lower(env);
    if (v == "auto") {
        const char* loc = std::getenv("LC_ALL");
        if (!loc || !*loc) loc = std::getenv("LC_MESSAGES");
        if (!loc || !*loc) loc = std::getenv("LANG");
        v = loc ? lower(loc) : "en";
        gLang = v.rfind("pt", 0) == 0 ? Lang::PT_BR : Lang::EN;
        return;
    }
    if (!setFromCode(v)) gLang = Lang::EN;
}

bool setFromCode(const std::string& codeIn) {
    std::string v = lower(codeIn);
    if (v == "en" || v == "en-us" || v == "english" || v == "en_us") {
        gLang = Lang::EN;
        return true;
    }
    if (v == "pt" || v == "pt-br" || v == "pt_br" || v == "portuguese" ||
        v == "português" || v == "portugues") {
        gLang = Lang::PT_BR;
        return true;
    }
    return false;
}

std::string code() { return gLang == Lang::PT_BR ? "pt-BR" : "en"; }
std::string name() { return gLang == Lang::PT_BR ? "Português (Brasil)" : "English"; }

std::string text(const std::string& en) {
    if (gLang == Lang::EN || en.empty()) return en;
    const auto& m = exactMap();
    auto it = m.find(en);
    if (it != m.end()) return it->second;
    for (const auto& p : kPrefixes) {
        const size_t n = std::char_traits<char>::length(p.en);
        if (en.size() >= n && en.compare(0, n, p.en) == 0) {
            std::string out = p.pt + en.substr(n);
            for (const auto& f : kFragments) out = replaceAll(out, f.en, f.pt);
            return out;
        }
    }
    std::string out = en;
    for (const auto& f : kFragments) out = replaceAll(out, f.en, f.pt);
    return out;
}

std::string paramName(const std::string& key) {
    const auto& m = paramMap();
    auto it = m.find(key);
    return it != m.end() ? it->second : key;
}

} // namespace i18n
