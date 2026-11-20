"""One look for every taskbar popup (Python twin of
shared/liquid-popup/src/PopupStyle.h; keep the two in sync).

Start menu, quick settings, calendar and AI station share the shape, edge,
flat fallback, text colours and the in-place fade. There are no per-popup
skins; the global effects tier decides glass vs. fallback and motion scale.
"""
from PyQt5 import QtCore as C, QtGui as G

RADIUS = 24
GAP = 8
# In-place fade: per-frame X11 moves/reshapes stall the compositor on the FTG340.
OPEN_MS, CLOSE_MS = 150, 120
EDGE_WIDTH = 1.0
TEXT, MUTED, ACCENT = '#eef3fa', '#b9c9d8', '#a0e7ce'


def edge_color():
    return G.QColor(255, 255, 255, 64)


def fallback_fill():
    return G.QColor(23, 33, 44, 242)


def shape(rect, radius=RADIUS):
    path = G.QPainterPath()
    path.addRoundedRect(C.QRectF(rect), radius, radius)
    return path


def paint_edge(painter, rect, radius=RADIUS):
    painter.save()
    painter.setRenderHint(G.QPainter.Antialiasing)
    painter.setBrush(C.Qt.NoBrush)
    painter.setPen(G.QPen(edge_color(), EDGE_WIDTH))
    inset = EDGE_WIDTH / 2
    painter.drawRoundedRect(C.QRectF(rect).adjusted(inset, inset, -inset, -inset), radius - inset, radius - inset)
    painter.restore()


def paint_fallback(painter, rect, radius=RADIUS):
    painter.save()
    painter.setRenderHint(G.QPainter.Antialiasing)
    painter.fillPath(shape(rect, radius), fallback_fill())
    painter.restore()
    paint_edge(painter, rect, radius)


def fade_duration(opening, remaining=1.0):
    """Milliseconds for the open/close fade, scaled by the global effects policy."""
    import effects_policy
    full = OPEN_MS if opening else CLOSE_MS
    return max(0, round(full * effects_policy.motion_scale() * abs(remaining)))
