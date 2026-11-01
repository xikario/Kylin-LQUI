import sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from PyQt5 import QtCore,QtGui,QtWidgets
from gi.repository import Gio,GLib
from glass_widgets import diffuse_backdrop,GlassCard
from system_backend import SystemBackend,NM,NMP,service_error

app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
class GlassRegression(unittest.TestCase):
    def test_capture_is_opaque_at_every_dpr_and_screen_edge(self):
        for dpr in (1,1.25,1.5,2):
            pix=QtGui.QPixmap(800,600);pix.fill(QtGui.QColor('#94a9be'));pix.setDevicePixelRatio(dpr)
            result=diffuse_backdrop(pix,QtCore.QSize(round(800/dpr),round(600/dpr)))
            for xf in (0,.1,.5,.9,1):
                for yf in (0,.1,.5,.9,1):
                    c=result.pixelColor(round(xf*(result.width()-1)),round(yf*(result.height()-1)))
                    self.assertEqual(c.alpha(),255,(dpr,xf,yf))
                    self.assertLess(abs(c.red()-148),4,(dpr,xf,yf,c.name()))
    def test_fine_text_like_stripes_are_diffused(self):
        pix=QtGui.QPixmap(800,600);pix.fill(QtCore.Qt.black);p=QtGui.QPainter(pix)
        for y in range(0,600,8):p.fillRect(0,y,800,4,QtCore.Qt.white)
        p.end();pix.setDevicePixelRatio(1.5)
        image=diffuse_backdrop(pix,QtCore.QSize(533,400))
        values=[image.pixelColor(300,y).red() for y in range(40,360)]
        self.assertLess(max(values)-min(values),15)
    def test_scan_throttle_and_connected_network_are_noops(self):
        b=SystemBackend();b.snapshot=lambda:{};calls=[];b.call=lambda *a:calls.append(a)
        b.wifi=lambda:{'devices':['/dev'],'enabled':True,'networks':[{'id':'/ap','connected':True}]}
        import time
        b.props=lambda *a:{'LastScan':int(time.clock_gettime(time.CLOCK_BOOTTIME)*1000)}
        b.action('scan',{});b.action('connect',{'id':'/ap'})
        self.assertFalse(calls)
    def test_expired_access_point_does_not_hide_remaining_networks(self):
        b=SystemBackend();b.call=lambda *a:(['/device'],)
        def props(service,path,interface):
            if path==NMP:return {'WirelessEnabled':True,'WirelessHardwareEnabled':True}
            if interface.endswith('.Wireless'):return {'ActiveAccessPoint':'/ap','AccessPoints':['/expired','/ap']}
            if interface.endswith('.Device'):return {'DeviceType':2,'State':100}
            if path=='/expired':raise Gio.dbus_error_new_for_dbus_error('org.freedesktop.DBus.Error.UnknownObject','expired')
            return {'Ssid':b'TEST','Strength':80,'RsnFlags':256,'Flags':1}
        b.props=props;result=b.wifi();self.assertTrue(result['available']);self.assertEqual(len(result['networks']),1);self.assertTrue(result['networks'][0]['connected'])
    def test_glass_reading_area_contrast_on_white_backdrop(self):
        owner=QtWidgets.QWidget();owner.backdrop=QtGui.QImage(600,600,QtGui.QImage.Format_RGB32);owner.backdrop.fill(QtCore.Qt.white);owner.backdrop_origin=QtCore.QPoint()
        card=GlassCard(owner);card.resize(220,180);card.show();app.processEvents();image=card.grab().toImage()
        def luminance(color):
            channels=[color.redF(),color.greenF(),color.blueF()]
            return sum(weight*(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4) for weight,v in zip((.2126,.7152,.0722),channels))
        for x,y in ((25,25),(100,40),(100,100),(190,150)):
            background=luminance(image.pixelColor(x,y))
            for foreground in ('#eef3fa','#dde6f0'):self.assertGreaterEqual((luminance(QtGui.QColor(foreground))+.05)/(background+.05),4.5)
        card.close();card.deleteLater();owner.deleteLater()
    def test_dbus_errors_are_actionable_without_service_payload(self):
        for code,word in [('NotAllowed','扫描'),('PermissionDenied','授权'),('UnknownMethod','不支持'),('NoReply','超时')]:
            error=Gio.dbus_error_new_for_dbus_error('org.freedesktop.NetworkManager.'+code,'private-password-123')
            text=service_error(error);self.assertIn(word,text);self.assertNotIn('private-password',text)

    def test_wifi_raw_identity_and_security_profile_match(self):
        b=SystemBackend()
        def call(service,path,interface,method,*args):
            if method=='GetDevices':return (['/device'],)
            return ({'802-11-wireless':{'ssid':b'Cafe'},'802-11-wireless-security':{'key-mgmt':'wpa-psk'}},)
        b.call=call
        def props(service,path,interface):
            if path==NMP:return {'WirelessEnabled':True,'WirelessHardwareEnabled':True}
            if interface.endswith('.Wireless'):return {'AccessPoints':['/open','/secure','/bad1','/bad2']}
            if interface.endswith('.Device'):return {'DeviceType':2,'State':30,'AvailableConnections':['/profile']}
            return {'Ssid':b'Cafe'+(b'\xff' if path=='/bad1' else b'\xfe' if path=='/bad2' else b''),'RsnFlags':256 if path=='/secure' else 0,'Flags':1 if path=='/secure' else 0,'Strength':80}
        b.props=props;items={item['id']:item for item in b.wifi()['networks']}
        self.assertEqual(len(items),4);self.assertEqual(items['/open']['profile'],'');self.assertEqual(items['/secure']['profile'],'/profile')
        self.assertNotEqual(items['/bad1']['rawSsid'],items['/bad2']['rawSsid'])

if __name__=='__main__':unittest.main()
