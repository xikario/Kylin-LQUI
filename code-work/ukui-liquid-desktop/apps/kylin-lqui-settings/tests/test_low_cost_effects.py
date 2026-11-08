#!/usr/bin/python3
"""Isolated correctness checks and same-process incremental resource benchmark."""
import gc,json,os,sys,time,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_EnableHighDpiScaling)
QtCore.QCoreApplication.setAttribute(QtCore.Qt.AA_UseHighDpiPixmaps)
from low_cost_effects import attach_button,attach_scroll,install_effects
from glass_widgets import GlassCard,GlassStatusLabel
from fusion_view import FUSION_STYLE
app=QtWidgets.QApplication([]);app.setStyle('Fusion');app.setQuitOnLastWindowClosed(False)


def settle(ms):
    loop=QtCore.QEventLoop();timer=QtCore.QTimer();timer.setSingleShot(True);timer.setTimerType(QtCore.Qt.PreciseTimer);timer.timeout.connect(loop.quit);timer.start(ms);loop.exec_()


class CachedMaterial:
    def __init__(self):self.cache={};self.render_count=0;self.reads=0
    def key(self,rect,dpr,radius):return (rect.x(),rect.y(),rect.width(),rect.height(),dpr,radius)
    def render(self,rect,dpr,radius):
        key=self.key(rect,dpr,radius)
        if key not in self.cache:
            image=QtGui.QImage(round(rect.width()*dpr),round(rect.height()*dpr),QtGui.QImage.Format_ARGB32_Premultiplied);image.setDevicePixelRatio(dpr)
            image.fill(QtGui.QColor('#26374b'));p=QtGui.QPainter(image)
            gradient=QtGui.QLinearGradient(0,0,rect.width(),rect.height());gradient.setColorAt(0,QtGui.QColor('#273d54'));gradient.setColorAt(1,QtGui.QColor('#162331'))
            p.fillRect(QtCore.QRect(QtCore.QPoint(),rect.size()),gradient);p.end()
            self.cache[key]=image;self.render_count+=1
        return self.cache[key]
    def cached(self,rect,dpr,radius):self.reads+=1;return self.cache.get(self.key(rect,dpr,radius))


class Fixture(QtWidgets.QWidget):
    def __init__(self,effects=True):
        super().__init__();self.setStyleSheet(FUSION_STYLE);self.native_glass=CachedMaterial();self.opening_pending=False;self.reduce=False
        self.resize(460,500);self.card=GlassCard(self);self.card.setParent(self);self.card.setGeometry(20,20,420,460)
        layout=QtWidgets.QVBoxLayout(self.card);layout.setContentsMargins(14,14,14,14)
        title=QtWidgets.QLabel('Wi-Fi');layout.addWidget(title)
        self.scroll=QtWidgets.QScrollArea();self.scroll.setWidgetResizable(True);self.scroll.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff);layout.addWidget(self.scroll)
        content=QtWidgets.QWidget();rows=QtWidgets.QVBoxLayout(content);rows.setContentsMargins(0,0,0,0);rows.setSpacing(4)
        self.rows=[]
        for i in range(30):
            b=QtWidgets.QPushButton('测试无线网络 '+str(i+1));b.setMinimumHeight(38);rows.addWidget(b);self.rows.append(b)
        self.scroll.setWidget(content);self.button=QtWidgets.QPushButton('刷新');layout.addWidget(self.button)
        if effects:install_effects(self,lambda:self.reduce)
        self.show();settle(150)
    def close_fixture(self):self.hide();self.deleteLater();settle(30);gc.collect()


class EffectsTest(unittest.TestCase):
    def setUp(self):self.w=Fixture()
    def tearDown(self):self.w.close_fixture()
    def test_status_label_reuses_card_painter_without_inheritance(self):
        label=GlassStatusLabel(self.w);label.setParent(self.w);label.setText('测试错误提示');label.resize(200,40);label.show();settle(30)
        self.assertFalse(label.grab().isNull())
    def test_scroll_edges_cache_no_shader_and_no_idle_repaint(self):
        w=self.w;bar=w.scroll.verticalScrollBar();fade=w.scroll._lqui_scroll_fade
        self.assertTrue(fade.top.isHidden());self.assertFalse(fade.bottom.isHidden())
        bar.setValue(200);settle(60)
        self.assertFalse(fade.top.isHidden());before=w.native_glass.render_count
        builds=(fade.top.cache_builds,fade.bottom.cache_builds)
        for value in range(210,400,10):bar.setValue(value);settle(4)
        self.assertEqual(w.native_glass.render_count,before)
        self.assertEqual((fade.top.cache_builds,fade.bottom.cache_builds),builds)
        paints=(fade.top.paint_count,fade.bottom.paint_count);settle(700)
        self.assertEqual((fade.top.paint_count,fade.bottom.paint_count),paints)
        self.assertFalse(fade.findChildren(QtCore.QTimer))
        self.assertTrue(fade.top.testAttribute(QtCore.Qt.WA_TransparentForMouseEvents))
        # A foreground strip must never trigger a new render, even if cold.
        saved=w.native_glass.render
        w.native_glass.render=lambda *a:(_ for _ in ()).throw(AssertionError('extra material render'))
        image=QtGui.QImage(24,24,QtGui.QImage.Format_ARGB32_Premultiplied);p=QtGui.QPainter(image)
        self.assertTrue(w.card.paint_background(p,cached_only=True));p.end();w.native_glass.render=saved
        bar.setValue(bar.maximum());settle(30);self.assertTrue(fade.bottom.isHidden())
        w.hide();settle(40);self.assertTrue(fade.top.image.isNull());self.assertTrue(fade.bottom.image.isNull())
    def test_press_is_lazy_bounded_and_leaves_actions_and_geometry_intact(self):
        w=self.w;b=w.button;feedback=b._lqui_press_feedback;self.assertIsNone(feedback.motion)
        clicks=[];b.clicked.connect(lambda:clicks.append(1));before=b.geometry();text=b.text()
        QtTest.QTest.mousePress(b,QtCore.Qt.LeftButton);settle(140)
        rim=feedback;self.assertIsNotNone(rim.motion);self.assertEqual(rim.motion.state(),QtCore.QTimeLine.NotRunning)
        self.assertAlmostEqual(rim.amount,1.);painted=rim.paint_count;settle(450);self.assertEqual(rim.paint_count,painted)
        self.assertFalse(rim.damage().contains(b.rect().center()))
        QtTest.QTest.mouseRelease(b,QtCore.Qt.LeftButton);settle(180)
        self.assertEqual(clicks,[1]);self.assertEqual(b.geometry(),before);self.assertEqual(b.text(),text)
        self.assertLessEqual(rim.amount,.001);self.assertEqual(rim.motion.state(),QtCore.QTimeLine.NotRunning)
        QtTest.QTest.mousePress(b,QtCore.Qt.LeftButton);settle(30);b.setEnabled(False);settle(20)
        self.assertLessEqual(rim.amount,.001);self.assertEqual(rim.motion.state(),QtCore.QTimeLine.NotRunning)
    def test_toolbutton_keeps_native_check_and_click_behavior(self):
        b=QtWidgets.QToolButton(self.w);b.setGeometry(300,1,40,32);b.setText('✓');b.setCheckable(True);b.show()
        feedback=attach_button(b,lambda:False);clicks=[];b.clicked.connect(lambda value:clicks.append(value))
        QtTest.QTest.mousePress(b,QtCore.Qt.LeftButton);settle(110)
        self.assertGreater(feedback.amount,.9);self.assertFalse(b.grab().isNull())
        QtTest.QTest.mouseRelease(b,QtCore.Qt.LeftButton);settle(170)
        self.assertEqual(clicks,[True]);self.assertTrue(b.isChecked());self.assertEqual(b.text(),'✓')
        self.assertEqual(feedback.motion.state(),QtCore.QTimeLine.NotRunning)

    def test_reduced_motion_and_pointer_passthrough(self):
        w=self.w;w.reduce=True;QtTest.QTest.mouseClick(w.button,QtCore.Qt.LeftButton);settle(30)
        self.assertIsNone(w.button._lqui_press_feedback.motion)
        fade=w.scroll._lqui_scroll_fade;old=w.scroll.verticalScrollBar().value()
        w.scroll.verticalScrollBar().setValue(100);settle(30)
        for strip in (fade.top,fade.bottom):self.assertTrue(strip.testAttribute(QtCore.Qt.WA_TransparentForMouseEvents))
        w.scroll.verticalScrollBar().setValue(0);settle(20);self.assertTrue(fade.top.isHidden())
    def test_strip_uses_clean_surface_without_content_capture(self):
        w=self.w;fade=w.scroll._lqui_scroll_fade;w.scroll.verticalScrollBar().setValue(120);settle(60)
        image=fade.top.image;self.assertFalse(image.isNull());d=image.devicePixelRatioF()
        self.assertGreater(image.pixelColor(5,0).alpha(),240)
        self.assertLess(image.pixelColor(5,image.height()-1).alpha(),40)
        maximum=2*round(w.scroll.viewport().width()*d)*round(12*d)*4
        self.assertLessEqual(fade.top.image.byteCount()+fade.bottom.image.byteCount(),maximum)
        out=os.environ.get('LQUI_EFFECT_ARTIFACT')
        if out:
            Path(out).mkdir(parents=True,exist_ok=True);w.grab().save(str(Path(out)/'scroll-edge.png'))


def pss():
    for line in Path('/proc/self/smaps_rollup').read_text().splitlines():
        if line.startswith('Pss:'):return int(line.split()[1])
    return 0


def benchmark(output):
    w=Fixture(False);bar=w.scroll.verticalScrollBar()
    def measure(kind,ms,active=None):
        timer=QtCore.QTimer();timer.setTimerType(QtCore.Qt.PreciseTimer);timer.setInterval(16);step=[0]
        if active:
            def tick():step[0]+=1;active(step[0])
            timer.timeout.connect(tick);timer.start()
        cpu=time.process_time();wall=time.monotonic();settle(ms);timer.stop()
        elapsed=time.monotonic()-wall
        return {'phase':kind,'wall_s':elapsed,'cpu_ms':(time.process_time()-cpu)*1000,'ticks':step[0],'pss_kib':pss()}
    def scroll(i):bar.setValue(100+(i*7)%max(1,bar.maximum()-200))
    def press(i):
        if i%16==1:QtTest.QTest.mousePress(w.button,QtCore.Qt.LeftButton)
        elif i%16==7:QtTest.QTest.mouseRelease(w.button,QtCore.Qt.LeftButton)
    # Warm fonts, layout and existing surfaces before comparing the same view.
    measure('warmup',1500,scroll);bar.setValue(200);settle(300)
    records=[measure('baseline-idle',5000),measure('baseline-scroll',3000,scroll),measure('baseline-press',3000,press)]
    QtTest.QTest.mouseRelease(w.button,QtCore.Qt.LeftButton);settle(200);before=pss()
    install_effects(w,lambda:w.reduce);bar.setValue(220);QtTest.QTest.mouseClick(w.button,QtCore.Qt.LeftButton);settle(300)
    records.extend([measure('effects-idle',5000),measure('effects-scroll',3000,scroll),measure('effects-press',3000,press)])
    QtTest.QTest.mouseRelease(w.button,QtCore.Qt.LeftButton);settle(250)
    fade=w.scroll._lqui_scroll_fade;rim=w.button._lqui_press_feedback
    paints=(fade.top.paint_count,fade.bottom.paint_count,rim.paint_count)
    records.append(measure('settled-idle',3000))
    idle_paints=sum(a-b for a,b in zip((fade.top.paint_count,fade.bottom.paint_count,rim.paint_count),paints))
    after=pss();w.hide();settle(100);records.append(measure('hidden',3000))
    result={'dpr':w.devicePixelRatioF(),'view':[w.width(),w.height()],'records':records,'incremental_pss_kib':after-before,'settled_extra_paints':idle_paints,
            'scroll_cache_builds':[fade.top.cache_builds,fade.bottom.cache_builds],'press_animation_running':rim.motion.state()!=QtCore.QTimeLine.NotRunning,
            'hidden_strip_bytes':fade.top.image.byteCount()+fade.bottom.image.byteCount(),'material_bakes':w.native_glass.render_count,
            'scope':'Same-process warmed Qt raster fixture on this CPU; 30 rows; cached material; no real system backend polling. CPU is one-core time, not total-system usage.'}
    Path(output).write_text(json.dumps(result,ensure_ascii=False,indent=2));print(json.dumps(result,ensure_ascii=False));w.close_fixture()
    assert idle_paints==0 and not result['press_animation_running'] and result['hidden_strip_bytes']==0

if __name__=='__main__':
    if '--benchmark' in sys.argv:benchmark(sys.argv[sys.argv.index('--benchmark')+1])
    else:unittest.main()
