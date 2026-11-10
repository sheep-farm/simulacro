# Solução de problemas (simulacro)

> Documento de sintoma → causa provável → comando, herdado do projeto nodesim
> que este projeto absorveu. **Convenções atualizadas para o simulacro:**
> o binário é `./bin/simulacro`; opções que existiam no CLI antigo e não existem
> aqui (`--check-plugins`, `--until`, `--report`, `--output`, `--list`) devem ser
> lidas como os equivalentes de hoje: `--run`, `--steps`, `--rate`, `--summary`,
> além do diagnóstico no menu `Edit` (Ctrl+D, SVG) e `F3` na GUI.
> Contratos mantidos: formato `.nodesim` (10), ABI 14, `NODESIM_*` e
> `~/.local/state/nodesim`.


This document gathers common nodesim symptoms, the likely cause and the command
that confirms the diagnosis. The general rule: **read stderr**. Headless mode
reserves stdout for the CSV; warnings, errors and the report go to stderr.

## 30-second diagnosis

Run from the repository root:

```sh
make -j4 && make plugins
./bin/simulacro --version
./bin/simulacro --check-plugins --conformance
./bin/simulacro --run file.nodesim --until 1 --report --output /tmp/nodesim-teste.csv
```

- `--version` shows version, ABI and format.
- `--check-plugins` shows the directories searched, ABI, `struct_size`,
  identity, resources, events, substeps and problems.
- `--report` shows steps, cycles, non-finite values, clamps, substeps,
  convergence, events and nodes with alerts.
- If `--check-plugins` exits with **1**, fix plugin loading before
  investigating the graph.

## Exit codes

| Code | Meaning | Where it appears | What to do |
|---|---|---|---|
| `0` | success | all modes, `--help`, `--version` | — |
| `1` | general failure | `--run` failed to load/save a file, GUI failed to start, `--check-plugins` with a problem, campaign with a failing child run | read the message; try `--list`, `--check-plugins` and the output file |
| `2` | invalid usage or non-finite value | unknown option, missing argument, invalid `--dt`/`--until`, `--tolerance`/`--max-iterations` out of range, malformed `--event`, malformed `--scan`, `--fail-on-nan` triggered | fix the command line; with `--fail-on-nan`, find the node in `--report` |
| `3` | performance budget exceeded | `--bench --budget US` | it is a CI limit, not a result error; measure without the budget or adjust `US` |
| `4` | cycle did not converge | `--fail-on-no-convergence` | see the *Cycle does not converge* section; without the flag the last iterate is kept and there is a warning |

In a campaign (`--scan`, `--replicas`), the final code reflects the most
relevant failure among the child runs: **2** has priority, then **4**, then **1**.
Code **127** may appear in a child run's message if the binary itself cannot be
re-executed (broken environment); the parent process exits
with **1** and it is not a graph error.

## The GUI does not open

**Symptoms:** the process exits with code 1, there is no window, or an
SDL/`libSDL2` error appears.

- Confirm the dependencies: `ldd bin/nodesim`.
- Install SDL2 and SDL2_ttf (see *Requirements* in the README).
- On a machine without a display (SSH, CI), use headless mode:
  `./bin/simulacro --run file.nodesim --until 1`.
- To test only GUI startup without a display:
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 3 ./bin/simulacro; echo $?`
  — code **124** is expected (the `timeout` cuts the loop).
- If the file passed on the command line does not load, the GUI also fails;
  test it first with `./bin/simulacro --run file.nodesim --list`.

## The GUI shows a red banner

The footer banner is the actionable version of the error: it indicates whether
the problem was saving, loading, autosave, a plugin or export, and what to try.
It stays on screen for 12 s; stderr still has the full message. Common cases:

- **save failed**: check directory permissions and disk space;
  `Ctrl+S` retries. Autosave continues in
  `~/.local/state/nodesim/`.
- **load failed**: the previous graph is kept. Test the file with
  `--run file.nodesim --list` and see the *The .nodesim file does not open* section.
- **autosave failed**: save with `Ctrl+S`; if it persists, check
  `$XDG_STATE_HOME`/`~/.local/state/nodesim`.
- **scope/export**: check that the `.nodesim` directory is writable.

The `F1` key opens the help with the first-use walkthrough;
`docs/primeiros-passos.md` has the text version.

## The map does not appear

**Symptoms:** the map Scope window shows “choose a GeoJSON file”,
“file not found” or “GeoJSON without supported geometry”.

- Confirm the path in **Properties → MAP → file**. Relative paths
  are resolved against the `.nodesim` directory.
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
- SVG export of maps does not exist yet; use `Ctrl+Shift+E` for PNG.

## Plugin does not load

**Symptoms:** the node shows “plugin not found”, the type does not appear in the
Add menu, or `--run` warns that no plugin was found.

The search follows this order:

1. `$NODESIM_PLUGIN_PATH` (separated by `:`);
2. `./plugins` (development);
3. `<executable dir>/../share/nodesim/plugins` and
   `../lib/nodesim/plugins` (install with any `PREFIX`);
4. `$XDG_DATA_HOME/nodesim/plugins` (default `~/.local/share/nodesim/plugins`);
5. each `$XDG_DATA_DIRS` + `/nodesim/plugins` (default `/usr/local/share` and
   `/usr/share`).

`--plugins DIR` forces a single directory (repeatable in the catalog).

Checklist:

```sh
make plugins                         # make clean removes bin/ and plugins/
ls -l plugins/*.so
./bin/simulacro --check-plugins
./bin/simulacro --check-plugins --plugins ./plugins
```

- Run from the repository root, or pass `--plugins` with the correct
  path; `./plugins` is relative to the execution directory.
- Check that the `.so` exists and is the right architecture: `file plugins/rng.so`.
- Check that the symbol exists: `nm -D plugins/rng.so | grep nodesim_plugin`.
- Third-party plugins can be C and depend only on libc; the built-in ones are C++ and
  may need `libstdc++`, `libm` and `libgcc_s` on the system. See
  `ldd plugins/rng.so` when distributing.
- Plugins installed under XDG may load types that do not exist in `./plugins`.
  The default order makes `./plugins` win over a duplicate `id`, but a directory in
  `NODESIM_PLUGIN_PATH` comes first. Use `--plugins ./plugins` to isolate.
- If you installed with `make install PREFIX=…`, the `.so` files go in
  `$(PREFIX)/share/nodesim/plugins`; discovery relative to the executable covers
  prefixes outside XDG. `make uninstall` removes only the installed `.so` files.

## Plugin rejected (ABI, struct_size or symbol)

**Symptoms:** `--check-plugins` shows `!!` for the file, or the host says the
ABI does not match.

- The host requires `api == 14`. Recompile the plugin against the
  current `sdk/nodesim_plugin.h`; do not edit the ABI number in the `.so`.
- Use `NS_PLUGIN_INIT_META` (or `NS_PLUGIN_INIT`) to fill in
  `struct_size = sizeof(NsPlugin)`.
- Make sure `extern "C" const NsPlugin* nodesim_plugin();` is visible. With
  `-fvisibility=hidden`, mark the function with default visibility.
- Types with the same `id` in different plugins are a problem; use unique ids.
- Missing required resources and mismatched hashes also make
  `--check-plugins` exit with 1. Relative paths are resolved from the
  `.so` directory.
- The full guide is in [`abi.md`](abi.md).

## `--conformance` fails

`--check-plugins --conformance` creates each type and tests `save → load → save`
without calling `step`. It fails if:

- `create()` returns `null`;
- `save`/`load` are not idempotent (the logical state changes on the round trip);
- the type declares state but does not implement the pair correctly.

Fix the plugin and run again:

```sh
./bin/simulacro --check-plugins --plugins ./meu-dir --conformance
```

The test does not run the step; a plugin can pass conformance and still
fail at runtime. In that case, use a minimal graph with `--run
--report`.

## Plugin node does not compute

**Symptoms:** the output stays at zero/constant, or the value does not react to the inputs.

- Run `./bin/simulacro --run file.nodesim --list` and check the node's type, plugin
  and ports.
- If no plugin is loaded, the host warns that plugin nodes “will not
  compute”. Run `make plugins` or pass `--plugins`.
- Check that the type appears in `./bin/simulacro --types | python3 -m json.tool`.
- Check the inputs: a socket with no wire is 0; lists/matrices are
  transmitted as the first non-numeric wire.
- A SCRIPT error in the node itself (or in another node feeding it) can leave
  the output at the previous value; errors show in red on the canvas and in the
  `--run` stderr.
- Use `--report` to see nodes with alerts, clamps and non-finite values.

## NaN, infinity or clamp

nodesim does not fail silently: NaN/inf keeps the port's previous value and
counts the occurrence; values above ±1e9 are clamped and counted.
`--report` shows the total and the affected nodes.

```sh
./bin/simulacro --run file.nodesim --until 5 --report --output /tmp/x.csv
./bin/simulacro --run file.nodesim --until 5 --fail-on-nan --output /tmp/x.csv
```

With `--fail-on-nan`, execution stops at the first step with a non-finite output and
exits with **2**. Investigate:

- division by zero or log/root of an invalid value in the SCRIPT;
- `dt` too large for an integrator;
- overflow in a plugin or a parameter out of range;
- an unconnected input (0) in an operation that expects a positive value.

Clamps at ±1e9 are protection, not a physical result: if they appear, fix
the scale or the model.

## Events do not appear

**Symptoms:** `evt(0)` stays `0`, the callback is not called or the report
shows `0 generated`.

- The service is **opt-in**: use `--events` or *Sim → Discrete events*. Without
  that, `evt()`/`evt_kind()` are `0` and `evt_t()` is `-1`.
- In the GUI, `F2` opens the source editor; adding a source turns the service on.
  The editor footer shows the origin (`host` or `plugin`) and the status in the top
  right corner shows `evt` when it is on.
- Check that there is a source: `./bin/simulacro --types | grep -A3 eventos` or
  `--check-plugins`. In headless mode, `--event NODE.port=rise:0.5` creates one.
- The port must be **numeric** and finite; text, list, matrix and NaN/inf do not
  generate an event.
- Delivery has **one step of latency**: the transition in step *n* appears in
  step *n+1*. Increase `--until` to see the edge.
- `rise` with threshold `0` does not fire on `0 → 1` (the rule is `previous < 0` and
  `current >= 0`). Use, for example, `rise:0.5`.
- A topology change clears the queue and resets the baseline; pending events
  from the old topology are not delivered.
- If the report shows failures, the callback threw an exception and the instance was
  disabled; see the plugin's stderr.

## Cycle does not converge

By default, a cycle uses **unit delay**: nodes outside the topological order
read the previous step's outputs. This is correct for filters and dynamic
feedback, and it is what preserves the goldens. For an **algebraic loop** (for
example, `a = b + 1`, `b = a/2`), enable convergence:

```sh
./bin/simulacro --run cycle.nodesim --converge --tolerance 1e-9 --max-iterations 64
./bin/simulacro --run cycle.nodesim --converge --report --output /tmp/c.csv
./bin/simulacro --run cycle.nodesim --converge --fail-on-no-convergence
```

- Without `--converge`, the result is the unit delay; do not expect a fixed point.
- `--tolerance` is the largest delta accepted between iterations; smaller values require
  more iterations.
- `--max-iterations` is the per-step limit (1..10000, default 64). If the limit is
  reached, the last iterate is kept and there is a warning; with
  `--fail-on-no-convergence`, the code is **4**.
- `--report` shows steps, iterations, last delta and the responsible node.
- Plugins in the non-acyclic section use `save`/`load` to advance state once
  per step. Without a snapshot, they are treated as stateless; plugins with external
  effects may repeat the effect and should not be in that section.
- If the cycle oscillates or diverges, the problem may be the model (gain ≥ 1), the
  evaluation order or a discontinuity. Increasing `--max-iterations` does not
  fix a loop without contraction.

## The result changes between runs

- Use fixed seeds: `--seed`, `--seed0` and the plugins' `seed` parameter.
- `--jobs` only changes the time; the result should be identical to `--jobs 1`.
- Check the plugins' **discovery order**. An old `.so` under XDG can
  replace the one from `./plugins`; use `--plugins` to isolate.
- External resources go into the manifest: files with path and hash;
  environment variables only with presence/absence. Compare the manifest of two
  runs.
- Compare `./bin/simulacro --version`, the binary hash and the hash of each `.so` in
  the manifest. Different builds can change the result.
- The `.nodesim` file is the source of truth; saving in the middle of a run does not
  freeze the accumulated state, unless you use *Save: current state* in the GUI.

## The manifest was not generated

The `<csv>.manifest.json` manifest accompanies a **CSV written to a file**:

- `--output -` (stdout) does not generate a manifest;
- `--summary` does not generate a manifest;
- `--no-manifest` turns it off;
- in a campaign, the aggregate has the campaign manifest; each trace also has
  its own.

If the CSV was written and the manifest was not, check write permission on the
directory and whether the command did not use `--no-manifest`. The manifest is
JSON; validate with `python3 -m json.tool file.csv.manifest.json`.

## Autosave and recovery

The GUI writes an autosave after edits (at most once per second) to:

```text
$XDG_STATE_HOME/nodesim/autosave.nodesim
$XDG_STATE_HOME/nodesim/autosave.json
```

Without `XDG_STATE_HOME`, the path is `~/.local/state/nodesim/`. `--run` mode
**never** writes to that directory.

- If the app dies without saving, the next opening of the **same file** recovers the
  autosave when it is newer than the `.nodesim`; `Ctrl+S` saves it for good.
- Saving or reloading the file clears the slot.
- To discard a recovery, close the app and remove `autosave.nodesim` and
  `autosave.json`. Do not remove them before saving if you still need the content.
- If the autosave is corrupted, the normal file opens without hanging.

## The `.nodesim` file does not open

- The first line must be `nodesim 9`. The loader accepts v1–v8 and converts old
  nodes (FONTE/SOMA/GANHO/INTEGRADOR) to SCRIPT.
- The file is text; an editor can show the problem. Make a copy before
  editing by hand.
- When saving, the previous file is preserved as `<file>.bak`; if the current file
  is bad, try the `.bak`.
- `--run file.nodesim --list` shows what the host managed to load without
  running the simulation.
- Nodes from missing plugins remain in the file but do not compute. `--zero`
  cannot rebuild a plugin's initial state without the `.so`.
- To return to the starting state while preserving parameters, texts and layout:
  `./bin/simulacro --run file.nodesim --zero` (the previous one goes to `.bak`).

## Performance and code 3

`--bench --budget US` exits with **3** when the average step exceeds `US`
microseconds. It is a CI limit, not a result error.

```sh
./bin/simulacro --run file.nodesim --bench --until 10 --output /dev/null
tests/bench.sh
```

Builds with ASan/UBSan are slower; measure the normal binary. If the budget is
the problem, adjust the limit or investigate the graph before changing the engine.

## Plugin crashes the process

A segfault in a plugin is not contained by the host (C++ exceptions are
contained; memory faults are not). Autosave helps recover the graph.

1. Open the app and recover the autosave; save the file.
2. Run `./bin/simulacro --check-plugins --plugins DIR` and
   `--conformance`.
3. Isolate the plugin with `NODESIM_PLUGIN_PATH` or `--plugins` and run a minimal
   graph with `--run --report`.
4. If the crash happens only when creating the node, the problem may be in `create()`;
   if it happens during the step, in `step`/`stepMulti`.
5. Per-process sandboxing is outside the current scope; remove the plugin until
   it is fixed.

## What to send when asking for help

Since the project is personal, there is no formal support channel. For a useful
analysis, include:

- output of `./bin/simulacro --version`;
- output of `./bin/simulacro --check-plugins --conformance`;
- the exact command, including flags;
- the full stderr (and `--report`, if applicable);
- the minimal `.nodesim` that reproduces it, or the relevant nodes/parameters;
- the `<csv>.manifest.json`, if the problem is about reproducibility.

The manifest contains paths and hashes from the machine; review it before sharing.
