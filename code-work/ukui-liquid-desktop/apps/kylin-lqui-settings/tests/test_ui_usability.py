"""Settings organization and rendering regressions; synthetic backend only."""
import copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
from main import SettingsWindow
from ui_schema import normalize_page,navigation_pages,plain_help,searchable_text
from apple_fixture import Fixture

app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
app.setQuitOnLastWindowClosed(False)

class Usability(unittest.TestCase):
    def setUp(self):
        self.backend=Fixture();self.window=SettingsWindow(self.backend,font_scale=1);self.window.poll.stop();QtTest.QTest.qWait(40)
    def tearDown(self):
        self.window.confirm_discard=lambda:True;self.window.close();self.window.deleteLater();QtTest.QTest.qWait(10)
    def item(self,key):return next(self.window.navigation.item(i) for i in range(self.window.navigation.count()) if self.window.navigation.item(i).data(QtCore.Qt.UserRole)==key)
    def test_tertiary_navigation_and_search_keep_parent_context(self):
        pages=[{'id':key,'title':title} for key,title in [('appearance','液态外观'),('layout','分区与布局'),('widgets','桌面小组件'),('monitor','系统监视'),('sync','文件同步'),('session','调试专用'),('fence:a','工作文件')]]
        self.window.receive_catalog('fences',{'ok':True,'pages':pages})
        self.window.collapsed.discard('fences');self.window.filter_search('')
        self.assertTrue(self.item('fences:fence:a').isHidden());self.assertTrue(self.item('fences:monitor').isHidden())
        self.window.toggle_group(self.item('fences:layout'));self.assertFalse(self.item('fences:fence:a').isHidden())
        self.assertEqual(self.item('fences:fence:a').data(QtCore.Qt.UserRole+4),2)
        self.window.search.setText('系统监视')
        for key in ('fences:__group__','fences:widgets','fences:monitor'):self.assertFalse(self.item(key).isHidden())
        self.window.search.clear();self.assertTrue(self.item('fences:monitor').isHidden())
        self.assertFalse(any(method in ('Set','Activate') for _,method,_ in self.backend.calls))
    def test_open_nested_page_expands_ancestors(self):
        self.window.receive_catalog('fences',{'ok':True,'pages':[{'id':x,'title':x} for x in ('widgets','activity','layout','fence:a')]})
        self.window.open_page('fences:activity');QtTest.QTest.qWait(30)
        self.assertNotIn('fences:widgets',self.window.collapsed)
        self.assertFalse(self.item('fences:activity').isHidden())
    def test_select_search_result_reapplies_its_empty_query_and_expands_parent(self):
        self.window.receive_catalog('fences',{'ok':True,'pages':[{'id':key,'title':title} for key,title in [('layout','分区与布局'),('widgets','桌面小组件'),('monitor','系统监视')]]})
        self.window.search.setText('系统监视');self.window.navigation.setCurrentItem(self.item('fences:monitor'));QtTest.QTest.qWait(30)
        self.assertEqual(self.window.search.text(),'');self.assertFalse(self.item('fences:layout').isHidden())
        self.assertFalse(self.item('fences:monitor').isHidden());self.assertNotIn('fences:widgets',self.window.collapsed)
    def test_stale_catalog_reply_cannot_remove_selected_child(self):
        pending=[];original=self.backend.call
        def call(module,method,args,callback):
            if method=='Catalog' and module=='fences':pending.append(callback)
            else:original(module,method,args,callback)
        self.backend.call=call;self.window.load_catalogs();self.window.load_catalogs()
        newer={'ok':True,'pages':[{'id':x,'title':x} for x in ('widgets','monitor')]}
        pending[1](newer);self.window.open_page('fences:monitor');QtTest.QTest.qWait(30)
        pending[0]({'ok':True,'pages':[{'id':'widgets','title':'widgets'}]})
        self.assertEqual(self.window.current,'fences:monitor');self.assertIs(self.window.navigation.currentItem(),self.item('fences:monitor'))
        self.window.receive_catalog('fences',{'ok':True,'pages':[{'id':'widgets','title':'widgets'}]});QtTest.QTest.qWait(30)
        self.assertEqual(self.window.current,'fences:widgets');self.assertIs(self.window.navigation.currentItem(),self.item('fences:widgets'))
    def test_late_catalog_expands_new_ancestors_without_undoing_user_collapse(self):
        self.window.open_page('fences:monitor');QtTest.QTest.qWait(20)
        catalog={'ok':True,'pages':[{'id':x,'title':x} for x in ('widgets','monitor')]}
        self.window.receive_catalog('fences',catalog)
        self.assertFalse(self.item('fences:monitor').isHidden());self.assertNotIn('fences:widgets',self.window.collapsed)
        self.window.toggle_group(self.item('fences:widgets'));self.assertTrue(self.item('fences:monitor').isHidden())
        self.window.receive_catalog('fences',catalog)
        self.assertTrue(self.item('fences:monitor').isHidden());self.assertIn('fences:widgets',self.window.collapsed)
    def test_rejected_catalog_fallback_keeps_navigation_and_private_draft(self):
        self.backend.pages['fences:general']={'ok':True,'fields':[{'id':'secret','name':'secret','kind':'text','label':'凭据','secret':True,'value':''}]}
        self.window.receive_catalog('fences',{'ok':True,'pages':[{'id':'general','title':'当前页'},{'id':'widgets','title':'小组件'}]})
        self.window.open_page('fences:general');QtTest.QTest.qWait(30)
        editor=self.window.fields['secret']['control'];editor.setText('unsubmitted fixture');editor.setModified(True)
        selected=self.item('fences:general');self.window.confirm_discard=lambda:False
        self.window.receive_catalog('fences',{'ok':True,'pages':[{'id':'widgets','title':'小组件'}]})
        self.assertEqual(self.window.current,'fences:general');self.assertIs(self.window.navigation.currentItem(),selected)
        self.assertEqual(editor.text(),'unsubmitted fixture');self.assertTrue(editor.isModified())
    def test_cancelled_parent_click_does_not_collapse_current_child(self):
        self.backend.pages['fences:monitor']={'ok':True,'fields':[{'id':'secret','name':'secret','kind':'text','label':'凭据','secret':True,'value':''}]}
        self.window.receive_catalog('fences',{'ok':True,'pages':[{'id':x,'title':x} for x in ('widgets','monitor')]})
        self.window.open_page('fences:monitor');QtTest.QTest.qWait(30)
        editor=self.window.fields['secret']['control'];editor.setText('private draft');editor.setModified(True)
        self.window.confirm_discard=lambda:False
        rect=self.window.navigation.visualItemRect(self.item('fences:widgets'))
        QtTest.QTest.mouseClick(self.window.navigation.viewport(),QtCore.Qt.LeftButton,pos=rect.center());QtTest.QTest.qWait(20)
        self.assertEqual(self.window.current,'fences:monitor');self.assertFalse(self.item('fences:monitor').isHidden())
        self.assertNotIn('fences:widgets',self.window.collapsed);self.assertTrue(editor.isModified())
    def test_draft_confirmation_focus_change_does_not_save_private_text(self):
        self.backend.pages['ai:general']={'ok':True,'fields':[{'id':'secret','name':'secret','kind':'text','label':'凭据','secret':True,'value':''}]}
        self.window.open_page('ai:general');QtTest.QTest.qWait(30);self.window.activateWindow();QtTest.QTest.qWait(10)
        editor=self.window.fields['secret']['control'];editor.setFocus();QtTest.QTest.keyClicks(editor,'fixture')
        QtCore.QTimer.singleShot(20,lambda:QtWidgets.QApplication.activeModalWidget().reject())
        self.window.open_page('fences:appearance');QtTest.QTest.qWait(30)
        self.assertEqual(self.window.current,'ai:general');self.assertTrue(editor.isModified())
        self.assertFalse(any(method=='Set' for _,method,_ in self.backend.calls))
    def test_help_markup_is_text_not_executable_or_repeated_heading(self):
        field={'id':'x','name':'x','kind':'text','label':'工作目录','value':'/example/<folder>','help':'<b>工作目录</b><br>下次连接生效 &amp; 保留历史<script>bad()</script>'}
        page=normalize_page({'fields':[field]});clean=page['fields'][0]
        self.assertEqual(clean['help'],'下次连接生效 & 保留历史');self.assertEqual(clean['value'],field['value'])
        self.assertNotIn('<br>',searchable_text(clean));self.assertEqual(plain_help('x < 2'), 'x < 2')
    def test_integration_has_plain_language_and_working_navigation(self):
        self.window.open_page('integration');QtTest.QTest.qWait(20)
        text=' '.join(w.text() for w in self.window.findChildren(QtWidgets.QLabel))
        self.assertIn('找不到系统设置',text);self.assertNotIn('NextKde',text)
        button=next(b for b in self.window.findChildren(QtWidgets.QPushButton) if b.text()=='查看输入法设置')
        button.click();QtTest.QTest.qWait(40);self.assertEqual(self.window.current,'input:general')
    def test_focused_slider_has_no_outer_blue_frame_and_keeps_keyboard(self):
        self.window.open_page('fences:activity');QtTest.QTest.qWait(50);self.window.activateWindow()
        slider=self.window.fields['runtime-activityMetricsInterval']['control'];slider.setFocus();QtTest.QTest.qWait(20)
        before=slider.value();QtTest.QTest.keyClick(slider,QtCore.Qt.Key_Right);QtTest.QTest.qWait(30)
        self.assertGreater(slider.value(),before)
        writes=[call for call in self.backend.calls if call[1]=='Set'];self.assertEqual(len(writes),1)
        image=slider.grab().toImage()
        edge=[image.pixelColor(x,y) for x in range(image.width()) for y in (0,image.height()-1)]
        # The blue filled track legitimately reaches the left/right edge;
        # the regression is the enclosing top/bottom rectangular frame.
        self.assertFalse(any(c.blue()>180 and c.red()<80 and 70<c.green()<190 for c in edge))

if __name__=='__main__':unittest.main()
