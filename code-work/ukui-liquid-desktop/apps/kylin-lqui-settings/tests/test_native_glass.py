import os,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets
from native_glass import NativeGlass
app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
class NativeOptics(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.old=os.environ.get('UKUI_LIQUID_WALLPAPER')
        self.path=Path(self.temp.name)/'wallpaper.png';self.source=QtGui.QImage(600,400,QtGui.QImage.Format_ARGB32_Premultiplied);self.source.fill(QtCore.Qt.white);self.source.save(str(self.path));os.environ['UKUI_LIQUID_WALLPAPER']=str(self.path)
    def tearDown(self):
        if self.old is None:os.environ.pop('UKUI_LIQUID_WALLPAPER',None)
        else:os.environ['UKUI_LIQUID_WALLPAPER']=self.old
        self.temp.cleanup()
    def test_hidpi_alpha_and_readable_white_wallpaper(self):
        for dpr in (1,1.5,2):
            material=NativeGlass(self.source,QtCore.QRect(0,0,600,400));image=material.render(QtCore.QRect(60,60,220,180),dpr,23)
            self.assertEqual(image.size(),QtCore.QSize(round(220*dpr),round(180*dpr)));self.assertEqual(image.devicePixelRatio(),dpr)
            self.assertEqual(image.pixelColor(0,0).alpha(),0);color=image.pixelColor(round(100*dpr),round(100*dpr));self.assertEqual(color.alpha(),255);self.assertLessEqual(max(color.red(),color.green(),color.blue()),100);material.close()
    def test_unchanged_repaint_reuses_material_and_resampling_uses_location(self):
        painter=QtGui.QPainter(self.source);painter.fillRect(0,0,300,400,QtGui.QColor('#005030'));painter.fillRect(300,0,300,400,QtGui.QColor('#304080'));painter.end();self.source.save(str(self.path))
        material=NativeGlass(self.source,QtCore.QRect(0,0,600,400));left=material.render(QtCore.QRect(10,20,200,150),1.5,23)
        for _ in range(40):self.assertEqual(left,material.render(QtCore.QRect(10,20,200,150),1.5,23))
        self.assertEqual(material.render_count,1);right=material.render(QtCore.QRect(360,20,200,150),1.5,23);self.assertNotEqual(left,right);self.assertEqual(material.render_count,2);material.close()
    def test_missing_wallpaper_uses_capture_without_live_window_transparency(self):
        os.environ['UKUI_LIQUID_WALLPAPER']=str(self.path.with_name('missing.png'));material=NativeGlass(self.source,QtCore.QRect(0,0,600,400));image=material.render(QtCore.QRect(20,20,100,100),1,20);self.assertFalse(image.isNull());self.assertEqual(image.pixelColor(50,50).alpha(),255);material.close()
if __name__=='__main__':unittest.main()
