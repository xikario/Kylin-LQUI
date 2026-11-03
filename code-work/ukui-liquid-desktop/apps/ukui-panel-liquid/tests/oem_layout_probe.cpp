// Test-only wrapper; never installed. Drives the production settings controls
// inside the real OEM executable without accessing private C++ fields.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QTabBar>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QPointer>
#include <QProxyStyle>
#include <QScreen>
#include <QSlider>
#include <QStyleFactory>
#include <QStylePlugin>
#include <QTimer>
#include <QWidget>
#include <QAction>
#include <QMetaMethod>
#include <QScrollBar>
#include <QAbstractScrollArea>
#include <QAbstractButton>
#include <QToolButton>
#include <QProcess>
#include <cstdio>
#include <dlfcn.h>
class LayoutProbe : public QObject {
    QList<QWidget *> testWindows;
    QLocalServer server; QPointer<QWidget> panel; QJsonArray events,dndEvents,calendarEvents,dockMotion; bool traceDock=false;
    QJsonObject item(QWidget *w) {
        const QRect g(w->mapToGlobal(QPoint()),w->size());
        QJsonArray children;
        for(auto *c:w->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))children.append(item(c));
        QJsonArray methods;
        if(w->inherits("UKUITaskBar"))for(int i=w->metaObject()->methodOffset();i<w->metaObject()->methodCount();++i)
            methods.append(QString::fromLatin1(w->metaObject()->method(i).methodSignature()));
        QJsonObject extra;
        if(auto *scroll=qobject_cast<QAbstractScrollArea *>(w))extra={{"verticalMaximum",scroll->verticalScrollBar()->maximum()},{"verticalVisible",scroll->verticalScrollBar()->isVisible()},{"horizontalVisible",scroll->horizontalScrollBar()->isVisible()},{"value",scroll->horizontalScrollBar()->value()},{"maximum",scroll->horizontalScrollBar()->maximum()},{"pageStep",scroll->horizontalScrollBar()->pageStep()}};
        return {{"methods",methods},{"scroll",extra},{"class",w->metaObject()->className()},{"name",w->objectName()},
            {"windowClass",w->window()->metaObject()->className()},{"parentName",w->parentWidget()?w->parentWidget()->objectName():QString()},{"cell",w->property("lquiCell").toString()},{"layoutClass",w->layout()?w->layout()->metaObject()->className():""},
            {"x",g.x()},{"y",g.y()},{"w",g.width()},{"h",g.height()},
            {"visible",w->isVisible()},{"offscreen",w->testAttribute(Qt::WA_DontShowOnScreen)},{"minimum",w->minimumWidth()},
            {"iconNull",qobject_cast<QAbstractButton*>(w)?qobject_cast<QAbstractButton*>(w)->icon().isNull():true},
            {"iconWidth",qobject_cast<QAbstractButton*>(w)?qobject_cast<QAbstractButton*>(w)->iconSize().width():0},
            {"dockGroupActive",w->property("liquidDockGroupActive").toBool()},{"dockWindows",w->property("liquidDockWindows").toInt()},{"collapsed",w->property("liquidDockCollapsed").toBool()},{"dockScale",w->property("liquidDockScale").toDouble()},{"dockIconSize",w->property("liquidDockIconSize").toInt()},
            {"accessibleName",w->accessibleName()},{"tooltip",w->toolTip()},{"iconName",qobject_cast<QAbstractButton*>(w)?qobject_cast<QAbstractButton*>(w)->icon().name():QString()},{"title",w->windowTitle()},{"style",w->style()->objectName()},
            {"enabled",w->isEnabled()},
            {"layoutMinimum",w->layout()?w->layout()->minimumSize().width():-1},
            {"children",children}};
    }
    QWidget *find(const QString &name) {
        for(auto *w:QApplication::allWidgets())if(w->objectName()==name||w->accessibleName()==name)return w;
        return nullptr;
    }
    void command(const QJsonObject &request,QLocalSocket *socket) {
        if(!panel)for(auto *w:QApplication::topLevelWidgets())if(w->inherits("UKUIPanel"))panel=w;
        const QString action=request.value("action").toString();bool ok=bool(panel);
        events={};
        if(panel && action=="settings") {
            QMenu menu(panel);menu.ensurePolished();menu.show();
            qApp->processEvents();
            if(auto *a=menu.findChild<QAction *>("liquidPanelSettingsAction"))a->trigger();else ok=false;
            menu.hide();
        } else if(action=="settingsTab") {
            auto *tabs=qobject_cast<QTabBar*>(find("liquidPanelSettingsTabs"));ok=tabs;
            if(tabs)tabs->setCurrentIndex(qBound(0,request.value("index").toInt(),2));
        } else if(panel && action=="layout") {
            auto *center=qobject_cast<QCheckBox *>(find("liquidPanelCentered"));
            auto *length=qobject_cast<QSlider *>(find("liquidPanelCenteredLength"));
            ok=center && center->isEnabled() && length;
            if(ok) {
                center->setChecked(request.value("enabled").toBool());
                length->setValue(request.value("percent").toInt(68));
            }
        } else if(panel && action=="closeSettings") {
            for(auto *w:panel->findChildren<QDialog *>())w->close();
        } else if(action=="shutdown") {
            QTimer::singleShot(150,qApp,&QCoreApplication::quit);
        } else if(action=="traceDock") {
            traceDock=request.value("value").toBool();if(traceDock)dockMotion=QJsonArray();
        } else if(panel && action=="rapidBlank") {
            QAbstractScrollArea *bar=nullptr;
            for(auto *area:panel->findChildren<QAbstractScrollArea*>())if(area->inherits("UKUITaskBar")){bar=area;break;}
            ok=false;
            if(bar)for(int x=bar->viewport()->width()-2;x>0;--x) {
                const QPoint point=bar->viewport()->mapToGlobal(QPoint(x,bar->viewport()->height()/2));
                auto *hit=QApplication::widgetAt(point);bool button=false;
                for(auto *w=hit;w&&w!=bar;w=w->parentWidget())if(qobject_cast<QAbstractButton*>(w)){button=true;break;}
                if(hit&&!button) {
                    const qreal scale=panel->devicePixelRatioF();
                    ok=QProcess::startDetached("xdotool",{"mousemove",QString::number(qRound(point.x()*scale)),QString::number(qRound(point.y()*scale)),"click","--repeat","12","--delay","35","1"});break;
                }
            }
        } else if(panel && action=="dragTask") {
            auto *from=find(request.value("name").toString());auto *to=find(request.value("target").toString());ok=from&&to;
            if(ok) {
                const qreal scale=panel->devicePixelRatioF();const QPoint a=from->mapToGlobal(from->rect().center()),b=to->mapToGlobal(to->rect().center());
                ok=QProcess::startDetached("xdotool",{"mousemove",QString::number(qRound(a.x()*scale)),QString::number(qRound(a.y()*scale)),"mousedown","1","sleep","0.1",
                    "mousemove",QString::number(qRound((a.x()+12)*scale)),QString::number(qRound(a.y()*scale)),"sleep","0.15",
                    "mousemove",QString::number(qRound(b.x()*scale)),QString::number(qRound(b.y()*scale)),"sleep","0.25","mouseup","1"});
            }
        } else if(panel && action=="hidable") {
            ok=QMetaObject::invokeMethod(panel,"setHidable",Qt::DirectConnection,
                Q_ARG(bool,request.value("value").toBool()),Q_ARG(bool,false));
        } else if(panel && action=="hide") {
            ok=QMetaObject::invokeMethod(panel,"hidePanel",Qt::DirectConnection);
        } else if(panel && action=="show") {
            ok=QMetaObject::invokeMethod(panel,"showPanel",Qt::DirectConnection,Q_ARG(bool,true));
        } else if(panel && action=="taskbarRealignFrame") {
            ok=false;
            for(auto *w:panel->findChildren<QWidget*>())if(w->inherits("UKUITaskBar")){using Realign=void (*)(QWidget*);auto fn=reinterpret_cast<Realign>(dlsym(RTLD_DEFAULT,"_ZN11UKUITaskBar7realignEv"));
                if(!fn){Dl_info info{};if(dladdr(w->metaObject(),&info)&&info.dli_fname){void *module=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);if(module){fn=reinterpret_cast<Realign>(dlsym(module,"_ZN11UKUITaskBar7realignEv"));dlclose(module);}}}
                if(fn){fn(w);ok=true;}}
            auto *background=find("BackgroundWidget");if(background)background->grab();
            // Captured in the same call before a zero-delay repair could run.
            QJsonArray positions;for(auto *w:panel->findChildren<QWidget*>())if(w->inherits("UKUITaskButton"))positions.append(QJsonObject{{"name",w->objectName()},{"x",w->mapToGlobal(QPoint()).x()},{"width",w->width()}});
            panel->setProperty("probeImmediateDock",positions);
        } else if(panel && action=="realign") {
            ok=QMetaObject::invokeMethod(panel,"realign",Qt::DirectConnection);
        } else if(panel && action=="height") {
            ok=QMetaObject::invokeMethod(panel,"setPanelSize",Qt::DirectConnection,
                Q_ARG(int,request.value("value").toInt(48)),Q_ARG(bool,false));
        } else if(panel && action=="theme") {
            QMenu menu(panel);menu.ensurePolished();menu.show();qApp->processEvents();
            if(auto *a=menu.findChild<QAction *>("liquidPanelEnabledAction")) {
                if(a->isChecked()!=request.value("value").toBool())a->trigger();
            }else ok=false;menu.hide();
        } else if(action=="activateApplication") {
            const int index=request.value("index").toInt();ok=index>=0&&index<testWindows.size();
            if(ok){testWindows[index]->raise();testWindows[index]->activateWindow();}
        } else if(action=="closeFirstApplication") {
            if(!testWindows.isEmpty())delete testWindows.takeFirst();
        } else if(action=="applications") {
            const int count=request.value("count").toInt();
            while(testWindows.size()>count) delete testWindows.takeLast();
            while(testWindows.size()<count) {
                auto *w=new QWidget(nullptr, Qt::Window);
                const QString title="LayoutCompatApp"+QString::number(testWindows.size());
                w->setWindowTitle(title); w->resize(240,120);
                const QString id=QString::number(w->winId());
                const QString appClass=request.value("grouped").toBool()?QString("LayoutCompatApp0"):title;
                QProcess::execute("xdotool",{"set_window","--class",appClass,"--classname",appClass,id});w->show();
                testWindows.append(w);
            }
        } else if(action=="orb-drop") {
            auto *w=find("aiStationOrb");ok=w;
            if(w) {
                QMimeData mime;
                if(request.contains("image"))mime.setImageData(QImage(request.value("image").toString()));
                else if(request.contains("files")){QList<QUrl> urls;for(const auto &v:request.value("files").toArray())urls<<QUrl::fromLocalFile(v.toString());mime.setUrls(urls);}
                else mime.setText(request.value("text").toString());
                QDragEnterEvent enter(w->rect().center(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(w,&enter);
                QDropEvent drop(w->rect().center(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(w,&drop);ok=drop.isAccepted();
            }
        } else if(action=="orb-settings") {
            auto *w=find("aiStationOrb");ok=w;
            if(w) {
                QContextMenuEvent context(QContextMenuEvent::Mouse,w->rect().center(),w->mapToGlobal(w->rect().center()));QApplication::sendEvent(w,&context);
                auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());ok=false;
                if(menu){for(auto *action:menu->actions())if(action->text().contains("设置")){action->trigger();ok=true;}menu->close();}
            }
        } else if(action=="context") {
            auto *w=find(request.value("name").toString());ok=w;
            if(w){QContextMenuEvent context(QContextMenuEvent::Mouse,w->rect().center(),w->mapToGlobal(w->rect().center()));QApplication::sendEvent(w,&context);}
        } else if(action=="press"||action=="release") {
            auto *w=find(request.value("name").toString());ok=w;
            if(w){const QPoint local=w->rect().center(),global=w->mapToGlobal(local);QMouseEvent event(action=="press"?QEvent::MouseButtonPress:QEvent::MouseButtonRelease,local,global,Qt::LeftButton,action=="press"?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(w,&event);}
        } else if(action=="wheel") {
            QAbstractScrollArea *bar=nullptr;for(auto *w:panel->findChildren<QAbstractScrollArea*>())if(w->inherits("UKUITaskBar")){bar=w;break;}
            ok=bar;
            if(bar){auto *w=bar->viewport();const QPoint point=w->rect().center();const QPoint delta(request.value("horizontal").toBool()?request.value("delta").toInt():0,request.value("horizontal").toBool()?0:request.value("delta").toInt());
                QWheelEvent event(point,w->mapToGlobal(point),request.value("pixel").toBool()?delta:QPoint(),request.value("pixel").toBool()?QPoint():delta,Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(w,&event);}
        } else if(action=="check") {
            auto *box=qobject_cast<QCheckBox*>(find(request.value("name").toString()));ok=box&&box->isEnabled();
            if(ok)box->setChecked(request.value("value").toBool());
        } else if(action=="select") {
            auto *combo=qobject_cast<QComboBox*>(find(request.value("name").toString()));ok=combo;
            if(combo)combo->setCurrentIndex(request.value("index").toInt());
        } else if(action=="click"||action=="doubleClick") {
            auto *w=find(request.value("name").toString());ok=w;
            if(w) {
                const QPoint local=w->rect().center(),global=w->mapToGlobal(local);
                QMouseEvent press(action=="doubleClick"?QEvent::MouseButtonDblClick:QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,local,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                const int repeat=qMax(1,request.value("repeat").toInt(1));
                for(int i=0;i<repeat;++i) {
                    QApplication::sendEvent(w,&press);QApplication::sendEvent(w,&release);
                }
            }
        } else if(action=="menuPick") {
            auto *menu=qobject_cast<QMenu*>(find("liquidDockMoreMenu"));ok=false;
            if(menu&&request.value("last").toBool()&&!menu->actions().isEmpty()){menu->actions().last()->trigger();menu->close();ok=true;}
            else if(menu)for(auto *action:menu->actions())if(action->text()==request.value("text").toString()){action->trigger();menu->close();ok=true;break;}
        } else if(action=="systemControls") {
            qApp->setProperty("liquidSystemControlsEnabled",request.value("value").toBool());
        } else if(action=="dockEnable") {
            if(panel){panel->setProperty("liquidDockEnabled",request.value("value").toBool());}
        } else if(action=="hover") {
            auto *w=find(request.value("name").toString());ok=w;
            if(w)QCursor::setPos(w->mapToGlobal(w->rect().center()));
        } else if(action=="away") {
            if(panel)QCursor::setPos(panel->mapToGlobal(QPoint(panel->width()/2,-120)));
        } else if(action=="closePopups") {
            for(auto *w:QApplication::topLevelWidgets())if(qobject_cast<QMenu*>(w))w->close();
        } else if(action=="capture") {
            auto *w=find(request.value("name").toString());
            ok=w && w->grab().save(request.value("path").toString());
        }
        QPointer<QLocalSocket> guard(socket);
        QTimer::singleShot(request.value("wait").toInt(650),this,[=]{
            if(!guard)return;QJsonObject result{{"ok",ok},{"controlSyncs",qApp->property("lquiControlSyncs").toInt()},{"systemControls",qApp->property("liquidSystemControlsEnabled").toBool()},{"controlLauncher",qEnvironmentVariable("LQUI_CONTROL_CENTER_LAUNCHER")}};
            if(panel) {
                result.insert("panel",item(panel));result.insert("winId",QString::number(panel->winId()));
                result.insert("backend",panel->property("liquidPanelLayoutBackend").toString());
                result.insert("immediateDock",panel->property("probeImmediateDock").toJsonArray());
                result.insert("batches",double(panel->property("liquidPanelLayoutBatches").toULongLong()));
                result.insert("corrections",double(panel->property("liquidPanelLayoutCorrections").toULongLong()));
                result.insert("dock",QJsonObject{{"icon",panel->property("liquidDockIconSize").toInt()},{"count",panel->property("liquidDockCount").toInt()},
                    {"overflow",panel->property("liquidDockOverflow").toBool()},{"frames",double(panel->property("liquidDockWaveFrames").toULongLong())},
                    {"running",panel->property("liquidDockWaveRunning").toBool()},{"batches",double(panel->property("liquidDockLayoutBatches").toULongLong())},
                    {"resizing",panel->property("liquidDockResizing").toBool()},{"materialBuilds",panel->property("liquidMaterialBuilds").toInt()},{"materialFallbacks",panel->property("liquidMaterialFallbacks").toInt()}});
            }
            QJsonArray windows;for(auto *w:QApplication::topLevelWidgets())if(w->isVisible() && w!=panel)windows.append(item(w));
            result.insert("dockMotion",dockMotion);result.insert("calendarEvents",calendarEvents);result.insert("windows",windows);result.insert("events",events);result.insert("dndEvents",dndEvents);
            guard->write(QJsonDocument(result).toJson(QJsonDocument::Compact)+char(10));guard->flush();guard->disconnectFromServer();
        });
    }
    bool eventFilter(QObject *o,QEvent *e) override {
        if(dndEvents.size()<150 && (e->type()==QEvent::MouseButtonPress || e->type()==QEvent::MouseButtonRelease ||
            (e->type()==QEvent::MouseMove && static_cast<QMouseEvent*>(e)->buttons()!=Qt::NoButton))) {
            auto *w=qobject_cast<QWidget*>(o);auto *mouse=static_cast<QMouseEvent*>(e);
            if(w)dndEvents.append(QJsonObject{{"widget",w->objectName()},{"class",w->metaObject()->className()},{"type",int(e->type())},{"x",mouse->pos().x()},{"y",mouse->pos().y()},{"dpr",w->devicePixelRatioF()}});
        }
        if(traceDock&&o->inherits("UKUITaskButton")&&(e->type()==QEvent::Paint||e->type()==QEvent::Move||e->type()==QEvent::Resize)&&dockMotion.size()<10000){
            auto *w=qobject_cast<QWidget*>(o);if(w&&w->isVisible()){
                const QPoint pt=w->mapToGlobal(QPoint());dockMotion.append(QJsonObject{{"name",w->objectName()},{"type",int(e->type())},{"x",pt.x()},{"y",pt.y()},{"w",w->width()},{"h",w->height()},{"pressed",QApplication::mouseButtons()!=Qt::NoButton}});
            }
        }
        if(o->inherits("LunarCalendarWidget")&&e->type()==QEvent::Move){
            auto *w=qobject_cast<QWidget*>(o);
            if(w&&w->isVisible()&&calendarEvents.size()<200)calendarEvents.append(QJsonObject{{"x",w->x()},{"y",w->y()},{"opacity",w->windowOpacity()}});
        }
        if(dndEvents.size()<100&&(e->type()==QEvent::DragEnter||e->type()==QEvent::DragMove||e->type()==QEvent::Drop)) {
            auto *w=qobject_cast<QWidget*>(o);auto *drop=static_cast<QDropEvent*>(e);
            if(w)dndEvents.append(QJsonObject{{"widget",w->objectName()},{"class",w->metaObject()->className()},{"type",int(e->type())},{"actions",int(drop->possibleActions())},{"formats",QJsonArray::fromStringList(drop->mimeData()->formats())}});
        }
        if(o==panel && (e->type()==QEvent::Move || e->type()==QEvent::Resize)) {
            if(events.size()<500)events.append(QJsonObject{{"type",int(e->type())},{"x",panel->x()},{"y",panel->y()},{"w",panel->width()},{"h",panel->height()}});
        }return false;
    }
public:
    explicit LayoutProbe(QObject *parent):QObject(parent) {
        qApp->installEventFilter(this);
        connect(&server,&QLocalServer::newConnection,this,[this]{
            auto *socket=server.nextPendingConnection();
            auto consume=[this,socket]{
                if(socket->canReadLine())command(QJsonDocument::fromJson(socket->readLine()).object(),socket);
            };
            connect(socket,&QLocalSocket::readyRead,this,consume);
            consume();connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
        });
        server.listen(qEnvironmentVariable("UKUI_PANEL_LAYOUT_PROBE_SOCKET"));
    }
};
class LayoutProbeStyle : public QProxyStyle {
public:LayoutProbeStyle():QProxyStyle(QStyleFactory::create("ukuiliquidpanel")) {
    QTimer::singleShot(0,qApp,[]{new LayoutProbe(qApp);});
}};
class LayoutProbeStylePlugin : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "layout_probe.json")
public:QStyle *create(const QString &key) override {return key=="layoutprobe"?new LayoutProbeStyle:nullptr;}
};
#include "oem_layout_probe.moc"
