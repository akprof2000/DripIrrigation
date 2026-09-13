#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Иллюстрации к docs/hardware-carrier-board.md. Размер рамок считается по тексту. Запуск: python hardware/gen_figs.py"""
import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, 'docs')
FONT = 'font-family="Segoe UI, Arial, sans-serif"'

def tw(text, size):
    """Оценка ширины строки (Segoe UI, кириллица) с запасом."""
    return len(text) * size * 0.62 + 6

class Canvas:
    def __init__(self, w, h, title, sub):
        self.w, self.h = w, h
        self.o = ['<svg viewBox="0 0 %d %d" xmlns="http://www.w3.org/2000/svg" %s>' % (w, h, FONT),
                  '<defs><marker id="arr" markerWidth="10" markerHeight="10" refX="8" refY="5" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#607D8B"/></marker></defs>',
                  '<rect width="%d" height="%d" fill="#FFFFFF"/>' % (w, h),
                  '<text x="40" y="44" font-size="24" font-weight="bold" fill="#263238">%s</text>' % title,
                  '<text x="40" y="68" font-size="13" fill="#78909C">%s</text>' % sub]
        self.boxes = {}

    def box(self, key, x, y, title, lines=(), fill='#E3F2FD', stroke='#1E88E5', tsize=15, lsize=12, minw=0, w=None, pad=14):
        width = w or max(minw, tw(title, tsize), *[tw(l, lsize) for l in lines]) + 2 * pad
        height = 20 + tsize + (len(lines) * (lsize + 5)) + (8 if lines else 4)
        self.o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="8" fill="%s" stroke="%s" stroke-width="2"/>' % (x, y, width, height, fill, stroke))
        self.o.append('<text x="%d" y="%d" font-size="%d" font-weight="bold" fill="#263238" text-anchor="middle">%s</text>' % (x + width / 2, y + 12 + tsize, tsize, title))
        for i, ln in enumerate(lines):
            self.o.append('<text x="%d" y="%d" font-size="%d" fill="#455A64" text-anchor="middle">%s</text>' % (x + width / 2, y + 20 + tsize + (i + 1) * (lsize + 5) - 2, lsize, ln))
        self.boxes[key] = (x, y, width, height)
        return (x, y, width, height)

    def text(self, x, y, t, size=12, color='#37474F', anchor='middle', bold=False):
        self.o.append('<text x="%d" y="%d" font-size="%d" fill="%s" text-anchor="%s"%s>%s</text>'
                      % (x, y, size, color, anchor, ' font-weight="bold"' if bold else '', t))

    def line(self, pts, color='#607D8B', w=2, dash=None, arrow=False):
        d = ' '.join('%s%d %d' % ('M' if i == 0 else 'L', x, y) for i, (x, y) in enumerate(pts))
        extra = (' stroke-dasharray="%s"' % dash) if dash else ''
        m = ' marker-end="url(#arr)"' if arrow else ''
        self.o.append('<path d="%s" fill="none" stroke="%s" stroke-width="%d"%s%s/>' % (d, color, w, extra, m))

    def note(self, x, y, w, lines, title=None):
        h = 16 + len(lines) * 20 + (22 if title else 0)
        self.o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="8" fill="#FFFDE7" stroke="#F9A825" stroke-width="2"/>' % (x, y, w, h))
        yy = y + 24
        if title:
            self.text(x + 16, yy, title, 13, '#5D4037', 'start', True); yy += 22
        for ln in lines:
            assert tw(ln, 12) < w - 30, 'строка не влезает: ' + ln
            self.text(x + 16, yy, ln, 12, '#5D4037', 'start'); yy += 20
        return h

    def save(self, name):
        self.o.append('</svg>')
        with open(os.path.join(DOCS, name), 'w', encoding='utf-8') as f:
            f.write('\n'.join(self.o))

def mid(b): return (b[0] + b[2] / 2, b[1] + b[3] / 2)
def left(b): return (b[0], b[1] + b[3] / 2)
def right(b): return (b[0] + b[2], b[1] + b[3] / 2)
def bottom(b): return (b[0] + b[2] / 2, b[1] + b[3])
def top(b): return (b[0] + b[2] / 2, b[1])

BLUE = ('#E3F2FD', '#1E88E5'); AMBER = ('#FFF8E1', '#F9A825'); PINK = ('#FCE4EC', '#D81B60')
GREEN = ('#E8F5E9', '#43A047'); ORANGE = ('#FFF3E0', '#FB8C00'); CYAN = ('#E0F7FA', '#00838F'); PURPLE = ('#F3E5F5', '#8E24AA'); GREY = ('#ECEFF1', '#90A4AE')

# ───────────── 1. Структура ─────────────
c = Canvas(1400, 760, 'Из чего собирается контроллер',
           'Плата-носитель связывает покупные модули дорожками. Оранжевое — реле, бирюзовое — датчики, розовое — питание.')
PX, PY, PW, PH = 320, 100, 760, 590
c.o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="14" fill="#F1F8E9" stroke="#2E7D32" stroke-width="3"/>' % (PX, PY, PW, PH))
c.text(PX + PW / 2, PY + 26, 'ПЛАТА-НОСИТЕЛЬ 150×110 мм', 15, '#1B5E20', bold=True)
c.text(PX + PW / 2, PY + 46, 'модули вставляются в гнёзда; паяются только гнёзда, клеммники и мелочь', 12, '#33691E')
# ряд 1: гнёзда
x = PX + 20; y = PY + 62
for key, t, ln, col in [('esp', 'ESP32 DevKitC 38', ['гнездо 2×(1×19)'], BLUE), ('pcf', 'PCF8574P', ['панелька DIP-16'], AMBER),
                        ('mux', '74HC4051', ['панелька DIP-16'], AMBER), ('rtc', 'DS3231', ['гнездо 1×6'], BLUE), ('sd', 'SD шилд D1 mini', ['гнездо 2×(1×8)'], BLUE)]:
    b = c.box(key, x, y, t, ln, *col, tsize=14, lsize=11); x += b[2] + 12
# ряд 2
x = PX + 20; y += 80
for key, t, ln, col in [('dc5', 'DC-DC 12→5 В', ['корпус TO-220, 3 pin'], PINK), ('dc3', 'DC-DC 5→3,3 В', ['корпус TO-220, 3 pin'], PINK),
                        ('btn', 'Кнопка сброса', ['тактовая 6×6'], BLUE), ('fuse', 'Защита входа', ['F1 5 А · F2 3 А · диод Шоттки'], GREY)]:
    b = c.box(key, x, y, t, ln, *col, tsize=14, lsize=11); x += b[2] + 12
# ряд 3: паяемое
y += 80
c.box('solder', PX + 20, y, 'Паяется на плату', ['держатели предохранителей, диод, 3 конденсатора, 6 резисторов, гнёзда, панельки, клеммники'],
      *GREY, tsize=13, lsize=11, w=PW - 40)
# ряд 4: клеммники
y += 76
b1 = c.box('term12', PX + 20, y, 'Клеммники 5,08 мм 2P ×5', ['12 В вход · 12 В на COM клапанов · 12 В насос', 'выносной фоторезистор · контакт датчика дождя'], *GREEN, tsize=13, lsize=11)
b2 = c.box('idc', b1[0] + b1[2] + 16, y, 'Клеммы к реле: своя на модуль', ['8 кан.: +5 В · GND · IN1…IN8', '1 кан.: +5 В · GND · насос', '2 кан.: +5 В · GND · налив · дренаж'], *GREEN, tsize=13, lsize=11)
# ряд 5
y += max(b1[3], b2[3]) + 16
b3 = c.box('term3', PX + 20, y, 'Клеммники 5,08 мм 3P ×9', ['8 × датчик влажности (3V3 · GND · AOUT)', 'расходомер'], *GREEN, tsize=13, lsize=11, w=PW - 40)
c.text(PX + PW / 2, PY + PH - 16, 'Ни одного провода «навесом»: все связи между модулями — дорожки платы', 13, '#1B5E20', bold=True)

# внешние блоки слева
psu = c.box('psu', 40, 120, 'Источник 12 В 5 А', ['на клеммник J1'], *PINK, tsize=14, lsize=11)
c.line([right(psu), (PX, right(psu)[1])], '#D81B60', 4, arrow=True)
r8 = c.box('r8', 40, 330, 'Реле 8 кан. 5 В, H/L', ['клапаны V1…V8', 'перемычки в положении L'], *ORANGE, tsize=14, lsize=11)
r4 = c.box('r4', 40, 440, 'Реле 1 + 2 кан. 5 В, H/L', ['насос · налив · дренаж', 'перемычки в положении H'], *ORANGE, tsize=14, lsize=11)
ib = c.boxes['idc']
yb = ib[1] + ib[3] + 8
c.line([bottom(ib), (bottom(ib)[0], yb)], '#FB8C00', 3)
c.line([(bottom(ib)[0], yb), (PX - 30, yb), (PX - 30, right(r8)[1]), right(r8)], '#FB8C00', 3)
c.line([(PX - 30, yb), (PX - 30, right(r4)[1]), right(r4)], '#FB8C00', 3)
c.text(mid(r4)[0], r4[1] + r4[3] + 18, 'провода к клеммам реле', 11, '#E65100')

# внешние блоки справа
RX = PX + PW + 60
s1 = c.box('soil', RX, 120, 'Датчики влажности ×8', ['ёмкостные v1.2, 3 провода'], *CYAN, tsize=14, lsize=11)
s2 = c.box('rain', RX, 210, 'Дождь · Свет', ['дождь — сухой контакт, как кнопка', 'свет — выносной фоторезистор'], *CYAN, tsize=14, lsize=11)
s3 = c.box('flow', RX, 300, 'Расходомер YF-S201', ['5 В; делитель на плате'], *CYAN, tsize=14, lsize=11)
v = c.box('valves', RX, 420, 'Клапаны 12 В ×8', ['на контакты релейного', 'модуля, не на плату'], *PURPLE, tsize=14, lsize=11)
pm = c.box('pump', RX, 530, 'Насос 12 В', ['через 1-канальное реле'], *PURPLE, tsize=14, lsize=11)
t3 = c.boxes['term3']
xr = PX + PW
for b in (s1, s2, s3):
    c.line([left(b), (RX - 24, left(b)[1])], '#00838F', 3)
c.line([(RX - 24, left(s1)[1]), (RX - 24, right(t3)[1]), right(t3)], '#00838F', 3)
# силовые провода к реле (пунктир), обходят плату снизу
yd = PY + PH + 30
c.line([left(v), (RX - 12, left(v)[1]), (RX - 12, yd), (20, yd), (20, bottom(r4)[1]), bottom(r4)], '#8E24AA', 3, dash='6 4')
c.line([left(pm), (RX - 12, left(pm)[1])], '#8E24AA', 3, dash='6 4')
c.text(PX + PW / 2, yd - 8, 'силовые провода 12 В идут к контактам релейных модулей, минуя плату', 11, '#6A1B9A')
c.save('carrier-blocks.svg')

# ───────────── 2. Питание ─────────────
c = Canvas(1400, 720, 'Питание: три шины 12 / 5 / 3,3 В', 'Всё питание проходит через плату-носитель; общая земля — GND.')
psu = c.box('psu', 40, 120, 'Источник 12 В 5 А', ['клеммник J1'], *PINK, tsize=14, lsize=11)
f1 = c.box('f1', 250, 128, 'F1 5 А', [], *GREY, tsize=13, minw=70)
d1 = c.box('d1', 360, 128, 'D1 Шоттки', [], *GREY, tsize=13)
c.line([right(psu), left(f1)], '#D81B60', 4); c.line([right(f1), left(d1)], '#D81B60', 4)
c.text(mid(f1)[0], f1[1] + f1[3] + 16, 'предохранитель', 11, '#78909C'); c.text(mid(d1)[0], d1[1] + d1[3] + 16, 'переполюсовка', 11, '#78909C')
X1, X2 = 560, 1360
def rail(y, color, name):
    c.line([(X1, y), (X2, y)], color, 6); c.text(X1 - 12, y + 5, name, 15, color, 'end', True)
def taps(y, color, items, yl=48):
    n = len(items); step = (X2 - X1 - 300) / max(n - 1, 1)
    for i, t in enumerate(items):
        x = X1 + 40 + i * step
        c.line([(x, y), (x, y + yl)], color, 3, arrow=True)
        c.text(x, y + yl + 18, t, 12)
Y12, Y5, Y33 = right(d1)[1], 330, 510
c.line([right(d1), (X1, Y12)], '#D81B60', 4)
rail(Y12, '#D81B60', '+12 В'); rail(Y5, '#FB8C00', '+5 В'); rail(Y33, '#43A047', '+3,3 В')
taps(Y12, '#D81B60', ['COM реле ×8 → клапаны', 'F2 3 А → реле → насос', 'C1 1000 мкФ'])
taps(Y5, '#FB8C00', ['ESP32 5V', 'реле 8 кан.', 'реле 1 кан.', 'реле 2 кан.', 'YF-S201'])
taps(Y33, '#43A047', ['PCF8574 · 4051', 'DS3231 · SD', 'датчики ×10', 'подтяжки'])
dc5 = c.box('dc5', X2 - 150, Y12 + 60, 'DC-DC 12→5', ['TO-220, 3 pin'], *PINK, tsize=13, lsize=11)
dc3 = c.box('dc3', X2 - 150, Y5 + 60, 'DC-DC 5→3,3', ['TO-220, 3 pin'], *PINK, tsize=13, lsize=11)
c.line([(mid(dc5)[0], Y12), top(dc5)], '#D81B60', 3, arrow=True); c.line([bottom(dc5), (mid(dc5)[0], Y5)], '#FB8C00', 3)
c.line([(mid(dc3)[0], Y5), top(dc3)], '#FB8C00', 3, arrow=True); c.line([bottom(dc3), (mid(dc3)[0], Y33)], '#43A047', 3)
c.note(40, 600, 1320, [
    'Каждый из трёх релейных модулей получает +5 В и GND со своей клеммы. 11 катушек по 70 мА и ESP32 — до 1,2 А, поэтому DC-DC 12→5 не слабее 2 А.',
    'Восемь клапанов по 0,3–0,6 А ≈ 4 А, отсюда блок 5 А. Насос 12 В до 3 А через F2 и реле; мощнее — отдельный источник 12 В.',
    'Клапаны включает PCF8574 низким уровнем — перемычки L. Насос, налив и дренаж ESP32 включает высоким — перемычки H.'])
c.save('carrier-power.svg')

# ───────────── 3. Корпус ─────────────
c = Canvas(1200, 860, 'Компоновка в корпусе', 'Плата-носитель, три релейных модуля и ввод питания 12 В на одной монтажной пластине (вид сверху, крышка снята).')
BX, BY, BW, BH = 40, 100, 1120, 560
c.o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="16" fill="#FAFAFA" stroke="#455A64" stroke-width="4"/>' % (BX, BY, BW, BH))
c.text(BX + BW / 2, BY + 26, 'Корпус ABS IP65 250×200×80 мм', 14, '#455A64', bold=True)
# плата
PX, PY, PW, PH = 70, 140, 560, 380
c.o.append('<rect x="%d" y="%d" width="%d" height="%d" rx="6" fill="#C8E6C9" stroke="#2E7D32" stroke-width="3"/>' % (PX, PY, PW, PH))
c.text(PX + PW / 2, PY + 22, 'ПЛАТА-НОСИТЕЛЬ 150×110 мм', 15, '#1B5E20', bold=True)
for (x, y) in [(PX + 14, PY + 14), (PX + PW - 14, PY + 14), (PX + 14, PY + PH - 14), (PX + PW - 14, PY + PH - 14)]:
    c.o.append('<circle cx="%d" cy="%d" r="7" fill="#FFFFFF" stroke="#455A64" stroke-width="2"/>' % (x, y))
c.box('pwr', PX + 30, PY + 34, '12 В · предохранители · DC-DC · клеммы реле', [], *PINK, tsize=12, w=PW - 60, pad=6)
x = PX + 30
for key, t, ln, col, ts in [('pcf', 'PCF', [], AMBER, 12), ('esp', 'ESP32', ['антенна ↑'], BLUE, 13), ('mux', '4051', [], AMBER, 12),
                            ('sd', 'SD', [], BLUE, 12), ('rtc', 'RTC', [], BLUE, 12), ('btn', 'кнопка', [], BLUE, 11)]:
    b = c.box(key, x, PY + 100, t, ln, *col, tsize=ts, lsize=11, minw=36, pad=8)
    x += b[2] + 10
assert x - 10 <= PX + PW - 20, 'модули не влезли в плату: %d' % x
c.box('sens', PX + 30, PY + PH - 50, 'клеммники датчиков ×8 · дождь · фоторезистор · расходомер', [], *CYAN, tsize=12, w=PW - 60, pad=6)
# реле
RX = PX + PW + 50
r8 = c.box('r8', RX, 140, 'РЕЛЕ 8 кан. 5 В на стойках М3', ['', '', 'клеммы контактов → клапаны V1…V8', 'вход: DC+ · DC− · IN1…IN8, перемычки L'], *ORANGE, tsize=14, lsize=11, w=430)
for i in range(8):
    c.o.append('<rect x="%d" y="%d" width="40" height="28" fill="#FFFFFF" stroke="#FB8C00"/>' % (RX + 24 + i * 48, 172))
r4 = c.box('r4', RX, 330, 'РЕЛЕ 1 кан. + 2 кан. на стойках М3', ['', '', 'насос · налив · дренаж'], *ORANGE, tsize=14, lsize=11, w=430)
for i in range(3):
    c.o.append('<rect x="%d" y="%d" width="60" height="28" fill="#FFFFFF" stroke="#FB8C00"/>' % (RX + 80 + i * 100, 362))
c.line([(PX + PW - 60, PY + 60), (PX + PW + 20, PY + 60), (PX + PW + 20, left(r8)[1]), left(r8)], '#E65100', 4)
c.line([(PX + PW - 30, PY + 60), (PX + PW + 32, PY + 60), (PX + PW + 32, left(r4)[1]), left(r4)], '#E65100', 4)
c.text(PX + PW + 40, left(r4)[1] + 60, 'провода к клеммам реле, концы в наконечниках', 11, '#E65100', 'start')
c.box('psu', RX, 500, 'Источник питания 12 В 5 А', ['провод на клеммник J1 платы'], *PINK, tsize=13, lsize=11, w=430)
# гермовводы
for i, t in enumerate(['питание 12 В', 'датчики', 'датчики', 'клапаны', 'насос']):
    x = BX + 120 + i * 220
    c.o.append('<circle cx="%d" cy="%d" r="16" fill="#CFD8DC" stroke="#455A64" stroke-width="2"/>' % (x, BY + BH))
    c.text(x, BY + BH + 36, 'PG9 · ' + t, 11)
c.note(40, 720, 1120, [
    '• модули — в гнёздах, релейные модули и плата — на стойках М3, сверху прижаты крышкой;',
    '• внешние провода — через гермовводы (фиксируют кабель), концы обжаты наконечниками НШВИ и зажаты винтом клеммника;',
    '• между платой и реле — провода в винтовых клеммах с обеих сторон, никаких Dupont.'], 'Как ничего не отваливается')
c.save('carrier-box.svg')
print('ok')
