"""Opt-in control-center material contract, exercised against the native bridge.

Set LQUI_NATIVE_GLASS_LIBRARY to the rebuilt bridge. Run once on the desktop
for GPU, and once with UKUI_LIQUID_GLASS_NO_GL=1 for the CPU fallback.
"""
import ctypes,os,sys,unittest
from pathlib import Path
from PyQt5 import QtCore,QtGui,QtWidgets
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from native_glass import NativeGlass
app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])

class AdaptiveGlass(unittest.TestCase):
    def setUp(self):
        library=os.environ.get('LQUI_NATIVE_GLASS_LIBRARY')
        if not library or not hasattr(ctypes.CDLL(library),'lqui_glass_set_adaptive_smoke'):
            self.skipTest('Rebuilt native bridge required')
        self.materials=[]
    def tearDown(self):
        for material in self.materials:material.close()
    def source(self,kind,dpr=1):
        image=QtGui.QImage(round(360*dpr),round(240*dpr),QtGui.QImage.Format_ARGB32_Premultiplied);image.setDevicePixelRatio(dpr)
        image.fill(QtGui.QColor('#101820' if kind=='dark' else '#ffffff'))
        p=QtGui.QPainter(image)
        if kind=='split':p.fillRect(0,120,360,120,QtCore.Qt.black)
        if kind=='detail':
            gradient=QtGui.QLinearGradient(0,0,360,0);gradient.setColorAt(0,QtGui.QColor('#8090a0'));gradient.setColorAt(1,QtGui.QColor('#e0e8f0'));p.fillRect(0,0,360,240,gradient)
        p.end();return image
    def material(self,image,adaptive):
        material=NativeGlass(image,QtCore.QRect(0,0,360,240),adaptive_smoke=adaptive);self.materials.append(material);return material
    def render(self,image,adaptive):return self.material(image,adaptive).render(QtCore.QRect(0,0,360,240),image.devicePixelRatio(),23)
    def light(self,image,x,y):
        c=image.pixelColor(round(x*image.devicePixelRatio()),round(y*image.devicePixelRatio()));return .299*c.red()+.587*c.green()+.114*c.blue()
    def test_dark_background_retains_existing_material(self):
        for dpr in (1,1.5,2):
            image=self.source('dark',dpr);self.assertEqual(self.render(image,False),self.render(image,True))
    def test_white_has_narrow_rim_and_readable_body(self):
        for dpr in (1,1.5,2):
            image=self.source('white',dpr);old=self.render(image,False);new=self.render(image,True)
            self.assertLess(self.light(new,180,8)-self.light(new,180,50),8)
            if not os.environ.get("UKUI_LIQUID_GLASS_NO_GL"):
                self.assertGreater(self.light(old,180,8)-self.light(old,180,50),12)
            self.assertGreater(self.light(new,180,1),self.light(new,180,50)+6)
            self.assertLess(self.light(new,180,120),105)
            self.assertGreater(self.light(new,180,120),60)
            self.assertEqual(new.pixelColor(0,0).alpha(),old.pixelColor(0,0).alpha())
            self.assertEqual(new.pixelColor(new.width()//2,new.height()//2).alpha(),255)
    def test_split_card_compresses_extremes_without_flattening(self):
        image=self.source('split');old=self.render(image,False);new=self.render(image,True)
        original=self.light(old,180,60)-self.light(old,180,180);adapted=self.light(new,180,60)-self.light(new,180,180)
        self.assertLess(adapted,original*.7);self.assertGreater(adapted,20)
        self.assertGreater(self.light(new,180,180),20)
    def test_color_detail_survives_and_cache_does_not_rebake(self):
        image=self.source('detail');material=self.material(image,True);rect=QtCore.QRect(0,0,360,240)
        first=material.render(rect,1,23);self.assertGreater(self.light(first,300,120)-self.light(first,60,120),8)
        c=first.pixelColor(180,120);self.assertGreater(c.blue(),c.red())
        for _ in range(10):self.assertEqual(first,material.render(rect,1,23))
        self.assertEqual(material.render_count,1)
        if not os.environ.get('UKUI_LIQUID_GLASS_NO_GL'):self.assertTrue(material.used_gpu)
    def test_opt_in_does_not_leak_between_contexts(self):
        image=self.source('white');original=self.material(image,False);adaptive=self.material(image,True)
        full=QtCore.QRect(0,0,360,240);small=QtCore.QRect(0,0,350,240)
        before=original.render(small,1,23);adaptive.render(full,1,23)
        original.cache.clear();self.assertEqual(before,original.render(small,1,23))

if __name__=='__main__':unittest.main()
