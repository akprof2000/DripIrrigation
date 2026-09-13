#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
3D-модели покупных модулей (VRML) для 3D-вида KiCad и выгрузки GLB.
Размеры — реальные габариты модулей; начало координат совпадает с началом посадочного места
в gen_pcb.py. Ось Y модели направлена вверх по плате (противоположно Y платы), Z — вверх от платы.
В VRML KiCad одна единица = 2,54 мм.

Запуск:  python hardware/gen_3d.py   ->  hardware/3d/*.wrl
"""
import math, os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '3d')
U = 2.54

PCB_BLACK = (0.06, 0.06, 0.07)
PCB_BLUE = (0.04, 0.22, 0.62)
CHIP = (0.05, 0.05, 0.05)
METAL = (0.60, 0.61, 0.64)
DARKMETAL = (0.35, 0.36, 0.38)
GOLD = (0.83, 0.68, 0.28)
SOLDER = (0.72, 0.72, 0.70)
WHITE = (0.85, 0.85, 0.82)
POT_BLUE = (0.10, 0.35, 0.85)
RED = (0.85, 0.08, 0.05)
GREEN = (0.10, 0.75, 0.15)
BROWN = (0.55, 0.40, 0.25)
PLASTIC = (0.08, 0.08, 0.08)


def _shape(points, faces, color, spec=0.25, shin=0.3):
    pts = ', '.join('%.4f %.4f %.4f' % (x / U, y / U, z / U) for x, y, z in points)
    idx = ', '.join(', '.join(str(i) for i in f) + ', -1' for f in faces)
    return ('Shape { appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f '
            'specularColor %.2f %.2f %.2f shininess %.2f } } '
            'geometry IndexedFaceSet { solid FALSE coord Coordinate { point [ %s ] } coordIndex [ %s ] } }'
            % (color + (spec, spec, spec) + (shin, pts, idx)))


def box(cx, cy, z0, sx, sy, sz, color, spec=0.25):
    x0, x1, y0, y1, z1 = cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, z0 + sz
    p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return _shape(p, f, color, spec)


def cylinder(cx, cy, z0, r, h, color, n=32, spec=0.5):
    p, f = [], []
    for i in range(n):
        a = 2 * math.pi * i / n
        p.append((cx + r * math.cos(a), cy + r * math.sin(a), z0))
        p.append((cx + r * math.cos(a), cy + r * math.sin(a), z0 + h))
    for i in range(n):
        j = (i + 1) % n
        f.append((2 * i, 2 * j, 2 * j + 1, 2 * i + 1))
    f.append(tuple(2 * i for i in range(n))[::-1])
    f.append(tuple(2 * i + 1 for i in range(n)))
    return _shape(p, f, color, spec)


# ────────── составные детали ──────────
def pin_row_top(x, ys, z_pcb, pcb_t=1.6):
    """Концы штырей над платой модуля и капли припоя вокруг них."""
    out = []
    for y in ys:
        out.append(cylinder(x, y, z_pcb + pcb_t, 0.95, 0.45, SOLDER, n=12, spec=0.8))
        out.append(box(x, y, z_pcb + pcb_t, 0.64, 0.64, 2.2, GOLD, spec=0.7))
    return out


def header_plastic(x, y, length, z_pcb, horizontal=False):
    """Пластиковая планка штыревой гребёнки под платой модуля."""
    if horizontal:
        return [box(x, y, z_pcb - 2.5, length, 2.5, 2.5, PLASTIC)]
    return [box(x, y, z_pcb - 2.5, 2.5, length, 2.5, PLASTIC)]


def soic(cx, cy, z0, n, body_l, body_w, pitch=1.27, rotate=False):
    """Микросхема SOIC: корпус и ножки с двух длинных сторон."""
    out = []
    if rotate:
        out.append(box(cx, cy, z0 + 0.1, body_w, body_l, 1.5, CHIP, spec=0.15))
    else:
        out.append(box(cx, cy, z0 + 0.1, body_l, body_w, 1.5, CHIP, spec=0.15))
    half = n // 2
    for i in range(half):
        off = (i - (half - 1) / 2) * pitch
        for side in (-1, 1):
            if rotate:
                out.append(box(cx + side * (body_w / 2 + 0.5), cy + off, z0, 1.0, 0.4, 0.5, METAL, 0.6))
            else:
                out.append(box(cx + off, cy + side * (body_w / 2 + 0.5), z0, 0.4, 1.0, 0.5, METAL, 0.6))
    out.append(cylinder(cx - (body_l / 2 - 1.0 if not rotate else 0), cy + (0 if not rotate else body_l / 2 - 1.0),
                        z0 + 1.6, 0.35, 0.02, (0.25, 0.25, 0.25), n=10))
    return out


def smd(cx, cy, z0, l=1.6, w=0.8, color=CHIP, vertical=False):
    """Резистор/конденсатор 0603 с металлическими торцами."""
    sx, sy = (w, l) if vertical else (l, w)
    out = [box(cx, cy, z0, sx * 0.6 if not vertical else sx, sy if not vertical else sy * 0.6, 0.5, color, 0.2)]
    for s in (-1, 1):
        if vertical:
            out.append(box(cx, cy + s * l * 0.4, z0, w, l * 0.2, 0.5, METAL, 0.6))
        else:
            out.append(box(cx + s * l * 0.4, cy, z0, l * 0.2, w, 0.5, METAL, 0.6))
    return out


def led(cx, cy, z0, color):
    return [box(cx, cy, z0, 1.6, 0.8, 0.35, WHITE, 0.2), box(cx, cy, z0 + 0.35, 1.0, 0.7, 0.25, color, 0.6)]


def write(name, shapes):
    os.makedirs(OUT, exist_ok=True)
    flat = []
    for s in shapes:
        flat.extend(s if isinstance(s, list) else [s])
    with open(os.path.join(OUT, name), 'w', encoding='utf-8') as fh:
        fh.write('#VRML V2.0 utf8\nTransform { children [\n')
        fh.write(',\n'.join(flat))
        fh.write('\n] }\n')
    return len(flat)


Z = 11.0   # низ платы модуля над основной платой: гнездо 8,5 мм + пластик штырей 2,5 мм
rows19 = [(9 - i) * 2.54 for i in range(19)]
rows8 = [(3.5 - i) * 2.54 for i in range(8)]
rows6 = [(2.5 - i) * 2.54 for i in range(6)]
rows3 = [(1 - i) * 2.54 for i in range(3)]

# ───────── ESP32 DevKitC 38 pin, ESP-32U, Type-C, CP2102 ─────────
T = Z + 1.6
esp = [
    box(0, 0, Z, 28.0, 51.5, 1.6, PCB_BLACK, 0.1),
    header_plastic(-12.7, 0, 48.3, Z), header_plastic(12.7, 0, 48.3, Z),
    pin_row_top(-12.7, rows19, Z), pin_row_top(12.7, rows19, Z),
    # модуль ESP32-WROOM-32U: плата модуля, металлический экран, разъём IPEX
    box(0, 14.6, T, 18.0, 19.2, 0.8, PCB_BLACK, 0.1),
    box(0, 13.9, T + 0.8, 16.6, 17.6, 2.4, METAL, 0.9),
    box(0, 13.9, T + 3.2, 12.0, 6.0, 0.02, DARKMETAL, 0.2),
    cylinder(6.3, 22.2, T + 0.8, 1.0, 1.1, GOLD, n=16, spec=0.9),
    box(6.3, 22.2, T + 0.8, 2.6, 2.6, 0.3, WHITE, 0.2),
    # USB Type-C, выступает за край платы
    box(0, -23.2, T, 8.9, 7.3, 3.2, METAL, 0.9),
    box(0, -26.6, T + 0.6, 7.0, 0.3, 2.0, PLASTIC),
    soic(0, -12.5, T, 8, 5.0, 5.0, pitch=1.2),                  # CP2102 (QFN, упрощённо)
    box(-7.5, -14.0, T, 6.5, 3.5, 1.6, CHIP, 0.15),             # AMS1117
    box(-7.5, -11.6, T, 3.0, 1.2, 1.4, METAL, 0.6),
    # кнопки EN и BOOT
    box(-9.8, -21.5, T, 4.2, 3.2, 1.5, METAL, 0.8), cylinder(-9.8, -21.5, T + 1.5, 0.9, 0.7, PLASTIC, n=16),
    box(9.8, -21.5, T, 4.2, 3.2, 1.5, METAL, 0.8), cylinder(9.8, -21.5, T + 1.5, 0.9, 0.7, PLASTIC, n=16),
    led(-4.5, -18.5, T, RED), led(5.0, -8.0, T, (0.1, 0.3, 0.95)),
    smd(-6.0, -6.0, T), smd(-3.0, -6.0, T), smd(3.0, -5.0, T, color=BROWN), smd(6.0, -5.0, T, color=BROWN),
    smd(-8.0, -2.0, T), smd(8.0, -2.0, T), smd(-8.0, 2.0, T, color=BROWN),
    box(4.0, -17.0, T, 2.9, 1.6, 1.1, CHIP, 0.15), box(-3.0, -1.0, T, 2.9, 1.6, 1.1, CHIP, 0.15),
]
n1 = write('esp32_devkitc_38.wrl', esp)

# ───────── Шилд Micro SD формата D1 mini ─────────
T = Z + 1.2
sd = [
    box(0, 0, Z, 25.6, 34.2, 1.2, PCB_BLUE, 0.2),
    header_plastic(-11.43, 0, 20.3, Z), header_plastic(11.43, 0, 20.3, Z),
    pin_row_top(-11.43, rows8, Z, 1.2), pin_row_top(11.43, rows8, Z, 1.2),
    # металлический слот microSD с пружиной и картой
    box(0, 2.5, T, 14.6, 15.0, 1.85, METAL, 0.9),
    box(0, 2.5, T + 1.85, 10.0, 9.0, 0.02, DARKMETAL, 0.3),
    box(0, 11.2, T + 0.3, 11.0, 3.0, 0.8, (0.12, 0.12, 0.14), 0.3),
    box(0, 12.6, T + 0.3, 11.0, 0.3, 0.8, WHITE, 0.2),
    smd(-6.0, -10.5, T), smd(-3.0, -10.5, T), smd(3.0, -10.5, T, color=BROWN), smd(6.0, -10.5, T),
    led(0, -14.5, T, RED),
]
n2 = write('d1mini_sd.wrl', sd)

# ───────── Модуль DS3231, 38×22 мм, гребёнка 1×6 на правом краю ─────────
T = Z + 1.6
rtc = [
    box(-17.7, 0, Z, 38.0, 22.0, 1.6, PCB_BLUE, 0.2),
    header_plastic(0, 0, 15.2, Z), pin_row_top(0, rows6, Z),
    # держатель CR2032: чёрное основание, металлическая клипса, батарейка
    cylinder(-24.0, 0, T, 10.6, 2.2, PLASTIC, n=48, spec=0.1),
    cylinder(-24.0, 0, T + 2.2, 10.0, 3.2, METAL, n=48, spec=0.9),
    cylinder(-24.0, 0, T + 5.4, 8.5, 0.02, DARKMETAL, n=48, spec=0.4),
    box(-24.0, 0, T + 5.45, 3.0, 22.0, 0.35, METAL, 0.9),
    soic(-7.0, 2.0, T, 16, 10.3, 7.5),                          # DS3231SN
    smd(-7.0, -6.0, T), smd(-4.0, -6.0, T), smd(-10.0, -6.0, T, color=BROWN),
    box(-4.0, 7.5, T, 3.2, 1.6, 1.0, CHIP, 0.15),               # сборка резисторов
    led(-2.5, -8.5, T, RED),
]
n3 = write('ds3231.wrl', rtc)

# ───────── Модуль датчика света LM393, 32×14 мм, фоторезистор выпаян ─────────
T = Z + 1.6
lm = [
    box(-14.5, 0, Z, 32.0, 14.0, 1.6, PCB_BLUE, 0.2),
    cylinder(-29.0, 4.5, Z, 1.5, 1.61, (0.02, 0.02, 0.02), n=16),   # крепёжное отверстие
    header_plastic(0, 0, 7.6, Z), pin_row_top(0, rows3, Z),
    # подстроечник 3296: синий корпус, латунный винт
    box(-11.0, 0, T, 9.5, 4.8, 10.0, POT_BLUE, 0.3),
    cylinder(-15.2, 0, T + 10.0, 1.1, 0.8, GOLD, n=16, spec=0.9),
    soic(-20.5, 0, T, 8, 4.9, 3.9),                             # LM393
    led(-4.5, 4.5, T, RED), led(-4.5, -4.5, T, GREEN),
    smd(-6.5, 0, T, vertical=True), smd(-25.0, 4.5, T), smd(-25.0, -4.5, T, color=BROWN),
    # два штырька вместо фоторезистора, к ним идёт кабель на JL1
    box(-28.5, 1.3, T, 0.64, 0.64, 6.0, GOLD, 0.7), box(-28.5, -1.3, T, 0.64, 0.64, 6.0, GOLD, 0.7),
    box(-28.5, 0, T, 2.5, 5.0, 2.5, PLASTIC),
]
n4 = write('lm393_light.wrl', lm)

# ───────── Импульсный преобразователь формата TO-220 (K7805-2000) ─────────
dc = [
    box(0, 0, 2.5, 11.6, 7.2, 10.2, (0.07, 0.07, 0.08), 0.15),
    box(0, -3.62, 5.0, 9.0, 0.04, 5.5, WHITE, 0.1),
    box(-2.5, -3.66, 9.3, 4.5, 0.04, 1.0, (0.1, 0.1, 0.1)),
    box(-2.54, 0, 0, 0.6, 0.4, 2.5, METAL, 0.8), box(0, 0, 0, 0.6, 0.4, 2.5, METAL, 0.8), box(2.54, 0, 0, 0.6, 0.4, 2.5, METAL, 0.8),
]
n5 = write('dcdc_to220.wrl', dc)

# ───────── Джампер на штырях 1–2 ─────────
n6 = write('jumper.wrl', [box(-1.27, 0, 2.6, 5.0, 2.5, 6.0, PLASTIC, 0.2), box(-1.27, 0, 8.6, 3.0, 1.2, 0.02, GOLD)])

print('ok: shapes esp32=%d sd=%d ds3231=%d lm393=%d dcdc=%d jumper=%d' % (n1, n2, n3, n4, n5, n6))
