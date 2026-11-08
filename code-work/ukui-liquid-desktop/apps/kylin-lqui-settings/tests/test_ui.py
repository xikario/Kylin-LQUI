#!/usr/bin/python3
import json, os, sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtWidgets,QtCore,QtTest
from main import SettingsWindow
from design import navigation_text

class Fixture:
    def __init__(self):
        self.calls=[];self.page={'ok':True,'fields':[
        {'id':'1','kind':'number','label':'液态强度','min':0,'max':100,'step':1,'value':70,'suffix':' %'},
        {'id':'2','kind':'boolean','label':'保持常驻','value':True,'help':'保持界面常驻，模型按需启动。'},
        {'id':'3','kind':'text','label':'翻译语言','value':'简体中文'},
        {'id':'4','kind':'choice','label':'模型','value':0,'options':['跟随配置','另一模型']},
        {'id':'5','kind':'action','label':'应用'},
        {'id':'6','kind':'multiline','label':'排除规则','value':'*.tmp'},
        {'id':'7','kind':'list','label':'支持格式','items':[{'text':'PDF','checkable':True,'checked':True,'selected':False}]},
        {'id':'8','kind':'tabs','label':'分类','value':0,'options':['一般','高级']},
    ]};
        for field in self.page['fields']:field['name']='fixture_'+field['id']
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
    def test_page_actions_stay_at_top_while_scrolling_and_searching(self):
        fields=[{'id':str(i),'name':'row_'+str(i),'kind':'text','label':'选项 '+str(i),'value':'内容'} for i in range(30)]
        fields.extend([{'id':'saved','name':'settingsNote','kind':'note','label':'settingsNote','value':'已应用并保存。'},
            {'id':'reset','name':'resetDefaults','kind':'action','label':'恢复默认'},
            {'id':'clear','name':'clearConversation','kind':'action','label':'清空聊天记录并开始新对话…'},
            {'id':'apply','name':'applySettings','kind':'action','label':'应用'}])
        self.fixture.page={'ok':True,'fields':fields};self.window.render_fields(fields);self.window.resize(720,480);QtTest.QTest.qWait(50)
        self.window.filter_search('');button=self.window.fields['apply']['control']
        self.assertTrue(self.window.page_actions.isAncestorOf(button));self.assertTrue(self.window.page_actions.isAncestorOf(self.window.fields['saved']['control']))
        before=button.mapTo(self.window,QtCore.QPoint());bar=self.window.scroll.verticalScrollBar();self.assertGreater(bar.maximum(),0)
        bar.setValue(bar.maximum());QtTest.QTest.qWait(20);self.assertEqual(before,button.mapTo(self.window,QtCore.QPoint()))
        self.assertLess(before.y()+button.height(),self.window.scroll.mapTo(self.window,QtCore.QPoint()).y())
        self.window.capture_view_state();self.window.search.setText('找不到的选项');QtTest.QTest.qWait(20)
        self.assertTrue(button.isVisible());self.assertTrue(self.window.no_results.isVisible())
        button.click();QtTest.QTest.qWait(30);self.assertEqual(sum(call==('ai','Activate',['general','apply']) for call in self.fixture.calls),1)
        target=os.environ.get('LQUI_PINNED_ACTIONS_ARTIFACT')
        if target:self.window.search.clear();QtTest.QTest.qWait(20);bar.setValue(bar.maximum());self.window.grab().save(target)
        self.window.open_page('overview');QtTest.QTest.qWait(30);self.assertTrue(self.window.page_actions.isHidden())
    def test_contextual_picker_and_model_refresh_stay_with_fields(self):
        fields=[{'id':'refresh','kind':'action','label':'刷新','name':'refreshModels'},
            {'id':'choose','kind':'action','label':'选择图片','name':'chooseWallpaper'},
            {'id':'value','kind':'text','label':'目录','value':'/'},
            {'id':'apply','kind':'action','label':'应用'}]
        self.window.render_fields(fields);QtTest.QTest.qWait(20)
        for fid in ('refresh','choose'):self.assertTrue(self.window.scroll.isAncestorOf(self.window.fields[fid]['control']))
        self.assertTrue(self.window.page_actions.isAncestorOf(self.window.fields['apply']['control']))
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

    def test_late_save_acknowledgement_preserves_new_text(self):
        callbacks=[];original=self.fixture.call
        def call(module,method,args,callback):
            if method=='Set':callbacks.append(callback)
            else:original(module,method,args,callback)
        self.fixture.call=call
        edit=self.window.fields['6']['control'];edit.setPlainText('first save');edit.document().setModified(True)
        self.window.mutate('6','first save');self.assertEqual(len(callbacks),1)
        edit.setPlainText('NEW UNSAVED EDIT');edit.document().setModified(True)
        callbacks[0](self.fixture.page)
        self.assertEqual(edit.toPlainText(),'NEW UNSAVED EDIT');self.assertTrue(edit.document().isModified());self.assertFalse(self.window.busy)


    def test_save_acknowledgement_cannot_replace_a_recreated_editor(self):
        callbacks=[];self.fixture.call=lambda module,method,args,callback:callbacks.append(callback)
        self.window.mutate('6','old submission');self.window.render_fields(self.fixture.page['fields'])
        current=self.window.fields['6']['control'];current.setPlainText('new editor draft');current.document().setModified(True)
        callbacks[0](self.fixture.page)
        self.assertEqual(current.toPlainText(),'new editor draft');self.assertTrue(current.document().isModified())

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
