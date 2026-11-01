#!/usr/bin/python3
import json, os, sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtWidgets,QtCore,QtTest
from main import SettingsWindow
from design import navigation_text

class Fixture:
    def __init__(self):self.calls=[];self.page={'ok':True,'fields':[
        {'id':'1','kind':'number','label':'液态强度','min':0,'max':100,'step':1,'value':70,'suffix':' %'},
        {'id':'2','kind':'boolean','label':'保持常驻','value':True,'help':'保持界面常驻，模型按需启动。'},
        {'id':'3','kind':'text','label':'翻译语言','value':'简体中文'},
        {'id':'4','kind':'choice','label':'模型','value':0,'options':['跟随配置','另一模型']},
        {'id':'5','kind':'action','label':'应用'},
        {'id':'6','kind':'multiline','label':'排除规则','value':'*.tmp'},
        {'id':'7','kind':'list','label':'支持格式','items':[{'text':'PDF','checkable':True,'checked':True,'selected':False}]},
        {'id':'8','kind':'tabs','label':'分类','value':0,'options':['一般','高级']},
    ]}
    def call(self,module,method,args,callback):
        self.calls.append((module,method,args))
        result={'ok':False} if method=='Catalog' else self.page
        QtCore.QTimer.singleShot(0,lambda:callback(result))
    def process(self,args,callback):QtCore.QTimer.singleShot(0,lambda:callback({'components':[{'id':'ai-station','name':'AI 小站','enabledState':'按需连接','startupState':'已启用','processes':[{'pid':123}],'pssKiB':4096}]}))

app=QtWidgets.QApplication([])
class UiTest(unittest.TestCase):
    def setUp(self):self.fixture=Fixture();self.window=SettingsWindow(self.fixture);self.window.open_page('ai:general');QtTest.QTest.qWait(50)
    def tearDown(self):self.window.close();self.window.deleteLater();QtTest.QTest.qWait(10)
    def test_integrated_titlebar_and_full_chinese_labels(self):
        self.assertTrue(self.window.windowFlags() & QtCore.Qt.FramelessWindowHint)
        for name in ('minimize','maximize','close'):self.assertIsNotNone(self.window.findChild(QtWidgets.QToolButton,'window_'+name))
        self.window.findChild(QtWidgets.QToolButton,'window_maximize').click();QtTest.QTest.qWait(30);self.assertTrue(self.window.isMaximized());self.window.findChild(QtWidgets.QToolButton,'window_maximize').click();QtTest.QTest.qWait(30);self.assertFalse(self.window.isMaximized())
        for label in ('文件管理','文档','文件管理与扩展'):self.assertEqual(navigation_text(label),label)
        self.assertEqual(navigation_text('▸ 文件管理与扩展'),'文件管理与扩展')
        self.assertEqual(navigation_text('文 输入法'),'输入法')
    def test_action_buttons_share_row_and_wrap(self):
        fields=[{'id':str(i),'kind':'action','label':text} for i,text in enumerate(['恢复默认','应用','保存并关闭','关闭（不保存预览）'])]
        self.window.render_fields(fields);self.window.resize(1040,740);QtTest.QTest.qWait(40)
        controls=[entry['control'] for entry in self.window.fields.values()]
        self.assertEqual(len(set(w.y() for w in controls)),1)
        self.window.resize(720,480);QtTest.QTest.qWait(40)
        controls[0].parentWidget().layout().setGeometry(QtCore.QRect(0,0,230,180));self.assertGreater(len(set(w.y() for w in controls)),1)
        for widget in controls:self.assertLessEqual(widget.geometry().right(),widget.parentWidget().width())
    def test_fields_and_values(self):
        self.assertEqual(len(self.window.fields),8)
        self.assertEqual(self.window.fields['1']['readout'].text(),'70 %')
        self.window.fields['2']['control'].click();QtTest.QTest.qWait(10)
        self.assertIn(('ai','Set',['general','2',json.dumps({'value':False},ensure_ascii=False)]),self.fixture.calls)
        self.window.fields['5']['control'].click();QtTest.QTest.qWait(20)
        self.assertIn(('ai','Activate',['general','5']),self.fixture.calls)
    def test_poll_does_not_destroy_editor(self):
        edit=self.window.fields['3']['control'];edit.setFocus();edit.selectAll();QtTest.QTest.keyClicks(edit,'English')
        self.window.receive_page(self.fixture.page)
        self.assertIs(self.window.fields['3']['control'],edit);self.assertEqual(edit.text(),'English')
    def test_page_change_preserves_maximized_window(self):
        self.window.showMaximized();QtTest.QTest.qWait(30);self.window.open_page('input:general');QtTest.QTest.qWait(30)
        self.assertTrue(self.window.isMaximized())
    def test_multiline_draft_survives_page_navigation(self):
        editor=self.window.fields['6']['control'];editor.setPlainText('pending rules');editor.document().setModified(True)
        self.window.open_page('input:general');QtTest.QTest.qWait(30);self.window.open_page('ai:general');QtTest.QTest.qWait(30)
        self.assertEqual(self.window.fields['6']['control'].toPlainText(),'pending rules')
    def test_narrow_layout(self):
        self.window.resize(720,480);QtTest.QTest.qWait(50)
        self.assertLessEqual(self.window.scroll.widget().width(),self.window.scroll.viewport().width())
        target=os.environ.get('LQUI_SETTINGS_ARTIFACT')
        if target:self.window.grab().save(target)
    def test_search(self):
        self.window.search.setText('翻译');QtTest.QTest.qWait(10)
        self.assertTrue(self.window.fields['3']['row'].isVisible());self.assertFalse(self.window.fields['1']['row'].isVisible())
    def test_overview_states(self):
        self.window.open_page('overview');QtTest.QTest.qWait(20)
        text=' '.join(x.text() for x in self.window.findChildren(QtWidgets.QLabel))
        self.assertIn('登录启动：已启用',text);self.assertIn('运行中',text)

class DelayedFixture(Fixture):
    def call(self,module,method,args,callback):
        self.calls.append((module,method,args))
        result={'ok':True,'pages':[{'id':'material','title':'液态外观'},{'id':'layout','title':'布局'}]} if method=='Catalog' and module=='panel' else {'ok':False} if method=='Catalog' else self.page
        QtCore.QTimer.singleShot(130 if method=='Catalog' else 30,lambda:callback(result))
    def process(self,args,callback):QtCore.QTimer.singleShot(220,lambda:callback({'components':[{'id':'fences','name':'桌面','processes':[]}]}))

class RoutingTest(unittest.TestCase):
    def test_direct_entry_while_overview_is_loading(self):
        window=SettingsWindow(DelayedFixture());window.open_page('panel:material');QtTest.QTest.qWait(300)
        self.assertFalse(window.busy);self.assertEqual(window.current,'panel:material');self.assertEqual(len(window.fields),8)
        self.assertNotIn('正在读取',' '.join(x.text() for x in window.scroll.findChildren(QtWidgets.QLabel)))
        window.close();window.deleteLater();QtTest.QTest.qWait(10)
    def test_out_of_order_responses_and_refresh(self):
        window=SettingsWindow(DelayedFixture())
        for key in ('fences:appearance','input:general','panel:layout','ai:general','panel:material'):window.open_page(key)
        QtTest.QTest.qWait(350);self.assertEqual(window.current,'panel:material');self.assertFalse(window.busy);self.assertEqual(len(window.fields),8)
        window.refresh(force=True);QtTest.QTest.qWait(80);self.assertFalse(window.busy)
        window.close();window.deleteLater();QtTest.QTest.qWait(10)
    def test_group_selects_real_page(self):
        window=SettingsWindow(DelayedFixture());QtTest.QTest.qWait(260)
        for i in range(window.navigation.count()):
            item=window.navigation.item(i)
            if item.data(QtCore.Qt.UserRole+2)=='panel':window.toggle_group(item);break
        QtTest.QTest.qWait(90);self.assertEqual(window.current,'panel:material');self.assertFalse(window.busy);self.assertEqual(len(window.fields),8)
        window.close();window.deleteLater();QtTest.QTest.qWait(10)

if __name__=='__main__':unittest.main()
