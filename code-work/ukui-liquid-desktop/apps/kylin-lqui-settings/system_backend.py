"""System control adapters. Actions use fixed argv or typed system D-Bus calls."""
import calendar,datetime,json,os,re,subprocess,sys,uuid,time
from pathlib import Path
from gi.repository import Gio,GLib
from appconfig import ROOT,RUNTIME
from desktop_controls import DesktopControls
NM='org.freedesktop.NetworkManager';NMP='/org/freedesktop/NetworkManager'

def service_error(error):
    """Expose a useful category, never service payloads or Wi-Fi secrets."""
    if isinstance(error,ValueError):return str(error)
    code=Gio.DBusError.get_remote_error(error) if isinstance(error,GLib.Error) else ''
    code=code or ''
    if code.endswith(('PermissionDenied','NotAuthorized','AccessDenied')):
        return '系统未授权此操作。请完成系统授权；也可从高级网络设置操作。'
    if code.endswith(('ServiceUnknown','NameHasNoOwner')):
        return '对应的系统服务尚未启动，请稍后刷新。'
    if code.endswith(('NoReply','Timeout','TimedOut')) or isinstance(error,subprocess.TimeoutExpired):
        return '系统服务响应超时。正在重新读取实际状态，请稍后刷新确认。'
    if code.endswith(('UnknownMethod','NotSupported')):
        return '当前系统服务不支持此操作，请使用系统高级设置。'
    if code.endswith('NotAllowed'):
        return '无线设备暂不允许此操作：可能正在扫描、连接或被硬件开关关闭。请稍后刷新。'
    if code.endswith(('InvalidConnection','NoSecrets','SecretsRequired')):
        return '网络配置或认证信息不完整，请在高级网络设置中检查。'
    return '系统服务未完成操作，请稍后刷新或打开系统高级设置。'

class SystemBackend(DesktopControls):
    def __init__(self):self.bus=None
    def call(self,service,path,interface,method,args=None,timeout=4000):
        if self.bus is None:self.bus=Gio.bus_get_sync(Gio.BusType.SYSTEM,None)
        return self.bus.call_sync(service,path,interface,method,args,None,Gio.DBusCallFlags.NO_AUTO_START|Gio.DBusCallFlags.ALLOW_INTERACTIVE_AUTHORIZATION,timeout,None).unpack()
    def props(self,service,path,interface):return self.call(service,path,'org.freedesktop.DBus.Properties','GetAll',GLib.Variant('(s)',(interface,)))[0]
    def set_property(self,service,path,interface,key,value):return self.call(service,path,'org.freedesktop.DBus.Properties','Set',GLib.Variant('(ssv)',(interface,key,value)))
    @staticmethod
    def command(arguments):
        env=dict(os.environ,LC_ALL='C');result=subprocess.run(arguments,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,text=True,timeout=5)
        if result.returncode:raise RuntimeError('系统服务未接受操作，请重试。')
        return result.stdout
    def audio(self):
        info=self.command(['/usr/bin/pactl','info']);match=re.search(r'^Default Sink:\s*(.+)$',info,re.M);default=match.group(1).strip() if match else ''
        text=self.command(['/usr/bin/pactl','list','sinks']);outputs=[]
        for block in re.split(r'(?m)^Sink #\d+',text)[1:]:
            def field(name):
                match=re.search(r'^\s*'+name+r':\s*(.*)$',block,re.M);return match.group(1).strip() if match else ''
            volumes=re.findall(r'(\d+)%',field('Volume'));name=field('Name')
            outputs.append({'id':name,'name':field('Description') or name,'volume':round(sum(map(int,volumes))/len(volumes)) if volumes else 0,'muted':field('Mute')=='yes'})
        selected=next((item for item in outputs if item['id']==default),outputs[0] if outputs else None)
        return {'available':bool(selected),'outputs':outputs,'selected':selected}
    def wifi(self):
        manager=self.props(NM,NMP,NM);devices=self.call(NM,NMP,NM,'GetDevices')[0];networks=[];interfaces=[]
        for path in devices:
            device=self.props(NM,path,NM+'.Device')
            if device.get('DeviceType')!=2:continue
            wireless=self.props(NM,path,NM+'.Device.Wireless');interfaces.append(path);active=wireless.get('ActiveAccessPoint','/')
            saved=[]
            for connection in device.get('AvailableConnections',[]):
                try:
                    settings=self.call(NM,connection,NM+'.Settings.Connection','GetSettings')[0]
                    if '802-11-wireless' in settings:saved.append((connection,settings))
                except GLib.Error:pass
            for ap in wireless.get('AccessPoints',[]):
                # Access points can expire between the device list and GetAll.
                # One vanished AP must not make the entire Wi-Fi card unavailable.
                try:properties=self.props(NM,ap,NM+'.AccessPoint')
                except GLib.Error as error:
                    if 'UnknownObject' in (Gio.DBusError.get_remote_error(error) or ''):continue
                    raise
                ssid=bytes(properties.get('Ssid',[]))
                if not ssid:continue
                flags=int(properties.get('RsnFlags',0))|int(properties.get('WpaFlags',0));secure=bool(properties.get('Flags',0)&1)
                security='enterprise' if flags&0x200 else 'sae' if flags&0x400 and not flags&0x100 else 'psk' if flags&0x100 else 'unsupported' if secure else 'open'
                profile=''
                for connection,settings in saved:
                    wireless_settings=settings['802-11-wireless'];section=settings.get('802-11-wireless-security',{});key=section.get('key-mgmt','')
                    if bytes(wireless_settings.get('ssid',[]))!=ssid:continue
                    if wireless_settings.get('mode','infrastructure')!='infrastructure':continue
                    wanted_interface=settings.get('connection',{}).get('interface-name')
                    if wanted_interface and wanted_interface!=device.get('Interface'):continue
                    bssid=wireless_settings.get('bssid')
                    if bssid:
                        configured=bssid.upper() if isinstance(bssid,str) else ':'.join('%02X'%n for n in bssid)
                        if configured!=str(properties.get('HwAddress','')).upper():continue
                    compatible=(security=='open' and not section) or (key=='wpa-psk' and bool(flags&0x100)) or (key=='sae' and bool(flags&0x400)) or (key in ('wpa-eap','wpa-eap-suite-b-192','ieee8021x') and security=='enterprise')
                    if compatible:profile=connection;break
                networks.append({'id':ap,'device':path,'rawSsid':ssid.hex(),'ssid':ssid.decode('utf-8','backslashreplace'),'strength':int(properties.get('Strength',0)),'connected':ap==active and device.get('State')==100,'connecting':ap==active and 40<=device.get('State',0)<100,'profile':profile,'security':security})
        unique={}
        for item in sorted(networks,key=lambda x:(x['connected'],x['strength']),reverse=True):unique.setdefault((item['device'],item['rawSsid'],item['security']),item)
        return {'available':bool(interfaces),'enabled':bool(manager.get('WirelessEnabled')),'hardwareEnabled':bool(manager.get('WirelessHardwareEnabled')),'devices':interfaces,'networks':list(unique.values())[:32],'state':int(manager.get('State',0))}
    def power(self):
        service='org.freedesktop.UPower';path='/org/freedesktop/UPower';root=self.props(service,path,service)
        device=self.call(service,path,service,'GetDisplayDevice')[0];props=self.props(service,device,service+'.Device')
        result={'available':True,'present':bool(props.get('IsPresent')),'onBattery':bool(root.get('OnBattery')),'percentage':round(float(props.get('Percentage',0))),'state':int(props.get('State',0)),'timeToEmpty':int(props.get('TimeToEmpty',0)),'timeToFull':int(props.get('TimeToFull',0)),'capacity':round(float(props.get('Capacity',0))),'profiles':[]}
        for name,object_path,interface in [('net.hadess.PowerProfiles','/net/hadess/PowerProfiles','net.hadess.PowerProfiles'),('org.freedesktop.UPower.PowerProfiles','/org/freedesktop/UPower/PowerProfiles','org.freedesktop.UPower.PowerProfiles')]:
            try:
                data=self.props(name,object_path,interface);result.update(profileService=name,profilePath=object_path,profileInterface=interface,profiles=[p['Profile'] for p in data.get('Profiles',[])],activeProfile=data.get('ActiveProfile'));break
            except GLib.Error:pass
        return result
    def snapshot(self):
        result={}
        for key,method in [('audio',self.audio),('wifi',self.wifi),('power',self.power),('bluetooth',self.bluetooth),('brightness',self.brightness),('notifications',self.notifications),('media',self.media),('session',self.session)]:
            try:result[key]=method()
            except (GLib.Error,RuntimeError,OSError,subprocess.SubprocessError,ValueError):result[key]={'available':False}
        return result
    def action(self,operation,values):
        if operation=='volume':
            volume=values.get('value')
            if type(volume) is not int or not 0<=volume<=100:raise ValueError('音量应在 0 到 100 之间')
            self.command(['/usr/bin/pactl','set-sink-volume','@DEFAULT_SINK@',str(volume)+'%'])
        elif operation=='mute':
            if type(values.get('value')) is not bool:raise ValueError('无效的静音状态')
            self.command(['/usr/bin/pactl','set-sink-mute','@DEFAULT_SINK@','1' if values['value'] else '0'])
        elif operation=='output':
            sink=values.get('id');outputs=self.audio()['outputs']
            if sink not in [item['id'] for item in outputs]:raise ValueError('音频设备已变化，请刷新')
            self.command(['/usr/bin/pactl','set-default-sink',sink])
            for line in self.command(['/usr/bin/pactl','list','short','sink-inputs']).splitlines():
                ident=line.split('\t')[0]
                if ident.isdigit():self.command(['/usr/bin/pactl','move-sink-input',ident,sink])
        elif operation=='wifi-enabled':
            if type(values.get('value')) is not bool:raise ValueError('无效的 Wi-Fi 状态')
            self.set_property(NM,NMP,NM,'WirelessEnabled',GLib.Variant('b',values['value']))
        elif operation=='scan':
            wifi=self.wifi()
            if not wifi.get('enabled',True):raise ValueError('请先启用 Wi-Fi 再刷新网络列表。')
            for device in wifi['devices']:
                last=self.props(NM,device,NM+'.Device.Wireless').get('LastScan',-1)
                # NetworkManager rate-limits scans. A recent scan already has
                # the data the refresh button needs; never restart a connection.
                if last>=0 and 0<=time.clock_gettime(time.CLOCK_BOOTTIME)*1000-last<10000:continue
                self.call(NM,device,NM+'.Device.Wireless','RequestScan',GLib.Variant('(a{sv})',({},)))
        elif operation=='connect':
            selected=next((n for n in self.wifi()['networks'] if n['id']==values.get('id')),None)
            if not selected:raise ValueError('网络已变化，请刷新列表')
            if selected.get('connected') or selected.get('connecting'):return self.snapshot()
            if selected['profile']:self.call(NM,NMP,NM,'ActivateConnection',GLib.Variant('(ooo)',(selected['profile'],selected['device'],selected['id'])),20000)
            else:
                if selected['security'] not in ('open','psk','sae'):raise ValueError('此网络需要在“高级网络设置”中配置')
                password=values.get('password','')
                if selected['security']!='open' and (not isinstance(password,str) or not (8<=len(password)<=63 or len(password)==64 and all(c in '0123456789abcdefABCDEF' for c in password))):raise ValueError('请输入有效的 Wi-Fi 密码')
                ap=self.props(NM,selected['id'],NM+'.AccessPoint');ssid=bytes(ap['Ssid'])
                config={'connection':{'id':GLib.Variant('s',selected['ssid']),'uuid':GLib.Variant('s',str(uuid.uuid4())),'type':GLib.Variant('s','802-11-wireless'),'autoconnect':GLib.Variant('b',bool(values.get('remember')))},'802-11-wireless':{'ssid':GLib.Variant('ay',ssid),'mode':GLib.Variant('s','infrastructure')},'ipv4':{'method':GLib.Variant('s','auto')},'ipv6':{'method':GLib.Variant('s','auto')}}
                if selected['security']!='open':config['802-11-wireless-security']={'key-mgmt':GLib.Variant('s','sae' if selected['security']=='sae' else 'wpa-psk'),'psk':GLib.Variant('s',password)}
                options={'persist':GLib.Variant('s','disk' if values.get('remember') else 'memory')}
                self.call(NM,NMP,NM,'AddAndActivateConnection2',GLib.Variant('(a{sa{sv}}ooa{sv})',(config,selected['device'],selected['id'],options)),20000)
        elif operation=='power-profile':
            current=self.power();profile=values.get('value')
            if profile not in current['profiles']:raise ValueError('此电源模式暂不可用')
            self.set_property(current['profileService'],current['profilePath'],current['profileInterface'],'ActiveProfile',GLib.Variant('s',profile))
        else:self.desktop_action(operation,values)
        return self.snapshot()


def calendar_data(year,month):
    if not 1901<=year<=2099 or not 1<=month<=12:raise ValueError('日期超出支持范围')
    scripts=RUNTIME/'apps/ukui-fences/scripts'
    if str(scripts) not in sys.path:sys.path.insert(0,str(scripts))
    from calendar_snapshot import read_snapshot
    from chinese_calendar import metadata
    begin=datetime.date(year,month,1);end=begin.replace(day=calendar.monthrange(year,month)[1]);items=[];warning=''
    # Same read-only native calendar source as the Fences calendar widget.
    database=Path(os.environ.get('XDG_CONFIG_HOME',str(Path.home()/'.config')))/'.kylin_calendar_Schedule2.db'
    try:result=read_snapshot(database,begin,end);items=result['items'];warning=result.get('warning','')
    except Exception:warning='系统日程暂时无法读取'
    try:dates=metadata(begin,end)
    except Exception:dates={}
    return {'items':items,'dates':dates,'warning':warning}
