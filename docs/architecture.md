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
