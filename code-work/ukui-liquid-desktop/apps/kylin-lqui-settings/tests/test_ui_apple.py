#!/usr/bin/python3
import base64,copy,json,os,sys,unittest
from pathlib import Path
from unittest.mock import patch,Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets,QtTest
from main import SettingsWindow
from design import DesktopPreview
from ui_components import SettingRow,DisclosureRow,PluginRow,WallpaperPreview
from apple_fixture import Fixture,NAMES
app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
app.setFont(QtGui.QFont('Noto Sans CJK SC',10));app.setQuitOnLastWindowClosed(False)

class CompactUi(unittest.TestCase):
    def setUp(self):
        self.fixture=Fixture();self.window=SettingsWindow(self.fixture,font_scale=1);self.window.poll.stop();self.open('fences:appearance')
    def tearDown(self):
        self.window.confirm_discard=lambda:True;self.window.close();self.window.deleteLater();QtTest.QTest.qWait(10)
    def wait(self):
        clock=QtCore.QElapsedTimer();clock.start()
        while self.window.busy and clock.elapsed()<2000:QtTest.QTest.qWait(2)
        self.assertFalse(self.window.busy);QtTest.QTest.qWait(20)
    def open(self,key):self.window.open_page(key);self.wait()
    def entry(self,name):return next(e for e in self.window.fields.values() if e['field'].get('name')==name)
    def writes(self):return [v for v in self.fixture.calls if v[1] in ('Set','Activate')]
    def test_appearance_is_compact_without_decorative_hero(self):
        self.assertFalse(self.window.findChildren(DesktopPreview));self.assertEqual(len(self.window.groups),3)
        self.assertEqual(len([r for r in self.window._rendered_rows if isinstance(r['widget'],SettingRow)]),3)
        action=self.entry('refreshMaterialButton')['control'];self.assertLessEqual(action.mapTo(self.window.scroll.viewport(),QtCore.QPoint(0,action.height())).y(),self.window.scroll.viewport().height())
        self.assertFalse(self.window.groups[-1].surface.property('card'))
    def test_checkbox_space_uses_runtime_id_once(self):
        control=self.entry('fenceLiquidGlass')['control'];control.setFocus();QtTest.QTest.keyClick(control,QtCore.Qt.Key_Space);self.wait()
        calls=self.writes();self.assertEqual(len(calls),1);self.assertEqual(calls[0][2][1],'runtime-fenceLiquidGlass');self.assertFalse(json.loads(calls[0][2][2])['value'])
    def test_action_enter_and_long_caption_wrap(self):
        button=self.entry('refreshMaterialButton')['control'];button.setFocus();QtTest.QTest.keyClick(button,QtCore.Qt.Key_Return);self.wait();self.assertEqual(len(self.writes()),1)
        self.window.render_fields([{'id':'long','name':'longAction','kind':'action','label':'用于验证长中文和长翻译文本在窄窗口中能够完整换行的操作按钮'}]);self.window.resize(720,480);QtTest.QTest.qWait(30)
        button=self.window.fields['long']['control'];self.assertLessEqual(button.width(),button.parentWidget().width());self.assertGreater(button.height(),32)
    def test_help_delay_and_destroy_cancels_target(self):
        button=QtWidgets.QPushButton('临时控件',self.window);button.show();self.window.help.install(button,'补充说明')
        self.window.help.target=button;self.window.help.timer.start(1000);QtTest.QTest.qWait(30);self.assertFalse(self.window.help.bubble.isVisible())
        button.deleteLater();QtTest.QTest.qWait(20);self.assertIsNone(self.window.help.target);self.assertFalse(self.window.help.timer.isActive());self.assertFalse(self.window.help.bubble.isVisible())
    def test_render_poll_resize_and_search_do_not_write(self):
        for key in NAMES.values():
            self.open(key);self.window.resize(720,480);self.window.resize(1040,740);self.window.receive_page(copy.deepcopy(self.fixture.pages[key]));self.window.search.setText('不存在的词');self.window.search.clear();QtTest.QTest.qWait(10)
        self.assertEqual(self.writes(),[])
    def test_activity_single_readout_and_step_callback(self):
        self.open('fences:activity');entry=self.entry('activityMetricsInterval');self.assertEqual(entry['readout'].text(),'10 秒')
        self.assertEqual(sum(l.text()=='采样间隔' for l in self.window.scroll.findChildren(QtWidgets.QLabel) if l.isVisible()),1)
        entry['control'].setFocus();QtTest.QTest.keyClick(entry['control'],QtCore.Qt.Key_Right);self.wait()
        calls=self.writes();self.assertEqual(len(calls),1);self.assertEqual(json.loads(calls[0][2][2])['value'],15)
    def test_interval_disabled_preserves_value_and_retention(self):
        self.open('fences:activity');self.entry('activityMetricsEnabled')['control'].click();self.wait()
        self.assertFalse(self.entry('activityMetricsInterval')['control'].isEnabled());self.assertEqual(self.entry('activityMetricsInterval')['readout'].text(),'10 秒');self.assertTrue(self.entry('activityLogRetention')['control'].isEnabled())
    def test_metadata_content_change_keeps_widgets_and_geometry(self):
        self.open('fences:wallpaper');preview=self.entry('wallpaperPreview')['control'];path=self.entry('wallpaperImagePath')['control'];before=self.entry('wallpaperChooseImage')['control'].geometry()
        data=copy.deepcopy(self.fixture.pages['fences:wallpaper']);next(f for f in data['fields'] if f['name']=='wallpaperPreview')['value']='';self.window.receive_page(data);QtTest.QTest.qWait(20)
        self.assertIs(preview,self.entry('wallpaperPreview')['control']);self.assertIs(path,self.entry('wallpaperImagePath')['control']);self.assertEqual(before,self.entry('wallpaperChooseImage')['control'].geometry())
    def test_wallpaper_cancel_draft_apply_and_failure(self):
        self.open('fences:wallpaper');self.fixture.cancel_selection=True;self.entry('wallpaperChooseImage')['control'].click();self.wait();self.assertFalse(self.fixture.pages['fences:wallpaper']['dirty'])
        self.fixture.cancel_selection=False;self.entry('wallpaperChooseImage')['control'].click();self.wait();self.assertTrue(self.fixture.pages['fences:wallpaper']['dirty'])
        self.assertFalse(any(args[1]=='runtime-wallpaperApply' for _,method,args in self.writes()))
        self.fixture.fail_apply=True;self.entry('wallpaperApply')['control'].click();self.wait();self.assertTrue(self.fixture.pages['fences:wallpaper']['dirty']);self.assertIn('失败',self.window.message.text())
    def test_video_warning_fallback_and_separate_theme_semantics(self):
        self.open('fences:wallpaper');self.window.mutate('runtime-wallpaperKind',1);self.wait()
        self.assertTrue(self.entry('videoWallpaperExperimentalNotice')['control'].isVisible());self.assertFalse(self.entry('wallpaperChooseImage')['control'].isVisible())
        disclosure=self.entry('wallpaperImagePath')['disclosure'];self.assertFalse(disclosure.user_expanded);self.assertFalse(disclosure.content.isVisible())
        self.assertTrue(self.entry('videoWallpaperPath')['control'].isEnabled());self.assertIn('待应用',self.window.save_mode.text());self.assertIn('配色',self.window.description.text())
    def test_search_no_results_removes_all_empty_groups(self):
        self.window.search.setText('不存在的词');QtTest.QTest.qWait(10)
        self.assertTrue(self.window.no_results.isVisible());self.assertTrue(all(g.isHidden() for g in self.window.groups))
        self.window.search.clear();self.assertFalse(self.window.no_results.isVisible());self.assertTrue(any(g.isVisible() for g in self.window.groups))
    def test_search_in_collapsed_details_restores_expansion(self):
        self.open('fences:activity');row=next(r['widget'] for r in self.window._rendered_rows if isinstance(r['widget'],DisclosureRow))
        self.assertFalse(row.content.isVisible());self.window.search.setText('RSS');QtTest.QTest.qWait(10);self.assertTrue(row.content.isVisible());self.window.search.clear();self.assertFalse(row.content.isVisible())
    def test_plugin_unknown_uninstalled_and_search(self):
        self.open('peony:plugins');rows=[r['widget'] for r in self.window._rendered_rows if isinstance(r['widget'],PluginRow)];self.assertEqual(len(rows),5)
        self.assertNotIn('runtime-plugin:example-unknown.so',self.window.fields);self.assertNotIn('runtime-plugin:example-missing.so',self.window.fields)
        self.assertTrue(any(r.notice.isVisible() for r in rows));long=next(r for r in rows if '长中文' in r.caption)
        self.window.search.setText('较长的示例说明');self.assertTrue(long.content.isVisible());self.window.search.clear();self.assertFalse(long.content.isVisible())
    def test_plugin_refresh_preserves_expansion_and_selection(self):
        self.open('peony:plugins');row=next(r['widget'] for r in self.window._rendered_rows if r['spec'].get('entity')=='example-copy.so');row.set_expanded(True)
        self.window.receive_page(copy.deepcopy(self.fixture.pages['peony:plugins']));self.assertTrue(row.content.isVisible());self.assertEqual(self.writes(),[])
    def test_remove_cancel_and_install_failure(self):
        self.open('peony:plugins')
        with patch.object(QtWidgets.QMessageBox,'question',return_value=QtWidgets.QMessageBox.No):self.entry('remove-copy')['control'].click()
        self.assertEqual(self.writes(),[]);self.fixture.fail_actions=True;self.entry('install-copy')['control'].click();self.wait();self.assertIn('失败',self.window.message.text());self.assertTrue(self.entry('install-copy')['control'].isEnabled())
    def test_default_file_manager_is_single_column(self):
        self.open('peony:general');self.window.resize(1040,740);QtTest.QTest.qWait(20);self.assertTrue(all(g.columns==1 for g in self.window.groups));self.assertEqual(len([e for e in self.window.fields.values() if e['field']['kind']=='boolean']),6)
    def test_font_scale_changes_actual_metrics(self):
        before=self.entry('fenceLiquidGlass')['label'].fontMetrics().height();font=QtGui.QFont(app.font());larger=QtGui.QFont(font);larger.setPointSizeF(font.pointSizeF()*1.25)
        try:
            app.setFont(larger);QtTest.QTest.qWait(30);after=self.entry('fenceLiquidGlass')['label'].fontMetrics().height();self.assertGreater(after,before);self.assertGreater(self.entry('fenceLiquidGlass')['label'].font().pixelSize(),14)
        finally:app.setFont(font);QtTest.QTest.qWait(20)
    def test_narrow_and_large_font_controls_stay_in_viewport(self):
        self.window._explicit_font_scale=1.25;self.window.ui_scale=1.25;self.window.apply_ui_style();self.window.resize(720,480)
        for key in NAMES.values():
            self.open(key);self.window.reflow();QtTest.QTest.qWait(30);canvas=self.window.scroll.widget()
            self.assertLessEqual(canvas.width(),self.window.scroll.viewport().width())
            for entry in self.window.fields.values():
                control=entry['control']
                if control.isVisible():self.assertLessEqual(control.mapTo(canvas,QtCore.QPoint(control.width(),0)).x(),canvas.width()+2,(key,entry['field']['name']))
    def test_preview_bounds_cache_and_invalid_data(self):
        value=next(f['value'] for f in self.fixture.pages['fences:wallpaper']['fields'] if f['kind']=='preview');preview=WallpaperPreview(value);self.assertEqual(preview.decode_count,1);preview.set_data(value);self.assertEqual(preview.decode_count,1)
        preview.set_data('x'*(3*1024*1024));self.assertTrue(preview.canvas.image.isNull());self.assertIn('不可用',preview.status.text());preview.set_data('x'*(3*1024*1024));self.assertIn('不可用',preview.status.text())
        preview.deleteLater()
    def test_legacy_unnamed_draft_cancels_navigation_without_discard(self):
        self.fixture.pages['ai:general']={'ok':True,'fields':[{'id':'old','kind':'multiline','label':'旧协议','value':'原值'}]};self.open('ai:general');editor=self.window.fields['old']['control'];editor.setPlainText('draft');editor.document().setModified(True)
        with patch.object(self.window,'confirm_discard',return_value=False) as confirm:
            self.window.open_page('fences:appearance');self.assertEqual(self.window.current,'ai:general');self.assertIs(self.window.fields['old']['control'],editor);self.assertEqual(editor.toPlainText(),'draft');confirm.assert_called_once()
        self.assertFalse(any(m=='Discard' for _,m,_ in self.fixture.calls))
    def test_duplicate_names_and_structural_refresh_keep_instance(self):
        page={'ok':True,'fields':[{'id':'a','name':'duplicate','kind':'multiline','label':'第一项','value':'A'},{'id':'b','name':'duplicate','kind':'text','label':'第二项','value':'B'}]};self.fixture.pages['ai:general']=page;self.open('ai:general');editor=self.window.fields['a']['control'];editor.setPlainText('draft');editor.document().setModified(True);self.window.search.setFocus()
        changed=copy.deepcopy(page);changed['fields'].append({'id':'c','kind':'note','label':'说明','value':'新说明'})
        with patch.object(self.window,'confirm_discard',return_value=False) as confirm:
            self.window.receive_page(changed);self.window.receive_page(changed);self.assertIs(editor,self.window.fields['a']['control']);self.assertEqual(confirm.call_count,1)
    def test_secret_not_in_search_or_saved_state(self):
        self.fixture.pages['ai:general']={'ok':True,'fields':[{'id':'secret','name':'credential','kind':'text','label':'凭据','secret':True,'value':'server-secret'}]};self.open('ai:general');editor=self.window.fields['secret']['control'];editor.setText('typed-secret');editor.setModified(True)
        self.window.search.setText('typed-secret');self.assertTrue(self.window.no_results.isVisible());self.assertEqual(self.writes(),[])
        self.assertNotIn('typed-secret',repr(self.window.capture_view_state()['fields']));self.assertNotIn('server-secret',repr(self.window.page_cache));editor.setModified(False)
    def test_stable_rebuild_keeps_text_cursor_and_never_submits(self):
        self.fixture.pages['ai:general']={'ok':True,'fields':[{'id':'a','name':'rules','kind':'multiline','label':'规则','value':'original'}]};self.open('ai:general');editor=self.window.fields['a']['control'];editor.setPlainText('pending rules');editor.document().setModified(True);editor.setFocus();self.window.search.setFocus()
        changed=copy.deepcopy(self.fixture.pages['ai:general']);changed['fields'].append({'id':'b','name':'new','kind':'note','label':'说明','value':'新增说明'});self.window.receive_page(changed);QtTest.QTest.qWait(20)
        current=self.window.fields['a']['control'];self.assertIsNot(current,editor);self.assertEqual(current.toPlainText(),'pending rules');self.assertTrue(current.document().isModified());self.assertEqual(self.writes(),[])

    def test_slider_release_once_and_cancellation_never_commits(self):
        self.open('fences:activity');slider=self.entry('activityMetricsInterval')['control'];slider.setFocus();QtTest.QTest.qWait(10)
        start=slider.rect().center();end=QtCore.QPoint(slider.width()-10,start.y())
        QtTest.QTest.mousePress(slider,QtCore.Qt.LeftButton,pos=start);QtTest.QTest.mouseMove(slider,end);QtTest.QTest.mouseRelease(slider,QtCore.Qt.LeftButton,pos=end);self.wait()
        self.assertEqual(len(self.writes()),1)
        for cancel in ('hide','disable'):
            slider.pointer=True;slider.setSliderDown(True);before=len(self.writes())
            if cancel=='hide':slider.hide();slider.show()
            else:slider.setEnabled(False);slider.setEnabled(True)
            self.assertFalse(slider.isSliderDown());self.assertFalse(slider.pointer);self.assertEqual(len(self.writes()),before)
    def test_open_choice_poll_keeps_keyboard_selection(self):
        self.open('fences:activity');combo=self.entry('activityLogRetention')['control'];combo.showPopup();QtTest.QTest.qWait(10)
        before=combo.currentIndex();combo.setCurrentIndex((before+1)%combo.count());pending=combo.currentIndex()
        self.window.receive_page(copy.deepcopy(self.fixture.pages['fences:activity']));self.assertEqual(combo.currentIndex(),pending)
        combo.hidePopup();self.assertEqual(self.writes(),[])
        combo.setFocus();QtTest.QTest.keyClick(combo,QtCore.Qt.Key_Down);self.wait();self.assertEqual(len(self.writes()),1)
    def test_unknown_kind_is_visible_with_description(self):
        self.window.render_fields([{'id':'future','name':'future','kind':'future-control','label':'未来设置','help':'保留这段独立说明','value':'opaque'}]);QtTest.QTest.qWait(10)
        self.assertIn('暂不支持',self.window.fields['future']['control'].text());self.assertTrue(self.window.fields['future']['row'].isVisible())
        self.window.search.setText('独立说明');self.assertTrue(self.window.fields['future']['row'].isVisible());self.assertEqual(self.writes(),[])
    def test_empty_plugins_still_shows_maintenance(self):
        page=self.fixture.pages['peony:plugins'];page['fields']=[f for f in page['fields'] if not f.get('ui',{}).get('entity')];self.open('peony:plugins')
        self.assertFalse(any(isinstance(r['widget'],PluginRow) for r in self.window._rendered_rows));self.assertTrue(self.entry('install-copy')['control'].isVisible());self.assertEqual(self.writes(),[])
    def test_emoji_selection_scroll_and_list_survive_rebuild(self):
        page={'ok':True,'fields':[{'id':'line','name':'line','kind':'text','label':'文字','value':'A😀B表情'}, {'id':'list','name':'list','kind':'list','label':'选择','multiple':True,'items':[{'text':'甲'},{'text':'乙'}]}]+[{'id':'n'+str(i),'name':'n'+str(i),'kind':'note','label':'说明','value':'较长的说明，用于检查滚动位置。'*3} for i in range(12)]}
        self.fixture.pages['ai:general']=page;self.open('ai:general');self.window.resize(720,480);QtTest.QTest.qWait(20)
        editor=self.window.fields['line']['control'];editor.setSelection(1,2);items=self.window.fields['list']['control'];items.blockSignals(True);items.item(1).setSelected(True);items.setCurrentItem(items.item(1),QtCore.QItemSelectionModel.NoUpdate);items.blockSignals(False)
        bar=self.window.scroll.verticalScrollBar();bar.setValue(220);before=bar.value();changed=copy.deepcopy(page);changed['fields'].append({'id':'new','name':'new','kind':'note','value':'新增'})
        self.window.receive_page(changed);QtTest.QTest.qWait(30);self.assertEqual(self.window.fields['line']['control'].selectedText(),'😀');self.assertEqual(self.window.fields['list']['control'].currentItem().text(),'乙');self.assertEqual([i.text() for i in self.window.fields['list']['control'].selectedItems()],['乙']);self.assertAlmostEqual(bar.value(),before,delta=2);self.assertEqual(self.writes(),[])
    def test_queued_state_restore_yields_to_new_user_input(self):
        self.window.activateWindow();QtTest.QTest.qWait(10);state=self.window.capture_view_state();state['focus']=self.entry('fenceLiquidGlass')['field']['_key'];self.window.restore_view_state(state)
        self.window.search.setFocus();QtTest.QTest.keyClicks(self.window.search,'abc');QtTest.QTest.qWait(20)
        self.assertIs(QtWidgets.QApplication.focusWidget(),self.window.search);self.assertEqual(self.window.search.text(),'abc');self.assertEqual(self.writes(),[])

    def test_unchanged_poll_keeps_layout_and_preview_cache(self):
        self.open('fences:wallpaper');group=self.window.groups[0];signature=group._layout_signature;preview=self.entry('wallpaperPreview')['control']
        for _ in range(8):self.window.receive_page(copy.deepcopy(self.fixture.pages['fences:wallpaper']))
        self.assertIs(group._layout_signature,signature);self.assertEqual(preview.decode_count,1);self.assertEqual(self.writes(),[])
    def test_slow_route_disables_old_form_and_stale_click_cannot_write(self):
        old=self.entry('fenceLiquidGlass')['control'];self.fixture.delay=160;self.window.open_page('fences:activity')
        self.assertFalse(old.isEnabled());old.click();self.assertEqual(self.writes(),[])
        QtTest.QTest.qWait(100);self.assertIn('正在读取',self.window.scroll.widget().text());self.wait();self.assertEqual(self.window.current,'fences:activity');self.assertTrue(self.entry('activityMetricsEnabled')['control'].isEnabled())
    def test_tooltip_teardown_handles_deleted_top_level(self):
        self.window.help.bubble.deleteLater();QtTest.QTest.qWait(10);self.window.help.cancel()

if __name__=='__main__':unittest.main()
