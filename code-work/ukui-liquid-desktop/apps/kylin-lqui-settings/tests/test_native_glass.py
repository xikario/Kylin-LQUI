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
    def test_window_crop_preserves_global_coordinates(self):
        painter=QtGui.QPainter(self.source);painter.fillRect(0,0,300,400,QtGui.QColor('#005030'));painter.fillRect(300,0,300,400,QtGui.QColor('#304080'));painter.end();self.source.save(str(self.path))
        screen=QtCore.QRect(-600,0,600,400);rect=QtCore.QRect(-240,30,180,150)
        whole=NativeGlass(self.source,screen);crop=NativeGlass(self.source.copy(360,30,180,150),screen,rect)
        self.assertEqual(whole.render(rect,1.5,23),crop.render(rect,1.5,23));whole.close();crop.close()
    def test_real_scene_has_priority_and_bright_detail_is_not_clipped_flat(self):
        wallpaper=QtGui.QImage(self.source);wallpaper.fill(QtCore.Qt.green);wallpaper.save(str(self.path))
        self.source.fill(QtGui.QColor('#3040c0'));material=NativeGlass(self.source,QtCore.QRect(0,0,600,400));image=material.render(QtCore.QRect(40,40,220,180),1,23)
        self.assertGreater(image.pixelColor(110,90).blue(),image.pixelColor(110,90).green()+35);material.close()
        self.source.fill(QtCore.Qt.white);p=QtGui.QPainter(self.source);p.fillRect(0,0,300,400,QtGui.QColor(200,200,200));p.end();material=NativeGlass(self.source,QtCore.QRect(0,0,600,400))
        dark=material.render(QtCore.QRect(30,50,220,180),1,23);bright=material.render(QtCore.QRect(350,50,220,180),1,23)
        self.assertGreater(bright.pixelColor(110,90).red()-dark.pixelColor(110,90).red(),10)
        self.assertGreater(bright.pixelColor(110,2).red(),bright.pixelColor(110,90).red()+25);material.close()
if __name__=='__main__':unittest.main()
