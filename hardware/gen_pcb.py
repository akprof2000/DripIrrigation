#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генератор платы-носителя DripIrrigation.

Расставляет посадочные места, задаёт цепи, трассирует плату (двухслойный
сеточный трассировщик A*) и пишет:
  hardware/DripCarrier.kicad_pcb  — открывается в KiCad 8 (File → Open),
                                    оттуда экспорт Gerber для JLCPCB;
  docs/pcb-top.svg, docs/pcb-bottom.svg — рендер платы для документации;
  hardware/netlist.md             — таблица соединений для ручной проверки.

Запуск:  python hardware/gen_pcb.py
"""
import heapq, math, os, sys, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# ────────────────────────── геометрия платы ──────────────────────────
W, H = 150.0, 110.0          # мм
GRID = 0.635                # шаг сетки трассировки (¼ от 2.54)
EDGE = 2.0                  # отступ трасс от края

# ────────────────────────── посадочные места ──────────────────────────
# pads: список (имя_пина, dx, dy, размер, сверло, форма)
def pin_row(n, pitch=2.54, vertical=True, first=1):
    """n пинов в ряд, центрированы."""
    out = []
    for i in range(n):
        off = (i - (n - 1) / 2) * pitch
        out.append((str(first + i), 0 if vertical else off, off if vertical else 0, 1.6, 1.0, 'circle' if i else 'rect'))
    return out

def dip(n=16, rowdist=7.62):
    out = []
    half = n // 2
    for i in range(half):
        y = (i - (half - 1) / 2) * 2.54
        out.append((str(i + 1), -rowdist / 2, y, 1.6, 0.9, 'rect' if i == 0 else 'oval'))
    for i in range(half):
        y = ((half - 1) / 2 - i) * 2.54
        out.append((str(half + i + 1), rowdist / 2, y, 1.6, 0.9, 'oval'))
    return out

def esp32_38():
    """ESP32 DevKitC 38 pin (ESP-32 / ESP-32U, Type-C, CP2102). Антенна вверх (−y), USB вниз.
    Ряды на 25.4 мм — перед заказом платы измерьте свой модуль."""
    left = ['3V3', 'EN', 'VP', 'VN', 'D34', 'D35', 'D32', 'D33', 'D25', 'D26', 'D27', 'D14', 'D12', 'GND', 'D13', 'SD2', 'SD3', 'CMD', '5V']
    right = ['GND2', 'D23', 'D22', 'TX0', 'RX0', 'D21', 'GND3', 'D19', 'D18', 'D5', 'D17', 'D16', 'D4', 'D0', 'D2', 'D15', 'SD1', 'SD0', 'CLK']
    out = []
    for i, nm in enumerate(left):
        out.append((nm, -12.7, (i - 9) * 2.54, 1.6, 1.0, 'rect' if i == 0 else 'circle'))
    for i, nm in enumerate(right):
        out.append((nm, 12.7, (i - 9) * 2.54, 1.6, 1.0, 'circle'))
    return out

def d1mini_2x8():
    """Шилд Micro SD формата WeMos D1 mini: два ряда по 8, между рядами 22.86 мм."""
    a = ['RST', 'A0', 'D0', 'D5', 'D6', 'D7', 'D8', '3V3']
    b = ['TX', 'RX', 'D1', 'D2', 'D3', 'D4', 'G', '5V']
    out = []
    for i, nm in enumerate(a):
        out.append((nm, -11.43, (i - 3.5) * 2.54, 1.6, 1.0, 'rect' if i == 0 else 'circle'))
    for i, nm in enumerate(b):
        out.append((nm, 11.43, (i - 3.5) * 2.54, 1.6, 1.0, 'circle'))
    return out

def terminal(n):
    out = []
    for i in range(n):
        out.append((str(i + 1), (i - (n - 1) / 2) * 5.08, 0, 2.6, 1.3, 'rect' if i == 0 else 'circle'))
    return out

def idc2x5():
    out = []
    for i in range(5):
        x = (i - 2) * 2.54
        out.append((str(2 * i + 1), x, -1.27, 1.7, 1.0, 'rect' if i == 0 else 'circle'))
        out.append((str(2 * i + 2), x, 1.27, 1.7, 1.0, 'circle'))
    return out

def two_pin(pitch, size=1.8, drill=1.0):
    return [('1', -pitch / 2, 0, size, drill, 'rect'), ('2', pitch / 2, 0, size, drill, 'circle')]

FP = {
    'ESP32_38':  dict(pads=esp32_38(), body=(28.5, 51.5), desc='Гнездо ESP32 DevKitC 38pin (2× гребёнка 1×19)'),
    'D1MINI':    dict(pads=d1mini_2x8(), body=(25.6, 34.2), desc='Гнездо шилда Micro SD D1 mini (2× гребёнка 1×8)'),
    'DIP16':     dict(pads=dip(16), body=(10.4, 20.8), desc='Панелька DIP-16'),
    'HDR1x6':    dict(pads=pin_row(6), body=(2.6, 15.5), desc='Гребёнка-мама 1×6'),
    'JMP1x3':    dict(pads=pin_row(3, vertical=False), body=(8.0, 2.6), desc='Штыри 1×3 + джампер'),
    'TO220':     dict(pads=pin_row(3, vertical=False), body=(10.2, 5.0), desc='Модуль DC-DC в корпусе TO-220 (IN GND OUT)'),
    'TERM2':     dict(pads=terminal(2), body=(10.2, 8.0), desc='Клеммник KF301/KF2EDG 5.08 2P'),
    'TERM3':     dict(pads=terminal(3), body=(15.3, 8.0), desc='Клеммник KF301/KF2EDG 5.08 3P'),
    'TERM4':     dict(pads=terminal(4), body=(20.4, 8.0), desc='Клеммник KF2EDG 5.08 4P'),
    'TERM10':    dict(pads=terminal(10), body=(50.9, 8.0), desc='Клеммник KF2EDG 5.08 10P'),
    'IDC2x5':    dict(pads=idc2x5(), body=(20.3, 9.0), desc='IDC box header 2×5 с защёлкой'),
    'FUSE5x20':  dict(pads=two_pin(22.0, 2.8, 1.5), body=(30.0, 7.0), desc='Держатель предохранителя 5×20'),
    'DO201':     dict(pads=two_pin(12.7, 2.8, 1.5), body=(9.5, 5.5), desc='Диод DO-201 (1N5822)'),
    'CAP_P5':    dict(pads=two_pin(5.0, 2.0, 1.1), body=(10.5, 10.5), desc='Электролит 1000 мкФ, шаг 5'),
    'CAP_P25':   dict(pads=two_pin(2.5, 1.7, 0.9), body=(6.5, 6.5), desc='Электролит 100 мкФ, шаг 2.5'),
    'R_AX':      dict(pads=two_pin(10.16, 1.6, 0.8), body=(7.0, 2.6), desc='Резистор 0.25 Вт'),
    'BTN6x6':    dict(pads=[('1', -3.25, -2.25, 1.7, 1.0, 'rect'), ('2', 3.25, -2.25, 1.7, 1.0, 'circle'),
                            ('3', -3.25, 2.25, 1.7, 1.0, 'circle'), ('4', 3.25, 2.25, 1.7, 1.0, 'circle')],
                      body=(9.0, 7.0), desc='Кнопка тактовая 6×6'),
    'LM393MOD':  dict(pads=pin_row(3), body=(32.0, 14.0), off=(-14.5, 0.0), desc='Гнездо модуля датчика света LM393 1×3 (VCC GND DO), модуль 32×14 мм уходит влево'),
    'CAP_C5':    dict(pads=two_pin(5.08, 1.6, 0.8), body=(7.5, 3.0), desc='Керамический конденсатор, шаг 5.08'),
    'HDR1x2':    dict(pads=pin_row(2, vertical=False), body=(5.5, 2.6), desc='Штыри 1×2 к площадкам фоторезистора модуля'),
    'DS3231MOD': dict(pads=pin_row(6), body=(38.0, 22.0), off=(-17.7, 0.0), desc='Гнездо модуля DS3231 1×6, модуль 38×22 мм уходит влево'),
    'HOLE':      dict(pads=[('1', 0, 0, 6.0, 3.2, 'circle')], body=(6.0, 6.0), desc='Отверстие М3'),
}

# ────────────────────────── 3D-модели ──────────────────────────
# (путь, смещение мм (x, y вверх по плате, z), поворот градусы). Стандартные модели KiCad
# имеют начало в выводе 1, поэтому смещение = положение вывода 1 в нашем посадочном месте.
S3D = '/usr/share/kicad/3dmodels/'
PRJ3D = '${KIPRJMOD}/3d/'
SOCK = S3D + 'Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x%02d_P2.54mm_Vertical.step'
HDR = S3D + 'Connector_PinHeader_2.54mm.3dshapes/PinHeader_1x%02d_P2.54mm_Vertical.step'
TB = S3D + 'TerminalBlock_Phoenix.3dshapes/TerminalBlock_Phoenix_MKDS-1,5-%d-5.08_1x%02d_P5.08mm_Horizontal.step'
MODELS = {
    'ESP32_38':  [(SOCK % 19, (-12.7, 22.86, 0), (0, 0, 0)), (SOCK % 19, (12.7, 22.86, 0), (0, 0, 0)),
                  (PRJ3D + 'esp32_devkitc_38.wrl', (0, 0, 0), (0, 0, 0))],
    'D1MINI':    [(SOCK % 8, (-11.43, 8.89, 0), (0, 0, 0)), (SOCK % 8, (11.43, 8.89, 0), (0, 0, 0)),
                  (PRJ3D + 'd1mini_sd.wrl', (0, 0, 0), (0, 0, 0))],
    'DIP16':     [(S3D + 'Package_DIP.3dshapes/DIP-16_W7.62mm_Socket.step', (-3.81, 8.89, 0), (0, 0, 0)),
                  (S3D + 'Package_DIP.3dshapes/DIP-16_W7.62mm.step', (-3.81, 8.89, 3.2), (0, 0, 0))],
    'DS3231MOD': [(SOCK % 6, (0, 6.35, 0), (0, 0, 0)), (PRJ3D + 'ds3231.wrl', (0, 0, 0), (0, 0, 0))],
    'LM393MOD':  [(SOCK % 3, (0, 2.54, 0), (0, 0, 0)), (PRJ3D + 'lm393_light.wrl', (0, 0, 0), (0, 0, 0))],
    'HDR1x2':    [(HDR % 2, (-1.27, 0, 0), (0, 0, 270))],
    'JMP1x3':    [(HDR % 3, (-2.54, 0, 0), (0, 0, 270)), (PRJ3D + 'jumper.wrl', (0, 0, 0), (0, 0, 0))],
    'TO220':     [(PRJ3D + 'dcdc_to220.wrl', (0, 0, 0), (0, 0, 0))],
    'TERM2':     [(TB % (2, 2), (-2.54, 0, 0), (0, 0, 0))],
    'TERM3':     [(TB % (3, 3), (-5.08, 0, 0), (0, 0, 0))],
    'TERM4':     [(TB % (4, 4), (-7.62, 0, 0), (0, 0, 0))],
    'TERM10':    [(TB % (10, 10), (-22.86, 0, 0), (0, 0, 0))],
    'FUSE5x20':  [(S3D + 'Fuse.3dshapes/Fuseholder_Cylinder-5x20mm_Schurter_0031_8201_Horizontal_Open.step', (-11.0, 0, 0), (0, 0, 0))],
    'DO201':     [(S3D + 'Diode_THT.3dshapes/D_DO-201AD_P12.70mm_Horizontal.step', (6.35, 0, 0), (0, 0, 180))],
    'CAP_P5':    [(S3D + 'Capacitor_THT.3dshapes/CP_Radial_D10.0mm_P5.00mm.step', (-2.5, 0, 0), (0, 0, 0))],
    'CAP_P25':   [(S3D + 'Capacitor_THT.3dshapes/CP_Radial_D6.3mm_P2.50mm.step', (-1.25, 0, 0), (0, 0, 0))],
    'CAP_C5':    [(S3D + 'Capacitor_THT.3dshapes/C_Disc_D5.0mm_W2.5mm_P5.00mm.step', (-2.54, 0, 0), (0, 0, 0))],
    'R_AX':      [(S3D + 'Resistor_THT.3dshapes/R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal.step', (-5.08, 0, 0), (0, 0, 0))],
    'BTN6x6':    [(S3D + 'Button_Switch_THT.3dshapes/SW_PUSH_6mm.step', (-3.25, 2.25, 0), (0, 0, 0))],
}

# ────────────────────────── компоненты и цепи ──────────────────────────
# (ref, footprint, x, y, rot, value, {pin: net})
COMPONENTS = []
def add(ref, fp, x, y, rot, value, nets):
    COMPONENTS.append(dict(ref=ref, fp=fp, x=x, y=y, rot=rot, value=value, nets=nets))

# — верхний край: клеммы питания и управления реле (провод заводится снаружи платы)
add('J1',  'TERM2',   12.3,  8.0, 180, '12V IN',        {'1': '12V_RAW', '2': 'GND'})
add('J3',  'TERM2',   24.0,  8.0, 180, '12V VALVES',    {'1': '+12V', '2': 'GND'})
add('J4',  'TERM2',   35.7,  8.0, 180, '12V PUMP',      {'1': '12V_PUMP', '2': 'GND'})
add('J6',  'TERM10',  67.8,  8.0, 180, 'RELAY 8: +5 GND IN1..IN8',
    {'1': '+5V', '2': 'GND', '3': 'P0', '4': 'P1', '5': 'P2', '6': 'P3', '7': 'P4', '8': 'P5', '9': 'P6', '10': 'P7'})
add('J7',  'TERM3',   102.4, 8.0, 180, 'RELAY 1: +5 GND PUMP', {'1': '+5V', '2': 'GND', '3': 'PUMP'})
add('J5',  'TERM4',   125.7, 8.0, 180, 'RELAY 2: +5 GND FILL DRAIN', {'1': '+5V', '2': 'GND', '3': 'FILL', '4': 'DRAIN'})

# — второй ряд: высокие детали питания, подальше от клемм
add('F1',  'FUSE5x20', 22.0, 21.0,  0, 'F1 5A',         {'1': '12V_RAW', '2': '12V_F'})
add('D1',  'DO201',   44.0, 21.0,   0, '1N5822',        {'1': '12V_F', '2': '+12V'})
add('C1',  'CAP_P5',  57.0, 21.0,   0, '1000u/16V',     {'1': '+12V', '2': 'GND'})
add('U2',  'TO220',   70.0, 21.0,   0, 'DC-DC 12-5V 2A', {'1': '+12V', '2': 'GND', '3': '+5V'})
add('C2',  'CAP_P25', 80.0, 21.0,   0, '100u',          {'1': '+5V', '2': 'GND'})
add('U3',  'TO220',   90.0, 21.0,   0, 'DC-DC 5-3.3V',  {'1': '+5V', '2': 'GND', '3': '+3V3'})
add('C3',  'CAP_P25', 100.0, 21.0,  0, '100u',          {'1': '+3V3', '2': 'GND'})
add('F2',  'FUSE5x20', 125.0, 21.0, 0, 'F2 3A',         {'1': '+12V', '2': '12V_PUMP'})

# — логика
add('U1',  'ESP32_38', 42.0, 54.0,  0, 'ESP32 DevKitC 38',
    {'5V': '+5V', 'GND': 'GND', 'GND2': 'GND', 'GND3': 'GND', '3V3': '+3V3',
     'D21': 'SDA', 'D22': 'SCL',
     'D12': 'S0', 'D13': 'S1', 'D14': 'S2', 'D33': 'ADC',
     'D5': 'SD_CS', 'D18': 'SD_SCK', 'D19': 'SD_MISO', 'D23': 'SD_MOSI',
     'D26': 'PUMP', 'D17': 'FILL', 'D25': 'DRAIN', 'D27': 'FLOW', 'D15': 'RAIN', 'D4': 'LIGHT', 'D16': 'BTN'})
add('U4',  'DIP16',   12.0, 54.0,   0, 'PCF8574P',
    {'1': 'GND', '2': 'GND', '3': 'GND', '4': 'P0', '5': 'P1', '6': 'P2', '7': 'P3', '8': 'GND',
     '9': 'P4', '10': 'P5', '11': 'P6', '12': 'P7', '14': 'SCL', '15': 'SDA', '16': '+3V3'})
add('U5',  'DIP16',   67.0, 50.0,   0, '74HC4051',
    {'1': 'Y4', '2': 'Y6', '3': 'ADC', '4': 'Y7', '5': 'Y5', '6': 'GND', '7': 'GND', '8': 'GND',
     '9': 'S2', '10': 'S1', '11': 'S0', '12': 'Y3', '13': 'Y0', '14': 'Y1', '15': 'Y2', '16': '+3V3'})
add('J8',  'D1MINI',  87.0, 50.0,   0, 'SD D1 mini',
    {'G': 'GND', '3V3': '+3V3', 'D6': 'SD_MISO', 'D7': 'SD_MOSI', 'D5': 'SD_SCK', 'D8': 'SD_CS'})
add('J9',  'DS3231MOD', 137.0, 40.0, 0, 'DS3231',
    {'3': 'SCL', '4': 'SDA', '5': '+3V3', '6': 'GND'})
add('SW1', 'BTN6x6',  100.0, 80.0,  0, 'RESET CFG',     {'1': 'BTN', '2': 'BTN', '3': 'GND', '4': 'GND'})
add('R4',  'R_AX',    100.0, 88.0,  0, '10k',           {'1': 'BTN', '2': '+3V3'})
add('R1',  'R_AX',    68.0, 72.0,   0, '10k',           {'1': 'S0', '2': 'GND'})
add('R2',  'R_AX',    68.0, 77.0,   0, '10k',           {'1': 'S1', '2': 'GND'})
add('R3',  'R_AX',    68.0, 82.0,   0, '10k',           {'1': 'S2', '2': 'GND'})
add('R5',  'R_AX',    120.0, 77.0,  0, '10k',           {'1': 'FLOW_IN', '2': 'FLOW'})
add('R6',  'R_AX',    120.0, 84.0,  0, '20k',           {'1': 'FLOW', '2': 'GND'})
add('JP1', 'JMP1x3',  18.0, 82.0,   0, '3V3 SENS 5V',   {'1': '+3V3', '2': 'SENS_V', '3': '+5V'})

# — правый край: расходомер, свет, дождь
add('J10', 'TERM3',   145.0, 53.5, 90, 'FLOW',          {'1': '+5V', '2': 'GND', '3': 'FLOW_IN'})
add('J12', 'TERM2',   145.0, 67.0, 90, 'LDR',           {'1': 'LDR_A', '2': 'LDR_B'})
add('U6',  'LM393MOD', 135.0, 67.0, 0, 'LIGHT LM393',   {'1': '+3V3', '2': 'GND', '3': 'LIGHT'})
add('JL1', 'HDR1x2',  108.0, 77.5,  0, 'LDR',           {'1': 'LDR_A', '2': 'LDR_B'})
add('J11', 'TERM2',   145.0, 80.0, 90, 'RAIN SW',       {'1': 'RAIN_IN', '2': 'GND'})
add('R7',  'R_AX',    118.0, 91.0,  0, '10k',           {'1': 'RAIN', '2': '+3V3'})
add('R8',  'R_AX',    131.0, 91.0,  0, '1k',            {'1': 'RAIN_IN', '2': 'RAIN'})
add('C4',  'CAP_C5',  141.0, 91.0,  0, '100n',          {'1': 'RAIN', '2': 'GND'})
# — нижний край: датчики влажности
for i in range(8):
    add('S%d' % (i + 1), 'TERM3', 20.0 + i * 15.5, 102.0, 0, 'SOIL %d' % (i + 1),
        {'1': 'SENS_V', '2': 'GND', '3': 'Y%d' % i})

for i, (x, y) in enumerate([(4, 4), (W - 4, 4), (4, H - 4), (W - 4, H - 4)]):
    add('H%d' % (i + 1), 'HOLE', x, y, 0, 'M3', {})

# ────────────────────────── проверка пересечений корпусов ──────────────────────────
def body_rect(c):
    bw, bh = FP[c['fp']]['body']
    ox, oy = FP[c['fp']].get('off', (0.0, 0.0))
    if c['rot'] % 180: bw, bh = bh, bw
    cx, cy = c['x'] + ox, c['y'] + oy
    return (cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2)
_bad = False
for i, a in enumerate(COMPONENTS):
    ra = body_rect(a)
    if ra[0] < 1 or ra[1] < 1 or ra[2] > W - 1 or ra[3] > H - 1:
        print('  !! %s выходит за край платы' % a['ref'], file=sys.stderr); _bad = True
    for b in COMPONENTS[i + 1:]:
        rb = body_rect(b)
        if ra[0] < rb[2] + 0.1 and rb[0] < ra[2] + 0.1 and ra[1] < rb[3] + 0.1 and rb[1] < ra[3] + 0.1:
            print('  !! корпуса %s и %s пересекаются' % (a['ref'], b['ref']), file=sys.stderr); _bad = True
if _bad:
    sys.exit(2)

FAT_NETS = {'GND', '12V_RAW', '12V_F', '+12V', '12V_PUMP', '+5V', '+3V3'}
FAT_W, THIN_W = 1.0, 0.35

# ────────────────────────── вычисление пинов ──────────────────────────
def rot(dx, dy, deg):
    a = math.radians(deg)
    return dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)

pads = []   # dict(ref, pin, x, y, size, drill, net)
for c in COMPONENTS:
    for (pin, dx, dy, size, drill, shape) in FP[c['fp']]['pads']:
        rx, ry = rot(dx, dy, -c['rot'])   # KiCad поворачивает против часовой на экране
        pads.append(dict(ref=c['ref'], pin=pin, x=c['x'] + rx, y=c['y'] + ry, size=size, drill=drill,
                         shape=shape, net=c['nets'].get(pin)))

netnames = sorted({p['net'] for p in pads if p['net']}, key=lambda n: (n not in FAT_NETS, n))
netid = {n: i + 1 for i, n in enumerate(netnames)}

# ────────────────────────── трассировщик ──────────────────────────
# Сетка GRID, проверка зазоров — по реальной геометрии: расстояние от центра дорожки
# до меди чужой цепи должно быть не меньше радиус_меди + полуширина + CLR.
import collections
NX, NY = int(W / GRID) + 1, int(H / GRID) + 1
CLR = 0.25                  # правило KiCad 0.2 мм + запас на дискретность сетки
VIA_R = 0.4
def cell(x, y): return (int(round(x / GRID)), int(round(y / GRID)))
def pos(c): return (c[0] * GRID, c[1] * GRID)

obst = [collections.defaultdict(list), collections.defaultdict(list)]
MAXR = 1.9                   # полудиагональ квадратной площадки 2.6 мм
def add_obst(L, x, y, r, nid):
    obst[L][cell(x, y)].append((x, y, r, nid))

holes = collections.defaultdict(list)   # cell -> (x, y, радиус отверстия, радиус меди, net)
for p in pads:
    r = p['size'] / 2 * (1.42 if p['shape'] == 'rect' else 1.0)   # углы квадратных площадок
    for L in (0, 1):
        add_obst(L, p['x'], p['y'], r, netid.get(p['net'], -1))
    holes[cell(p['x'], p['y'])].append((p['x'], p['y'], p['drill'] / 2, p['size'] / 2, netid.get(p['net'], -1)))

def _near_holes(c, reach=4):
    for dx in range(-reach, reach + 1):
        for dy in range(-reach, reach + 1):
            yield from holes.get((c[0] + dx, c[1] + dy), ())

def own_pad(c, nid):
    x, y = pos(c)
    return any(hn == nid and (hx - x) ** 2 + (hy - y) ** 2 <= (hr * 0.7) ** 2 for (hx, hy, _, hr, hn) in _near_holes(c, 2))

def via_hole_ok(c):
    x, y = pos(c)
    for (hx, hy, dr, _, _) in _near_holes(c):
        if (hx - x) ** 2 + (hy - y) ** 2 < (dr + 0.2 + 0.35) ** 2:
            return False
    for (vx, vy, _) in vias:
        if abs(vx - x) < 1.2 and abs(vy - y) < 1.2 and (vx - x) ** 2 + (vy - y) ** 2 < 0.9 ** 2:
            return False
    return True

_cache = {}
def free(L, c, half, nid):
    key = (L, c, half, nid)
    if key in _cache:
        return _cache[key]
    x, y = pos(c)
    ok = EDGE <= x <= W - EDGE and EDGE <= y <= H - EDGE
    if ok:
        need_max = MAXR + half + CLR
        reach = int(math.ceil(need_max / GRID))
        for dx in range(-reach, reach + 1):
            if not ok: break
            for dy in range(-reach, reach + 1):
                for (ox, oy, r, on) in obst[L].get((c[0] + dx, c[1] + dy), ()):
                    if on != nid and (ox - x) ** 2 + (oy - y) ** 2 < (r + half + CLR) ** 2:
                        ok = False; break
                if not ok: break
    _cache[key] = ok
    return ok

segments = []   # (x1,y1,x2,y2,width,layer,net)
vias = []       # (x,y,net)

def route_net(net):
    nid = netid[net]
    half = (FAT_W if net in FAT_NETS else THIN_W) / 2
    pins = [cell(p['x'], p['y']) for p in pads if p['net'] == net]
    if len(pins) < 2:
        return True
    tree = {(pins[0], 0), (pins[0], 1)}
    todo = pins[1:]
    ok = True
    while todo:
        best = min(todo, key=lambda c: min(abs(c[0] - t[0][0]) + abs(c[1] - t[0][1]) for t in tree))
        todo.remove(best)
        if (best, 0) in tree:
            continue
        path = astar(best, tree, nid, half)
        if path is None:
            print('  !! не удалось провести %s к %s' % (net, pos(best)), file=sys.stderr)
            ok = False
            continue
        commit(path, nid, half)
        for node in path:
            tree.add(node)
    return ok

def astar(start, targets, nid, half):
    tcells = list({t[0] for t in targets})
    def h(c):
        return min(abs(c[0] - t[0]) + abs(c[1] - t[1]) for t in tcells) if len(tcells) < 300 else 0
    pq, dist, prev, cnt = [], {}, {}, 0
    for L in (0, 1):
        st = (start, L, None)
        dist[st] = 0
        cnt += 1; heapq.heappush(pq, (h(start), 0, cnt, st))
    DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1)]
    while pq:
        f, g, _, st = heapq.heappop(pq)
        if dist.get(st, 1e9) < g:
            continue
        c, L, d = st
        if (c, L) in targets:
            path, cur = [], st
            while cur is not None:
                path.append((cur[0], cur[1])); cur = prev.get(cur)
            return path
        for nd in DIRS:
            nc = (c[0] + nd[0], c[1] + nd[1])
            if (nc, L) not in targets and not free(L, nc, half, nid):
                continue
            pref = 0 if (L == 0 and nd[1] == 0) or (L == 1 and nd[0] == 0) else 1
            cost = 1 + pref + (3 if d is not None and d != nd else 0)
            ns, ng = (nc, L, nd), g + cost
            if ng < dist.get(ns, 1e9):
                dist[ns] = ng; prev[ns] = st
                cnt += 1; heapq.heappush(pq, (ng + h(nc), ng, cnt, ns))
        NL = 1 - L
        if own_pad(c, nid):
            sw = 1                      # сквозная площадка сама соединяет слои
        elif free(NL, c, VIA_R, nid) and free(L, c, VIA_R, nid) and via_hole_ok(c):
            sw = 12
        else:
            sw = None
        if sw is not None:
            ns, ng = (c, NL, None), g + sw
            if ng < dist.get(ns, 1e9):
                dist[ns] = ng; prev[ns] = st
                cnt += 1; heapq.heappush(pq, (ng + h(c), ng, cnt, ns))
    return None

def commit(path, nid, half):
    w = half * 2
    run = [path[0]]
    for a_, b_ in zip(path, path[1:]):
        if a_[1] != b_[1]:
            x, y = pos(a_[0])
            if not own_pad(a_[0], nid):
                vias.append((x, y, nid))
                for L in (0, 1): add_obst(L, x, y, VIA_R, nid)
            flush(run, w, nid); run = [b_]
        else:
            run.append(b_)
    flush(run, w, nid)
    for (c, L) in path:
        x, y = pos(c); add_obst(L, x, y, half, nid)
    _cache.clear()

def flush(run, w, nid):
    if len(run) < 2:
        return
    L = run[0][1]
    pts = [run[0][0]]
    for c, _ in run[1:]:
        if len(pts) >= 2 and (pts[-1][0] - pts[-2][0], pts[-1][1] - pts[-2][1]) == (c[0] - pts[-1][0], c[1] - pts[-1][1]):
            pts[-1] = c
        else:
            pts.append(c)
    for a_, b_ in zip(pts, pts[1:]):
        (x1, y1), (x2, y2) = pos(a_), pos(b_)
        segments.append((x1, y1, x2, y2, w, L, nid))

def netlen(net):
    ps = [p for p in pads if p['net'] == net]
    return sum(abs(a['x'] - b['x']) + abs(a['y'] - b['y']) for a, b in zip(ps, ps[1:]))

order = sorted(netnames, key=lambda n: (n not in FAT_NETS, netlen(n)))
# GND проводим последним: у него больше всего точек, пусть обходит остальное
order = [n for n in order if n != 'GND'] + ['GND']
failed = []
for n in order:
    print('route', n)
    if not route_net(n):
        failed.append(n)
print('Готово. Сегментов: %d, переходов: %d. Не проведено: %s' % (len(segments), len(vias), failed or 'нет'))

# ────────────────────────── запись KiCad ──────────────────────────
def uid(): return str(uuid.uuid4())

out = []
out.append('(kicad_pcb (version 20240108) (generator "gen_pcb.py") (generator_version "8.0")')
out.append('  (general (thickness 1.6) (legacy_teardrops no))')
out.append('  (paper "A4")')
out.append('  (layers (0 "F.Cu" signal) (31 "B.Cu" signal) (32 "B.Adhes" user "B.Adhesive") (33 "F.Adhes" user "F.Adhesive")'
           ' (34 "B.Paste" user) (35 "F.Paste" user) (36 "B.SilkS" user "B.Silkscreen") (37 "F.SilkS" user "F.Silkscreen")'
           ' (38 "B.Mask" user) (39 "F.Mask" user) (40 "Dwgs.User" user "User.Drawings") (41 "Cmts.User" user "User.Comments")'
           ' (44 "Edge.Cuts" user) (46 "B.CrtYd" user "B.Courtyard") (47 "F.CrtYd" user "F.Courtyard") (48 "B.Fab" user) (49 "F.Fab" user))')
out.append('  (setup (pad_to_mask_clearance 0) (pcbplotparams (layerselection 0x00010fc_ffffffff) (plot_on_all_layers_selection 0x0000000_00000000)'
           ' (disableapertmacros no) (usegerberextensions no) (usegerberattributes yes) (usegerberadvancedattributes yes) (creategerberjobfile yes)'
           ' (dashed_line_dash_ratio 12.0) (dashed_line_gap_ratio 3.0) (svgprecision 4) (plotframeref no) (viasonmask no) (mode 1) (useauxorigin no)'
           ' (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15.0) (pdf_front_fp_property_popups yes) (pdf_back_fp_property_popups yes)'
           ' (dxfpolygonmode no) (dxfimperialunits yes) (dxfusepcbnewfont yes) (psnegative no) (psa4output no) (plotreference yes) (plotvalue yes)'
           ' (plotfptext yes) (plotinvisibletext no) (sketchpadsonfab no) (subtractmaskfromsilk no) (outputformat 1) (mirror no) (drillshape 1)'
           ' (scaleselection 1) (outputdirectory "")))')
out.append('  (net 0 "")')
for n in netnames:
    out.append('  (net %d "%s")' % (netid[n], n))

def fp_text(kind, txt, x, y, layer, size=1.0):
    return ('    (property "%s" "%s" (at %.3f %.3f 0) (layer "%s") (uuid "%s") (effects (font (size %.1f %.1f) (thickness 0.15))))'
            % (kind, txt, x, y, layer, uid(), size, size))

for c in COMPONENTS:
    fp = FP[c['fp']]
    bw, bh = fp['body']
    ox, oy = fp.get('off', (0.0, 0.0))
    out.append('  (footprint "DripCarrier:%s" (layer "F.Cu") (uuid "%s") (at %.3f %.3f %d)' % (c['fp'], uid(), c['x'], c['y'], c['rot']))
    out.append('    (descr "%s")' % fp['desc'])
    out.append('    (attr through_hole)')
    inside = bh >= 6 and bw >= 6 and c['fp'] not in ('HOLE',)
    ref_layer = 'F.Fab' if c['fp'] == 'HOLE' else 'F.SilkS'
    out.append(fp_text('Reference', c['ref'], ox, oy + ((-bh / 2 + 1.3) if inside else (-bh / 2 - 1.0)), ref_layer, 0.9))
    out.append(fp_text('Value', c['value'], ox, oy + ((bh / 2 - 1.3) if inside else (bh / 2 + 1.0)), 'F.Fab', 0.8))
    out.append('    (property "Footprint" "DripCarrier:%s" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "%s") (effects (font (size 1 1) (thickness 0.15))))' % (c['fp'], uid()))
    out.append('    (property "Datasheet" "" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "%s") (effects (font (size 1 1) (thickness 0.15))))' % uid())
    out.append('    (property "Description" "" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "%s") (effects (font (size 1 1) (thickness 0.15))))' % uid())
    if c['fp'] != 'HOLE':
        out.append('    (fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.15) (type default)) (fill none) (layer "F.SilkS") (uuid "%s"))'
                   % (ox - bw / 2, oy - bh / 2, ox + bw / 2, oy + bh / 2, uid()))
        out.append('    (fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.05) (type default)) (fill none) (layer "F.CrtYd") (uuid "%s"))'
                   % (ox - bw / 2, oy - bh / 2, ox + bw / 2, oy + bh / 2, uid()))
    # подписи пинов ESP32 на шелкографии
    if c['fp'] in ('ESP32_38', 'D1MINI'):
        for (pin, dx, dy, *_r) in fp['pads']:
            out.append('    (fp_text user "%s" (at %.3f %.3f 0) (layer "F.SilkS") (uuid "%s") (effects (font (size 0.8 0.8) (thickness 0.12)) (justify %s)))'
                       % (pin, dx + (2.0 if dx < 0 else -2.0), dy, uid(), 'left' if dx < 0 else 'right'))
    for (pin, dx, dy, size, drill, shape) in fp['pads']:
        net = c['nets'].get(pin)
        nstr = ' (net %d "%s")' % (netid[net], net) if net else ''
        if c['fp'] == 'HOLE':
            out.append('    (pad "%s" np_thru_hole circle (at 0 0) (size %.2f %.2f) (drill %.2f) (layers "*.Cu" "*.Mask") (uuid "%s"))'
                       % (pin, size, size, drill, uid()))
        else:
            out.append('    (pad "%s" thru_hole %s (at %.3f %.3f %d) (size %.2f %.2f) (drill %.2f) (layers "*.Cu" "*.Mask")%s (uuid "%s"))'
                       % (pin, shape, dx, dy, c['rot'], size, size, drill, nstr, uid()))
    for (mpath, (mx, my, mz), (rx, ry, rz)) in MODELS.get(c['fp'], []):
        out.append('    (model "%s" (offset (xyz %.3f %.3f %.3f)) (scale (xyz 1 1 1)) (rotate (xyz %d %d %d)))'
                   % (mpath, mx, my, mz, rx, ry, rz))
    out.append('  )')

# контур
out.append('  (gr_rect (start 0 0) (end %.1f %.1f) (stroke (width 0.1) (type default)) (fill none) (layer "Edge.Cuts") (uuid "%s"))' % (W, H, uid()))
# подписи
def gr_text(t, x, y, size=1.2, layer='F.SilkS'):
    return ('  (gr_text "%s" (at %.2f %.2f 0) (layer "%s") (uuid "%s") (effects (font (size %.1f %.1f) (thickness 0.2))))'
            % (t, x, y, layer, uid(), size, size))
out.append(gr_text('DripIrrigation carrier v1', 82, 28.5, 1.5))
out.append(gr_text('ANT ->', 42, 27.0, 1.0))
out.append(gr_text('USB', 42, 82.5, 1.0))
for i, t in enumerate(['DRILL: TERM 1.3  FUSE/D1 1.5', 'HDR/DCDC/BTN 1.0  C1 1.1', 'DIP/C2/C3 0.9  R/C4 0.8']):
    out.append(gr_text(t, 40, 88.5 + i * 1.6, 1.0))
out.append(gr_text('3V3', 11.4, 82.0, 1.2))
out.append(gr_text('5V', 24.2, 82.0, 1.2))
# полярность: плюс электролитов, катод диода, выводы DC-DC
for t, x, y in [('+', 54.5, 15.0), ('+', 78.75, 16.8), ('+', 98.75, 16.8), ('K', 50.35, 17.4),
                ('IN', 67.46, 17.6), ('OUT', 72.54, 17.6), ('IN', 87.46, 17.6), ('OUT', 92.54, 17.6)]:
    out.append(gr_text(t, x, y, 0.9 if len(t) > 1 else 1.4))
for name, x, y in [('GND', 8.2, 68.5), ('3V3', 5.7, 68.5)]:
    pass

for (x1, y1, x2, y2, w, L, nid) in segments:
    out.append('  (segment (start %.3f %.3f) (end %.3f %.3f) (width %.2f) (layer "%s") (net %d) (uuid "%s"))'
               % (x1, y1, x2, y2, w, 'F.Cu' if L == 0 else 'B.Cu', nid, uid()))
for (x, y, nid) in vias:
    out.append('  (via (at %.3f %.3f) (size 0.8) (drill 0.4) (layers "F.Cu" "B.Cu") (net %d) (uuid "%s"))' % (x, y, nid, uid()))

# зона земли на нижнем слое (заливается в KiCad клавишей B)
out.append('  (zone (net %d) (net_name "GND") (layer "B.Cu") (uuid "%s") (hatch edge 0.5) (connect_pads (clearance 0.3))'
           ' (min_thickness 0.25) (filled_areas_thickness no) (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5))'
           ' (polygon (pts (xy 0.5 0.5) (xy %.1f 0.5) (xy %.1f %.1f) (xy 0.5 %.1f))))' % (netid['GND'], uid(), W - 0.5, W - 0.5, H - 0.5, H - 0.5))
out.append(')')

os.makedirs(HERE, exist_ok=True)
with open(os.path.join(HERE, 'DripCarrier.kicad_pcb'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(out))

# ────────────────────────── SVG рендер ──────────────────────────
def render(side, path):
    S = 6.0  # px per mm
    mirror = side == 'bottom'
    def X(x): return (W - x if mirror else x) * S + 20
    def Y(y): return y * S + 40
    o = []
    o.append('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" font-family="Segoe UI, Arial, sans-serif">' % (W * S + 40, H * S + 60))
    o.append('<rect width="100%" height="100%" fill="#FFFFFF"/>')
    o.append('<text x="20" y="26" font-size="16" font-weight="bold" fill="#263238">Плата-носитель DripIrrigation — %s (%s)</text>'
             % ('верхняя сторона, вид сверху' if side == 'top' else 'нижняя сторона, вид снизу', '%.0f×%.0f мм' % (W, H)))
    o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#1B5E20" stroke="#000" stroke-width="1.5"/>' % (X(0 if not mirror else W), Y(0), W * S, H * S))
    # дорожки: сначала другой слой бледно, потом активный
    for active in (False, True):
        for (x1, y1, x2, y2, w, L, nid) in segments:
            on_side = (L == 0) == (side == 'top')
            if on_side != active: continue
            col = ('#FFB300' if on_side else '#4A6572')
            o.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f" stroke-linecap="round" opacity="%s"/>'
                     % (X(x1), Y(y1), X(x2), Y(y2), col, w * S, '1' if on_side else '0.55'))
    for (x, y, nid) in vias:
        o.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#BDBDBD" stroke="#333" stroke-width="0.6"/>' % (X(x), Y(y), 0.4 * S))
    # корпуса и пады
    for c in COMPONENTS:
        fp = FP[c['fp']]; bw, bh = fp['body']
        if c['rot'] % 180: bw, bh = bh, bw
        ox, oy = fp.get('off', (0.0, 0.0))
        cx, cy = c['x'] + ox, c['y'] + oy
        if c['fp'] != 'HOLE':
            o.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="#ECEFF1" stroke-width="1"%s/>'
                     % (min(X(cx - bw / 2), X(cx + bw / 2)), Y(cy - bh / 2), bw * S, bh * S,
                        ' stroke-dasharray="4 3"' if 'off' in fp else ''))
            inside = bh >= 6 and bw >= 6
            o.append('<text x="%.1f" y="%.1f" font-size="8" font-weight="bold" fill="#FFFFFF" text-anchor="middle">%s</text>'
                     % (X(cx), Y(cy - bh / 2) + (9 if inside else -2), c['ref']))
            if inside and bh >= 8 and c['fp'] not in ('ESP32_38', 'D1MINI'):
                o.append('<text x="%.1f" y="%.1f" font-size="6.5" fill="#CFD8DC" text-anchor="middle">%s</text>'
                         % (X(c['x']), Y(c['y'] + bh / 2) - 3, c['value']))
    for p in pads:
        r = p['size'] / 2 * S
        o.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#D4AF37" stroke="#5D4037" stroke-width="0.6"/>' % (X(p['x']), Y(p['y']), r))
        o.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#1B5E20"/>' % (X(p['x']), Y(p['y']), p['drill'] / 2 * S))
        if p['ref'] == 'U1' and side == 'top':
            o.append('<text x="%.1f" y="%.1f" font-size="6" fill="#FFFFFF" text-anchor="%s">%s</text>'
                     % (X(p['x']) + (8 if p['x'] < c['x'] else -8), Y(p['y']) + 2, 'start' if p['x'] < 42 else 'end', p['pin']))
    o.append('<text x="20" y="%.0f" font-size="11" fill="#78909C">Жёлтые дорожки — этот слой, серые — обратная сторона; серые кружки — переходные отверстия. Сгенерировано hardware/gen_pcb.py.</text>' % (H * S + 55))
    o.append('</svg>')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(o))

render('top', os.path.join(ROOT, 'docs', 'pcb-top.svg'))
render('bottom', os.path.join(ROOT, 'docs', 'pcb-bottom.svg'))

# ────────────────────────── таблица соединений ──────────────────────────
with open(os.path.join(HERE, 'netlist.md'), 'w', encoding='utf-8') as f:
    f.write('# Таблица цепей платы-носителя\n\nСгенерировано `gen_pcb.py`. Формат: `Компонент.пин`.\n\n| Цепь | Выводы |\n|---|---|\n')
    for n in netnames:
        f.write('| `%s` | %s |\n' % (n, ', '.join('%s.%s' % (p['ref'], p['pin']) for p in pads if p['net'] == n)))
    f.write('\n## Компоненты\n\n| Ref | Значение | Посадочное место |\n|---|---|---|\n')
    for c in COMPONENTS:
        f.write('| %s | %s | %s |\n' % (c['ref'], c['value'], FP[c['fp']]['desc']))
if failed:
    sys.exit(1)
