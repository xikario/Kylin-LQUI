"""Compare the mapped X11 surface BEFORE QWidget.grab forces a repaint.

Run under Xvfb for backing-store regression; additionally run on a composited
X11 desktop with LQUI_NATIVE_GLASS_LIBRARY for the real GPU path.
"""
import os,sys,unittest
from pathlib import Path
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps)
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from test_control_center import app,Fixture,SAMPLE
from control_center import ControlCenter
from glass_widgets import GlassCard

class Pattern(QtWidgets.QWidget):
    def paintEvent(self,event):
        painter=QtGui.QPainter(self)
        for x in range(0,self.width(),60):
            for y in range(0,self.height(),60):
                painter.fillRect(x,y,60,60,QtGui.QColor(('#197060','#a87834','#384988','#b04969')[(x//60+y//60)%4]))

class MappedSurface(unittest.TestCase):
    def setUp(self):
        self.background=Pattern(None,QtCore.Qt.FramelessWindowHint)
        self.background.setGeometry(app.primaryScreen().availableGeometry());self.background.show();self.background.raise_()
        QtTest.QTest.qWait(700)
        self.window=ControlCenter(Fixture);self.window.dialog_open=True
        self.anchor=app.primaryScreen().availableGeometry().bottomRight()-QtCore.QPoint(450,60)
    def tearDown(self):
        self.window.hide();self.window.material_pool.waitForDone(5000);QtCore.QThreadPool.globalInstance().waitForDone(5000)
        QtTest.QTest.qWait(30);self.window.deleteLater();self.background.close();self.background.deleteLater();QtTest.QTest.qWait(30)
    def open(self):
        self.window.open_at('home',self.anchor.x(),self.anchor.y(),40,40)
        QtTest.QTest.qWait(700);self.window.poll.stop()
    def assert_surface(self,name):
        w=self.window;QtTest.QTest.qWait(100)
        # Never use QWidget.grab as the observation: that is a full repaint.
        actual=app.primaryScreen().grabWindow(0,w.x(),w.y(),w.width(),w.height()).toImage()
        expected=w.grab().toImage()
        root=os.environ.get('LQUI_SURFACE_ARTIFACT')
        if root:
            Path(root).mkdir(parents=True,exist_ok=True)
            actual.save(str(Path(root)/(name+'-screen.png')));expected.save(str(Path(root)/(name+'-expected.png')))
        dpr=w.devicePixelRatioF();errors=[]
        # Sample opaque card interiors; tolerate one physical pixel of X11
        # rounding at fractional DPR, but never whole fallback rectangles.
        for card in w.findChildren(GlassCard):
            if not card.isVisible():continue
            origin=card.mapTo(w,QtCore.QPoint());bounds=QtCore.QRect(origin,card.size()).adjusted(12,12,-12,-12)
            for y in range(bounds.top(),bounds.bottom(),7):
                for x in range(bounds.left(),bounds.right(),7):
                    if not w.mask().isEmpty() and not w.mask().contains(QtCore.QPoint(x,y)):continue
                    px,py=round(x*dpr),round(y*dpr)
                    if not expected.rect().contains(px,py) or not actual.rect().contains(px,py):continue
                    target=expected.pixelColor(px,py)
                    if target.alpha()!=255:continue
                    errors.append(min(max(abs(target.red()-c.red()),abs(target.green()-c.green()),abs(target.blue()-c.blue()))
                        for dx in (-1,0,1) for dy in (-1,0,1)
                        if actual.rect().contains(px+dx,py+dy)
                        for c in [actual.pixelColor(px+dx,py+dy)]))
        self.assertGreater(len(errors),100)
        bad=sum(error>12 for error in errors)/len(errors)
        self.assertLess(bad,.02,(name,'stale surface fraction',bad))
    def test_delayed_material_repaints_descendants_at_rest(self):
        pending=[];job=self.window.job
        def defer(work,done,pool=None):
            if pool is self.window.material_pool:pending.append((work,done))
            else:job(work,done,pool=pool)
        self.window.job=defer;self.open();self.assertEqual(len(pending),1)
        self.assertTrue(self.window.opening_pending);self.assertEqual(self.window.windowOpacity(),0)
        self.assertEqual(self.window.popup_progress,0);self.assertFalse(self.window.state()['motionRunning'])
        work,done=pending.pop();done({'ok':True,'data':work()})
        QtTest.QTest.qWait(600)
        self.assert_surface('delayed-home')
    def test_every_opening_frame_uses_prepared_glass_without_baking(self):
        pending=[];job=self.window.job
        def defer(work,done,pool=None):
            if pool is self.window.material_pool:pending.append((work,done))
            else:job(work,done,pool=pool)
        self.window.job=defer
        self.window.open_at('home',self.anchor.x(),self.anchor.y(),40,40)
        QtTest.QTest.qWait(150)
        self.assertEqual(self.window.popup_progress,0)
        frames=[]
        self.window.popup_motion.valueChanged.connect(lambda value:frames.append((float(value),self.window.native_glass.render_count if self.window.native_glass else 0)))
        work,done=pending.pop();done({'ok':True,'data':work()});QtTest.QTest.qWait(650);self.window.poll.stop()
        moving=[count for progress,count in frames if 0<progress<1]
        self.assertGreater(len(moving),3);self.assertGreaterEqual(min(moving),8);self.assertEqual(min(moving),max(moving))
        for progress in (.3,.55,.8,1):
            self.window.popup_frame(progress);self.assert_surface('opening-'+str(progress))
        self.assertEqual(self.window.native_glass.render_count,moving[-1])
    def test_first_real_partial_frame_has_glass_before_any_forced_grab(self):
        paused=[]
        def pause(value):
            if float(value)>=.3 and not paused:
                paused.append(float(value));self.window.popup_motion.pause()
        self.window.popup_motion.valueChanged.connect(pause)
        self.open();self.assertTrue(paused);self.assertLess(paused[0],1)
        self.assert_surface('first-real-partial-frame')
        self.window.popup_motion.resume();QtTest.QTest.qWait(350)
        self.assert_surface('first-real-settled-frame')
    def test_settled_navigation_and_motion_keep_the_same_material(self):
        self.open();self.assert_surface('home')
        for page in ('wifi','bluetooth','sound','power','home'):
            self.window.navigate(page);QtTest.QTest.qWait(150);self.assert_surface(page)
        material=self.window.native_glass;self.assertIsNotNone(material)
        count=material.render_count
        self.window.popup_frame(.8);self.assert_surface('moving')
        self.window.popup_frame(1);self.assert_surface('settled-again')
        self.assertEqual(count,material.render_count)
    def test_scrolling_long_lists_preserves_fixed_glass_and_footer(self):
        class ManyDevices(Fixture):
            def snapshot(self):
                data=super().snapshot()
                for page,key in (('wifi','networks'),('bluetooth','devices')):
                    data[page][key]=[dict(data[page][key][0],id='/item'+str(i)) for i in range(40)]
                return data
        self.window.backend_factory=ManyDevices;self.open()
        for page in ('wifi','bluetooth'):
            self.window.navigate(page);QtTest.QTest.qWait(150)
            self.assert_surface(page+'-long')
            scroll=self.window.network_scroll if page=='wifi' else self.window.bluetooth_scroll
            bar=scroll.verticalScrollBar();self.assertGreater(bar.maximum(),0)
            count=self.window.native_glass.render_count
            for fraction in (.5,1,0):
                bar.setValue(round(bar.maximum()*fraction));QtTest.QTest.qWait(80)
                self.assert_surface(page+'-scroll-'+str(fraction))
            self.assertEqual(count,self.window.native_glass.render_count)
    def test_calendar_first_and_anchor_flip_cover_all_detail_cards(self):
        # At this anchor a 660px home is below it while shorter pages fit
        # above. Every page must still sample the original pre-map scene.
        area=app.primaryScreen().availableGeometry()
        for y in (self.anchor.y(),area.top()+600):
            self.window.hide();self.window.open_at('calendar',self.anchor.x(),y,40,40)
            QtTest.QTest.qWait(700);self.window.poll.stop()
            material=self.window.native_glass;self.assertIsNotNone(material)
            for page in ('home','wifi','sound','power'):
                self.window.navigate(page);QtTest.QTest.qWait(100)
                for card in self.window.findChildren(GlassCard):
                    if not card.isVisible():continue
                    rect=QtCore.QRect(card.mapToGlobal(QtCore.QPoint()),card.size())
                    self.assertTrue(self.window.backdrop_region.contains(rect),(page,rect,self.window.backdrop_region))
                    self.assertFalse(material.render(rect,card.devicePixelRatioF(),card.radius).isNull())
                self.assertIs(material,self.window.native_glass)
    def test_navigation_target_contains_laid_out_rows_before_animation(self):
        self.open();self.window.refresh=lambda:None;self.window.maybe_scan_wifi=lambda:None
        for page in ('wifi','bluetooth','sound','power'):
            self.window.navigate(page);target=self.window.navigation_overlay.after.toImage()
            self.assertFalse(target.isNull());self.window.finish_navigation();QtTest.QTest.qWait(60)
            settled=self.window.grab().toImage()
            if os.environ.get('LQUI_SURFACE_ARTIFACT'):
                root=Path(os.environ['LQUI_SURFACE_ARTIFACT']);root.mkdir(parents=True,exist_ok=True)
                target.save(str(root/(page+'-navigation-target.png')));settled.save(str(root/(page+'-navigation-settled.png')))
            self.assertEqual(target,settled,page)

if __name__=='__main__':unittest.main()
