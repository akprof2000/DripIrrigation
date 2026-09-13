#!/usr/bin/env bash
# Проверка платы и выгрузка файлов для завода через KiCad 10 в Docker.
#   bash hardware/kicad-docker.sh
# Результат: hardware/drc-report.json, hardware/gerbers/*.gbr + *.drl, hardware/DripCarrier-gerbers.zip
# Код возврата 1, если DRC нашёл ошибки или неразведённые цепи (предупреждения не считаются).
set -euo pipefail
HW="$(cd "$(dirname "$0")" && (pwd -W 2>/dev/null || pwd))"
IMG="kicad/kicad:10.0.5-full"
run() { MSYS_NO_PATHCONV=1 docker run --rm -u 0 -v "$HW:/hw" -w /hw "$IMG" "$@"; }

docker info >/dev/null 2>&1 || { echo "Docker не запущен: откройте Docker Desktop и повторите" >&2; exit 2; }
rm -rf "$(dirname "$0")/gerbers" "$(dirname "$0")/drc-report.json"
mkdir -p "$(dirname "$0")/gerbers"

echo "== DRC =="
run kicad-cli pcb drc --refill-zones --save-board --format json --severity-error --severity-warning \
    --output drc-report.json DripCarrier.kicad_pcb
python - "$(dirname "$0")/drc-report.json" <<'PY'
import json, sys, collections
d = json.load(open(sys.argv[1], encoding='utf-8'))
v = d['violations']
err = [x for x in v if x['severity'] == 'error']
print('ошибок:', len(err), ' предупреждений:', len(v) - len(err), ' неразведено:', len(d['unconnected_items']))
print(collections.Counter((x['severity'], x['type']) for x in v).most_common())
sys.exit(1 if err or d['unconnected_items'] else 0)
PY

echo "== Gerber =="
run kicad-cli pcb export gerbers --output gerbers/ \
    --layers F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
    --no-protel-ext DripCarrier.kicad_pcb

echo "== Сверловка =="
run kicad-cli pcb export drill --output gerbers/ --format excellon --excellon-separate-th \
    --generate-map --map-format gerberx2 DripCarrier.kicad_pcb

echo "== Архив =="
run sh -c 'cd gerbers && rm -f ../DripCarrier-gerbers.zip && python3 -m zipfile -c ../DripCarrier-gerbers.zip *'
echo "Готово"
