#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Картинки этапов сборки: для каждого шага оставляет в копии платы только 3D-модели
деталей, уже установленных к этому шагу, и рендерит плату в KiCad 10 (Docker).
Запуск:  python hardware/render_steps.py   ->  docs/assembly/step-NN.png
"""
import os, re, shutil, subprocess

HW = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HW), 'docs', 'assembly')
IMG = 'kicad/kicad:10.0.5-full'

# Шаги: какие позиции появляются. Для гнёзд модулей и панелек отдельно указано,
# ставится ли только гнездо ('socket') или модуль/микросхема ('module').
STEPS = [
    ('01', ['R1', 'R2', 'R3', 'R4', 'R5', 'R6', 'R7', 'R8', 'D1']),
    ('02', ['C4', 'SW1', 'JP1:socket', 'JL1']),
    ('03', ['U4:socket', 'U5:socket']),
    ('04', ['U1:socket', 'J8:socket', 'J9:socket', 'U6:socket']),
    ('05', ['C1', 'C2', 'C3', 'U2', 'U3']),
    ('06', ['F1', 'F2']),
    ('07', ['J1', 'J3', 'J4', 'J5', 'J6', 'J7', 'J10', 'J11', 'J12'] + ['S%d' % i for i in range(1, 9)]),
    ('08', ['U4:module', 'U5:module', 'JP1:module']),
    ('09', ['U1:module', 'J8:module', 'J9:module', 'U6:module']),
]
MODULE_MODELS = ('esp32_devkitc_38', 'd1mini_sd', 'ds3231', 'lm393_light', 'jumper', 'DIP-16_W7.62mm.step')


def footprints(text):
    """Возвращает список (start, end, ref) для каждого footprint верхнего уровня."""
    res, i = [], 0
    while True:
        j = text.find('(footprint ', i)
        if j < 0:
            return res
        d, k = 0, j
        while True:
            ch = text[k]
            if ch == '(':
                d += 1
            elif ch == ')':
                d -= 1
                if d == 0:
                    break
            k += 1
        block = text[j:k + 1]
        m = re.search(r'\(property "Reference" "([^"]+)"', block)
        res.append((j, k + 1, m.group(1) if m else ''))
        i = k + 1


def strip_models(block, keep):
    """keep: None — удалить все модели; 'socket' — оставить только гнёзда; 'all' — всё."""
    if keep == 'all':
        return block
    out, i = [], 0
    while True:
        j = block.find('(model ', i)
        if j < 0:
            out.append(block[i:])
            return ''.join(out)
        d, k = 0, j
        while True:
            ch = block[k]
            if ch == '(':
                d += 1
            elif ch == ')':
                d -= 1
                if d == 0:
                    break
            k += 1
        model = block[j:k + 1]
        is_module = any(n in model for n in MODULE_MODELS)
        drop = keep is None or (keep == 'socket' and is_module)
        out.append(block[i:j])
        if not drop:
            out.append(model)
        i = k + 1


def main():
    src = open(os.path.join(HW, 'DripCarrier.kicad_pcb'), encoding='utf-8').read()
    fps = footprints(src)
    os.makedirs(OUT, exist_ok=True)
    installed = {}
    work = os.path.join(HW, '_steps')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    # модели модулей подключены как ${KIPRJMOD}/3d/..., а KIPRJMOD копии — это папка _steps
    shutil.copytree(os.path.join(HW, '3d'), os.path.join(work, '3d'))
    cmds = []
    for num, parts in STEPS:
        for p in parts:
            ref, _, stage = p.partition(':')
            installed[ref] = 'socket' if stage == 'socket' else 'all'
        pieces, pos = [], 0
        for a, b, ref in fps:
            pieces.append(src[pos:a])
            pieces.append(strip_models(src[a:b], installed.get(ref)))
            pos = b
        pieces.append(src[pos:])
        name = 'step-%s' % num
        open(os.path.join(work, name + '.kicad_pcb'), 'w', encoding='utf-8').write(''.join(pieces))
        shutil.copy(os.path.join(HW, 'DripCarrier.kicad_pro'), os.path.join(work, name + '.kicad_pro'))
        cmds.append("kicad-cli pcb render --quality high --floor --perspective --rotate '-50,0,20' --zoom 1.15 "
                    "--width 1600 --height 1100 -o /out/%s.png _steps/%s.kicad_pcb >/dev/null" % (name, name))
    for d in ('3d',):
        pass
    hw_win = subprocess.run(['cygpath', '-w', HW], capture_output=True, text=True).stdout.strip() or HW
    out_win = subprocess.run(['cygpath', '-w', OUT], capture_output=True, text=True).stdout.strip() or OUT
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    r = subprocess.run(['docker', 'run', '--rm', '-u', '0', '-v', hw_win + ':/hw', '-v', out_win + ':/out', '-w', '/hw',
                        IMG, 'sh', '-c', ' && '.join(cmds)], env=env)
    shutil.rmtree(work, ignore_errors=True)
    print('rc', r.returncode, sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
