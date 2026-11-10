// nodesim — node graph editor and simulator
// Copyright (C) 2026 Flavio de Vasconcellos Correa
// SPDX-License-Identifier: GPL-3.0-or-later

// Public SDK for nodesim plugins (.so).
//
// This is the only header a third-party plugin needs to include. It is
// deliberately self-contained: it includes nothing from the host, SDL, or the
// STL. A plugin exports `extern "C" const NsPlugin* nodesim_plugin();` and
// returns a static table of types. The state blob uses malloc/free (libc).
//
// ABI 14: package identity/provenance (author, license, homepage,
//          description, and build) in addition to struct_size/capabilities from ABI 13.
// ABI 13: `NsPlugin::struct_size` + capabilities declared per type.
// ABI 12: parameters with kind/unit/help/options + input labels.
// (ABI 11: numeric values as double on the wire, NsContext::in and stepMulti.)
//
// Additive extensions of ABI 14 (optional symbols, looked up with dlsym;
// plugins that do not export them remain valid and the ABI does not change):
//  - `nodesim_plugin_resources()` declares files, directories, and environment
//    variables the plugin depends on (NS_PLUGIN_RESOURCES).
//  - `nodesim_plugin_exec()` declares a per-type execution policy, such as
//    internal substeps (NS_PLUGIN_EXEC).
//  - `nodesim_plugin_events()` declares discrete event sources and sinks
//    (NS_PLUGIN_EVENTS).
//  - `nodesim_plugin_ui()` declares the in-node widget layout (sliders,
//    switches, combos, inputs, LEDs) bound to params/texts of the same type
//    (NS_PLUGIN_UI). The host renders it; plugins without the symbol keep
//    the generic parameter editor.
//  - `nodesim_plugin_acts()` declares canvas CLICK actions driven by the
//    host on a params[] entry (pushdown/hold or toggle) — NS_PLUGIN_ACTS.
#pragma once

#define NODESIM_PLUGIN_API 14
#define NODESIM_SDK_MAJOR 1
#define NODESIM_SDK_MINOR 4

// Maximum number of ports per node (inputs and outputs). The host allocates out[MAX] and
// passes buffers of that size to plugins (stepMulti, xout); it holds as an ABI contract.
static constexpr int NS_MAX_PORTS = 16;

// ---- Values on the wires (ABI 10) ----
// A wire can carry a number, text, a list (1×n), or a matrix (rows×cols,
// row by row). Pointers are BORROWED: they are valid for the duration of the
// step call (the host copies immediately). A socket receives the first non-numeric
// wire; if all of them are numbers, the sum applies (as in 'in').
enum NsValKind {
    NS_VAL_NUM = 0,  // number in NsValue::num
    NS_VAL_TXT = 1,  // text in NsValue::txt
    NS_VAL_LIST = 2, // list in NsValue::data (cols elements, rows = 1)
    NS_VAL_MAT = 3,  // matrix in NsValue::data (rows×cols, row by row)
};

struct NsValue {
    int kind = NS_VAL_NUM;
    double num = 0;
    const char* txt = nullptr;
    const double* data = nullptr;
    int rows = 0, cols = 0;
};

// Context of one step (mirrors ExprCtx): x = current node value.
struct NsContext {
    double t = 0, dt = 0, x = 0, sum = 0, n = 0;
    const double* in = nullptr;
    int nin = 0;
    // Socket boundaries in the 'in' list: socket s equals
    // in[sockOff[s]] + ... + in[sockOff[s+1]-1] (sum of the wires in that socket).
    // sockOff has nsock+1 entries and nsock == number of node inputs. It lets
    // plugins such as logic gates treat each socket as an operand.
    const int* sockOff = nullptr;
    int nsock = 0;
    // Value of each socket (nsock entries, same index as sockOff). A socket
    // whose wires are all numeric has kind=NS_VAL_NUM and num=sum; with text/list/
    // matrix it carries the first non-numeric wire (and num = numeric fallback).
    const NsValue* xval = nullptr;
};

// Rectangle in SCREEN pixels (zoom/pan already applied by the host).
struct NsRect { int x, y, w, h; };

// Drawing "brush" provided by the host (the plugin does NOT depend on SDL):
// all primitives paint inside the node area. Color in 0xRRGGBBAA.
struct NsDraw {
    void* ctx; // opaque (from the host)
    void (*rect)(void* ctx, int x, int y, int w, int h, unsigned rgba);
    void (*line)(void* ctx, int x0, int y0, int x1, int y1, unsigned rgba);
    void (*text)(void* ctx, int x, int y, const char* utf8, unsigned rgba); // top-left corner
    int  (*textW)(void* ctx, const char* utf8);                            // width in px
    // Node title (the same one the host would display: user-given name or
    // type label). Valid only during the 'draw' call. Custom shapes
    // use it to draw their own label.
    const char* label;
};

// Node shape (NsNodeType::shape). The default is the host box.
enum NsShape {
    NS_SHAPE_BOX = 0,    // default box: host draws body, title bar, and outline
    NS_SHAPE_CUSTOM = 1, // the plugin draws the whole node (silhouette + label) in 'draw'
};

// Capabilities declared by an NsNodeType (ABI 13). The host adds those it
// can itself infer from the populated pointers; declaring a capability
// does not replace the corresponding pointer. It is used for catalog/tools and
// for compatible evolution: unknown bits are ignored by the host.
enum NsCaps {
    NS_CAP_STEP_MULTI = 1 << 0,   // uses stepMulti
    NS_CAP_STATE = 1 << 1,        // state save/load
    NS_CAP_PARAMS = 1 << 2,       // structured parameters
    NS_CAP_TEXTS = 1 << 3,        // text parameters
    NS_CAP_DYNAMIC_OUT = 1 << 4,  // numOut controls the outputs
    NS_CAP_TEXT_OUT = 1 << 5,     // outText publishes text per port
    NS_CAP_VALUE_OUT = 1 << 6,    // outValue publishes text/list/matrix
    NS_CAP_CUSTOM_DRAW = 1 << 7,  // draw draws the body/shape
    NS_CAP_INPUT_LABELS = 1 << 8, // portLabelIn names sockets
    // Additive extension (ABI 14): output 0 of the node is a PAUSE request when
    // != 0. The host pauses the simulation after the step and informs the node. Use with
    // a type that receives 0/1 and publishes 1 at the stop moment (e.g. "pausa").
    NS_CAP_PAUSE = 1 << 9,
    // Additive extension: output 0 is the DURATION in milliseconds of a
    // timed pause (0 = no request). The host pauses and resumes on its own at the deadline.
    NS_CAP_PAUSE_MS = 1 << 10,
};

// ---- .so side (C) ----
// Type of a structured parameter. The new NsParam fields are optional:
// an old-style table ({name,min,max,def}) remains valid as NUM.
enum NsParamKind {
    NS_PARAM_NUM = 0,   // real number (default)
    NS_PARAM_BOOL = 1,  // 0/1, edited as yes/no
    NS_PARAM_ENUM = 2,  // integer index into choices[] (0..num_choices-1)
    NS_PARAM_INT = 3,   // integer (rounded by the host before setParam)
};

struct NsParam {
    const char* name; // e.g. "k"
    double min, max, def;
    // ---- ABI 12 (optional; zero/null keeps the old behavior) ----
    int kind;                   // NsParamKind
    double step;                // ↑/↓ increment; 0 = automatic (1 for INT/BOOL)
    const char* unit;           // suffix displayed ("%", "Hz", "samples"); null = none
    const char* help;           // one-line help shown on hover; null = none
    int num_choices;            // ENUM: number of options in choices
    const char* const* choices; // ENUM: labels; the persisted value is the index
};

struct NsNodeType {
    const char* id;    // e.g. "delay30" (unique among plugins)
    const char* label; // e.g. "Delay 30"
    float color[3];    // 0..255, ring color
    void* (*create)();
    void (*destroy)(void*);
    double (*step)(void*, const NsContext*);
    char* (*save)(void*);            // can be null (= no state)
    int (*load)(void*, const char*); // can be null
    // Structured parameters (optional): the host generates the editor.
    // The plugin must include them in the save/load blob to persist.
    int num_params;
    const NsParam* params;
    double (*getParam)(void*, int);
    int (*setParam)(void*, int, double);
    // Multi-output (optional): fills out[0..nout-1] (already clamped by the host at the end)
    // with the values of all ports; returns 1 if ok (0 = keeps previous values).
    // Used when the node has nOut>1. If null, 'step' feeds port 0 and the others mirror it.
    int (*stepMulti)(void*, const NsContext*, double* out, int nout);
    // Text parameters (optional): the host generates an editable field per entry.
    // getText returns a malloc'd string (the host frees it with ::free); setText validates/stores (1=ok).
    // They must also be included in the save/load blob to persist.
    int num_text;
    const char* const* text_names;
    char* (*getText)(void*, int);
    int (*setText)(void*, int, const char*);
    // Plugin-controlled outputs (optional): self_out=1 enables numOut().
    // numOut() returns the desired number of ports (0 = the host controls it via the 'saídas' field);
    // >0 makes the host hide 'saídas', adjust node.nOut, and prune edges from extra ports.
    int self_out;
    int (*numOut)(void*);
    // TEXT output per port (optional): malloc'd string (the host frees it) when port
    // k carries text in this iteration, or null if numeric. They must be included in the save/load blob.
    char* (*outText)(void*, int);
    // Output port label (optional): name of port k (e.g. CSV column) or null.
    char* (*portLabel)(void*, int);
    // Number of inputs with which the host CREATES the node (e.g. 2 for an AND port); 0 = 1.
    // Valid only at creation: the count remains editable ('entradas') and load takes precedence.
    int def_in;
    // Preferred node size in world units (0 = the host computes it from the title).
    // Useful for displays (e.g. clock) that need a larger body.
    int pref_w, pref_h;
    // Custom drawing of the node BODY (optional): the host calls it every frame,
    // with its own primitives (NsDraw) and the available area in screen pixels.
    // null = the host draws the default body.
    void (*draw)(void* handle, const NsDraw* g, const NsRect* area);
    // Node shape (NsShape): 0 = default box (host draws body + title);
    // 1 = custom: the host does NOT draw body/title/outline — the plugin
    // draws the entire silhouette in 'draw' (the area is the whole node box, without
    // clip) and the label comes in NsDraw::label. Sockets/links remain the host's.
    // Values out of range become box. Absent at initialization = box.
    int shape;
    // Value published per port (ABI 10, optional): returns 1 and fills *v with
    // pointers valid until the next step (the host copies them); 0 = keeps the previous one.
    // Complements stepMulti: the port can carry text/list/matrix. outValue
    // text survives the following 0; numbers normally come from step/stepMulti
    // (an outValue NUM=1 is valid only for that step).
    int (*outValue)(void* handle, int port, NsValue* v);
    // INPUT port label (ABI 12, optional): malloc'd string (the host frees it)
    // with the name of socket k (e.g. "y", "x1", "A"); null = no label.
    char* (*portLabelIn)(void* handle, int port);
    // Declared capabilities (ABI 13, optional): bits from NsCaps. Zero = the host
    // infers everything from the pointers. Unknown bits are ignored.
    int capabilities;
};

struct NsPlugin {
    int api; // == NODESIM_PLUGIN_API
    const char* name;
    const char* version;
    int num_types;
    const NsNodeType* types;
    // Category (group) of this plugin's types, shown in the Add menu.
    // E.g. "Logic", "Math", "Signal", "Data". null/empty = "General".
    const char* group;
    // ---- ABI 13 (optional; zero = ABI 13 behavior without extensions) ----
    // sizeof(NsPlugin) at plugin compile time. The host uses this to
    // accept structs from future versions without reading fields that do not exist. Zero is
    // accepted as "minimum ABI 13 size" (old initializers).
    int struct_size;
    // Reserved for global plugin capabilities; today the host uses the
    // capabilities declared in each NsNodeType.
    int capabilities;
    // ---- ABI 14: package identity and provenance ----
    // All optional; the host replicates them in the catalog and in the execution
    // manifest. `build` should identify the artifact (commit, tag, or build
    // date) so that an academic result can be traced.
    const char* author;
    const char* license;
    const char* homepage;
    const char* description;
    const char* build;
};

// ---- Declared resources (additive extension of ABI 14) ----
// The host does not prevent the plugin's access to the filesystem (isolation per
// process is out of scope), but a plugin can DECLARE what it reads so that
// the catalog, diagnostics, and manifest record provenance. Relative paths
// are resolved from the directory of the `.so` itself; absolute paths
// are used as-is. For NS_RES_ENV, `path` is the variable NAME
// (the host records only presence/absence, never the value).
enum NsResourceKind {
    NS_RES_FILE = 0, // regular file
    NS_RES_DIR = 1,  // directory
    NS_RES_ENV = 2,  // environment variable
};

struct NsResource {
    const char* id;          // short identifier unique within the plugin ("tabela")
    int kind;                // NsResourceKind
    const char* path;        // relative to the .so, absolute, or variable name
    int required;            // 1 = the plugin does not work without the resource
    const char* description; // optional, shown in the catalog/diagnostics
    const char* sha256;      // optional: expected file hash (lowercase hex)
};

struct NsResources {
    int api;              // == NODESIM_PLUGIN_API
    int struct_size;      // sizeof(NsResources); 0 = minimal table
    int num_resources;    // 0..1024
    const NsResource* resources;
};

// Exports `nodesim_plugin_resources()`. Use it in the same .cpp as
// `nodesim_plugin()`, after the static table:
//   static const NsResource kRecursos[] = {
//       {"tabela", NS_RES_FILE, "dados/tabela.csv", 1, "tabela de coeficientes",
//        "0123...abcd"},
//   };
//   NS_PLUGIN_RESOURCES(kRecursos, 1);
// The host accepts the absence of the symbol (plugin with no external resources).
#define NS_PLUGIN_RESOURCES(table_, n_)                                      \
    extern "C" const NsResources* nodesim_plugin_resources() {                \
        static const NsResources ns_resources_ = {                             \
            NODESIM_PLUGIN_API, (int)sizeof(NsResources), (n_), (table_)};   \
        return &ns_resources_;                                                 \
    }

// ---- Execution policy (additive extension of ABI 14) ----
// Substeps: instead of a single call to step/stepMulti per simulation step,
// the host calls the type N times with dt/N, advancing t at the end of each
// subinterval. Input wires remain CONSTANT during the substeps
// (explicit integration) and each call sees in NsContext::x the value returned
// by the previous call. The final result is the one from the last call; texts and
// values published by outText/outValue are queried only once, at the
// end. This allows stiff equations to be integrated with a smaller internal step without
// changing the graph step. `substeps` 0 means 1; the accepted range is 1..1024.
//
// The policy is PER TYPE and does not change the .nodesim file format. A type
// without a declaration (or a plugin without the symbol) runs once per step.
struct NsExec {
    const char* type; // NsNodeType id (must exist in the same .so)
    int substeps;     // 1..1024; 0 = 1
};

struct NsExecTable {
    int api;           // == NODESIM_PLUGIN_API
    int struct_size;   // sizeof(NsExecTable); 0 = minimal table
    int num_entries;   // 0..1024
    const NsExec* entries;
};

// Exports `nodesim_plugin_exec()`. Example, in the same .cpp as nodesim_plugin():
//   static const NsExec kExec[] = {{"meu_tipo", 4}};
//   NS_PLUGIN_EXEC(kExec, 1);
// The host accepts the absence of the symbol. Unknown types, duplicate ids, and
// out-of-range substeps go into the diagnostics (--checar-plugins).
#define NS_PLUGIN_EXEC(table_, n_)                                         \
    extern "C" const NsExecTable* nodesim_plugin_exec() {                   \
        static const NsExecTable ns_exec_ = {                               \
            NODESIM_PLUGIN_API, (int)sizeof(NsExecTable), (n_), (table_)}; \
        return &ns_exec_;                                                   \
    }

// ---- Discrete events (additive extension of ABI 14) ----
// An event represents an OCCURRENCE, not a level: the output port crossed
// from one side of the threshold to the other, or changed. The host detects the transition at the
// end of a step and delivers the event at the start of the next step, through the
// existing wires, at the destination node's input socket. The event is valid only for that
// step and there is no cascade within the same step.
//
// Source: declaration by type, port, and edge. The port must be numeric.
// `rise` uses previous < threshold <= current; `fall`, previous >= threshold > current;
// `change`, current != previous. `threshold` is used only by rise/fall and must be
// 0 in change. The port is 0..15.
//
// Sink: optional callback per type. The host calls `on_event` before the step's
// step/stepMulti, once per event delivered to the node, in deterministic
// order. The implementation must not re-enter the host or block.
enum NsEventKind {
    NS_EVENT_RISE = 1,
    NS_EVENT_FALL = 2,
    NS_EVENT_CHANGE = 3,
};

struct NsEvent {
    int kind;          // NsEventKind
    int source;        // source node id
    int source_port;   // source output port
    int socket;        // destination node input socket
    double t;          // instant of the step that generated the transition
    double value;      // final value of the output
    double previous;   // value at the start of the step
};

struct NsEventSource {
    const char* type;  // NsNodeType id (must exist in the same .so)
    int port;          // 0..15
    int kind;          // NsEventKind
    double threshold;  // rise/fall; 0 in change
};

struct NsEventSink {
    const char* type;  // NsNodeType id (must exist in the same .so)
    void (*on_event)(void* handle, const NsEvent* event);
};

struct NsEvents {
    int api;                  // == NODESIM_PLUGIN_API
    int struct_size;          // sizeof(NsEvents); 0 = minimal table
    int num_sources;          // 0..1024
    const NsEventSource* sources;
    int num_sinks;            // 0..1024
    const NsEventSink* sinks;
};

// Exports `nodesim_plugin_events()`. Example:
//   static const NsEventSource kFontes[] = {{"meu_tipo", 0, NS_EVENT_RISE, 0}};
//   static const NsEventSink kSinks[] = {{"meu_tipo", ao_evento}};
//   NS_PLUGIN_EVENTS(kFontes, 1, kSinks, 1);
// The host accepts the absence of the symbol (plugin without events) and validates types,
// ports, edges, thresholds, duplicates, and null callbacks in --checar-plugins.
#define NS_PLUGIN_EVENTS(sources_, nf_, sinks_, ns_)                              \
    extern "C" const NsEvents* nodesim_plugin_events() {                         \
        static const NsEvents ns_events_ = {                                   \
            NODESIM_PLUGIN_API, (int)sizeof(NsEvents), (nf_), (sources_),         \
            (ns_), (sinks_)};                                                    \
        return &ns_events_;                                                     \
    }

// ABI 13-compatible initializer: fills struct_size and leaves the ABI 14
// metadata empty. Example:
//   static const NsPlugin p = NS_PLUGIN_INIT(NODESIM_PLUGIN_API, "meu", "1.0",
//                                            1, types, "Math");
#define NS_PLUGIN_INIT(api_, name_, version_, num_types_, types_, group_) \
    { (api_), (name_), (version_), (num_types_), (types_), (group_),      \
      (int)sizeof(NsPlugin), 0, nullptr, nullptr, nullptr, nullptr, nullptr }

// Initializer with full metadata (recommended for third-party plugins).
#define NS_PLUGIN_INIT_META(api_, name_, version_, num_types_, types_, group_, author_, \
                            license_, homepage_, desc_, build_)                    \
    { (api_), (name_), (version_), (num_types_), (types_), (group_),                   \
      (int)sizeof(NsPlugin), 0, (author_), (license_), (homepage_), (desc_), (build_) }

// Convenience for the repository's built-in plugins. Third parties should use
// NS_PLUGIN_INIT_META with their own data.
#define NS_PLUGIN_BUILTIN(name_, version_, num_types_, types_, group_)                    \
    NS_PLUGIN_INIT_META(NODESIM_PLUGIN_API, (name_), (version_), (num_types_), (types_), \
                        (group_), "Flavio de Vasconcellos Correa",                   \
                        "GPL-3.0-or-later", "",                                      \
                        "Plugin embutido do nodesim", "built-in")

// Convenience for the repository's built-in plugins. Third parties should use
// NS_PLUGIN_INIT_META with their own data.
#define NS_PLUGIN_BUILTIN(name_, version_, num_types_, types_, group_)                    \
    NS_PLUGIN_INIT_META(NODESIM_PLUGIN_API, (name_), (version_), (num_types_), (types_), \
                        (group_), "Flavio de Vasconcellos Correa",                   \
                        "GPL-3.0-or-later", "",                                      \
                        "Plugin embutido do nodesim", "built-in")

// ---- In-node UI declaration (additive extension of ABI 14) ----
// A declarative widget layout rendered by the host over (or inside) the node
// box when the type is selected/im node canvas uses it. It binds to parameters
// and text entries of the same type; the plugin does NOT draw anything -- it
// declares items, with a straightforward mapping:
//
//   kind             binding                    rendered as
//   NS_UI_SLIDER     params[p] (NUM)            drag slider with min/max
//   NS_UI_INT        params[p] (INT)            int slider / numeric input
//   NS_UI_SWITCH     params[p] (BOOL)           checkbox
//   NS_UI_COMBO      params[p] (ENUM)           combo with choices[]
//   NS_UI_TEXT       texts[t]                   single-line input
//   NS_UI_LED        params[p] (>=0.5 lights)   colored dot
//   NS_UI_LABEL      (text literal)             read-only caption
//   NS_UI_VSPACE     (none)                     vertical spacing
//
// Unknown kinds and invalid bindings are ignored by the host (safe to keep
// growing the table). The layout is *declarative*: no drawing callbacks, no
// layout math in the plugin; the host honors the theme.
enum NsUiKind {
    NS_UI_END = 0,
    NS_UI_SLIDER = 1,   // params[p], NUM slider
    NS_UI_INT = 2,      // params[p], INT slider/input
    NS_UI_SWITCH = 3,   // params[p], BOOL checkbox
    NS_UI_COMBO = 4,    // params[p], ENUM combo
    NS_UI_TEXT = 5,     // texts[t], single-line input
    NS_UI_LED = 6,      // params[p] >= 0.5 lights up
    NS_UI_LABEL = 7,    // static caption (uses `name` below)
    NS_UI_VSPACE = 8,   // empty line
};

struct NsUiItem {
    int kind;            // NsUiKind
    const char* name;    // LABEL text or (empty = parameter name); also used as
                         // tooltip override when non-empty
    int index;           // params[] or texts[] index (-1 for LABEL/VSPACE)
    double min, max, step; // 0,0,0 = use the NsParam values
    // Reserved for future (optional hosts ignore). Zero-initialized by the table.
    int flags;
};

struct NsUiSpec {
    const char* type;    // NsNodeType id (must exist in the same .so)
    int num_items;       // number of entries in items (0 = no UI)
    const NsUiItem* items;
};

struct NsUiTable {
    int api;             // == NODESIM_PLUGIN_API
    int struct_size;     // sizeof(NsUiTable); 0 = minimal table
    int num_specs;       // 0..1024
    const NsUiSpec* specs;
};

// Exports `nodesim_plugin_ui()`. Example, in the same .cpp as nodesim_plugin():
//   static const NsUiItem kLedItens[] = {
//       {NS_UI_LED, ___, nullptr, 0, 0, 0, 0},   ...
//   };
//   static const NsUiSpec kSpecs[] = {{"led", 3, kLedItens}};
//   NS_PLUGIN_UI(kSpecs, 1);
// The host accepts the absence of the symbol (generic parameter editor).
#define NS_PLUGIN_UI(specs_, n_)                                            \
    extern "C" const NsUiTable* nodesim_plugin_ui() {                       \
        static const NsUiTable ns_ui_ = {                                    \
            NODESIM_PLUGIN_API, (int)sizeof(NsUiTable), (n_), (specs_)};     \
        return &ns_ui_;                                                      \
    }

// ---- Canvas CLICK actions (additive extension of ABI 14) ----
// Declares which parameter(s) a type wants the HOST to drive when the user
// clicks (presses) the node *body* on the canvas — the "panel control"
// contract. Two kinds:
//   NS_ACT_HOLD   : while the button is held the host sets param=1 and on
//                   release sets param=0 (pushdown/momentary);
//   NS_ACT_TOGGLE : each click flips the param (0/1) (switch/latch).
// `param` is the params[] index driven by the host. While the act fires the
// host does NOT create undo checkpoints and does NOT start node dragging
// (panel control semantics; move the node via the property grid or by fixing
// its x/y). Third-party plugins derive from nsui::Pushdown / nsui::Switch in
// nodesim_ui_base.h (state, hooks and authored design included) and declare
// the matching act; the host validates the table like every other one.
enum NsActKind {
    NS_ACT_HOLD = 1,   // press+hold: param=1 while down, param=0 on release
    NS_ACT_TOGGLE = 2, // each click flips the param (latch)
};

struct NsAct {
    const char* type; // NsNodeType id (must exist in the same .so)
    int kind;         // NsActKind
    int param;        // params[] index driven by the click
};

struct NsActTable {
    int api;           // == NODESIM_PLUGIN_API
    int struct_size;   // sizeof(NsActTable); 0 = minimal table
    int num_entries;   // 0..1024
    const NsAct* entries;
    // Additive trailing field (read by the host ONLY when struct_size >=
    // sizeof(NsActTable); older plugins read as `inset = 0`):
    int inset;         // world px of FREE MARGIN around the press face
};

#define NS_PLUGIN_ACTS(entries_, n_)                                          \
    extern "C" const NsActTable* nodesim_plugin_acts() {                       \
        static const NsActTable ns_acts_ = {                                   \
            NODESIM_PLUGIN_API, (int)sizeof(NsActTable), (n_), (entries_), 0}; \
        return &ns_acts_;                                                      \
    }

// Variant with an inner margin: the host routes clicks in the face to the
// press and keeps the margin free for selecting/dragging the node. The
// plugin draws ONLY the face (kCustomShape + inset in nsui::Pushdown::draw).
#define NS_PLUGIN_ACTS_IN(entries_, n_, inset_)                               \
    extern "C" const NsActTable* nodesim_plugin_acts() {                       \
        static const NsActTable ns_acts_ = {                                   \
            NODESIM_PLUGIN_API, (int)sizeof(NsActTable), (n_), (entries_),     \
            (inset_)};                                                          \
        return &ns_acts_;                                                      \
    }

