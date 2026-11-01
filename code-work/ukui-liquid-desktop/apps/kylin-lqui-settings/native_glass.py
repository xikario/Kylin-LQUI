"""Cached native launcher optics; optional for standalone Python-only installs."""
import ctypes,os
from pathlib import Path
from PyQt5 import QtCore,QtGui

class NativeGlass:
    def __init__(self,image,screen):
        self.handle=None;self.cache={};self.render_count=0
        candidate=Path(__file__).with_name('liblqui-glass-bridge.so')
        if not candidate.is_file():candidate=Path(__file__).resolve().parents[1]/'ukui-panel-liquid/build/liblqui-glass-bridge.so'
        self.library=ctypes.CDLL(str(candidate))
        create=self.library.lqui_glass_create;create.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_double]+[ctypes.c_int]*4;create.restype=ctypes.c_void_p
        render=self.library.lqui_glass_render;render.argtypes=[ctypes.c_void_p]+[ctypes.c_int]*4+[ctypes.c_double]*2+[ctypes.c_void_p]+[ctypes.c_int]*3;render.restype=ctypes.c_int
        self.library.lqui_glass_destroy.argtypes=[ctypes.c_void_p];self.library.lqui_glass_destroy.restype=None
        source=image.convertToFormat(QtGui.QImage.Format_ARGB32_Premultiplied)
        self.handle=create(int(source.constBits()),source.width(),source.height(),source.bytesPerLine(),image.devicePixelRatio(),screen.x(),screen.y(),screen.width(),screen.height())
        if not self.handle:raise RuntimeError('Native glass unavailable')
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
        if self.handle:self.library.lqui_glass_destroy(self.handle);self.handle=None
        self.cache.clear()
    def __del__(self):self.close()
