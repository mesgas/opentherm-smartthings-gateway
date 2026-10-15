#!/bin/bash
cd "$(dirname "$0")/.."
"/c/Program Files/KiCad/10.0/bin/python.exe" gen/build_pcb.py 2>&1 | grep -v swig | tail -1
"/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb drc --severity-all --format json -o build/drc.json opentherm-gateway.kicad_pcb >/dev/null
python - <<'PY'
import json
d=json.load(open('build/drc.json',encoding='utf-8'))
n=0
for v in d['violations']:
    if v['type'] in ('courtyards_overlap','items_not_allowed','hole_clearance','copper_edge_clearance','clearance','shorting_items','solder_mask_bridge'):
        n+=1
        print(v['type'], [i['description'].split(' di ')[-1][:30] for i in v['items']])
print("placement problems:", n)
PY
