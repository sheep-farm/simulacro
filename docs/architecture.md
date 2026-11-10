# Architecture

`simulacro` is self-contained: the engine that started its life in the
archived `nodesim` program now lives inside this tree, under `src/core/` and
`plugins/*.cpp`. The old *project boundary* is gone, but the *code boundary*
is deliberate and it keeps the same shape: the engine and the renderers know
nothing about wxWidgets, and the wx views never touch graph internals directly
except through `Engine`.

## Layers

```
+---------------------------------------------------------------+
| wxWidgets: MainFrame, menus, toolbar, AUI docks, dialogs      |
|   palette      properties      scope notebook      script dlg |
+--------------------+----------------------+-------------------+
| GraphCanvas        | ScopeView            | native panels     |
| (SDL off-screen)   | (SDL off-screen)     |                   |
+--------------------+----------------------+-------------------+
| Engine: graph, plugins, clock, scopes, undo, files             |
| src/core/: graph, expr, scope, draw, theme, map, export        |
| plugins/:  built-in plugin sources, compiled to plugins/*.so   |
+---------------------------------------------------------------+
```

- **Engine (`src/engine.*`)** owns the model: `Graph`, `PluginManager`,
  simulation clock, scope history, map caches and undo stacks. It offers
  coarse operations (`load`, `save`, `stepOnce`, `addPlugin`, `checkpoint`,
  `exportScopeSvg`) and no UI code.
- **Views** observe the engine. `GraphCanvas` is a `wxPanel` that hosts an
  off-screen `SdlCanvas`; `ScopeView` does the same for charts. They receive
  wx mouse/keyboard events, translate coordinates and call the engine.
- **Native chrome** is pure wxWidgets: `MainFrame` builds menus, the toolbar,
  the status bar and the AUI panes. `PropertiesPanel` uses `wxPropertyGrid`;
  `ScriptDialog` uses `wxTextCtrl`; `PalettePanel` uses `wxTreeCtrl`.

## Why SDL remains

The node canvas and the scope charts are immediate-mode renderers carried
over from the engine core (`drawWire`, `nodeBox`, `drawScope`,
`drawMapScope`). Rewriting them in wxGraphicsContext would duplicate a lot
of tested geometry. Instead:

1. `SdlCanvas::ensure(w,h)` creates an ARGB surface and a software renderer.
2. The renderer draws the frame with the core rasterizers.
3. `SdlCanvas::present(wxDC&)` converts the surface to RGB and blits it.

This keeps HiDPI straightforward (the panel size is the surface size) and
makes PNG export a copy of the same pixels.

## Threading and timing

Everything runs on the wx main thread. A `wxTimer` ticks at the selected rate
(1–60 steps/s). Each tick steps the engine once when running and repaints the
canvas; scope pages refresh on a slower cadence. No OpenGL/SDL window is
created, so there are no platform event loops to reconcile.

**Canvas refit (paint starvation):** while PAUSED / idle the wx canvas paints
only on damage — a load-time `requestFit()` (12 paints) may never run after
the AUI/panes settle. The auto-refit is therefore IMMEDIATE and bounded:

- `centerOnGraph()` records the pane size it fit into (`lastFitW_/lastFitH_`)
  and opens the bounded chain (`fitRound_`, ≤3);
- `onSize()` refits right away when the pane changes ≥25% against the last
  fit size;
- `ensureInView()` (bounded by the same chain) refits when the graph is less
  than 30% inside the visible viewport — the sliver case counts as invisible;
- a manual Fit resets the chain by bumping `fitRound_` from 0.

## Compliance

- `.nodesim` format 10 is read and written by the core; the smoke suite plus
  `tests/golden/*.csv` guard the step-by-step behavior.
- Plugins export the same ABI 14 symbols (`src/sdk/nodesim_plugin.h`); the
  built-in sources live in `plugins/` and compile to `plugins/*.so`.

## Dear ImGui layers ("node = window")

Fase 4 makes the *node drawing itself* window-shaped via ImGui. The SDL
renderer keeps the shared painters (wires, port labels/values, sparklines,
plugin draw hooks and group chrome) with the same visual hierarchy as before;
on top of it, an ImDrawList layer paints the *chrome* of every non-custom node
rounded window frame, colored title bar (type/plugin ring color), title text
at readable zoom, hover/selection outline and the pending wire in the
foreground (so it passes above nodes while connecting). Custom-shape plugins
keep drawing their whole silhouette themselves.

Compile-time switches (`Makefile`/`CMakeLists`):

- `SIMULACRO_NODE_WINDOW=1` (default on, the fase 4 layer) — can be compiled
  out to get the exact fase-2/3 canvas pipeline with no ImGui chrome.
- `SIMULACRO_NODE_UI` (default off) — the fase 1..3 *in-node parameter
  widgets* (rail, `inner` strip, plugin-authored layout). Kept in-tree,
  behind `#if defined(...)`, because the editing concept earned both fans and
  critics; nothing of the fase descriptors is deleted. When both flags are
  set at build time, they cooperate (chrome below, widgets above).

Runtime kill switch (either layer): `SIMULACRO_IMGUI=0/off` skips the shell
without changing any other behavior; `SIMULACRO_IMGUI_DEBUG=1` logs the
per-command render of the chrome/widgets under the SDL surface.

## Dear ImGui widgets (in-node)

The node canvas hosts a scoped Dear ImGui layer (`src/imgui_shell.*`,
vendored under `third_party/imgui/`, backend `imgui_impl_sdlrenderer2`).
Design rules:

- **Renderer untouched:** scope charts, maps and the wx chrome stay
  custom; ImGui is only for in-node widget rails.

- **nodeui policy (`src/nodeui.h`):** `SIMULACRO_NODE_UI` selects where the
  parameter strip lives: `inner` (default) anchors it to the node body,
  clipped to the node box (compact layout — control takes the full row, name
  as hover tooltip — when the box is too narrow for a label); `below` keeps
  the fase-1 rail; `off` disables in-canvas editing. `nodeui::mode()` is read
  once at startup: switching modes needs a restart, by design.

- **Decision (keep-warm, do not flip silently):** the canvas *property
  popup* over nodes is what fase 1..3 Hid behind
  `#if defined(SIMULACRO_NODE_UI)` (compile-time, default **off**) — by
  explicit request ("a ideia tem potencial, mas não por agora"; keep the
  code, gate the build). The `nodesim_plugin_ui()` *declaration* is a
  separate, active contract in every build; only the *host renderer* that
  paints these widgets on the canvas is compile-gated. The fase 4 window
  chrome (`SIMULACRO_NODE_WINDOW`) stays default-on and does not depend on
  the popup flag.

- **Ports do not move:** the strip is an overlay on the existing node box;
  `nodeBox/nodeWidth/nodeHeight`, wires and port anchors keep the engine
  geometry. When a plugin declares in-node widgets (fase 3), the host
  reserves room in the *transient preferred size* (`szW/szH`) so the strip
  and the ports no longer collide; node fit and wire anchors follow the
  engine box automatically (no `.nodesim` change, no ABI bump).

- **Layout precedence (fase 3):** the plugin's own `nodesim_plugin_ui()`
  declaration wins; without it the host builds the same kind of strip from
  `params[]/texts[]`. Both are host-rendered, themed (`kUi`) and coalesced
  into the same undo gesture. Loader validation lives with the other ABI-14
  extensions (`docs/abi.md`).
- **Backend manual:** wxWidgets owns the event loop; the canvas feeds
  ImGuiIO (`mouseMove/Button/Wheel/Key/Char`) before deciding whether the
  widget layer should consume the event. A last-frame rectangle list
  (`widgetBoxes_`) closes the "capture lands on next frame" gap, so clicks
  over sliders never leak to the canvas selection.
- **Theme:** the project palette (`kUi`) is mirrored to `ImGuiStyle` at
  startup and refreshed by `MainFrame::applyTheme()`.
- **Undo:** every gesture writes through the plugin ABI (`callSetParam`) and
  is coalesced into a single engine `checkpoint()` (reset on
  `onLeftUp`/decision); `Engine::undo/redo` now re-applies the restored
  plugin `pstate` blob over the live instances so param edits undo/redo
  with the same behavior as topology edits.
- **Canonical editor:** the wxPropertyGrid is refreshed on gesture end; the
  property grid remains the source of truth (as it already was for plugin
  params); the widget rail is a fast-path for the common case.
- **Kill switch:** `SIMULACRO_IMGUI=0` / `off` disables the layer without
  changing any other behavior; `SIMULACRO_IMGUI_DEBUG=1` prints per-command
  render diagnostics (used during integration).
- **Renderer glue:** `SdlCanvas` signals `rendererRebuilt()` when a pane resize
  recreates the software renderer; `imui::renChanged()` then rebuilds the
  shell, because the atlas texture belonged to the renderer that died with
  the resize.

## Compatibility (the contracts that replaced the old repo)

- `.nodesim` format 10 is read and written by the core; the smoke suite plus
  `tests/golden/*.csv` guard the step-by-step behavior.
- Plugins export the same ABI 14 symbols (`src/sdk/nodesim_plugin.h`); the
  built-in sources live in `plugins/` and compile to `plugins/*.so`.
- Plugin discovery and configuration keep the historical names:
  `NODESIM_PLUGIN_PATH`, `NODESIM_LANG`, `NODESIM_THEME`,
  `~/.local/share/nodesim/plugins`, autosave under
  `~/.local/state/nodesim`. Renaming them would break user workflows for no
  functional reason; treat them as part of the public surface.
- Widget ids and serialized keys are never translated; only display strings
  go through `i18n::text`.
- The theme is read from Omarchy's `colors.toml`; `tema.cpp` publishes the
  palette to the core renderers and `themewx.h` mirrors it to wxWidgets.

## Adding a feature

1. Put model logic in `Engine` (or `src/core/`) and test it headlessly with
   `--run`; when the change is meant to alter results, regenerate the golden
   CSVs on purpose and say so in the change description.
2. Add the wx action in `MainFrame` and refresh only the affected views.
3. UI appearance decisions stop in `src/*.cpp`/`src/*.h`; the engine layer
   stays free of wxWidgets headers (SDL and wx do not mix).
