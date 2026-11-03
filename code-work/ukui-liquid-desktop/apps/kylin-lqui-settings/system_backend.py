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
    if code.endswith(('NoReply','Timeout','TimedOut')) or isinstance(error,(subprocess.TimeoutExpired,TimeoutError)):
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
        text=self.command(['/usr/bin/pactl','list','sinks']);outputs=[];blocks={}
        for block in re.split(r'(?m)^Sink #\d+',text)[1:]:
            volumes=re.findall(r'(\d+)%',self.pulse_field(block,'Volume'));name=self.pulse_field(block,'Name')
            if not name:continue
            outputs.append({'id':name,'name':self.pulse_field(block,'Description') or name,'volume':round(sum(map(int,volumes))/len(volumes)) if volumes else 0,'muted':self.pulse_field(block,'Mute')=='yes'})
            blocks[name]=block
        self.describe_pulse_routes(outputs,blocks,'output')
        selected=next((item for item in outputs if item['id']==default),outputs[0] if outputs else None)
        return {'available':bool(selected),'outputs':outputs,'selected':selected}
    @staticmethod
    def pulse_field(block,name):
        match=re.search(r'^[ \t]*'+re.escape(name)+r':[ \t]*(.*)$',block,re.M)
        return match.group(1).strip() if match else ''
    @staticmethod
    def pulse_property(block,name):
        match=re.search(r'^[ \t]*'+re.escape(name)+r'[ \t]*=[ \t]*(.*)$',block,re.M)
        if not match:return ''
        value=match.group(1).strip()
        try:return str(json.loads(value))
        except (ValueError,TypeError):return value.strip('"')
    def describe_pulse_routes(self,devices,blocks,kind):
        # Jack insertion changes Active Port, not the sink/source ID or its
        # generic card description. Filters inherit their physical endpoint.
        by_id={item['id']:item for item in devices}
        port_labels={'analog-output-headphones':'有线耳机','analog-output-speaker':'扬声器',
                     'analog-input-internal-mic':'内置麦克风','analog-input-mic':'外接麦克风',
                     'analog-input-headset-mic':'耳麦麦克风','analog-output-lineout':'线路输出',
                     'analog-input-linein':'线路输入'}
        for item in devices:
            current=item['id'];seen=set();chain=[];chain_ids=[];base=None
            while current in by_id and current not in seen:
                seen.add(current);block=blocks[current];chain.append(block);chain_ids.append(current)
                master=self.pulse_property(block,'device.master_device')
                if not master:base=by_id[current];break
                current=master
            own=blocks[item['id']]
            is_filter=bool(self.pulse_property(own,'device.master_device')) or self.pulse_property(own,'device.class')=='filter'
            if kind=='output':
                # A filter may be unmuted while its hardware master is muted
                # after a jack change. Report the entire playback path, without
                # changing audio as a side effect of polling or hotplug events.
                item.update(ownMuted=self.pulse_field(own,'Mute')=='yes',
                            muted=any(self.pulse_field(b,'Mute')=='yes' for b in chain),
                            muteTargets=chain_ids if base is not None and not (base is item and is_filter) else [])
            if base is None or (base is item and is_filter):
                # Never invent a physical route for dangling/cyclic filters.
                if is_filter:item.update(displayName=re.sub(r' \(echo cancelled with .+\)$','（回声消除）',item['name']),detail='虚拟处理通道 · 物理设备暂不可确认')
                continue
            block=blocks[base['id']];port=self.pulse_field(block,'Active Port')
            match=re.search(r'^[ \t]*'+re.escape(port)+r':[ \t]*(.*?)\s+\(priority:.*$',block,re.M) if port else None
            port_name=port_labels.get(port) or (match.group(1).strip() if match else '')
            if not port_name and not is_filter:continue
            label=port_name or base['name']
            item.update(port=port,routeLabel=label,physicalId=base['id'])
            if is_filter:
                echo=any(self.pulse_field(b,'Driver')=='module-echo-cancel.c' for b in chain)
                effect='回声消除' if echo else '音频处理'
                item.update(displayName=label+'（'+effect+'）',detail=base['name']+' · 同一设备的'+effect+'通道')
            else:
                item.update(displayName=label,detail=base['name']+' · '+('直接输出' if kind=='output' else '直接输入'))
    def microphone(self):
        info=self.command(['/usr/bin/pactl','info']);match=re.search(r'^Default Source:\s*(.+)$',info,re.M);default=match.group(1).strip() if match else ''
        text=self.command(['/usr/bin/pactl','list','sources']);parts=re.split(r'(?m)^Source #(\d+)',text);inputs=[];blocks={}
        for index,block in zip(parts[1::2],parts[2::2]):
            name=self.pulse_field(block,'Name');monitor=self.pulse_field(block,'Monitor of Sink')
            if not name or name.endswith('.monitor') or monitor not in ('','n/a'):continue
            volumes=re.findall(r'(\d+)%',self.pulse_field(block,'Volume'))
            inputs.append({'id':name,'index':index,'name':self.pulse_field(block,'Description') or name,'volume':round(sum(map(int,volumes))/len(volumes)) if volumes else 0,'muted':self.pulse_field(block,'Mute')=='yes'})
            blocks[name]=block
        self.describe_pulse_routes(inputs,blocks,'input')
        return {'available':bool(inputs),'inputs':inputs,'selected':next((item for item in inputs if item['id']==default),None)}
    @staticmethod
    def wifi_band(frequency):
        if 2400<=frequency<2500:return '2.4 GHz'
        if 4900<=frequency<5925:return '5 GHz'
        if 5925<=frequency<=7125:return '6 GHz'
        return '频段未知'
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
                frequency=int(properties.get('Frequency',0));band=self.wifi_band(frequency)
                flags=int(properties.get('RsnFlags',0))|int(properties.get('WpaFlags',0));secure=bool(properties.get('Flags',0)&1)
                security='enterprise' if flags&0x200 else 'sae' if flags&0x400 and not flags&0x100 else 'psk' if flags&0x100 else 'unsupported' if secure else 'open'
                profile=''
                for connection,settings in saved:
                    wireless_settings=settings['802-11-wireless'];section=settings.get('802-11-wireless-security',{});key=section.get('key-mgmt','')
                    if bytes(wireless_settings.get('ssid',[]))!=ssid:continue
                    if wireless_settings.get('mode','infrastructure')!='infrastructure':continue
                    wanted_band=wireless_settings.get('band')
                    if (wanted_band=='bg' and band!='2.4 GHz') or (wanted_band=='a' and band!='5 GHz'):continue
                    wanted_interface=settings.get('connection',{}).get('interface-name')
                    if wanted_interface and wanted_interface!=device.get('Interface'):continue
                    bssid=wireless_settings.get('bssid')
                    if bssid:
                        configured=bssid.upper() if isinstance(bssid,str) else ':'.join('%02X'%n for n in bssid)
                        if configured!=str(properties.get('HwAddress','')).upper():continue
                    compatible=(security=='open' and not section) or (key=='wpa-psk' and bool(flags&0x100)) or (key=='sae' and bool(flags&0x400)) or (key in ('wpa-eap','wpa-eap-suite-b-192','ieee8021x') and security=='enterprise')
                    if compatible:profile=connection;break
                networks.append({'id':ap,'device':path,'rawSsid':ssid.hex(),'ssid':ssid.decode('utf-8','backslashreplace'),'frequency':frequency,'band':band,'strength':int(properties.get('Strength',0)),'connected':ap==active and device.get('State')==100,'connecting':ap==active and 40<=device.get('State',0)<100,'profile':profile,'security':security})
        unique={}
        # Keep the strongest AP per SSID/security/band, preserving the active AP.
        # A dual-band network must expose both choices rather than hiding one.
        for item in sorted(networks,key=lambda x:(x['connected'],x['strength']),reverse=True):unique.setdefault((item['device'],item['rawSsid'],item['security'],item['band']),item)
        return {'available':bool(interfaces),'enabled':bool(manager.get('WirelessEnabled')),'hardwareEnabled':bool(manager.get('WirelessHardwareEnabled')),'devices':interfaces,'networks':list(unique.values())[:32],'state':int(manager.get('State',0))}
    def power(self):
        service='org.freedesktop.UPower';path='/org/freedesktop/UPower';root=self.props(service,path,service)
        device=self.call(service,path,service,'GetDisplayDevice')[0];props=self.props(service,device,service+'.Device')
        result={'available':True,'present':bool(props.get('IsPresent')),'onBattery':bool(root.get('OnBattery')),'percentage':round(float(props.get('Percentage',0))),'state':int(props.get('State',0)),'timeToEmpty':int(props.get('TimeToEmpty',0)),'timeToFull':int(props.get('TimeToFull',0)),'capacity':round(float(props.get('Capacity',0))),'profiles':[]}
        for name,object_path,interface in [('net.hadess.PowerProfiles','/net/hadess/PowerProfiles','net.hadess.PowerProfiles'),('org.freedesktop.UPower.PowerProfiles','/org/freedesktop/UPower/PowerProfiles','org.freedesktop.UPower.PowerProfiles')]:
            try:
                data=self.props(name,object_path,interface)
                if not data.get('Profiles'):continue
                result.update(profileBackend='power-profiles-daemon',profileService=name,profilePath=object_path,profileInterface=interface,profiles=[p['Profile'] for p in data.get('Profiles',[])],activeProfile=data.get('ActiveProfile'));break
            except GLib.Error:pass
        if not result['profiles']:
            try:result.update(self.ukui_power_profiles(result['onBattery']))
            except (GLib.Error,RuntimeError,ValueError):pass
        return result
    @staticmethod
    def ukui_power_settings():
        source=Gio.SettingsSchemaSource.get_default();schema=source.lookup('org.ukui.power-manager',True) if source else None
        if not schema or not all(schema.has_key(key) for key in ('power-policy-ac','power-policy-battery')):return None
        return Gio.Settings.new_full(schema,None,None)
    def ukui_power_profiles(self,on_battery):
        settings=self.ukui_power_settings()
        if settings is None:return {}
        if not self.session_call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','NameHasOwner',GLib.Variant('(s)',('org.ukui.powerManager',)))[0]:return {}
        key='power-policy-battery' if on_battery else 'power-policy-ac'
        # UKUI's native watcher maps 0=performance, 1=balance, 2=powersave.
        profiles=['performance','balanced','power-saver'];value=settings.get_int(key)
        writable=settings.is_writable(key)
        return {'profileBackend':'ukui','profileKey':key,'profiles':profiles,
                'activeProfile':profiles[value] if 0<=value<len(profiles) else None,'profileWritable':writable,
                'profileNote':('使用麒麟电源计划 · '+('电池供电' if on_battery else '接通电源')) if writable else '电源计划由系统策略管理，当前不可修改。'}
    def snapshot(self):
        result={}
        for key,method in [('audio',self.audio),('wifi',self.wifi),('power',self.power),('bluetooth',self.bluetooth),('brightness',self.brightness),('notifications',self.notifications),('media',self.media),('session',self.session)]:
            try:result[key]=method()
            except (GLib.Error,RuntimeError,OSError,subprocess.SubprocessError,ValueError):result[key]={'available':False}
        return result
    def controls_snapshot(self):
        # Wi-Fi discovery, notifications and power services must not delay the
        # two slider values needed by the first visible frame.
        result={}
        for key,method in (('audio',self.audio),('brightness',self.brightness)):
            try:result[key]=method()
            except (GLib.Error,RuntimeError,OSError,subprocess.SubprocessError,ValueError):result[key]={'available':False}
        return result
    def set_output_mute(self,selected,muted):
        # Muting just the selected path avoids silencing unrelated routes.
        # Explicit unmute must also release every verified upstream mute.
        targets=[selected['id']] if muted else selected.get('muteTargets',[selected['id']])
        if not targets:raise ValueError('音频处理通道的物理设备已变化，请重新选择输出设备。')
        for sink in reversed(targets):
            self.command(['/usr/bin/pactl','set-sink-mute',sink,'1' if muted else '0'])
    def action(self,operation,values):
        if operation=='volume':
            volume=values.get('value')
            if type(volume) is not int or not 0<=volume<=100:raise ValueError('音量应在 0 到 100 之间')
            selected=self.audio().get('selected')
            if not selected:raise ValueError('音频设备已变化，请刷新')
            self.command(['/usr/bin/pactl','set-sink-volume',selected['id'],str(volume)+'%'])
            # A deliberate nonzero slider gesture means "play at this level".
            # Bind both writes to the same resolved output if the default moves.
            if volume and selected.get('muted'):self.set_output_mute(selected,False)
            # Confirm just audio; Wi-Fi/power discovery must not delay the
            # committed slider value or its short volume-preview sound.
            return {'audio':self.audio()}
        elif operation=='mute':
            if type(values.get('value')) is not bool:raise ValueError('无效的静音状态')
            selected=self.audio().get('selected')
            if not selected:raise ValueError('音频设备已变化，请刷新')
            self.set_output_mute(selected,values['value'])
            return {'audio':self.audio()}
        elif operation=='output':
            sink=values.get('id');outputs=self.audio()['outputs']
            if sink not in [item['id'] for item in outputs]:raise ValueError('音频设备已变化，请刷新')
            self.command(['/usr/bin/pactl','set-default-sink',sink])
            parts=re.split(r'(?m)^Sink Input #(\d+)',self.command(['/usr/bin/pactl','list','sink-inputs']))
            for ident,block in zip(parts[1::2],parts[2::2]):
                if self.pulse_field(block,'Client').isdigit() and not re.search(r'media\.role\s*=\s*"filter"',block):
                    self.command(['/usr/bin/pactl','move-sink-input',ident,sink])
        elif operation in ('input','input-volume','input-mute'):
            state=self.microphone();source=values.get('id')
            if source not in [item['id'] for item in state['inputs']]:raise ValueError('输入设备已变化，请重新选择麦克风。')
            if operation=='input-volume':
                value=values.get('value')
                if type(value) is not int or not 0<=value<=100:raise ValueError('麦克风音量应在 0 到 100 之间')
                self.command(['/usr/bin/pactl','set-source-volume',source,str(value)+'%'])
            elif operation=='input-mute':
                if type(values.get('value')) is not bool:raise ValueError('无效的麦克风静音状态')
                self.command(['/usr/bin/pactl','set-source-mute',source,'1' if values['value'] else '0'])
            elif (state.get('selected') or {}).get('id')!=source:
                self.command(['/usr/bin/pactl','set-default-source',source])
                indexes={item['index'] for item in state['inputs']}
                parts=re.split(r'(?m)^Source Output #(\d+)',self.command(['/usr/bin/pactl','list','source-outputs']))
                for ident,block in zip(parts[1::2],parts[2::2]):
                    # Move application mic streams, never echo-cancel/loopback
                    # plumbing or an app recording a speaker-monitor source.
                    if self.pulse_field(block,'Client').isdigit() and self.pulse_field(block,'Source') in indexes and not re.search(r'media\.role\s*=\s*"filter"',block):
                        self.command(['/usr/bin/pactl','move-source-output',ident,source])
            return {'microphone':self.microphone()}
        elif operation=='wifi-enabled':
            if type(values.get('value')) is not bool:raise ValueError('无效的 Wi-Fi 状态')
            self.set_property(NM,NMP,NM,'WirelessEnabled',GLib.Variant('b',values['value']))
        elif operation=='scan':
            wifi=self.wifi()
            if not wifi.get('enabled',True):raise ValueError('请先启用 Wi-Fi 再刷新网络列表。')
            pending={}
            for device in wifi['devices']:
                last=self.props(NM,device,NM+'.Device.Wireless').get('LastScan',-1)
                # NetworkManager rate-limits scans. A recent scan already has
                # the data the refresh button needs; never restart a connection.
                if last>=0 and 0<=time.clock_gettime(time.CLOCK_BOOTTIME)*1000-last<10000:continue
                try:self.call(NM,device,NM+'.Device.Wireless','RequestScan',GLib.Variant('(a{sv})',({},)))
                except GLib.Error as error:
                    # Another request may already be in flight. Its completed
                    # generation is just as useful; never toggle the radio.
                    if not (Gio.DBusError.get_remote_error(error) or '').endswith('NotAllowed'):raise
                pending[device]=last
            # RequestScan acknowledges the request, not a fresh AP list. Wait
            # in the worker for LastScan to advance before publishing results.
            deadline=time.monotonic()+12
            while pending:
                remaining=deadline-time.monotonic()
                if remaining<=0:raise TimeoutError('Wi-Fi scan did not complete')
                for device,last in list(pending.items()):
                    wireless=self.call(NM,device,'org.freedesktop.DBus.Properties','GetAll',GLib.Variant('(s)',(NM+'.Device.Wireless',)),max(1,min(500,round(remaining*1000))))[0]
                    if wireless.get('LastScan',-1)>last:pending.pop(device)
                if pending:time.sleep(.15)
            return {'wifi':self.wifi()}
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
            if current.get('profileBackend')=='ukui':
                settings=self.ukui_power_settings();key=current.get('profileKey')
                if settings is None or key not in ('power-policy-ac','power-policy-battery') or not settings.is_writable(key):raise ValueError('当前电源计划不可修改。')
                value={'performance':0,'balanced':1,'power-saver':2}[profile]
                if not settings.set_int(key,value):raise RuntimeError('系统未接受电源模式切换。')
                Gio.Settings.sync()
                if settings.get_int(key)!=value:raise RuntimeError('电源模式尚未更新，请重试。')
            else:self.set_property(current['profileService'],current['profilePath'],current['profileInterface'],'ActiveProfile',GLib.Variant('s',profile))
            return {'power':self.power()}
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
