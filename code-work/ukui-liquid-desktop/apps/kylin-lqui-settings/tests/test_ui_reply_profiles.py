#!/usr/bin/python3
import copy,json,os,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtWidgets,QtTest
from main import SettingsWindow
from ui_components import RadioChoice
from test_ui import Fixture,app


def profile_page():
    def field(name,kind,label,**values):return dict(id='native-'+name,name=name,kind=kind,label=label,**values)
    return {'ok':True,'fields':[
        field('settingBackend','choice','AI 后台',options=['Codex'],value=0),
        field('settingDefaultReplyMode','choice','默认问答模式',options=['普通思考','快问快答'],value=1,help='下次启动生效。'),
        field('replyModeHelp','note','说明',value='两种模式分别保留模型配置和聊天记录。'),
        field('settingModel','choice','普通思考模型',options=['GPT-6-Luna','GPT-6.1-Sol'],value=0),
        field('refreshModels','action','刷新'),
        field('settingEffort','number','思考强度',min=0,max=5,step=1,value=4,valueLabels=['默认 medium','low','medium','high','xhigh','max']),
        field('cliModelStatus','note','状态',value='普通思考：Luna · xhigh'),
        field('cliModelReference','note','参考',value='CLI 默认：Luna'),
        field('settingQuickModel','choice','快问快答模型',options=['GPT-6-Luna','GPT-6.1-Sol'],value=0),
        field('settingQuickEffort','number','快问思考强度',min=0,max=5,step=1,value=4,valueLabels=['默认 medium','low','medium','high','xhigh','max']),
        field('settingQuickFast','boolean','启用 Fast',value=False),
        field('quickModelStatus','note','说明',value='独立保存，下一条消息生效。'),
        field('settingWidth','number','窗口宽度',min=360,max=1000,value=480,step=10),
        field('apply','action','应用')]}


class ProfileFixture(Fixture):
    def call(self,module,method,args,callback):
        if method=='Set':
            for field in self.page['fields']:
                if field['id']==args[1]:field['value']=json.loads(args[2])['value']
        super().call(module,method,args,callback)


class ReplyProfiles(unittest.TestCase):
    def setUp(self):
        self.fixture=ProfileFixture();self.fixture.page=profile_page();self.window=SettingsWindow(self.fixture,font_scale=1)
        self.window.poll.stop();self.window.open_page('ai:general');QtTest.QTest.qWait(60)
    def tearDown(self):self.window.close();self.window.deleteLater();QtTest.QTest.qWait(20)
    def entry(self,name):return next(e for e in self.window.fields.values() if e['field'].get('name')==name)
    def writes(self):return [c for c in self.fixture.calls if c[1] in ('Set','Activate')]
    def test_radio_columns_align_and_keep_all_native_fields(self):
        self.window.resize(1260,850);QtTest.QTest.qWait(40)
        selector=self.entry('settingDefaultReplyMode')['control'];self.assertIsInstance(selector,RadioChoice)
        self.assertEqual(selector.currentIndex(),1);canvas=self.window.scroll.widget()
        for index,name in enumerate(('settingModel','settingQuickModel')):
            radio=selector.buttons[index];title=self.entry(name)['label']
            self.assertLessEqual(abs(radio.mapTo(canvas,QtCore.QPoint()).x()-title.mapTo(canvas,QtCore.QPoint()).x()),2)
        left=self.window.findChild(QtWidgets.QWidget,'normalProfilePanel');right=self.window.findChild(QtWidgets.QWidget,'quickProfilePanel')
        self.assertEqual(left.y(),right.y());self.assertLess(left.x()+left.width(),right.x());self.assertLessEqual(abs(left.width()-right.width()),1)
        self.assertEqual(set(self.window.fields),{f['id'] for f in self.fixture.page['fields']});self.assertEqual(self.writes(),[])
    def test_radio_click_keyboard_and_poll_use_original_index_once(self):
        selector=self.entry('settingDefaultReplyMode')['control'];selector.buttons[0].click();QtTest.QTest.qWait(30)
        self.assertEqual(len(self.writes()),1);self.assertEqual(self.writes()[0],('ai','Set',['general','native-settingDefaultReplyMode',json.dumps({'value':0},ensure_ascii=False)]))
        self.assertEqual(selector.currentIndex(),0);selector.buttons[0].setFocus();QtTest.QTest.keyClick(selector.buttons[0],QtCore.Qt.Key_Right);QtTest.QTest.qWait(30)
        self.assertEqual(selector.currentIndex(),1);self.assertEqual(len(self.writes()),2)
        self.window.receive_page(copy.deepcopy(self.fixture.page));self.window.resize(720,480);QtTest.QTest.qWait(40)
        self.assertIs(self.entry('settingDefaultReplyMode')['control'],selector);self.assertEqual(len(self.writes()),2)
    def test_narrow_large_font_and_search_preserve_bounds_and_selection(self):
        self.window.ui_scale=1.25;self.window.apply_ui_style();self.window.resize(720,480);QtTest.QTest.qWait(50)
        canvas=self.window.scroll.widget();self.assertLessEqual(canvas.width(),self.window.scroll.viewport().width())
        for entry in self.window.fields.values():
            control=entry['control'];surface=self.window.page_actions if self.window.page_actions.isAncestorOf(control) else canvas
            if control.isVisible():self.assertLessEqual(control.mapTo(surface,QtCore.QPoint(control.width(),0)).x(),surface.width()+2)
        self.window.search.setText('独立');QtTest.QTest.qWait(30);self.window.search.clear();QtTest.QTest.qWait(30)
        self.assertEqual(self.entry('settingDefaultReplyMode')['control'].currentIndex(),1);self.assertEqual(self.writes(),[])
    def test_disabled_radio_option_cannot_write(self):
        field=next(f for f in self.fixture.page['fields'] if f['name']=='settingDefaultReplyMode');field['optionsEnabled']=[False,True]
        self.window.receive_page(self.fixture.page);QtTest.QTest.qWait(30);selector=self.entry('settingDefaultReplyMode')['control'];selector.buttons[0].click()
        self.assertEqual(selector.currentIndex(),1);self.assertEqual(self.writes(),[])
    def test_optional_live_snapshot_preview_without_native_writes(self):
        snapshot=os.environ.get('LQUI_PROFILE_SNAPSHOT');output=os.environ.get('LQUI_PROFILE_PREVIEW')
        if not snapshot or not output:self.skipTest('Optional captured-page preview')
        self.fixture.page=json.loads(Path(snapshot).read_text());self.window.receive_page(self.fixture.page)
        self.window.resize(1380,1000);QtTest.QTest.qWait(120)
        self.assertIsInstance(self.entry('settingDefaultReplyMode')['control'],RadioChoice)
        self.window.grab().save(output);self.assertEqual(self.writes(),[])


if __name__=='__main__':unittest.main()
