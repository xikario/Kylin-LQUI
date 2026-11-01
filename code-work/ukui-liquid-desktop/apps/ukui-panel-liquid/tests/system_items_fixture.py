import os,dbus,dbus.service,dbus.mainloop.glib
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus=dbus.SessionBus();name=dbus.service.BusName('org.kylin.TestSystemItems',bus)
class Item(dbus.service.Object):
 def __init__(self,key,title,icon):
  self.bus=dbus.SessionBus(private=True);self.name=dbus.service.BusName('org.kylin.TestSystemItems.'+key,self.bus);self.path='/StatusNotifierItem';self.title=title;self.icon=icon;super().__init__(self.bus,self.path)
 @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='s',out_signature='a{sv}')
 def GetAll(self,interface):
  return {'Category':'Hardware','Id':self.title,'Title':self.title,'Status':'Active','WindowId':dbus.UInt32(0),'IconName':self.icon,'IconPixmap':dbus.Array([],signature='(iiay)'),'AttentionIconName':'','AttentionIconPixmap':dbus.Array([],signature='(iiay)'),'OverlayIconName':'','OverlayIconPixmap':dbus.Array([],signature='(iiay)'),'ItemIsMenu':False,'Menu':dbus.ObjectPath('/NoMenu'),'ToolTip':dbus.Struct((self.icon,dbus.Array([],signature='(iiay)'),self.title,''),signature='sa(iiay)ss')}
 @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='ss',out_signature='v')
 def Get(self,interface,key):return self.GetAll(interface)[key]
 @dbus.service.method('org.kde.StatusNotifierItem',in_signature='ii',out_signature='')
 def Activate(self,x,y):pass
 @dbus.service.method('org.kde.StatusNotifierItem',in_signature='ii',out_signature='')
 def ContextMenu(self,x,y):pass
items=[Item('Network','kylin-nm','network-wireless-signal-good-symbolic'),Item('Sound','ukui-volume-control-applet-qt','audio-volume-high-symbolic'),Item('Power','ukui-power-manager-tray','battery-level-100-charging-symbolic'),Item('Notice','ukui-sidebar','ukui-tool-box-null-symbolic')]
items += [Item('Hidden'+str(i),'hidden-fixture-'+str(i),'applications-utilities') for i in range(9)]
watcher_name=dbus.service.BusName('org.kde.StatusNotifierWatcher',bus)
class Watcher(dbus.service.Object):
 def __init__(self):self.items=[];super().__init__(bus,'/StatusNotifierWatcher')
 @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='s',out_signature='a{sv}')
 def GetAll(self,interface):return {'RegisteredStatusNotifierItems':dbus.Array(self.items,signature='s'),'IsStatusNotifierHostRegistered':True,'ProtocolVersion':dbus.Int32(0)}
 @dbus.service.method('org.freedesktop.DBus.Properties',in_signature='ss',out_signature='v')
 def Get(self,interface,key):return self.GetAll(interface)[key]
 @dbus.service.method('org.kde.StatusNotifierWatcher',in_signature='s',out_signature='')
 def RegisterStatusNotifierHost(self,service):pass
 @dbus.service.method('org.kde.StatusNotifierWatcher',in_signature='s',out_signature='')
 def RegisterStatusNotifierItem(self,service):
  if service not in self.items:self.items.append(service);self.StatusNotifierItemRegistered(service)
 @dbus.service.signal('org.kde.StatusNotifierWatcher',signature='s')
 def StatusNotifierItemRegistered(self,service):pass
watcher=Watcher()
for item in items:watcher.RegisterStatusNotifierItem(item.bus.get_unique_name()+item.path)
sidebar_name=dbus.service.BusName('org.ukui.Sidebar',bus)
class Sidebar(dbus.service.Object):
 def __init__(self):super().__init__(bus,'/org/ukui/Sidebar/notification')
 @dbus.service.signal('org.ukui.Sidebar.notification',signature='sssssssssasasbi((iii)(iiii)i)i')
 def Sig_Notify(self,ident,replace,app,icon,summary,body,url,default,state,actions,states,fold,timeout,date,maximum):pass
 @dbus.service.method('org.ukui.Sidebar.notification',in_signature='s',out_signature='')
 def EmitFixture(self,body):self.Sig_Notify('7','0','测试应用','','通知测试',body,'','','',[],[],False,3000,((2026,10,31),(12,0,0,0),0),10)
 @dbus.service.method('org.ukui.Sidebar.notification',in_signature='u',out_signature='')
 def closeNotification(self,ident):pass
sidebar=Sidebar()
GLib.MainLoop().run()
