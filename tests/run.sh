#!/bin/sh
# simulacro — smoke tests (headless; no display required)
# Copyright (C) 2026 Flavio de Vasconcellos Correa
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
BIN=./bin/simulacro
EX=examples

fail=0
ok()   { printf 'ok   %s\n' "$1"; }
bad()  { printf 'FAIL %s\n' "$1"; fail=1; }

command -v wx-config >/dev/null 2>&1 || { echo "wx-config not found" >&2; exit 2; }

if ! make -s bin/simulacro plugins; then
    echo "build failed" >&2
    exit 2
fi

# --version
if "$BIN" --version | grep -q '^simulacro '; then ok "--version"; else bad "--version"; fi

# --help
if "$BIN" --help | grep -q -- '--run'; then ok "--help"; else bad "--help"; fi

# AR(1) example: 30 steps at 60 Hz -> 31 CSV lines, final t = 0.5
if [ -f "$EX/ar1.nodesim" ]; then
    out=$("$BIN" --run "$EX/ar1.nodesim" --steps 30 2>/dev/null)
    lines=$(printf '%s\n' "$out" | wc -l | tr -d ' ')
    if [ "$lines" -eq 31 ]; then ok "ar1 trace has 31 lines"; else
        bad "ar1 trace has $lines lines (expected 31)"
    fi
    if printf '%s\n' "$out" | head -n 1 | grep -q '^t,'; then
        ok "ar1 trace header"
    else
        bad "ar1 trace header"
    fi

    summary=$("$BIN" --run "$EX/ar1.nodesim" --steps 30 --summary 2>/dev/null)
    if printf '%s\n' "$summary" | grep -q '^t,0\.500000$'; then
        ok "ar1 summary t=0.5"
    else
        bad "ar1 summary t=0.5"
    fi
else
    bad "missing example $EX/ar1.nodesim"
fi

# Numeric regression: ar1 at 60 steps must match the golden trace byte for byte
# (the engine is deterministic; regenerate tests/golden/ only with intent).
for f in ar1 contador_logico push-level; do
    if [ -f "tests/golden/$f.csv" ] && [ -f "$EX/$f.nodesim" ]; then
        # steps por arquivo: push-level cobre a transição do nível (120 = 2s)
        case "$f" in
            push-level) n=120 ;;
            *) n=60 ;;
        esac
        if "$BIN" --run "$EX/$f.nodesim" --steps $n > .golden.tmp 2>/dev/null
        then diff -q tests/golden/$f.csv .golden.tmp >/dev/null 2>&1 && ok "$f golden trace" || bad "$f golden trace"
        else bad "$f golden trace (run failed)"
        fi
        rm -f .golden.tmp
    else
        bad "missing golden/example for $f"
    fi
done

# Map scope loads and renders headlessly (no channels, but must not fail)
if [ -f "$EX/mapas/mapa-base.nodesim" ]; then
    if "$BIN" --run "$EX/mapas/mapa-base.nodesim" --steps 1 --summary >/dev/null 2>&1; then
        ok "map example loads"
    else
        bad "map example loads"
    fi
else
    bad "missing map example"
fi

# A few more files from the corpus must load without missing plugins
# Controles nativos (push/entrada01/saida01) precisam carregar sem plugins faltando
for f in contador.nodesim logica.nodesim kalman.nodesim controles.nodesim push-level.nodesim; do
    [ -f "$EX/$f" ] || continue
    err=$("$BIN" --run "$EX/$f" --steps 3 --summary 2>&1 >/dev/null || true)
    if printf '%s' "$err" | grep -qi 'missing plugin\|could not load'; then
        bad "$f loads cleanly"
    else
        ok "$f loads cleanly"
    fi
done

# ------------------------------------------------------------- kill switches
# Cada camada gateada por compilação precisa compilar em TODA config: o bug
# "construtor engolido por #if" (definições de função aninhadas no corpo)
# só aparecia com SIMULACRO_NODE_UI=1 — config fora do loop de build diário.
syntax_check() { # files... (config via $1)
    desc="$1"; shift
    extra="$1"; shift
    flags="-std=c++17 -Isrc -Isrc/core -Isrc/sdk -I$THIRDPARTY -I$THIRDPARTY/backends $extra $(wx-config --cxxflags) $(sdl2-config --cflags)"
    for f in "$@"; do
        g++ -fsyntax-only $flags "$f" >/dev/null 2>&1 || { bad "syntax $desc: $f"; return; }
    done
    ok "syntax $desc"
}
THIRDPARTY=third_party/imgui
GATED_FILES="src/canvas.cpp src/engine.cpp src/app.cpp src/properties.cpp"
syntax_check "killswitch all-off" "" $GATED_FILES
syntax_check "killswitch NODE_WINDOW" "-DSIMULACRO_NODE_WINDOW=1" $GATED_FILES
syntax_check "killswitch NODE_WINDOW+NODE_UI" \
    "-DSIMULACRO_NODE_WINDOW=1 -DSIMULACRO_NODE_UI=1" $GATED_FILES

# ------------------------------------------------------------- controles
# Comportamento da família de controles (push/switch) direto na base:
# step com nível de entrada, design (cores 0xRRGGBBAA + textos), blob.
if g++ -std=c++17 -Isrc/sdk -o .controles_test tests/controles_test.cpp 2>/dev/null &&
   ./.controles_test > .controles_test.out 2>&1; then
    sed 's/^/     /' .controles_test.out
    ok "controles family behavior"
else
    cat .controles_test.out >&2 || true
    bad "controles family behavior"
fi
rm -f .controles_test .controles_test.out

if [ "$fail" -ne 0 ]; then
    echo "some tests failed" >&2
    exit 1
fi
echo "all tests passed"
