"""Calendar header/navigation icons must stay sharp at fractional scaling.

At 150% an 18px button icon covers 27 physical pixels. A fixed bitmap (or a
logical-size pixmap without AA_UseHighDpiPixmaps) is resampled to fit and the
strokes turn jagged. Icons must be drawn as vectors at the exact physical size.
"""
import os,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
# The user's main screen: 2880x1800 at 150%.
os.environ.pop('QT_SCALE_FACTOR',None);os.environ['QT_SCREEN_SCALE_FACTORS']='1.5'
from PyQt5 import QtCore as C, QtGui as G, QtWidgets as W
import calendar_popup
calendar_popup.configure_application()
app=W.QApplication.instance() or W.QApplication(sys.argv)
from calendar_appearance import icon

NAMES=('refresh','close','back','chevron')


class CalendarIconTest(unittest.TestCase):
    def test_high_dpi_pixmaps_enabled(self):
        self.assertTrue(W.QApplication.testAttribute(C.Qt.AA_UseHighDpiPixmaps))

    def test_icons_are_vector_not_fixed_bitmaps(self):
        for name in NAMES:
            with self.subTest(name=name):
                self.assertEqual(icon(name).availableSizes(),[],'fixed bitmap sizes get resampled')

    def test_pixmap_is_rendered_at_physical_size_with_antialiasing(self):
        ratio=app.devicePixelRatio();side=int(18*ratio+.5)  # qRound
        self.assertAlmostEqual(ratio,1.5)
        self.assertNotEqual(ratio,round(ratio),'fixture must exercise fractional scaling')
        for name in NAMES:
            with self.subTest(name=name):
                pixmap=icon(name).pixmap(C.QSize(18,18))
                self.assertEqual((pixmap.width(),pixmap.height()),(side,side))
                self.assertAlmostEqual(pixmap.devicePixelRatio(),ratio,places=2)
                image=pixmap.toImage().convertToFormat(G.QImage.Format_ARGB32)
                alphas={G.qAlpha(image.pixel(x,y)) for x in range(side) for y in range(side)}
                self.assertIn(255,alphas,'stroke core should be fully opaque')
                self.assertTrue(any(0<a<255 for a in alphas),'edges should be antialiased')

    def test_disabled_icon_is_faded(self):
        normal=icon('chevron').pixmap(C.QSize(18,18)).toImage()
        disabled=icon('chevron').pixmap(C.QSize(18,18),G.QIcon.Disabled).toImage()
        peak=lambda img:max(G.qAlpha(img.pixel(x,y)) for x in range(img.width()) for y in range(img.height()))
        self.assertLess(peak(disabled),peak(normal))


if __name__=='__main__':unittest.main()
