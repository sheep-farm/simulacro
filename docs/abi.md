# ABI — contract between the host and the plugins

The simulator's plugin ABI lives in `src/sdk/nodesim_plugin.h`. **Today it is
the ABI 14** (`NODESIM_PLUGIN_API = 14`), frozen: the file format (`.nodesim`
10) and the ABI do not move without an explicit decision and a migration note
in this document.

## How ABI 14 evolves (additive extensions)

New capabilities reach the plugins as **optional symbols** resolved with
`dlsym` by the host. A `.so` that does not export them behaves exactly as ABI
14 did, and *there is no ABI bump*; a `.so` that exports them must match the
expected table shape (`api == NODESIM_PLUGIN_API`, `struct_size` documented).
All tables declare `api`/`struct_size`/counts and are validated on load
(warnings go to the diagnostics; invalid entries are skipped).

Currently adopted extensions (all additive; no `.nodesim` change):

| Extension symbol | What it declares | Where |
| --- | --- | --- |
| `nodesim_plugin_resources()` | files/directories/env vars the plugin reads (provenance) | `NS_PLUGIN_RESOURCES` |
| `nodesim_plugin_exec()` | execution policy per type (e.g. substeps 1..1024) | `NS_PLUGIN_EXEC` |
| `nodesim_plugin_events()` | discrete event sources and optional sink | `NS_PLUGIN_EVENTS` |
| `nodesim_plugin_ui()` | declarative in-node widget layout (see below) | `NS_PLUGIN_UI` |
| `nodesim_plugin_acts()` | canvas CLICK actions on a params[] entry (see below) | `NS_PLUGIN_ACTS` |

## The declarative UI (`nodesim_plugin_ui`)

Contract split today (write it down to avoid confusion):

- The **declaration** (`nodesim_plugin_ui()`) is active in every build: a
  plugin may ship a UI table at any time and `.so` remains valid.
- The **canvas rendering** of that UI (the fase 1..3 in-node property popup
  over the node) is what is hidden behind `SIMULACRO_NODE_UI` (compile-time,
  default **off**) after the community decided the canvas popup "has
  potential but not for now"; keep the code, gate the build.
- When the flag is off the property grid (`wxPropertyGrid`) stays the
  canonical editor and nothing else changes for the plugin.

The host (canvas) renders parameter controls *directly on the node drawing*.
Two levels exist, and both are host-side; the plugin never draws them:

1. **Generic layout (default, no declaration):** sliders/switches/combos and
   Inputs generated from the type's `params[]`/`texts[]`, below the title or
   in a rail under the node (`SIMULACRO_NODE_UI`, see `docs/architecture.md`).
2. **Authored layout** (recommended for third-parties): the type exports
   `nodesim_plugin_ui()` and declares a widget list bound to its
   `params[]`/`texts[]`:

```cpp
#include "nodesim_plugin.h"   // (or the in-tree <plugin.h>)

// binding: params[0] NUM -> slider, params[1] TEXT -> input, etc.
static const NsUiItem kItems[] = {
    {NS_UI_LABEL, "settings", -1, 0, 0, 0, 0},
    {NS_UI_SLIDER, "", 0, 0, 0, 0, 0},          // "" = use the param name
    {NS_UI_SWITCH, "", 1, 0, 0, 0, 0},
    {NS_UI_TEXT,  "path label", 1, 0, 0, 0, 0}, // texts[1]
    {NS_UI_LED,   "", 1, 0, 0, 0, 0},           // lights when params[1] >= .5
    {NS_UI_VSPACE, nullptr, -1, 0, 0, 0, 0},
};
static const NsUiSpec kSpecs[] = {{{"meu_tipo", 5, kItems}}};
NS_PLUGIN_UI(kSpecs, 1);
```

Rules (validated by the host and reported as warnings when violated):

- `index` must be a valid `params[]` (or `texts[]`, for `NS_UI_TEXT`) entry.
- The kind should match the declared parameter kind
  (`NS_UI_SLIDER⟶NUM`, `NS_UI_INT⟶INT`, `NS_UI_SWITCH⟶BOOL`,
  `NS_UI_COMBO⟶ENUM`); a mismatch keeps the generic behavior and shows as a
  diagnostic.
- Unknown kinds, invalid bindings, unknown types, duplicate specs and
  out-of-range counts are skipped with a warning; the plugin never crashes
  the host because of UI metadata.
- When a type has a layout, the host **reserves** room in `szH/szW`
  (transient preferred size) so the strip never covers the ports; the graph
  positions of wires/pins follow the node box automatically.

The visual result participates in the undo/redo cycle: every gesture made on
an in-node widget writes through `setParam/setText` and yields a single
engine checkpoint (see `docs/architecture.md`, fase notes). Third-party
plugins without `nodesim_plugin_ui` keep working with the generic editor.

## Canvas click actions (`nodesim_plugin_acts`)

The "panel control" contract: the plugin declares which `params[]` entry the
HOST drives when the user *clicks the node body on the canvas* (no UI table
is required, works with or without `SIMULACRO_NODE_UI`). Kinds:

| kind | behavior on the canvas |
| --- | --- |
| `NS_ACT_HOLD` | sets param=1 while pressed, param=0 on release (pushdown) |
| `NS_ACT_TOGGLE` | each body click flips the param (switch/latch) |

Rules: `param` must be a valid `params[]` index of the same type; duplicate
acts on the same param are rejected with a diagnostic. While the act is
firing the host does **not** create undo checkpoints and does **not** start
node dragging (panel-control semantics) — the value changes are simulator
interactions, not graph edits. The property dock is refreshed after each
transition (via the same `onParamEdited` path used by every editor).

**Press area (inset):** the table may declare, as an ADDITIVE trailing field
(`NsActTable::inset`, world px, read only when `struct_size >=
sizeof(NsActTable)` — older plugins read as 0), the margin that stays FREE
around the press face:

- clicks inside the face run the act as described above;
- clicks in the margin follow the standard node behavior (select/drag);
- the plugin then draws ONLY the face (see `nsui::Pushdown::draw`),
  honoring the same inset, so what you see is where you click.

```cpp
// pushdown: params[0] ("pulsa") *behaves as* the visible button face; the
// button draws only its face (inset 10), leaving the margin draggable
static const NsAct kActsBotao[] = {{"push", NS_ACT_HOLD, 0}};
NS_PLUGIN_ACTS_IN(kActsBotao, 1, Botao::kPressInset);  // inset=10
```

## C++ base for control plugins (herança, header-only)

`src/sdk/nodesim_ui_base.h` turns the plain C ABI into a *plugin base class*
for "control-style" nodes. Third parties derive from `nsui::Control`; the
base adapts the class to the C ABI and serializes `params/texts` state in the
same blob format as the built-ins. Nothing in the ABI moves: the `.so` still
exports `nodesim_plugin()` (and, optionally, `nodesim_plugin_ui()` and
`nodesim_plugin_acts()`).

```cpp
#include "nodesim_ui_base.h"   // includes nodesim_plugin.h

// control: pushdown button (por HERANÇA de nsui::Pushdown)
struct Botao : nsui::Pushdown {
    static constexpr const char* kTypeId = "push";
    static constexpr const char* kLabel = "Botão (pushdown)";
    static void defColor(int rgb[3]) { rgb[0] = 120; rgb[1] = 235; rgb[2] = 180; }
};

// host glue (one NsNodeType per Control subclass)
extern "C" const NsPlugin* nodesim_plugin() {
    static const NsNodeType kTypes[] = {{nsui::makeType<Botao>()}};
    static const NsPlugin kPlugin =
        NS_PLUGIN_BUILTIN("ui", "1.0", 1, kTypes, "Control");
    return &kPlugin;
}

// and the CLICK contract (the host drives params[0] on canvas presses)
static const NsAct kActs[] = {{"push", NS_ACT_HOLD, 0}};
NS_PLUGIN_ACTS(kActs, 1);
```

Building blocks of `nsui::Control` (each subclass shades the *static* +
overrides the *virtuals*):

| member | meaning |
| --- | --- |
| `kTypeId/kLabel/defColor/kDefIn/kPrefW/kPrefH` | static identity of the type |
| `kParams/kNumParams, kTexts/kNumTexts` | static C-tables; the *host* reads them via the type table |
| `numParams()/params()/numTexts()/texts()` (virtual) | dynamic access for the *base-led* blob serializer |
| `paramGet/paramSet, textGet/textSet` | state hooks (user-written) |
| `step/stepMulti` | simulation (`[0;1]` in the control family) |
| `draw` + `kCustomShape` | authored body when `kCustomShape=true` (entire silhouette; host skips body/title) |
| `numOut()` | port count (with `kSelfOut=true`) |

The base also ships two ready-made controls:

- `nsui::Pushdown` — the button family (state, blob, `[0;1]` step, defColor,
  autoloaded authored design `kCustomShape=true`, `NS_ACT_HOLD` match). The
  built-in `push` type (`plugins/controles.cpp`) derives from it.
- `nsui::Control` generic blobs for the rest (e.g. `entrada01` and `saida01`,
  also in `controles.cpp`).

## The built-in "controls" library

`plugins/controles.so` — the `push` type only (the pushdown button; face
inset so the margin stays draggable). `examples/controles.nodesim` ships a
single button ready to press. The former built-ins `entrada01`/`saida01`
were removed on purpose (2026-11): the *pattern* stays documented above, so
third parties recreate them by inheritance exactly as described — we
kept the library lean instead of accumulating demo nodes.
