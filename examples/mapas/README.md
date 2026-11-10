# Map examples

Examples for the **map Scope**. The overlay, interaction and link examples use
only files from this folder and `../mapa.geojson`; `mapa-base` is an editable
base and can point to your own maps.

## Open

```sh
./bin/nodesim examples/mapas/mapa-base.nodesim
./bin/nodesim examples/mapas/mapa-sobreposto.nodesim
./bin/nodesim examples/mapas/mapa-interativo.nodesim
```

Then open the **Map** node window with a double-click or `O`.

## Files

| file | demonstrates |
|---|---|
| `mapa-base.nodesim` | editable base with values per region |
| `mapa-sobreposto.nodesim` | the same regions with a road layer on top |
| `mapa-interativo.nodesim` | initial state with selection, painting and annotation |
| `rodovias-exemplo.geojson` | three example `LineString`s |

## Map tools

| key | action |
|---|---|
| `S` | select/pan |
| `B` | paint |
| `1`…`6` | choose the paint color |
| `A` | annotate a line |
| `G` | annotate a polygon |
| `M` | measure distance |
| `Enter` | finish annotation |
| `Esc` | cancel draft/measurement |
| `Backspace` | remove the last point |
| `F` | refit |

## State

The map state lives in the node `pstate`, inside the `.nodesim` file:

- camera;
- selection;
- paintings;
- annotations.

Use **Save: current state** to write the live state and **Save: initial state**
to keep the starting state. `F5` restores the initial state.

## Use your own large maps

Change the paths in the **MAP** panel:

```text
file   = .../municipios.geojson
overlay  = .../rodovias.geojson
idfield   = id
label    = nome
label2   = nome
```

For several overlays, separate the paths with `|`:

```text
overlay=rodovias.geojson|rios.geojson
```
