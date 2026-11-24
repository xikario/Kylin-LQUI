#!/usr/bin/python3
"""Render the menu's original jade qilin at build-authoring time (Qt 5)."""
from dataclasses import dataclass
from math import cos, radians, sin
from pathlib import Path

from PyQt5.QtCore import QByteArray, Qt
from PyQt5.QtGui import QImage, QPainter

OUT = Path(__file__).resolve().parent
SIDE = 80
COUNTS = {'roar': 14, 'tail': 12, 'leap': 14, 'bow': 13}


def ease(t, *keys):
    """Smoothly approach and leave each key pose, including anticipation."""
    for (a, x), (b, y) in zip(keys, keys[1:]):
        if t <= b:
            u = max(0, (t - a) / (b - a))
            return x + (y - x) * u * u * (3 - 2 * u)
    return keys[-1][1]


@dataclass
class Pose:
    height: float = 0  # negative is up
    head: float = 0
    jaw: float = 0
    tail: float = 0
    tip: float = 0
    ears: float = 0
    fore: float = 0
    far_fore: float = 0
    hind: float = 0
    rear: float = 0
    eyes: float = 0
    voice: float = 0
    cloud: float = 0


def pose_at(action, t):
    p = Pose()
    if action == 'roar':
        lift = ease(t,(0,0),(.18,-.20),(.44,1),(.68,.94),(1,0))
        p.head = 22 * lift
        p.jaw = ease(t,(0,0),(.22,0),(.46,1),(.56,.77),(.68,.98),(.87,0),(1,0))
        p.eyes = .58 * max(0, lift)
        p.height = -3 * lift
        p.ears = -9 * lift
        p.tail = 11 * lift
        p.fore = p.far_fore = -4 * lift
        p.voice = ease(t,(0,0),(.39,0),(.53,1),(.68,.9),(.84,0),(1,0))
    elif action == 'tail':
        # The root moves first. The tip lags, overshoots and settles.
        p.tail = ease(t,(0,0),(.15,12),(.41,-61),(.71,24),(.88,-8),(1,0))
        p.tip = ease(t,(0,0),(.24,12),(.52,-33),(.80,16),(1,0))
        p.ears = -.17 * p.tail
        p.head = -.075 * p.tail
    elif action == 'leap':
        lift = ease(t,(0,0),(.18,-.28),(.49,1),(.66,.78),(.86,-.16),(1,0))
        p.height = -15 * lift
        p.head = 9 * lift
        p.fore = ease(t,(0,0),(.18,8),(.47,44),(.64,36),(.79,-14),(1,0))
        p.far_fore = .64*p.fore
        p.hind = ease(t,(0,0),(.17,-14),(.34,18),(.51,-39),(.72,-21),(.88,8),(1,0))
        p.rear = .72*p.hind
        p.tail = 17 * lift
        p.tip = -14 * lift
        p.cloud = ease(t,(0,0),(.21,0),(.43,1),(.66,.74),(.93,0),(1,0))
    elif action == 'bow':
        down = ease(t,(0,0),(.20,-.12),(.46,1),(.64,1),(.88,.14),(1,0))
        p.head = -16 * down
        p.fore = ease(t,(0,0),(.26,30),(.48,72),(.68,72),(.86,19),(1,0))
        p.height = 2 * down
        p.eyes = max(0, down)
        p.tail = -14 * down
        p.ears = 8 * down
    return p


def path(d, fill, extra=''):
    return f'<path d="{d}" fill="{fill}" {extra}/>'


def line(d, ink, width):
    return path(d,'none',f'stroke="{ink}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"')


def leg(x, y, angle, far, back):
    shade = '#88b4c6' if far else 'url(#ivory)'
    hoof = '#366278' if far else '#547d93'
    if back:
        shape='M -6 -2 Q 5 -6 7 1 Q 8 7 4 12 L 7 20 L 6 25 Q 3 28 -1 26 L 0 21 L -3 11 Q -9 7 -6 -2Z'
        foot='M 0 21 Q 3 22 7 20 L 6 25 Q 3 28 -1 26Z'
    else:
        shape='M -6 -2 Q 0 -5 5 -2 L 4 9 L 1 16 L 1 23 Q -2 27 -8 25 L -6 19 L -5 12 L -6 5Z'
        foot='M -6 19 Q -3 21 1 20 L 1 24 Q -2 27 -8 25Z'
    return f'<g transform="translate({x} {y}) rotate({angle})">'+path(shape,shade)+path(foot,hoof)+'</g>'


def render_pose(p):
    ink='#354d6a'
    # The silhouette uses the menu emblem's forked horns, high neck and tufted tail.
    parts=['''<svg xmlns="http://www.w3.org/2000/svg" width="320" height="320" viewBox="-7 -13 146 146">
<defs>
<linearGradient id="ivory" x1="0" y1="0" x2=".55" y2="1" gradientUnits="objectBoundingBox"><stop stop-color="#fffdf0"/><stop offset=".52" stop-color="#fff9e8"/><stop offset="1" stop-color="#c4e2db"/></linearGradient>
<linearGradient id="jade" x1="0" y1="0" x2=".85" y2="1" gradientUnits="objectBoundingBox"><stop stop-color="#9aefe2"/><stop offset=".49" stop-color="#51c4c0"/><stop offset="1" stop-color="#397ba3"/></linearGradient>
<linearGradient id="horn" x1="0" y1="0" x2=".55" y2="1" gradientUnits="objectBoundingBox"><stop stop-color="#fff0b0"/><stop offset=".7" stop-color="#eeb466"/><stop offset="1" stop-color="#c48957"/></linearGradient>
</defs>''']
    if p.cloud:
        c=p.cloud
        parts.append(f'<g opacity="{.9*c:.3f}" transform="translate({(1-c)*-4:.2f} 0)">')
        parts.append(line('M 30 110 Q 36 112 43 109 Q 48 106 44 104 Q 40 103 40 107', '#ddfff6',2.7))
        parts.append(line('M 61 112 Q 74 111 79 106 Q 84 102 81 100 Q 77 98 76 103 Q 76 107 84 105 L 91 103','#b7ede5',2.8))
        parts.append('</g>')
    parts.append(f'<g transform="translate(0 {p.height:.3f})">')
    # A bending tail shaft, with the flame turning about its own collar.
    turn = radians(p.tail)
    tipx=91 + 19*cos(turn) + 21*sin(turn)
    tipy=79 + 19*sin(turn) - 21*cos(turn)
    parts.append(line(f'M 84 79 Q {98+.13*p.tail:.2f} {91+.06*p.tail:.2f} {tipx:.2f} {tipy:.2f}', '#416c82',6.3))
    parts.append(line(f'M 85 78 Q {98+.13*p.tail:.2f} {89+.06*p.tail:.2f} {tipx:.2f} {tipy:.2f}', '#a3dfcf',3.8))
    parts.append(f'<g transform="translate({tipx:.2f} {tipy:.2f}) rotate({p.tail*.7+p.tip:.2f})">')
    parts.append(path('M -2 3 Q -10 -2 -7 -12 Q -5 -10 -4 -10 Q -5 -17 2 -22 Q 0 -16 5 -14 Q 8 -17 9 -21 Q 15 -13 10 -5 Q 8 -1 2 1Z','url(#jade)'))
    parts.append(path('M -1 0 Q -4 -6 -1 -11 Q -1 -6 3 -7 Q 7 -8 8 -11 Q 8 -4 -1 0Z','#d4f5d5'))
    parts.append('</g>')
    parts.append(leg(62,79,p.far_fore,True,False))
    parts.append(leg(74,78,p.rear,True,True))
    parts.append(leg(89,81,p.hind,False,True))
    parts.append(path('M 42 52 Q 50 47 59 54 Q 66 62 77 61 Q 90 60 95 68 Q 103 80 92 88 Q 82 94 71 88 Q 61 86 54 88 Q 42 90 37 79 Q 33 65 42 52Z','url(#ivory)'))
    parts.append(path('M 65 63 Q 78 62 89 66 Q 86 72 81 70 Q 77 77 72 72 Q 66 74 65 68Z','#74bcae'))
    parts.append(line('M 69 66 Q 72 70 76 66 Q 79 71 83 67','#e6f4d6',1.8))
    parts.append(leg(51,81,p.fore,False,False))
    # Forward, separate head and jaw. The mouth cavity follows the lower lip.
    parts.append(f'<g transform="rotate({p.head:.2f} 46 65)">')
    parts.append(path('M 44 34 Q 54 28 64 33 L 60 36 Q 70 36 73 45 L 65 44 Q 74 51 69 60 L 64 55 Q 66 66 57 72 L 56 65 Q 52 72 44 73 Q 50 61 44 54Z','url(#jade)'))
    parts.append(path('M 56 37 Q 64 35 68 41 Q 62 39 57 42Z','#b0f1de'))
    parts.append(path('M 38 34 Q 40 28 40 22 L 37 17 Q 37 14 39 16 L 42 19 L 45 9 Q 47 7 47 11 L 46 18 L 50 15 Q 53 14 51 18 L 45 24 L 44 33Z','#c79663'))
    parts.append(path('M 43 36 Q 48 27 49 21 L 46 16 Q 45 13 48 14 L 51 17 L 56 6 Q 58 4 58 8 L 55 19 L 61 15 Q 64 14 62 18 L 54 25 Q 53 32 49 36Z','url(#horn)'))
    parts.append(f'<g transform="rotate({p.ears:.2f} 52 36)">')
    parts.append(path('M 47 36 Q 54 23 67 25 Q 65 36 53 40Z','url(#ivory)'))
    parts.append(path('M 53 34 Q 58 29 62 29 Q 59 34 54 36Z','#e8aea8'))
    parts.append('</g>')
    gape=p.jaw
    parts.append(f'<g transform="rotate({-32*gape:.2f} 30 52)">')
    parts.append(path('M 10 54 Q 18 52 33 49 Q 34 58 28 62 Q 17 67 11 61 Q 8 59 10 54Z','url(#ivory)'))
    parts.append('</g>')
    if gape:
        parts.append(path(f'M 10 54 Q 17 54 30 50 Q 31 {55+6*gape:.2f} 23 {56+7*gape:.2f} Q 18 {58+9*gape:.2f} 13 {56+8*gape:.2f} Q 11 60 10 54Z','#78394b'))
        parts.append(path(f'M 14 {56+6*gape:.2f} Q 21 {55+4*gape:.2f} 24 {56+5*gape:.2f} Q 19 {58+8*gape:.2f} 14 {56+7*gape:.2f}Z','#f3a49a'))
    parts.append(path('M 24 36 Q 28 25 40 26 Q 53 24 57 37 Q 60 49 52 57 Q 46 62 35 61 Q 29 61 28 57 Q 18 60 10 57 Q 5 55 7 49 Q 8 44 16 44 Q 21 43 24 36Z','url(#ivory)'))
    parts.append(path('M 21 40 Q 20 33 25 29 Q 23 22 34 22 Q 40 16 47 21 L 43 24 Q 50 22 55 28 Q 45 27 39 33 Q 37 28 34 29 Q 33 36 26 38 L 26 33Z','url(#jade)'))
    parts.append(path('M 24 28 Q 28 22 34 24 Q 29 25 27 29Z','#ccf7e4'))
    parts.append(path('M 7 48 Q 11 45 16 47 Q 14 52 9 53 Q 6 52 7 48Z','#63758a'))
    parts.append(path('M 42 53 Q 47 48 51 50 Q 49 55 43 56Z','#edb0a7'))
    if p.eyes < .70:
        ry=5.9*(1-.60*p.eyes)
        parts.append(f'<ellipse cx="32.1" cy="43.2" rx="4.6" ry="{ry:.2f}" fill="{ink}"/>')
        parts.append('<ellipse cx="30.9" cy="41.5" rx="1.65" ry="1.8" fill="#ffffff"/>')
    else:
        parts.append(line('M 27 43 Q 32 47 36 42',ink,2.5))
    parts.append(line('M 15 51 Q 7 55 3 51 Q 0 48 2 46','#98d6ca',1.8))
    if gape>.40:parts.append(path('M 14 57 L 18 57 L 15 60Z','#fffbee'))
    if p.voice:
        parts.append(f'<g opacity="{p.voice:.2f}">'+line('M 4 44 Q 0 50 3 56','#fff2bc',2.5)+line('M -2 40 Q -8 48 -6 54','#a4eae2',2.2)+'</g>')
    parts.append('</g></g></svg>')
    image=QImage.fromData(QByteArray(''.join(parts).encode()),b'SVG')
    if image.isNull():raise RuntimeError('qilin SVG did not render')
    return image.scaled(SIDE,SIDE,Qt.IgnoreAspectRatio,Qt.SmoothTransformation)


def main():
    out=OUT/'frames'
    out.mkdir(exist_ok=True)
    if not render_pose(Pose()).save(str(out/'idle.png')):raise IOError('cannot save idle pose')
    for name,count in COUNTS.items():
        sheet=QImage(SIDE*count,SIDE,QImage.Format_ARGB32_Premultiplied)
        sheet.fill(Qt.transparent)
        painter=QPainter(sheet)
        for n in range(count):
            painter.drawImage(n*SIDE,0,render_pose(pose_at(name,(n+1)/(count+1))))
        painter.end()
        if not sheet.save(str(out/(name+'.png'))):raise IOError('cannot save '+name)


if __name__=='__main__':
    main()
