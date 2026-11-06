#pragma once
#include <QWindow>

struct DesktopPlacementTestAccess {
    static void setup(DesktopCanvas &canvas) {
        canvas.m_autoArrange=false;canvas.m_gridColumns=8;canvas.m_gridRows=6;
    }
    static QPoint cell(DesktopCanvas &canvas,DesktopIcon *icon,int col,int row) {
        const QRect area=canvas.desktopIconArea();
        return QPoint(qRound(area.left()+(col+.5)*area.width()/8.),
                      qRound(area.top()+(row+.5)*area.height()/6.))
            -QPoint(icon->width()/2,icon->height()/2);
    }
    static void position(DesktopCanvas &canvas,DesktopIcon *icon,int col,int row) {
        const QPoint pos=cell(canvas,icon,col,row);
        canvas.m_looseIconPositions[icon->item().filePath]=pos;icon->move(pos);
    }
    static void layout(DesktopCanvas &canvas){canvas.layoutLooseIcons();}
};
static int runDesktopPlacementTest(const QString &root) {
    QSettings settings;
    for(const QString &name:{QString("systemMonitor"),QString("clockWidget"),QString("activityWidget"),QString("musicWidget"),QString("calendarWidget")})settings.setValue(name+"/autoStart",false);
    settings.sync();
    const QString desktop=root+"/desktop";QDir().mkpath(desktop);qputenv("XDG_DESKTOP_DIR",desktop.toUtf8());
    const QString path=desktop+"/placement.txt";
    QFile file(path);file.open(QIODevice::WriteOnly);file.write("untouched contents");file.close();
    const QString wall=root+"/placement-wall.png";wallpaper(QSize(1440,900),1).save(wall);
    QDir().mkpath(root+"/config/kyfences");QFile layout(root+"/config/kyfences/layout.json");layout.open(QIODevice::WriteOnly);
    layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",wall},{"wallpaperMode",2},{"fences",QJsonArray{}}}).toJson());layout.close();
    DesktopCanvas canvas;settle(1800);DesktopPlacementTestAccess::setup(canvas);
    auto icons=canvas.findChildren<DesktopIcon *>(QString(),Qt::FindDirectChildrenOnly);
    DesktopIcon *moving=nullptr,*fixed=nullptr;
    for(auto *icon:icons){if(icon->item().filePath==path)moving=icon;else if(icon->item().filePath=="computer:///")fixed=icon;}
    check(moving&&fixed,"placement fixture supplies both file and system icons");if(!moving||!fixed)return 1;
    int row=0;for(auto *icon:icons)DesktopPlacementTestAccess::position(canvas,icon,0,row++);
    DesktopPlacementTestAccess::position(canvas,fixed,4,2);
    const QPoint fixedPos=fixed->pos();
    auto drop=[&](DesktopIcon *icon,QPoint pointer,bool system){
        QMimeData mime;
        if(system)mime.setData("application/x-kyfences-sysicon",icon->item().filePath.toUtf8());
        else {mime.setUrls({QUrl::fromLocalFile(icon->item().filePath)});mime.setData("application/x-ukui-fences-file-drag","1");}
        QDragEnterEvent enter(pointer,Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&enter);
        QDropEvent event(pointer,Qt::MoveAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&canvas,&event);
        check(event.isAccepted(),"desktop drop accepted");settle(80);
    };
    const QPoint anchor=moving->contentIconRect().center().toPoint();
    QPoint target=DesktopPlacementTestAccess::cell(canvas,moving,2,2);
    // The pointer is still nearer cell 2; treating it as a top-left puts it in 3.
    const QPoint offCenter(48,25);
    drop(moving,target+anchor+offCenter,false);
    check(moving->pos()==target,"file drops use the visible artwork center without a half-widget offset");
    check(fixed->pos()==fixedPos,"unmoved icon keeps its original cell");
    // An occupied target should choose the closest free cell below, not the
    // first row above from a Manhattan-ring search.
    target=DesktopPlacementTestAccess::cell(canvas,moving,4,3);
    drop(moving,fixedPos+anchor+QPoint(0,40),false);
    check(moving->pos()==target,"occupied cell chooses the nearest physical vacancy below");
    check(fixed->pos()==fixedPos,"drop never pushes a stationary icon away");
    const QPoint systemTarget=DesktopPlacementTestAccess::cell(canvas,fixed,6,2);
    drop(fixed,systemTarget+fixed->contentIconRect().center().toPoint()+offCenter,true);
    check(fixed->pos()==systemTarget,"system and file icons use identical drop anchoring");
    drop(fixed,target+fixed->contentIconRect().center().toPoint()+QPoint(0,35),true);
    check(moving->pos()==target,"earlier system icon cannot evict a later file from its cell");
    const QPoint systemPlaced=fixed->pos();
    const QPoint saved=moving->pos();canvas.refreshAll();settle(600);
    check(moving->pos()==saved&&fixed->pos()==systemPlaced,"refresh preserves the chosen grid cells");

    // Real Xdnd tests exercise the preview window, scaling and release path.
    const QPoint nativeTarget=DesktopPlacementTestAccess::cell(canvas,moving,2,4);
    const QPoint start=moving->mapToGlobal(anchor),end=canvas.mapToGlobal(nativeTarget+anchor);
    const qreal dpr=canvas.devicePixelRatioF();
    QTimer inspect;inspect.setInterval(30);int previews=0;
    const QString artifacts=qEnvironmentVariable("LQUI_DRAG_ARTIFACTS");
    QObject::connect(&inspect,&QTimer::timeout,&canvas,[&]{
        for(auto *window:QGuiApplication::allWindows())if(window->isVisible()&&window->inherits("QShapedPixmapWindow")){
            if(previews++==5){
                check(window->flags().testFlag(Qt::NoDropShadowWindowHint),"native drag preview disables the compositor shadow");
                if(!artifacts.isEmpty()){QDir().mkpath(artifacts);QApplication::primaryScreen()->grabWindow(0).save(artifacts+"/drag-active.png");}
            }
        }
    });inspect.start();
    QProcess mouse;
    mouse.start("/usr/bin/python3",{"-c",QString(
        "import subprocess,time\ndef x(*a):subprocess.run(['xdotool']+list(map(str,a)),check=True)\n"
        "try:\n x('mousemove',%1,%2);x('mousedown',1);time.sleep(.1)\n"
        " for i in range(1,21):\n  x('mousemove',int(%1+(%3-%1)*i/20),int(%2+(%4-%2)*i/20));time.sleep(.04)\n"
        " time.sleep(.3)\nfinally:\n x('mouseup',1)\n")
        .arg(qRound(start.x()*dpr)).arg(qRound(start.y()*dpr)).arg(qRound(end.x()*dpr)).arg(qRound(end.y()*dpr))});
    QElapsedTimer elapsed;elapsed.start();while(mouse.state()!=QProcess::NotRunning&&elapsed.elapsed()<7000)settle(25);
    inspect.stop();settle(200);
    check(previews>0,"native Xdnd preview was observed");
    check(moving->pos()==nativeTarget,"native drag releases into the displayed nearest grid cell");
    const auto incremental=QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage();canvas.update();settle(80);
    check(incremental==QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage(),"release leaves no stale pixels after the drag surface disappears");
    if(!artifacts.isEmpty())QApplication::primaryScreen()->grabWindow(0).save(artifacts+"/drag-released.png");
    file.open(QIODevice::ReadOnly);check(file.readAll()=="untouched contents","desktop repositioning preserves file contents");
    return failures?1:0;
}
