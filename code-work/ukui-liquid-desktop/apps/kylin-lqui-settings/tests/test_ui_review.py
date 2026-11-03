"""Review regressions: no real Backend, configuration, or native actions."""
import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
import sip
from main import SettingsWindow
from ui_components import DisclosureRow
from apple_fixture import Fixture

app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
app.setQuitOnLastWindowClosed(False)

class Review(unittest.TestCase):
    def setUp(self):
        self.fixture=Fixture();self.window=SettingsWindow(self.fixture,font_scale=1);self.window.poll.stop();self.open('fences:appearance')
    def tearDown(self):
        if not sip.isdeleted(self.window):
            self.window.confirm_discard=lambda:True;self.window.close();self.window.deleteLater();QtTest.QTest.qWait(10)
    def wait(self):
        for _ in range(200):
            if not self.window.busy:break
            QtTest.QTest.qWait(5)
        self.assertFalse(self.window.busy);QtTest.QTest.qWait(20)
    def open(self,key):self.window.open_page(key);self.wait()
    def fields(self,items,**extra):
        self.fixture.pages['ai:general']={'ok':True,'title':'合成回归','fields':items,**extra};self.open('ai:general')
    def writes(self):return [c for c in self.fixture.calls if c[1] in ('Set','Activate')]
    def test_search_survives_navigation_and_filters_restored_page(self):
        self.window.search.setText('玻璃');self.open('fences:activity');self.assertEqual(self.window.search.text(),'')
        self.open('fences:appearance');self.assertEqual(self.window.search.text(),'玻璃')
        self.assertFalse(self.window.fields['runtime-reduceMotion']['row'].isVisible());self.assertEqual(self.writes(),[])
    def test_focus_deferred_server_value_is_retried_on_identical_poll(self):
        self.fields([{'id':'line','name':'line','kind':'text','label':'文本','value':'A'}])
        editor=self.window.fields['line']['control'];self.window.activateWindow();QtTest.QTest.qWait(10);editor.setFocus();self.assertTrue(editor.hasFocus())
        data=copy.deepcopy(self.fixture.pages['ai:general']);data['fields'][0]['value']='B'
        self.window.receive_page(data);self.assertEqual(editor.text(),'A')
        self.window.search.setFocus();self.window.receive_page(data);self.assertEqual(editor.text(),'B');self.assertEqual(self.writes(),[])
    def test_deferred_list_update_is_retried_after_focus_leaves(self):
        self.fields([{'id':'list','name':'list','kind':'list','label':'列表','value':0,'items':[{'text':'旧项目'}]}])
        control=self.window.fields['list']['control'];self.window.activateWindow();QtTest.QTest.qWait(10);control.setFocus()
        data=copy.deepcopy(self.fixture.pages['ai:general']);data['fields'][0]['items']=[{'text':'新项目'}]
        self.window.receive_page(data);self.assertEqual(control.item(0).text(),'旧项目')
        self.window.search.setFocus();self.window.receive_page(data);self.assertEqual(control.item(0).text(),'新项目');self.assertEqual(self.writes(),[])
    def test_stale_action_reply_still_releases_its_button(self):
        pending=[];original=self.fixture.call
        def call(module,method,args,callback):
            if method=='Activate':pending.append(callback)
            else:original(module,method,args,callback)
        self.fixture.call=call;button=self.window.fields['runtime-refreshMaterialButton']['control'];button.click();self.assertFalse(button.isEnabled())
        toggle=self.window.fields['runtime-fenceLiquidGlass']['control'];toggle.click();self.wait();self.assertFalse(toggle.isChecked())
        pending[0]({'ok':False,'error':'应被丢弃的较早错误'})
        self.assertTrue(button.isEnabled());self.assertNotIn('较早错误',self.window.message.text());self.assertFalse(toggle.isChecked())
    def test_successful_normalization_can_restore_unchanged_server_value(self):
        snapshot=copy.deepcopy(self.fixture.pages['fences:appearance']);original=self.fixture.call
        def call(module,method,args,callback):
            if method=='Set':QtCore.QTimer.singleShot(0,lambda:callback(snapshot))
            else:original(module,method,args,callback)
        self.fixture.call=call;toggle=self.window.fields['runtime-fenceLiquidGlass']['control'];toggle.click();self.wait();self.assertTrue(toggle.isChecked())
    def test_old_list_instance_cannot_submit_using_reused_runtime_id(self):
        self.fields([{'id':'same','name':'list','kind':'list','label':'列表','value':0,'items':[{'text':'甲'}]}]);old=self.window.fields['same']['control']
        self.window.render_fields([{'id':'same','name':'replacement','kind':'text','label':'新文本','value':'B'}])
        self.window.send_selection('same',old,page_key='ai:general');self.assertEqual(self.writes(),[])
    def test_successful_empty_page_removes_previous_disabled_form(self):
        old=self.window.scroll.widget();self.fields([])
        self.assertIsNot(self.window.scroll.widget(),old);self.assertEqual(self.window.fields,{})
        self.assertIn('未提供',self.window.message.text());self.assertEqual(self.writes(),[])
    def test_editable_combo_uses_native_text_and_keeps_draft_selection(self):
        self.fields([{'id':'combo','name':'font','kind':'choice','label':'字体','editable':True,'value':0,'text':'自定义字体','options':['系统字体']}])
        combo=self.window.fields['combo']['control'];self.assertEqual(combo.currentText(),'自定义字体')
        self.window.activateWindow();QtTest.QTest.qWait(10);combo.setFocus();self.assertTrue(combo.hasFocus())
        combo.setEditText('待存😀字体');combo.lineEdit().setModified(True);combo.lineEdit().setSelection(2,2)
        self.open('fences:appearance');self.open('ai:general');combo=self.window.fields['combo']['control']
        self.assertEqual(combo.currentText(),'待存😀字体');self.assertTrue(combo.lineEdit().isModified());self.assertEqual(combo.lineEdit().selectedText(),'😀');self.assertEqual(self.writes(),[])
    def test_editable_combo_ack_clears_only_submitted_edit(self):
        self.fields([{'id':'combo','name':'font','kind':'choice','label':'字体','editable':True,'value':0,'text':'原字体','options':['系统字体']}])
        original=self.fixture.call;pending=[]
        def call(module,method,args,callback):
            if method=='Set':pending.append(callback)
            else:original(module,method,args,callback)
        self.fixture.call=call;combo=self.window.fields['combo']['control'];combo.setEditText('新字体');combo.lineEdit().setModified(True)
        self.window.mutate('combo','新字体',source=combo);result=copy.deepcopy(self.fixture.pages['ai:general']);result['fields'][0]['text']='新字体';pending.pop(0)(result)
        self.assertFalse(combo.lineEdit().isModified());self.assertEqual(combo.currentText(),'新字体')
        combo.setEditText('第二次提交');combo.lineEdit().setModified(True);self.window.mutate('combo','第二次提交',source=combo);combo.setEditText('提交后继续输入');combo.lineEdit().setModified(True)
        pending.pop(0)(result);self.assertTrue(combo.lineEdit().isModified());self.assertEqual(combo.currentText(),'提交后继续输入')
    def test_search_expansion_is_reapplied_after_structural_rebuild(self):
        self.open('fences:activity');self.window.search.setText('RSS')
        data=copy.deepcopy(self.fixture.pages['fences:activity']);data['fields'].append({'id':'new','name':'new','kind':'note','value':'额外说明'})
        self.window.receive_page(data);QtTest.QTest.qWait(20);row=self.window.scroll.findChildren(DisclosureRow)[0]
        self.assertTrue(row.content.isVisible());self.window.search.clear();self.assertFalse(row.content.isVisible())
    def test_unsupported_row_metadata_never_hides_an_input(self):
        self.fields([{'id':name,'name':name,'kind':'text','label':name,'value':name,'ui':{'group':'g','row':'shared'}} for name in ('first','second')],ui={'version':1,'groups':[{'key':'g','title':'分组'}]})
        first=self.window.fields['first'];second=self.window.fields['second'];self.assertIsNot(first['row'],second['row'])
        self.assertTrue(first['row'].isAncestorOf(first['control']));self.assertTrue(second['row'].isAncestorOf(second['control']));self.assertEqual(self.writes(),[])
    def test_right_button_slider_gesture_does_not_commit(self):
        self.open('fences:activity');slider=self.window.fields['runtime-activityMetricsInterval']['control'];QtTest.QTest.mouseClick(slider,QtCore.Qt.RightButton,pos=slider.rect().center());self.assertEqual(self.writes(),[])

    def test_input_method_commit_wins_over_queued_cursor_restore(self):
        self.fields([{'id':'line','name':'line','kind':'text','label':'文本','value':'A'}]);self.window.activateWindow();QtTest.QTest.qWait(10)
        editor=self.window.fields['line']['control'];editor.setFocus();editor.setCursorPosition(1);state=self.window.capture_view_state();self.window.restore_view_state(state)
        event=QtGui.QInputMethodEvent();event.setCommitString('中文');app.sendEvent(editor,event);QtTest.QTest.qWait(20)
        self.assertEqual(editor.text(),'A中文');self.assertEqual(editor.cursorPosition(),3);self.assertTrue(editor.isModified());self.assertEqual(self.writes(),[])
    def test_late_backend_replies_ignore_deleted_window(self):
        pending=[];self.fixture.call=lambda module,method,args,callback:pending.append(callback)
        self.window.load_catalogs();self.window.refresh(force=True);self.window.mutate('runtime-refreshMaterialButton',activate=True)
        self.window.deleteLater();QtTest.QTest.qWait(10);self.assertTrue(sip.isdeleted(self.window))
        for callback in pending:callback({'ok':False,'error':'迟到回复'})
    def test_invalid_description_owner_keeps_help_visible(self):
        self.fields([{'id':'action','name':'action','kind':'action','label':'动作','ui':{'group':'g'}}, {'id':'help','name':'help','kind':'note','value':'必须可见的说明','ui':{'group':'g','for':'action','role':'detail','collapsible':True}}],ui={'version':1,'groups':[{'key':'g','title':'分组'}]})
        self.assertTrue(self.window.fields['help']['control'].isVisible());self.assertEqual(self.writes(),[])

    def test_programmatic_disable_does_not_submit_focused_text(self):
        self.fields([{'id':'line','name':'line','kind':'text','label':'文本','value':'A'}]);self.window.activateWindow();QtTest.QTest.qWait(10)
        editor=self.window.fields['line']['control'];editor.setFocus();QtTest.QTest.keyClicks(editor,'B');self.assertTrue(editor.isModified())
        data=copy.deepcopy(self.fixture.pages['ai:general']);data['fields'][0]['enabled']=False;self.window.receive_page(data)
        self.assertEqual(self.writes(),[]);self.assertEqual(editor.text(),'AB');self.assertTrue(editor.isModified());self.assertFalse(editor.isEnabled())

    def test_pending_action_is_disabled_across_navigation_and_old_reply_releases_it(self):
        pending=[];original=self.fixture.call
        def call(module,method,args,callback):
            if method=='Activate':pending.append(callback)
            else:original(module,method,args,callback)
        self.fixture.call=call;self.window.fields['runtime-refreshMaterialButton']['control'].click()
        self.open('fences:activity');self.open('fences:appearance');current=self.window.fields['runtime-refreshMaterialButton']['control'];self.assertFalse(current.isEnabled())
        pending.pop()({'ok':True});self.assertTrue(current.isEnabled());self.assertFalse(self.window.busy)

if __name__=='__main__':unittest.main()
