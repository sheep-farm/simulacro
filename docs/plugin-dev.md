# Plugin development guide

> Fundamentals and step-by-step for writing **nodesim/simulacro** plugins.
> The reference for the ABI surface is [`abi.md`](abi.md); this document is
> the tutorial. Everything here compiles against the only header a
> third-party plugin needs: `src/sdk/nodesim_plugin.h` (SDK 1.4, **ABI 14**).

A plugin is a shared library (`.so`) that exports one mandatory symbol,
`nodesim_plugin()`, plus *optional* additive symbols (`nodesim_plugin_ui`,
`nodesim_plugin_acts`, `nodesim_plugin_resources`, `nodesim_plugin_exec`,
`nodesim_plugin_events`) that the host looks up with `dlsym`. Missing
optional symbols are normal and valid — the ABI does not move.

The host discovers plugins by scanning, in order:

1. `$NODESIM_PLUGIN_PATH` (`:`-separated);
2. `./plugins` (the repository, for development);
3. `<prefix>/share/nodesim/plugins` and `<prefix>/lib/nodesim/plugins`;
4. `~/.local/share/nodesim/plugins` (XDG user data);
5. each `$XDG_DATA_DIRS` entry + `/nodesim/plugins`.

Load-time problems (bad `api`, missing symbol, duplicate type id, invalid
tables) are printed to **stderr** as `problem: …` next to the file name.

## 1. Fundamentals

### The one mandatory table

```c
extern "C" const NsPlugin* nodesim_plugin() { return &plugin; }
```

`NsPlugin` carries identity (name, version, `group` shown in the palette)
and an array of `NsNodeType` — one entry per *node type* you offer. Third
parties should build it with `NS_PLUGIN_INIT_META(...)` (fills
`struct_size = sizeof(NsPlugin)` and the ABI 14 provenance metadata:
author, license, homepage, description, `build` — used in the catalog and
to trace academic results).

### A node type is a small C++ object with a C surface

Each `NsNodeType` declares:

| field | meaning |
| --- | --- |
| `id` / `label` / `color[3]` | unique id ("gain"), palette name, ring color 0..255 |
| `create` / `destroy` | your state object (plain `new`/`delete` are fine) |
| `step` / `stepMulti` | one simulation step; returns port 0 (or fills all ports) |
| `save` / `load` | state blob: `malloc`'d string in, `0/1` out |
| `num_params` / `params` + `getParam`/`setParam` | structured parameters — the host builds the editor |
| `num_text` / `text_names` + `getText`/`setText` | editable strings (getText returns a `malloc`'d copy) |
| `def_in` | inputs at creation time (0 = 1; the user can still change it) |
| `pref_w` / `pref_h` | preferred box size in world units (0 = from title) |
| `self_out` + `numOut` | plugin-controlled output count (hides the host's 'saídas') |
| `shape` + `draw` | `NS_SHAPE_CUSTOM` = you draw the whole node; `draw` gets `NsDraw` |
| `outText` / `outValue` / `portLabel` / `portLabelIn` | text/matrix outputs, port labels (advanced) |
| `capabilities` | optional bits (unknown ones are ignored — safe evolution) |

### The step contract

`double step(void* h, const NsContext* c)` runs once per simulation step
(or N times with `NS_PLUGIN_EXEC` substeps). The `NsContext` gives you:

- `t`, `dt`, `x` (the node's own current value),
- `in[]` + `sockOff[]`: the wires of socket *s* are `in[sockOff[s]] ..
  in[sockOff[s+1]-1]`; the socket value is their **sum** (all-numeric case),
- `xval[s]`: the `NsValue` of socket *s* — the first non-numeric wire
  (text/list/matrix) when any wire is non-numeric.

The return value is output port 0; with `nOut > 1` implement `stepMulti`
(otherwise all ports mirror port 0).

### State: the save/load blob

`save` returns a `malloc`'d string; `load` parses it and returns 1 on
success. Two conventions live side by side:

- **Built-ins/controls**: `name=value` pairs separated by spaces, with text
  entries as quoted C-strings (`rotulo="START"`); this is what the
  `nsui::Control` base produces/consumes automatically;
- **positional fallback**: a blob with only numbers (no `=`) assigns values
  to `params[]` in order (the single-param convention, e.g. `pgain`).

Persist *everything* user-visible (params, texts, internal state) or it
will not survive Save/Load.

## 2. Step-by-step: a minimal numeric plugin

Full `.so`, one type, one internal state buffer — the `delay30` pattern
(`plugins/delay.cpp`, abbreviated here):

```c
#include "nodesim_plugin.h"   // the only third-party include
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Delay {                       // your state object
    std::vector<double> buf {30, 0.0};
    size_t pos = 0;
};

void* create()  { return new (std::nothrow) Delay(); }
void destroy(void* h) { delete (Delay*)h; }

double step(void* h, const NsContext* c) {
    Delay* d = (Delay*)h;
    if (!d || !c) return 0;
    const double out = d->buf[d->pos];                 // old head
    d->buf[d->pos] = (c->nin > 0 && c->in) ? c->in[0] : 0.0;
    d->pos = (d->pos + 1) % d->buf.size();
    return out;
}

char* save(void* h) {                                 // state → malloc'd blob
    Delay* d = (Delay*)h;
    if (!d) return nullptr;
    std::string s = std::to_string(d->pos) + ";";
    for (double v : d->buf) { char b[48];
        std::snprintf(b, sizeof b, "%.17g,", v); s += b; }
    char* o = (char*)std::malloc(s.size() + 1);
    if (o) std::memcpy(o, s.c_str(), s.size() + 1);
    return o;
}

int load(void* h, const char* s) {                    // blob → state (0 = reject)
    Delay* d = (Delay*)h;
    if (!d || !s) return 0;
    size_t pos = 0;
    if (std::sscanf(s, "%zu;", &pos) != 1 || pos >= d->buf.size()) return 0;
    const char* p = std::strchr(s, ';');
    if (!p) return 0;
    ++p;
    std::vector<double> buf;
    while (*p) { char* e = nullptr;
        const double v = std::strtod(p, &e);
        if (e == p) break;
        buf.push_back(v);
        p = (e && *e == ',') ? e + 1 : e; }
    if (buf.size() != d->buf.size()) return 0;
    d->buf = buf;
    d->pos = pos;
    return 1;
}

NsNodeType types[] = {
    {"delay30", "Delay 30", {140, 220, 255},
     create, destroy, step, save, load},   // no params/texts yet
};

} // namespace

extern "C" const NsPlugin* nodesim_plugin() {
    static const NsPlugin p = NS_PLUGIN_INIT_META(
        NODESIM_PLUGIN_API, "meu-plugins", "1.0", 1, types, "Signal",
        "Seu Nome", "GPL-3.0-or-later", "https://github.com/voce/projeto",
        "Delay line example", "build dev");
    return &p;
}
```

Build and exercise it:

```sh
make plugins                  # builds plugins/*.so (built-ins)
g++ -std=c++17 -shared -fPIC -Isrc/sdk -o meu-plugins.so meu-plugins.cpp
NODESIM_PLUGIN_PATH=. ./bin/simulacro            # palette shows "Delay 30"
NODESIM_PLUGIN_PATH=. ./bin/simulacro --run teste.sim --steps 40 --summary
```

## 3. Step-by-step: parameters and texts

Add a structured parameter table and a text; the host generates the editor
in the property grid and (when the type declares it) in-node:

```c
static const NsParam kParams[] = {
    // name      min   max  def   kind           step unit  help
    {"k",        0.0, 10.0, 1.0,  NS_PARAM_NUM,   0,   "×", "gain"},
    {"satura",   0.0,  1.0, 0.0,  NS_PARAM_BOOL,  1,   nullptr, "limit to ±1"},
};
static constexpr int kNumParams = 2;
static const char* const kTexts[] = {"unidade"};
```

Wire the accessors and include both in the blob:

```c
double g_k = 1.0; bool g_sat = false; std::string g_unid = "u";

double getParam(void*, int i) { return i == 0 ? g_k : (g_sat ? 1 : 0); }
int setParam(void*, int i, double v) {
    if (i == 0) g_k = v; else if (i == 1) g_sat = v > 0.5;
    else return 0;                    // 0 = rejected (host keeps the old value)
    return 1;
}
char* getText(void*, int) { return nsplug::dupStr(g_unid.c_str()); } // malloc!
int setText(void*, int, const char* s) { if (s) g_unid = s; return 1; }

std::string saveBlob() {
    char b[64]; std::snprintf(b, sizeof b, "%.17g", g_k);
    std::string o = std::string("k=") + b + (g_sat ? " satura=1" : " satura=0")
                  + " unidade=\"" + g_unid + "\"";   // texts go QUOTED
    char* out = (char*)std::malloc(o.size() + 1);
    if (out) std::memcpy(out, o.c_str(), o.size() + 1);
    return out;
}
```

Rules of the blob format:

- `name=value` separated by spaces; texts are C-string quoted with
  backslash escapes; unknown/missing names keep their current value;
- a blob with **only numbers** (no `=`) is positional into `params[]`;
- `load` returning 0 rejects the blob — the node keeps its current state
  and the host warns on stderr.

If your type is a *control* (clickable panel widget), skip the manual
plumbing entirely: the `nsui::Control` base (section 5) does params/texts/
blob automatically.

## 4. Step-by-step: custom drawing

Two levels:

- **default box** (`shape = NS_SHAPE_BOX`, `draw = null`): the host draws
  body, title bar, and outline; your `step` values appear as sparklines;
- **authored node** (`shape = NS_SHAPE_CUSTOM`, `draw` set): you draw the
  WHOLE silhouette — the host skips body/title and gives you the full node
  box in `NsRect` (screen pixels, zoom already applied) plus `NsDraw::label`
  (the node title) and the primitives:

```c
void draw(void* h, const NsDraw* g, const NsRect* area) {
    if (!g->rect || !g->text) return;
    // contract: colors pack as 0xRRGGBBAA — alpha in the LOW byte!
    const unsigned face = 0xF5B361FFu;   // (r,g,b,a) = (245,179,97,255)
    g->rect(g->ctx, area->x, area->y, area->w, area->h, face);
    g->text(g->ctx, area->x + 4, area->y + area->h / 2, g->label, 0xF0F0F8FFu);
}
```

> The #1 drawing pitfall: packing `0xAARRGGBB` compiles silently and renders
> blended/miscolored (the alpha ends up in the blue channel). The base
> `nodesim_ui_base.h` ships a `pack()` lambda — copy it.

Sockets, wires, port dots and hit-testing remain the host's — you only own
the ink inside the box. Fonts come from the host (`NsDraw::text`), so your
node matches the app theme; for text width use `g->textW`.

## 5. Step-by-step: panel controls by inheritance (the fast path)

For clickable "panel" widgets — buttons, switches, knobs — do **not** start
from raw C: the header-only base `src/sdk/nodesim_ui_base.h` gives you the
whole family (state, hooks, blob serialization, authored design) and you
only declare identity + the act contract:

```cpp
#include "nodesim_ui_base.h"   // includes nodesim_plugin.h

struct Botao : nsui::Pushdown {
    static constexpr const char* kTypeId = "push";
    static constexpr const char* kLabel = "Botão (pushdown)";
    void defColor(int rgb[3]) const override {     // VIRTUAL: static here is
        rgb[0] = 120; rgb[1] = 235; rgb[2] = 180;  // INVISIBLE inside the base
    }
};

struct Chave : nsui::Switch {
    static constexpr const char* kTypeId = "switch";
    static constexpr const char* kLabel = "Chave (toggle)";
    void defColor(int rgb[3]) const override { rgb[0] = 70; rgb[1] = 130; rgb[2] = 235; }
};

extern "C" const NsPlugin* nodesim_plugin() {
    static const NsNodeType kTipos[] = {{nsui::makeType<Botao>()},
                                        {nsui::makeType<Chave>()}};
    static const NsPlugin p = NS_PLUGIN_INIT_META(
        NODESIM_PLUGIN_API, "controles", "1.0", 2, kTipos, "Control",
        "Seu Nome", "GPL-3.0-or-later", "", "panel controls", "dev");
    return &p;
}
```

What you inherit: params (`pulsa`/`ligado` BOOL), editable texts
(`rotulo`, face colors as hex), blob auto-serialization, input level for
the push (external `1` presses the face and adds ` EXT`), authored design
(`kCustomShape`) with a free margin, and the hooks `onPressed/onReleased/
onExtern/onToggled`.

Then declare the **acts** (the contract that makes the canvas click drive a
param) and the **UI** (declarative dock/in-node widgets). Note: the macros
define ONE dlsym symbol per `.so` — one acts table and one UI table per
plugin, with one spec per type:

```cpp
static const NsAct kActs[] = {
    {"push",   NS_ACT_HOLD,   0},   // pressed while held
    {"switch", NS_ACT_TOGGLE, 0},   // each click flips and STAYS
};
NS_PLUGIN_ACTS_IN(kActs, 2, Botao::kPressInset); // 10px face margin stays draggable

static const NsUiItem kItensButton[] = {
    {NS_UI_SWITCH, "", 0, 0, 0, 0, 0},   // pulsa
    {NS_UI_LED,    "", 0, 0, 0, 0, 0},
    {NS_UI_TEXT,   "rótulo", 0, 0, 0, 0, 0},
};
static const NsUiItem kItensSwitch[] = {
    {NS_UI_SWITCH, "", 0, 0, 0, 0, 0},   // ligado
    {NS_UI_TEXT,   "rótulo", 0, 0, 0, 0, 0},
};
static const NsUiSpec kSpecsUi[] = {{"push", 3, kItensButton},
                                    {"switch", 2, kItensSwitch}};
NS_PLUGIN_UI(kSpecsUi, 2);
```

The host adds keyboard acts for free: with the control selected, **Space**
presses/releases it (HOLD) and **Enter** toggles it (TOGGLE).

`nsui::Control` (without the Pushdown/Switch family) is for the rest:
it gives you the blob/params plumbing while you own `step` and the design —
the former built-ins `entrada01`/`saida01` are documented as exactly this
exercise in [`abi.md`](abi.md).

## 6. The optional tables at a glance

| symbol | macro | purpose |
| --- | --- | --- |
| `nodesim_plugin_resources()` | `NS_PLUGIN_RESOURCES` | declare files/dirs/env vars the plugin reads (provenance in the catalog/manifest; sha256 optional) |
| `nodesim_plugin_exec()` | `NS_PLUGIN_EXEC` | per-type substeps (1..1024) for stiff dynamics — inputs stay constant inside a step, `NsContext::x` chains subcalls |
| `nodesim_plugin_events()` | `NS_PLUGIN_EVENTS` | discrete event sources (`rise`/`fall`/`change` per port+threshold) and optional per-type `on_event` sinks; one step of latency |
| `nodesim_plugin_ui()` | `NS_PLUGIN_UI` | declarative in-node/dock widgets bound to params/texts |
| `nodesim_plugin_acts()` | `NS_PLUGIN_ACTS` / `_IN` | canvas click → param (HOLD/TOGGLE, optional face inset) |

All of them validate `api == 14` and `struct_size >= sizeof(...)`; problems
print as `problem: …` and the plugin keeps working with the rest.

## 7. Testing and debugging

- **Headless first**: `./bin/simulacro --run teste.sim --steps 60` — stdout
  is the CSV trace (one column per scope channel), stderr carries plugin
  diagnostics; no display needed;
- a minimal graph (`fonte → seu nó → scope`) is your unit test; the
  deterministic engine means the trace is reproducible byte for byte;
- **stderr** at load lists `plugins: <name> v<ver> (n tipos)` and every
  `problem: …` (bad api, unknown type in a table, duplicate id…);
- **`NODESIM_DEBUG_UI=1`** enables host-side canvas diagnostics (fit checks,
  menu checks dump) when your drawing "looks wrong";
- common pitfalls, all diagnosed at load or first paint:
  1. `api != 14` or missing `struct_size` → plugin rejected;
  2. type `id` colliding with another plugin's → duplicate problem;
  3. state not persisted → field missing from the blob;
  4. colors shifted/transparent → `0xAARRGGBB` instead of `0xRRGGBBAA`;
  5. acts on a param the UI doesn't show → confusing to users, keep them in sync.

## 8. Distribution

Platform note: a plugin may use platform APIs, but keep portability in
mind — the built-in `proc` (process spawn) is POSIX-only and is excluded
from the Windows build.

Drop the `.so` (any name, built against `src/sdk/nodesim_plugin.h` only)
into `~/.local/share/nodesim/plugins/`, or point `NODESIM_PLUGIN_PATH` at a
directory. Third-party plugins may be C and depend only on libc; built-ins
are C++ (see `ldd seu-plugin.so` before distributing). Plugins that read
files should declare them with `NS_PLUGIN_RESOURCES` so provenance is
recorded — relative paths resolve from the `.so` directory.

## 9. Compatibility rules (the whole game)

- **ABI 14 is frozen by tests.** New capabilities arrive as additive tables
  validated by `api`/`struct_size`; unknown bits/fields are ignored;
- never edit the ABI number inside a plugin — recompile against the
  current SDK instead;
- `NS_MAX_PORTS = 16` is an ABI contract (host buffers are sized with it);
- keep `save/load` idempotent: `load(save(x))` must equal `x` — it is the
  first thing a graph round-trip exercises.
