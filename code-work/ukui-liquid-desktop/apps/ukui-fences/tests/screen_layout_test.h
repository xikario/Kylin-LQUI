#pragma once
#include "DesktopScreenLayout.h"
class ScreenLayoutCanvas : public DesktopCanvas {
public:
    QList<QRect> work{{0,0,1920,1152},{1920,0,1280,672}};
    QList<QRect> full{{0,0,1920,1200},{1920,0,1280,720}};
    QList<QRect> screenAreas(bool available=true) const override{return available?work:full;}
};
struct ScreenLayoutTestAccess {
    static int run(const QString &root){
        QSettings settings;
        for(const QString &key:{"clock","calendar","activity","music"})settings.setValue("desklets/"+key+"/autoStart",false);
        settings.setValue("systemMonitor/autoStart",false);settings.sync();
        ScreenLayoutCanvas canvas;canvas.resize(3201,1201);
        const auto old=canvas.m_fences;canvas.m_fences.clear();qDeleteAll(old);
        for(auto *widget:canvas.desktopWidgets())if(widget)widget->hide();
        const QRect originalMain(1100,580,400,200),originalExternal(2200,100,400,240);
        auto *main=canvas.createFence("主屏",originalMain);auto *external=canvas.createFence("副屏",originalExternal);
        auto *bottom=canvas.createFence("边界",QRect(3000,600,380,240));
        canvas.m_lastScreenAreas.clear();canvas.fitScreenLayout();
        check(main->geometry()==originalMain,"adding external area preserves the primary fence");
        check(external->geometry()==originalExternal,"external fence stays on its own output");
        check(canvas.work[1].contains(bottom->geometry()),"shorter secondary output clamps objects above its taskbar");
        check(!external->geometry().intersects(bottom->geometry()),"overflow placement avoids existing external fence");
        const auto id=external->fenceId();
        canvas.work.removeLast();canvas.full.removeLast();canvas.resize(1921,1201);canvas.fitScreenLayout();
        check(canvas.work[0].contains(external->geometry()),"disconnect recovers external fence into a visible area");
        check(main->geometry()==originalMain,"disconnect preserves fitting primary content");
        check(DesktopScreenLayout::preferred(external)==originalExternal,"temporary projection retains saved external coordinates");
        canvas.saveLayout();
        QFile file(root+"/config/kyfences/layout.json");check(file.open(QIODevice::ReadOnly),"screen layout can be persisted");
        const auto saved=QJsonDocument::fromJson(file.readAll()).object().value("fences").toArray();bool preserved=false;
        for(const auto &value:saved){const auto item=value.toObject();if(item["id"].toString()==id)preserved=item["x"].toInt()==originalExternal.x()&&item["y"].toInt()==originalExternal.y();}
        check(preserved,"saving while unplugged does not overwrite the external placement");
        canvas.work.append(QRect(1920,0,1280,672));canvas.full.append(QRect(1920,0,1280,720));canvas.resize(3201,1201);canvas.fitScreenLayout();
        check(external->geometry()==originalExternal,"reconnect restores the external fence");
        check(main->geometry()==originalMain,"reconnect preserves the primary layout");
        QWidget sample;sample.setGeometry(QRect(2100,100,300,200));
        DesktopScreenLayout::apply(&sample,sample.geometry(),QRect(1500,100,300,200));sample.move(100,200);
        check(DesktopScreenLayout::preferred(&sample)==sample.geometry(),"explicit user move supersedes a temporary screen projection");
        return failures?1:0;
    }
};
