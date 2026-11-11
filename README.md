# simulacro

An editor and simulator for node graphs, self-contained: wxWidgets (native
GTK3 chrome) around the engine that started its life as **nodesim**, now
embedded in this tree (`src/core/`, `plugins/*.cpp`). Keep this interface
native: wxWidgets owns the window, docks and dialogs; SDL is kept only where
it earns its keep, as the two custom raster views, off-screen and blitted into
wx panels:

- engine under `src/core/` and the built-in plugin sources under `plugins/`;
- built-in **controls as node plugins** built on the `nsui::Control` C++
  base (`src/sdk/nodesim_ui_base.h`): `push` — **botão pushdown clicável na
  FACE** (`NS_ACT_HOLD`: out [0;1] enquanto pressionado; nível de entrada
  simula a pressão e o rótulo ganha ` EXT`) e `switch` — **chave liga/
  desliga** (`NS_ACT_TOGGLE`: cada clique alterna e o valor FICA; out = 1
  ligada). Ambos com rótulo/cores editáveis no dock e margem interna via
  `NS_PLUGIN_ACTS_IN` (a borda do box fica para SELECT/DRAG). Com um
  controle selecionado, **Space aperta/solta** (HOLD) e **Enter alterna**
  (TOGGLE) no canvas. Os antigos `entrada01`/`saida01` saíram da biblioteca
  por decisão; voltam como plugins de terceiros pelo **mesmo caminho de
  herança** (`nsui::Control`, docs/abi.md);
- file format `.nodesim` (format 10) and plugin **ABI 14** unchanged, so
  existing files and plugins keep working as before;
- headless runner (`--run`), port traces and numeric regression through
  `tests/golden/`;
- **in-canvas parameter widgets (Dear ImGui)**: the selected plugin node
  hosts its parameter controls right in the drawing. Default mode `inner`
  anchors the control strip **inside the node body** (below the title; a
  narrow node switches to the compact layout where the control takes the
  full row and the name shows on hover). `SIMULACRO_NODE_UI=below` keeps the
  fase-1 rail under the node, and `SIMULACRO_NODE_UI=off` disables the
  in-canvas layer entirely (property grid only). The property grid stays the
  canonical editor and each gesture yields a single undo checkpoint.
  `SIMULACRO_IMGUI=0` (or `off`) disables the ImGui layer itself;
  `SIMULACRO_IMGUI_DEBUG=1` logs per-command render errors. Scope charts and
  maps never touch ImGui.
- **nodes as ImGui windows (fase 4)**: every non-custom node gets a rounded
  window chrome (type-colored title bar, title text, hover/selection outline)
  and the pending wire is drawn on top of nodes while connecting. In build
  defaults: compiled in (`SIMULACRO_NODE_WINDOW=1`);
- **plugin-authored layout (fase 3)**: a type can declare its in-node widget
  list with the optional `nodesim_plugin_ui()` symbol (`NS_PLUGIN_UI`), bound
  to `params[]`/`texts[]` (sliders, switches, enums, inputs, LEDs, labels).
  The declaration itself is an ABI-additive contract and is active in every
  build — the *canvas rendering* of these controls (the fase 1..3 property
  popup in the node) is the part hidden behind `SIMULACRO_NODE_UI`
  (compile-time, default off). See docs/architecture.md for the boundary.
  Built-ins already declaring it: `led` (RGB sliders) and `pgain` (gain
  slider). Details: `docs/abi.md`.
  `examples/painel-innode.nodesim` (script → pgain → LED) exercises both
  layers when the flag is on;
- plugin discovery folder `~/.local/share/nodesim/plugins` and the `NODESIM_*`
  environment variables are compatibility surfaces: do not rename them.

Historical note: this project absorbed the former `nodesim` program (engine,
plugins, examples and tests). The old tree is archived; nothing here reads
from it. The public contracts listed above are frozen by tests, not by the
old repository's existence.

The native shell:
- native window, menus, toolbar, status bar and dialogs (GTK3);
- dockable panes via `wxAuiManager` (nodes palette, properties, scopes);
- a real property editor (`wxPropertyGrid`) for node, script, plugin and
  scope settings;
- SDL for the node canvas and the scope charts.

## Requirements

- C++17 compiler
- wxWidgets 3.2 with `aui` and `propgrid` (`wx-config --libs std,aui,propgrid`)
- SDL2 and SDL2_ttf
- A TrueType monospace font (Liberation/Noto/DejaVu are probed)

On Arch/Omarchy:

```sh
sudo pacman -S wxwidgets-gtk3 sdl2 sdl2_ttf
```

## Build

```sh
make            # app at bin/simulacro
make plugins    # ABI 14 plugins at plugins/*.so
```

Plugin sources live in `plugins/*.cpp` (next to `plugins/common.h`, the
helper for built-ins) and compile against the SDK that ships in
`src/sdk/nodesim_plugin.h`. Third-party plugins only need that header: they
do not include `common.h`.

## Run

```sh
./bin/simulacro                          # empty graph
./bin/simulacro examples/ar1.nodesim
./bin/simulacro --open-scopes FILE       # open every Scope page
./bin/simulacro --run FILE --steps 600   # headless CSV trace
./bin/simulacro --run FILE --summary     # final values only
```

`--run` prints one CSV column per Scope channel. Plugin discovery follows the
historical order: `NODESIM_PLUGIN_PATH`, `./plugins`,
`<prefix>/share/nodesim/plugins`, `~/.local/share/nodesim/plugins`, then the
XDG system data directories.

## Features

- Open, edit and save `.nodesim` files (format 10, ABI 14).
- Script nodes with a native multi-line editor, live compile feedback and the
  list of input sources.
- Plugin nodes: parameters, text fields, ports, custom drawing and state.
- Scope nodes: line/XY/bars/histogram/FFT/map charts, frozen history, SVG and
  PNG export. Maps support pan/zoom and fit (double-click).
- Visual groups (`Ctrl+G` / `Ctrl+Shift+G`).
- Undo/redo, copy/paste/duplicate, pan/zoom, wire selection, snap-free layout.
- Simulation transport: start/pause, step, reset, rate, stop-after, cycle
  convergence and discrete events.
- Omarchy theme bridge: the native chrome follows `colors.toml` and is polled
  while the app runs. UI language can be switched at runtime (English /
  Português do Brasil).
- Toolbar with **labels or icons-only** (View > Toolbar labels; saved in the
  session file).
- **Session state saved on close** (`~/.local/state/nodesim/ui.ini`): pane
  layout (AUI perspective), window geometry, canvas camera (zoom/pan — with
  a file open; an empty graph starts centered), View toggles (values/grid/
  curved wires) and UI language. Restored on the next start; `--lang` on the
  command line overrides the saved language.
- Headless runner for batch traces.

## Keyboard

| Key | Action |
| --- | --- |
| Space | start/pause (canvas focus); com um controle selecionado: aperta/solta (HOLD) |
| Ctrl+1 | Center on graph (View) |
| Scroll | pan horizontal/vertical da área de desenho |
| Ctrl+Scroll | zoom no cursor (Shift+Scroll também desloca na horizontal) |
| Enter | com um controle selecionado: alterna (TOGGLE) / pulso (HOLD) |
| F6 / F5 | step / reset |
| V / G / C | values / grid / curved wires |
| O | open the selected Scope |
| Ctrl+Z, Ctrl+Y | undo / redo |
| Ctrl+C, Ctrl+V, Ctrl+D | copy / paste / duplicate |
| Del, Ctrl+A | delete selection / select all |
| Ctrl+G, Ctrl+Shift+G | group / ungroup |
| Ctrl+Shift+L/R/T/B | align left / right / top / bottom (Edit > Align; também no dropdown da barra) |
| Ctrl+Shift+H/V | align horizontal / vertical centers |
| Ctrl+1 | center on graph |
| Ctrl+K | focus the node search |
| Ctrl+N/O/S, Ctrl+Shift+S | new / open / save / save as |
| F1 | shortcut help |

## Layout

```
src/core/      engine (graph, steps, expressions, scopes, maps, i18n key tables,
               theme, exporters)
src/sdk/       public plugin ABI header (ABI 14)
src/engine.*   headless model: graph, clock, scopes, undo, files
src/canvas.*   wxPanel node canvas (SDL off-screen renderer); split by
               responsibility: canvas_acts.cpp (NS_PLUGIN_ACTS clicks +
               keyboard), canvas_fit.cpp (auto-refit), canvas_chrome.cpp
               (fase 4 ImGui chrome), canvas_widgets.cpp (fase 1..3)
src/nodeui.*   policy of where in-node widget strips live (inner/below/off)
src/imgui_shell.* Dear ImGui shim: context/fonts/theme/events (fase 1)
third_party/imgui vendored (v1.93.0, SDL renderer backend)
src/properties.* wxPropertyGrid editor
src/palette.*  searchable node/plugin tree
src/scopeview.* Scope notebook and scope pages (SDL off-screen renderer)
src/scriptdlg.* native script editor
src/app.*      MainFrame, menus, docks, actions
src/main.cpp   GUI entry point and --run runner
plugins/       built-in plugin sources + common.h; build targets: plugins/*.so
examples/      sample graphs, GeoJSON overlays and manifests
tests/         run.sh smoke suite and tests/golden/*.csv numeric traces
```

See [docs/architecture.md](docs/architecture.md) for the layering rules.

## Regression discipline

The engine is deterministic: run thrice, get the same CSV. The smoke suite
compares `examples/ar1.nodesim` and `examples/contador_logico.nodesim` traces
against `tests/golden/*.csv`. Only regenerate the golden files when a change
in behavior is intended, and always with a note in the change description:

```sh
./bin/simulacro --run examples/ar1.nodesim --steps 60 > tests/golden/ar1.csv
./bin/simulacro --run examples/contador_logico.nodesim --steps 60 \
    > tests/golden/contador_logico.csv
```

## License

GPL-3.0-or-later. The engine and plugin sources started in the archived
`nodesim` project, by the same author; both trees remain GPL-3.0-or-later.
