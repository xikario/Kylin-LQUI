"""UKUI/BlueZ/MPRIS adapters for the unified control cards."""
import json,os,time
from pathlib import Path
from gi.repository import Gio,GLib

class DesktopControls:
    def session_call(self,service,path,interface,method,args=None,timeout=3000):
        bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
        return bus.call_sync(service,path,interface,method,args,None,Gio.DBusCallFlags.NO_AUTO_START,timeout,None).unpack()
    def session_props(self,service,path,interface):return self.session_call(service,path,'org.freedesktop.DBus.Properties','GetAll',GLib.Variant('(s)',(interface,)))[0]
    def bluetooth(self):
        objects=self.call('org.bluez','/','org.freedesktop.DBus.ObjectManager','GetManagedObjects')[0]
        adapters=[(path,info['org.bluez.Adapter1']) for path,info in objects.items() if 'org.bluez.Adapter1' in info]
        if not adapters:return {'available':False,'devices':[]}
        path,adapter=next((item for item in adapters if item[1].get('Powered')),adapters[0]);devices=[]
        for device,interfaces in objects.items():
            item=interfaces.get('org.bluez.Device1')
            if item and item.get('Adapter')==path:devices.append({'id':device,'name':item.get('Alias') or item.get('Name') or item.get('Address'),'paired':bool(item.get('Paired')),'connected':bool(item.get('Connected'))})
        return {'available':True,'adapter':path,'enabled':bool(adapter.get('Powered')),'discovering':bool(adapter.get('Discovering')),'devices':sorted(devices,key=lambda d:(not d['connected'],not d['paired'],d['name']))[:30]}
    def brightness(self):
        # Use the supported user-session display service. The hardware daemon
        # is restricted on this OEM, and is not bypassed through privileged helpers.
        value=int(self.session_call('org.ukui.SettingsDaemon','/org/ukui/SettingsDaemon/GammaManager','org.ukui.SettingsDaemon.GammaManager','getPrimaryBrightness')[0])
        return {'available':0<=value<=100,'value':max(0,min(100,value)),'backend':'ukui-display'}
    def notifications(self):
        schema=Gio.SettingsSchemaSource.get_default().lookup('org.ukui.quick-operation.panel',True)
        dnd=Gio.Settings.new_full(schema,None,None).get_boolean('disturbmodestatus') if schema else False
        try:entries=json.loads(self.session_call('org.ukui.AiStationPanel','/org/kylin/Lqui/Notifications','org.kylin.Lqui.Notifications','List')[0])
        except (GLib.Error,ValueError):entries=[]
        try:count=int(self.session_call('org.ukui.Sidebar','/org/ukui/Sidebar/notification','org.ukui.Sidebar.notification','getNotificationNum')[0])
        except GLib.Error:count=0
        return {'available':True,'dndAvailable':bool(schema),'dnd':dnd,'entries':list(reversed(entries)),'systemCount':count}
    def media(self):
        names=self.session_call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','ListNames')[0];players=[]
        for name in [n for n in names if n.startswith('org.mpris.MediaPlayer2.')][:8]:
            try:
                item=self.session_props(name,'/org/mpris/MediaPlayer2','org.mpris.MediaPlayer2.Player');meta=item.get('Metadata',{})
                players.append({'service':name,'title':str(meta.get('xesam:title','暂无曲目信息')),'artist':' / '.join(meta.get('xesam:artist',[])),'art':str(meta.get('mpris:artUrl','')),'playing':item.get('PlaybackStatus')=='Playing','canControl':bool(item.get('CanControl')),'canPrevious':bool(item.get('CanGoPrevious')),'canNext':bool(item.get('CanGoNext'))})
            except GLib.Error:continue
        return {'available':bool(players),'player':next((p for p in players if p['playing']),players[0] if players else {}),'players':players}
    def session(self):
        result={}
        for key,method in [('lock','canLockscreen'),('suspend','canSuspend'),('hibernate','canHibernate'),('logout','canLogout'),('reboot','canReboot'),('poweroff','canPowerOff')]:
            try:result[key]=bool(self.session_call('org.gnome.SessionManager','/org/gnome/SessionManager','org.gnome.SessionManager',method)[0])
            except GLib.Error:result[key]=False
        return {'available':any(result.values()),'actions':result}
    def desktop_action(self,operation,values):
        if operation=='brightness':
            value=values.get('value')
            if type(value) is not int or not 5<=value<=100:raise ValueError('亮度应在 5 到 100 之间')
            state=self.brightness()
            if not state['available']:raise ValueError('此显示器暂不支持亮度调节')
            result=self.session_call('org.ukui.SettingsDaemon','/org/ukui/SettingsDaemon/GammaManager','org.ukui.SettingsDaemon.GammaManager','setPrimaryBrightness',GLib.Variant('(si)',('Kylin-LQUI',value)))
            if result and result[0]<0:raise ValueError('系统显示服务未接受此亮度')
        elif operation=='bluetooth-enabled':
            state=self.bluetooth()
            if not state['available'] or type(values.get('value')) is not bool:raise ValueError('蓝牙适配器未就绪')
            self.set_property('org.bluez',state['adapter'],'org.bluez.Adapter1','Powered',GLib.Variant('b',values['value']))
        elif operation=='bluetooth-device':
            state=self.bluetooth();device=next((d for d in state['devices'] if d['id']==values.get('id')),None)
            if not device:raise ValueError('蓝牙设备已变化，请刷新')
            if not device['paired']:self.call('org.bluez',device['id'],'org.bluez.Device1','Pair',timeout=30000)
            self.call('org.bluez',device['id'],'org.bluez.Device1','Disconnect' if device['connected'] else 'Connect',timeout=20000)
        elif operation=='bluetooth-scan':
            state=self.bluetooth()
            if not state.get('enabled'):raise ValueError('请先开启蓝牙')
            if not state['discovering']:
                self.call('org.bluez',state['adapter'],'org.bluez.Adapter1','StartDiscovery')
                try:time.sleep(5)
                finally:self.call('org.bluez',state['adapter'],'org.bluez.Adapter1','StopDiscovery')
        elif operation=='dnd':
            value=values.get('value')
            if type(value) is not bool:raise ValueError('无效的勿扰状态')
            settings=Gio.Settings.new('org.ukui.quick-operation.panel')
            for key in ('disturbmodestatus','disturbmodenotifyclose'):
                if not settings.set_boolean(key,value):raise ValueError('系统未接受勿扰设置')
            Gio.Settings.sync()
        elif operation=='notifications-clear':self.session_call('org.ukui.AiStationPanel','/org/kylin/Lqui/Notifications','org.kylin.Lqui.Notifications','Clear')
        elif operation=='media':
            service=values.get('service');method=values.get('method');players=self.media()['players']
            if service not in [p['service'] for p in players] or method not in ('Previous','PlayPause','Next'):raise ValueError('播放器已变化，请重试')
            self.session_call(service,'/org/mpris/MediaPlayer2','org.mpris.MediaPlayer2.Player',method)
        elif operation=='session':
            action=values.get('action');state=self.session()['actions'];methods={'lock':'lock','suspend':'suspend','hibernate':'hibernate','logout':'logout','reboot':'reboot','poweroff':'powerOff'}
            if action not in methods or not state.get(action):raise ValueError('此会话操作暂不可用')
            if action=='lock':self.session_call('org.ukui.ScreenSaver','/','org.ukui.ScreenSaver','Lock')
            else:self.session_call('org.gnome.SessionManager','/org/gnome/SessionManager','org.gnome.SessionManager',methods[action])
        else:raise ValueError('不支持的操作')
