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
   inputs generated from the type's `params[]`/`texts[]`, below the title or
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

## C++ base for control plugins (herança, header-only)

`src/sdk/nodesim_ui_base.h` turns the plain C ABI into a *plugin base class*
for "control-style" nodes. Third parties derive from `nsui::Control`; the
base adapts the class to the C ABI and serializes `params/texts` state in the
same blob format as the built-ins. Nothing in the ABI moves: the `.so` still
exports `nodesim_plugin()` (and, optionally, `nodesim_plugin_ui()`).

```cpp
#include "nodesim_ui_base.h"   // includes nodesim_plugin.h

// control: botão pushdown — 1 entrada, [0;1] na saída
struct Botao : nsui::Control {
    static constexpr const char*  kTypeId = "push";
    static constexpr const char*  kLabel = "Botão (pushdown)";
    static void defColor(int rgb[3]) { rgb[0] = 120; rgb[1] = 235; rgb[2] = 180; }
    static constexpr int   kNumParams  = 1;
    static const NsParam   kParams[1];      // tabela estática, obrigatória
    static constexpr int   kDefIn = 1;
    static constexpr bool  kSelfOut = true;  // plugin controla o nº de portas
    static constexpr int   kNumOut  = 1;

    double pulsa = 0;                                   // [0;1]
    double paramGet(int i) const override { return i == 0 ? pulsa : 0; }
    void   paramSet(int i, double v) override  { if (i == 0) pulsa = v > .5; }

    double step(const NsContext&) override { return pulsa > .5 ? 1.0 : 0.0; }
};

// registration: one NsNodeType per Control subclass; host glue comes free
extern "C" const NsPlugin* nodesim_plugin() {
    static const NsNodeType kTypes[] = {{nsui::makeType<Botao>()}};
    static const NsPlugin kPlugin =
        NS_PLUGIN_BUILTIN("ui", "1.0", 1, kTypes, "Control");
    return &kPlugin;
}
```

Built-in reference implementation: `plugins/controles.cpp` — three controls
in one library (`push`, `entrada01` [0;1], `saida01` [0;1] with a
plugin-authored display via the `NsDraw` hook). Third-party plugins can
derive from `nsui::Control` in their own `.so` (header-only, the ABI stays
14 and the `.nodesim` format stays 10).

Notes:

- The identity/port preferences `kNumOut/kSelfOut` and tables `kParams/
  kNumParams`, `kTexts/kNumTexts`, `kDefIn/kPrefW/kPrefH/kCustomShape` are
  *static* in the subclass (the C table must outlive the type).
- The *rawing* hook (`draw`) is only wired when `kCustomShape=true` — the
  host skips body/title and the plugin paints the whole silhouette.
- Everything else (blob serialization for `params/texts`, defaults handled by
  the host, in-node widget layout optional via `NS_PLUGIN_UI`) is the same
  machinery the built-in plugins use.
