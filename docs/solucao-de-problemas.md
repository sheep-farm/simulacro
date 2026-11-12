# Solução de problemas (simulacro)

> Documento de sintoma → causa provável → comando. **Convenções atuais:**
> o binário é `./bin/simulacro`; o CLI existe só em duas formas: `GUI`
> (`simulacro [FILE]`) e `--run` headless com `--steps/--rate/--summary/
> --plugin-dir/--lang`. Recursos do motor que ficaram só na GUI (eventos
> discretos, convergência de ciclo) estão marcados abaixo como **GUI**.
> Contratos mantidos: formato `.sim` (10), ABI 14, `NODESIM_*` e
> `~/.local/state/nodesim`.

This document gathers common symptoms, the likely cause and the command that
confirms the diagnosis. The general rule: **read stderr**. Headless mode
reserves stdout for the CSV; warnings and errors go to stderr.

## 30-second diagnosis

Run from the repository root:

```sh
make -j8 && make plugins
./bin/simulacro --version
./bin/simulacro --run examples/ar1.sim --steps 30 --summary
make test                      # suite completa (traces golden + kill switches)
```

- `--version` shows version, ABI and format.
- Plugin problems are printed to stderr at load (`plugins: <name> v<ver>
  (<n> tipos)` or `problem: <detalhe>`); a run of any example confirms.
- `make test` builds, runs the golden traces, loads every example and
  compiles the gated layers (kill switches) in all configurations.

## Exit codes

| Code | Meaning | Where it appears | What to do |
|---|---|---|---|
| `0` | success | all modes, `--help`, `--version` | — |
| `1` | general failure | `--run` failed to load/save a file, GUI failed to start | read the message; retry with a minimal file |
| `2` | invalid usage | unknown option, missing argument | fix the command line (`--help`) |

## The GUI does not open

**Symptoms:** the process exits with code 1, there is no window, or an
SDL/`libSDL2` error appears.

- Confirm the dependencies: `ldd bin/simulacro`.
- Install SDL2, SDL2_ttf and wxWidgets 3.2 (see *Requirements* in the README).
- On a machine without a display (SSH, CI), use headless mode:
  `./bin/simulacro --run examples/ar1.sim --steps 30`.
- To test only GUI startup without a display:
  `timeout 3 ./bin/simulacro; echo $?` — code **124** is expected (the
  `timeout` cuts the event loop).
- If the file passed on the command line does not load, the GUI still opens
  with an empty graph; test the file first headlessly:
  `./bin/simulacro --run file.sim --steps 1 --summary`.

## Panels do not dock (Linux on Wayland)

**Symptoms:** dragging Properties/Nodes/Scopes floats the panel but it
never docks; the drop hint may not appear. Happens on Wayland compositors
(Hyprland, GNOME Wayland, Sway…); works on Windows and on X11.

**Cause:** native Wayland forbids what wxAUI's docking relies on — a
global pointer grab during the drag and an overlay hint drawn over other
windows. The app forces the GTK3 X11 backend (XWayland) automatically,
before any GUI initialization, unless you set `GDK_BACKEND` yourself.

- Check what the app is running under: `echo $XDG_SESSION_TYPE`.
- Manual override (not needed with 0.2.1+): `GDK_BACKEND=x11 ./bin/simulacro`.

## Status bar warnings

The GUI keeps the previous graph on load failure and shows the outcome in the
**status bar** (`Loaded examples/…`, node/wire counts, `PAUSED/RUNNING t=…`);
stderr has the full message. Common cases:

- **load failed**: test the file headlessly and see *The `.sim` file
  does not open*.
- **plugin problems**: a node from a missing plugin stays on the canvas but
  does not compute (status bar counts it); rebuild with `make plugins`.
- **save failed**: check directory permissions and disk space; `Ctrl+S` retries.

`F1` opens the keyboard help. `docs/solucao-de-problemas.md` (this file) is the
text version for the command line.

## The map does not appear

**Symptoms:** the map Scope window shows “choose a GeoJSON file”,
“file not found” or “GeoJSON without supported geometry”.

- Confirm the path in **Properties → MAP → file**. Relative paths
  are resolved against the `.sim` directory.
- The file must be a `FeatureCollection` with `Polygon`, `MultiPolygon`,
  `LineString`, `MultiLineString`, `Point` or `MultiPoint`.
- For very large maps, use a simplified version; the parser has limits
  on size, features and vertices.
- If the map opens without colors, connect a list/matrix to **socket 0** and check
  `idfield`/`idcolumn`/`valuecolumn`. Features without a value are gray.
- To overlay layers, use `overlay=camada1.geojson|camada2.geojson`; the base
  stays in `file`.
- Map selection, painting and annotations are stored in the node state. Use **Save:
  current state** to write them; **Save: initial state** keeps the starting
  state and `F5` returns to it.
- SVG export of maps does not exist yet; use the PNG export of the scope page.

## Plugin does not load

**Symptoms:** the node shows “plugin not found”, the type does not appear in the
palette, or `--run` warns that a plugin was missing.

The search follows this order:

1. `$NODESIM_PLUGIN_PATH` (separated by `:`);
2. `./plugins` (development);
3. `<executable dir>/../share/nodesim/plugins` and
   `../lib/nodesim/plugins` (install with any `PREFIX`);
4. `$XDG_DATA_HOME/nodesim/plugins` (default `~/.local/share/nodesim/plugins`);
5. each `$XDG_DATA_DIRS` + `/nodesim/plugins` (default `/usr/local/share` and
   `/usr/share`).

`--plugin-dir DIR` adds a directory to the search (headless).

Checklist:

```sh
make plugins                         # builds every plugins/*.so
ls -l plugins/*.so
./bin/simulacro --run examples/controles.sim --steps 1 --summary
```

- Run from the repository root; `./plugins` is relative to the execution
  directory, or use `--plugin-dir` with the correct path.
- Check that the `.so` exists and is the right architecture: `file plugins/rng.so`.
- Check that the symbol exists: `nm -D plugins/rng.so | grep nodesim_plugin`.
- Third-party plugins can be C and depend only on libc; the built-in ones are C++ and
  may need `libstdc++`, `libm` and `libgcc_s` on the system. See
  `ldd plugins/rng.so` when distributing.
- Plugins installed under XDG may load types that do not exist in `./plugins`.
  The default order makes `./plugins` win over a duplicate `id`, but a directory in
  `NODESIM_PLUGIN_PATH` comes first. Use `--plugin-dir ./plugins` to isolate.
- Load-time problems are printed to stderr (`problem: …`) together with the
  file that caused them.

## Plugin rejected (ABI, struct_size or symbol)

**Symptoms:** stderr shows a `problem:` for the file, or the host says the
ABI does not match.

- The host requires `api == 14`. Recompile the plugin against the
  current `src/sdk/nodesim_plugin.h`; do not edit the ABI number in the `.so`.
- Use `NS_PLUGIN_INIT_META` (or `NS_PLUGIN_INIT`) to fill in
  `struct_size = sizeof(NsPlugin)`.
- Make sure `extern "C" const NsPlugin* nodesim_plugin();` is visible. With
  `-fvisibility=hidden`, mark the function with default visibility.
- Types with the same `id` in different plugins are a problem; use unique ids.
- Additive tables (`nodesim_plugin_resources/exec/events/ui/acts`) validate
  `api` and `struct_size` on load; problems also print as `problem: …`.
- The full guide is in [`abi.md`](abi.md).

## Plugin node does not compute

**Symptoms:** the output stays at zero/constant, or the value does not react to the inputs.

- Check the status bar in the GUI (`2 nodes · 1 wires`) and stderr in
  `--run`: a node whose plugin is missing stays in the file but does not
  compute; `make plugins` fixes the common case.
- Check the inputs: a socket with no wire is 0; lists/matrices are
  transmitted as the first non-numeric wire.
- A SCRIPT error in the node itself (or in another node feeding it) can leave
  the output at the previous value; errors show in red on the canvas and in
  the `--run` stderr.
- Trace the values headlessly: add a Scope wired to the node and run
  `--run file.sim --steps 60` — one CSV column per scope channel.

## NaN, infinity or clamp

The engine does not fail silently: NaN/inf keeps the port's previous value and
counts the occurrence; values above ±1e9 are clamped and counted. The canvas
shows a red alert dot on affected nodes and `--run` reports it on stderr.

```sh
./bin/simulacro --run file.sim --steps 120 > /tmp/x.csv
```

Investigate:

- division by zero or log/root of an invalid value in the SCRIPT;
- `dt` too large for an integrator;
- overflow in a plugin or a parameter out of range;
- an unconnected input (0) in an operation that expects a positive value.

Clamps at ±1e9 are protection, not a physical result: if they appear, fix
the scale or the model.

## Events do not appear (**GUI**)

**Symptoms:** `evt(0)` stays `0`, the callback is not called.

- The service is **opt-in**: enable *Sim → Discrete events*. Without that,
  `evt()`/`evt_kind()` are `0` and `evt_t()` is `-1`.
- `F2` opens the source editor; adding a source turns the service on.
  The status in the top right corner shows `evt` when it is on.
- The port must be **numeric** and finite; text, list, matrix and NaN/inf do not
  generate an event.
- Delivery has **one step of latency**: the transition in step *n* appears in
  step *n+1* — step the simulation manually (F6) to see the edge.
- `rise` with threshold `0` does not fire on `0 → 1` (the rule is `previous < 0` and
  `current >= 0`). Use, for example, `rise:0.5`.
- A topology change clears the queue and resets the baseline; pending events
  from the old topology are not delivered.
- If the callback throws, the instance is disabled and the plugin's stderr
  explains the failure.

## Cycle does not converge (**GUI**)

By default, a cycle uses **unit delay**: nodes outside the topological order
read the previous step's outputs. This is correct for filters and dynamic
feedback, and it is what preserves the goldens. For an **algebraic loop** (for
example, `a = b + 1`, `b = a/2`), enable *Sim → Converge cycle*:

- Without it, the result is the unit delay; do not expect a fixed point.
- The per-step limit keeps the last iterate and warns on stderr when reached.
- Plugins in the non-acyclic section use `save`/`load` to advance state once
  per step. Without a snapshot, they are treated as stateless; plugins with external
  effects may repeat the effect and should not be in that section.
- If the cycle oscillates or diverges, the problem may be the model (gain ≥ 1), the
  evaluation order or a discontinuity.

## The result changes between runs

- Use fixed seeds: the plugins' `seed` parameter (e.g. `rng` com
  `semente=7` em `examples/ar1.sim`).
- Check the plugins' **discovery order**. An old `.so` under XDG can
  replace the one from `./plugins`; use `--plugin-dir ./plugins` to isolate.
- The `.sim` file is the source of truth; saving in the middle of a run does not
  freeze the accumulated state, unless you use *Save: current state* in the GUI.
- The engine is deterministic: `make test` compares golden traces
  (`tests/golden/*.csv`) byte for byte.

## The `.sim` file does not open

- The first line must be `nodesim 10` (older versions 1–9 are converted on
  load). The extension is `.sim`; the loader still opens legacy
  **`.nodesim`** files (same format, different convention). The file is text; an editor can show the problem. Make a copy before
  editing by hand.
- When saving, the previous file is preserved as `<file>.bak`; if the current file
  is bad, try the `.bak`.
- Nodes from missing plugins remain in the file but do not compute.
- To return to the starting state while preserving parameters, texts and layout:
  *Save: initial state* in the GUI keeps it as the node's initial state and
  `F5` returns to it.

## Plugin crashes the process

A segfault in a plugin is not contained by the host (C++ exceptions are
contained; memory faults are not). The GUI keeps the previous saved file —
save often while experimenting with third-party plugins.

1. Recover the graph from your last save (the file on disk is always the
   last `Ctrl+S`).
2. Isolate the plugin with `NODESIM_PLUGIN_PATH` or `--plugin-dir` and run a
   minimal graph headlessly: `./bin/simulacro --run mini.sim --steps 2`.
3. If the crash happens only when creating the node, the problem may be in `create()`;
   if it happens during the step, in `step`/`stepMulti`.
4. Per-process sandboxing is outside the current scope; remove the plugin until
   it is fixed.

## What to send when asking for help

Since the project is personal, there is no formal support channel. For a useful
analysis, include:

- output of `./bin/simulacro --version`;
- the exact command, including flags;
- the full stderr;
- the minimal `.sim` that reproduces it, or the relevant nodes/parameters;
- build configuration (`EXTRA_CXXFLAGS`, kill switches) if the problem is
  visual or compile-time.
