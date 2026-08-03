#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Ogham — a dual-voice bytebeat synthesizer for Eurorack
#
# Author:     Steven Collins, 2026, Keeos.io
# Copyright:  (c) 2026 Steven Collins
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
#
# Regenerate the Interactive HTML BOM from the board file.
# -----------------------------------------------------------------------------
#
# Produces a single self-contained HTML file: click a line in the BOM and the
# parts light up on the board, front and back, with checkboxes for tracking what
# you have sourced and placed. It is the tool to have open while populating.
#
# The output is COMMITTED, so it can go stale. Re-run this whenever the board
# changes, and commit the result alongside.
#
# Requires KiCad's own Python (for `pcbnew`) and InteractiveHtmlBom v2.11.2 or
# newer — earlier versions cannot read KiCad 10 files.
#
#   https://github.com/openscopeproject/InteractiveHtmlBom
#
# Usage:
#   ./gen-ibom.sh [path/to/InteractiveHtmlBom]
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD="$HERE/ogham-merged-v1.0/ogham-merged-v1.0.kicad_pcb"
DEST="$HERE/production"
IBOM="${1:-}"

# KiCad's bundled Python has pcbnew; a system Python almost certainly does not.
for p in \
    "/c/Program Files/KiCad/10.0/bin/python.exe" \
    "/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3" \
    "$(command -v python3 || true)"; do
    if [ -x "$p" ] && "$p" -c "import pcbnew" 2>/dev/null; then PY="$p"; break; fi
done
if [ -z "${PY:-}" ]; then
    echo "error: no Python with 'pcbnew' found. Use the one bundled with KiCad." >&2
    exit 1
fi

if [ -z "$IBOM" ] || [ ! -f "$IBOM/InteractiveHtmlBom/generate_interactive_bom.py" ]; then
    echo "error: pass the path to an InteractiveHtmlBom checkout (>= v2.11.2)." >&2
    echo "       e.g. ./gen-ibom.sh ~/src/InteractiveHtmlBom" >&2
    exit 1
fi

echo "python: $PY"
"$PY" "$IBOM/InteractiveHtmlBom/generate_interactive_bom.py" \
    --no-browser \
    --dest-dir "$DEST" \
    --name-format "ogham-merged-v1.0-ibom" \
    --include-tracks --include-nets \
    --layer-view FB \
    --highlight-pin1 selected \
    --show-fields "Value,Footprint,LCSC" \
    --sort-order "C,R,L,D,Q,U,J,SW,RV,A" \
    "$BOARD"

echo "wrote $DEST/ogham-merged-v1.0-ibom.html"
