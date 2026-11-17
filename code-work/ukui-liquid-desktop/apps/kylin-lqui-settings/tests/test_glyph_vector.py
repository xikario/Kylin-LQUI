"""Quick-settings glyphs must stay sharp at fractional scaling.

glyph() used to return a fixed 2x bitmap. On the user's 150% screen every icon
was resampled from 2x down to 1.5x, so strokes looked soft and stepped. Icons
are now drawn as vectors at the exact physical size that is requested.
"""
import os,sys,unittest
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
os.environ.pop('QT_SCALE_FACTOR',None);os.environ['QT_SCREEN_SCALE_FACTORS']='1.5'
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore as C, QtGui as G, QtWidgets as W
W.QApplication.setAttribute(C.Qt.AA_EnableHighDpiScaling);W.QApplication.setAttribute(C.Qt.AA_UseHighDpiPixmaps)
app=W.QApplication.instance() or W.QApplication(sys.argv)
from glass_widgets import glyph

NAMES=('wifi','bluetooth','sound','muted','microphone','microphone-muted','sun','power','display',
       'bell','music','battery','calendar','next','previous','pause','play','chevron','close')


def alphas(pixmap):
    image=pixmap.toImage().convertToFormat(G.QImage.Format_ARGB32)
    return [G.qAlpha(image.pixel(x,y)) for x in range(image.width()) for y in range(image.height())]


class GlyphVectorTest(unittest.TestCase):
    def test_glyphs_are_vectors_not_fixed_bitmaps(self):
        for name in NAMES:
            with self.subTest(name=name):self.assertEqual(glyph(name).availableSizes(),[])

    def test_rendered_at_physical_size_and_antialiased(self):
        self.assertAlmostEqual(app.devicePixelRatio(),1.5)
        for name in NAMES:
            with self.subTest(name=name):
                pixmap=glyph(name).pixmap(C.QSize(18,18))
                self.assertEqual((pixmap.width(),pixmap.height()),(27,27))
                self.assertAlmostEqual(pixmap.devicePixelRatio(),1.5)
                values=alphas(pixmap)
                self.assertTrue(any(0<a<255 for a in values),'edges should be antialiased')
                self.assertGreater(max(values),200,'glyph must be drawn')

    def test_label_pixmap_request_is_sharp(self):
        # fusion_view: glyph('music',size=33).pixmap(33,33) for the media cover.
        pixmap=glyph('music',size=33).pixmap(33,33)
        self.assertEqual(pixmap.width(),int(33*1.5+.5))

    def test_color_is_respected(self):
        image=glyph('close','#ff0000').pixmap(C.QSize(24,24)).toImage().convertToFormat(G.QImage.Format_ARGB32)
        opaque=[image.pixelColor(x,y) for x in range(image.width()) for y in range(image.height()) if image.pixelColor(x,y).alpha()==255]
        self.assertTrue(opaque and all(c.red()==255 and c.green()==0 and c.blue()==0 for c in opaque))

    def test_disabled_glyph_is_faded(self):
        normal=max(alphas(glyph('wifi').pixmap(C.QSize(18,18))))
        disabled=max(alphas(glyph('wifi').pixmap(C.QSize(18,18),G.QIcon.Disabled)))
        self.assertLess(disabled,normal)


if __name__=='__main__':unittest.main()
