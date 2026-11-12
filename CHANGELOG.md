# Changelog

## 0.2.0 — 2026-11-12

Cross-platform release: the same source tree now builds and runs on Linux,
macOS and Windows, with binaries published on GitHub Releases.

- **macOS + Windows portability**: dlopen shim (LoadLibrary on Windows),
  per-platform library extension (`.so`/`.dylib`/`.dll`), `executableDir`
  for the three platforms, portable monospace font probe (Consolas/Menlo),
  portable state/config directories (`LOCALAPPDATA` / `Application Support`;
  the Linux XDG contracts are unchanged). Built-in `proc` plugin stays
  POSIX-only and is excluded from the Windows build (documented).
- **Fix**: the plugin scanner on macOS only accepted `.dylib`, while CMake
  builds modules as `.so` — no plugin loaded on macOS; both extensions are
  accepted now.
- **CI**: matrix of four green jobs (linux make+test, linux cmake,
  macos arm64, windows msys2/ucrt64), each with a headless golden smoke and
  a plugin-load assertion; `NODESIM_VERBOSE_PLUGINS=1` prints the loaded
  plugin list from `--run`.
- **Release binaries**: new `release.yml` workflow packages `tar.gz`
  (linux/macos) and a self-contained `zip` (windows, runtime DLLs via
  `ntldd`) and uploads them to the tag's GitHub Release.
- **Version surfaces**: `SIMULACRO_VERSION` centralized in
  `src/core/version.h` (status bar, About dialog and `--version` read the
  same constant).

## 0.1.0 — 2026-11-11

First public release. An editor and simulator for node graphs: wxWidgets
(GTK3 chrome) around the engine that started its life as **nodesim**, with
SDL off-screen raster views and Dear ImGui layers.

- **Core editor/simulator**: node graphs (script, plugin and scope nodes),
  scopes (line/XY/bars/histogram/FFT/map with GeoJSON), undo/redo,
  copy/paste/duplicate, groups, wires, transport (start/pause/step/reset,
  1–60 steps/s), headless runner (`--run`, `--steps`, `--summary`).
- **Plugin SDK (ABI 14, frozen)**: C ABI types with additive tables
  (resources/exec/events/ui/acts) and the C++ inheritance base
  `src/sdk/nodesim_ui_base.h` (`nsui::Control` → `Pushdown`/`Switch`),
  incl. declarative in-node UI (`NS_PLUGIN_UI`) and canvas click acts
  (`NS_PLUGIN_ACTS_IN` with face inset).
- **Built-in controls by inheritance**: `push` (pushdown; face click,
  input level with `EXT` marker, editable label/colors) and `switch`
  (toggle; ON/OFF pill). Keyboard acts: Space press/release (HOLD),
  Enter toggle (TOGGLE).
- **ImGui layers**: node window chrome (fase 4, default on), in-node
  parameter widgets (fase 1..3, compile-time, default off — the property
  grid stays the canonical editor).
- **Editing**: alignment/distribution controls (Edit menu + toolbar
  dropdown; Ctrl+Shift+L/R/T/B/H/V), toolbar with labels or icons-only.
- **Canvas**: immediate auto-refit (works while paused), scroll pan
  (horizontal/vertical) with Ctrl+scroll zoom, Ctrl+1 center on graph.
- **Session state saved on close** (`~/.local/state/nodesim/ui.ini`):
  pane layout, window geometry, camera, View toggles, toolbar mode,
  UI language and the Open Recent list.
- **Files**: `.sim` extension over the frozen `nodesim 10` format; legacy
  `.nodesim` files keep loading (covered by a test).
- **Regression discipline**: deterministic engine with golden CSV traces,
  28 headless behavior tests for the control family and a compile matrix
  for every gated layer (48 checks total, no display needed).
