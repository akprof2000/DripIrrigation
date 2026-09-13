#!/usr/bin/env bash
# 3D-картинки собранной платы и 3D-модель для просмотра через KiCad 10 в Docker.
#   python hardware/gen_3d.py && python hardware/gen_pcb.py && bash hardware/render-3d.sh
# Результат: hardware/renders/*.png и hardware/renders/DripCarrier.glb
set -euo pipefail
HW="$(cd "$(dirname "$0")" && (pwd -W 2>/dev/null || pwd))"
IMG="kicad/kicad:10.0.5-full"
run() { MSYS_NO_PATHCONV=1 docker run --rm -u 0 -v "$HW:/hw" -w /hw "$IMG" "$@"; }

mkdir -p "$(dirname "$0")/renders"
R="kicad-cli pcb render --quality high --floor --width 1920 --height 1280"

run sh -c "
$R --side top -o renders/01-top.png DripCarrier.kicad_pcb &&
$R --perspective --rotate '-50,0,25' --zoom 1.1 -o renders/02-iso-front.png DripCarrier.kicad_pcb &&
$R --perspective --rotate '-50,0,-155' --zoom 1.1 -o renders/03-iso-back.png DripCarrier.kicad_pcb &&
$R --perspective --rotate '-75,0,0' --zoom 1.2 -o renders/04-low-angle.png DripCarrier.kicad_pcb &&
$R --side bottom -o renders/05-bottom.png DripCarrier.kicad_pcb &&
kicad-cli pcb export glb --subst-models -o renders/DripCarrier.glb DripCarrier.kicad_pcb
"
ls -la "$(dirname "$0")/renders"
