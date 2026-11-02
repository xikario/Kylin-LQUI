"""Cached native launcher optics; optional for standalone Python-only installs."""
import ctypes,os
from pathlib import Path
from PyQt5 import QtCore,QtGui
_libraries={}

class NativeGlass:
    def __init__(self,image,screen,region=None,adaptive_smoke=False):
        self.handle=None;self.cache={};self.render_count=0;self.adaptive_smoke=False
        candidate=Path(os.environ['LQUI_NATIVE_GLASS_LIBRARY']) if os.environ.get('LQUI_NATIVE_GLASS_LIBRARY') else Path(__file__).with_name('liblqui-glass-bridge.so')
        if not candidate.is_file() and not os.environ.get('LQUI_NATIVE_GLASS_LIBRARY'):candidate=Path(__file__).resolve().parents[1]/'ukui-panel-liquid/build/liblqui-glass-bridge.so'
        key=str(candidate)
        if key not in _libraries:_libraries[key]=ctypes.CDLL(key)
        self.library=_libraries[key]
        region=region or screen
        create=self.library.lqui_glass_create_region;create.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_double]+[ctypes.c_int]*8;create.restype=ctypes.c_void_p
        render=self.library.lqui_glass_render;render.argtypes=[ctypes.c_void_p]+[ctypes.c_int]*4+[ctypes.c_double]*2+[ctypes.c_void_p]+[ctypes.c_int]*3;render.restype=ctypes.c_int
        self.library.lqui_glass_destroy.argtypes=[ctypes.c_void_p];self.library.lqui_glass_destroy.restype=None
        self.library.lqui_glass_used_gpu.argtypes=[ctypes.c_void_p];self.library.lqui_glass_used_gpu.restype=ctypes.c_int
        source=image.convertToFormat(QtGui.QImage.Format_ARGB32_Premultiplied)
        self.handle=create(int(source.constBits()),source.width(),source.height(),source.bytesPerLine(),image.devicePixelRatio(),screen.x(),screen.y(),screen.width(),screen.height(),region.x(),region.y(),region.width(),region.height())
        if not self.handle:raise RuntimeError('Native glass unavailable')
        # Older standalone libraries retain their existing material.
        if adaptive_smoke and hasattr(self.library,'lqui_glass_set_adaptive_smoke'):
            setter=self.library.lqui_glass_set_adaptive_smoke;setter.argtypes=[ctypes.c_void_p,ctypes.c_int];setter.restype=None
            setter(self.handle,1);self.adaptive_smoke=True
    def render(self,rect,dpr,radius):
        key=(rect.x(),rect.y(),rect.width(),rect.height(),dpr,radius)
        if key not in self.cache:
            output=QtGui.QImage(max(1,int(rect.width()*dpr+.5)),max(1,int(rect.height()*dpr+.5)),QtGui.QImage.Format_ARGB32_Premultiplied);output.fill(QtCore.Qt.transparent)
            ok=self.library.lqui_glass_render(self.handle,rect.x(),rect.y(),rect.width(),rect.height(),dpr,radius,int(output.bits()),output.width(),output.height(),output.bytesPerLine())
            if not ok:return QtGui.QImage()
            output.setDevicePixelRatio(dpr)
            if len(self.cache)>=32:self.cache.clear()
            self.cache[key]=output;self.render_count+=1
        return self.cache[key]
    def close(self):
        self.cache.clear()
        if self.handle:self.library.lqui_glass_destroy(self.handle);self.handle=None
    @property
    def used_gpu(self):return bool(self.handle and self.library.lqui_glass_used_gpu(self.handle))
    def __del__(self):self.close()
