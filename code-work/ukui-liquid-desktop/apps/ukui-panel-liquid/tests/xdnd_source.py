#!/usr/bin/python3
"""Real Xdnd source for private X11 tests, never uses the user's desktop."""
import json,os,sys
from PyQt5.QtCore import Qt,QMimeData,QUrl,QTimer
from PyQt5.QtGui import QDrag,QPixmap
from PyQt5.QtWidgets import QApplication,QLabel
assert os.environ.get('AI_STATION_ISOLATED')=='1'
app=QApplication(sys.argv);app.setQuitOnLastWindowClosed(False)
class Source(QLabel):
 def mousePressEvent(self,event):self.origin=event.pos()
 def mouseMoveEvent(self,event):
  if not event.buttons()&Qt.LeftButton or (event.pos()-self.origin).manhattanLength()<8:return
  drag=QDrag(self);mime=QMimeData();mime.setUrls([QUrl.fromLocalFile(sys.argv[1])]);drag.setMimeData(mime)
  pix=QPixmap(sys.argv[1]).scaled(40,40,Qt.KeepAspectRatio);drag.setPixmap(pix);drag.setHotSpot(pix.rect().center())
  action=drag.exec_(Qt.MoveAction|Qt.CopyAction,Qt.MoveAction)
  print(json.dumps({'dropAction':int(action)}),flush=True);QTimer.singleShot(1800,app.quit)
w=Source('Drag fixture image');w.resize(200,100);w.move(80,100);w.show()
QTimer.singleShot(300,lambda:print(json.dumps({'x':w.mapToGlobal(w.rect().center()).x()*w.devicePixelRatioF(),'y':w.mapToGlobal(w.rect().center()).y()*w.devicePixelRatioF()}),flush=True))
QTimer.singleShot(15000,app.quit);app.exec_()
