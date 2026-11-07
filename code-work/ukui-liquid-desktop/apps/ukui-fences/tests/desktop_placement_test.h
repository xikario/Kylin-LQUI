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
    QString wall=qEnvironmentVariable("LQUI_DRAG_WALLPAPER");
    if(wall.isEmpty()){wall=root+"/placement-wall.png";wallpaper(QSize(1440,900),1).save(wall);}
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

    moving->setSelected(true);settle(80);
    const qreal pixelScale=canvas.devicePixelRatioF();
    auto artwork=[&]{
        const QRect r=moving->contentIconRect().translated(moving->pos()).toAlignedRect().adjusted(5,5,-5,-5);
        return QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage().copy(
            qRound(r.x()*pixelScale),qRound(r.y()*pixelScale),qRound(r.width()*pixelScale),qRound(r.height()*pixelScale));
    };
    const QImage beforePress=artwork();
    QMouseEvent press(QEvent::MouseButtonPress,anchor,moving->mapToGlobal(anchor),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(moving,&press);settle(55);
    check(artwork()==beforePress,"pickup does not cover the icon with a dark ping or rescale its artwork");
    QMouseEvent release(QEvent::MouseButtonRelease,anchor,moving->mapToGlobal(anchor),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(moving,&release);

    // Sample the entire gesture, including pickup and the 140 ms landing.
    // A fixed off-centre grab point must follow the pointer without a native
    // drag window or a change in the source item's identity.
    const QPoint nativeTarget=DesktopPlacementTestAccess::cell(canvas,moving,2,4);
    const QPoint grab=anchor+QPoint(12,7);
    const QPoint start=moving->mapToGlobal(grab),end=canvas.mapToGlobal(nativeTarget+grab+QPoint(30,24));
    const qreal dpr=canvas.devicePixelRatioF();
    QTimer inspect;inspect.setInterval(8);inspect.setTimerType(Qt::PreciseTimer);
    int samples=0,nativeWindows=0,maxAnchorError=0;bool draggingSeen=false;
    QSet<QString> landingPositions;
    QImage activeFrame;
    const QString artifacts=qEnvironmentVariable("LQUI_DRAG_ARTIFACTS");
    QObject::connect(&inspect,&QTimer::timeout,&canvas,[&]{
        for(auto *window:QGuiApplication::allWindows())if(window->isVisible()&&window->inherits("QShapedPixmapWindow"))++nativeWindows;
        if(moving->isLocalDragging()) {
            draggingSeen=true;++samples;
            const QPoint expected=canvas.mapFromGlobal(QCursor::pos())-grab;
            maxAnchorError=qMax(maxAnchorError,(expected-moving->pos()).manhattanLength());
            if(samples==20 && !artifacts.isEmpty()){
                // Encode only after the gesture: PNG compression on this UI
                // thread would itself stall input and invalidate the timing.
                activeFrame=QApplication::primaryScreen()->grabWindow(0).toImage();
            }
        } else if(draggingSeen && moving->pos()!=nativeTarget)landingPositions.insert(QString::number(moving->x())+","+QString::number(moving->y()));
    });inspect.start();
    auto drive=[&](const QPoint &from,const QPoint &to,bool cancel){
        QProcess mouse;
        mouse.start("/usr/bin/python3",{"-c",QString(
            "import ctypes as c,time,subprocess\n"
            "x=c.CDLL('libX11.so.6');t=c.CDLL('libXtst.so.6')\n"
            "x.XOpenDisplay.argtypes=[c.c_char_p];x.XOpenDisplay.restype=c.c_void_p\n"
            "x.XFlush.argtypes=[c.c_void_p];x.XCloseDisplay.argtypes=[c.c_void_p]\n"
            "t.XTestFakeMotionEvent.argtypes=[c.c_void_p,c.c_int,c.c_int,c.c_int,c.c_ulong]\n"
            "t.XTestFakeButtonEvent.argtypes=[c.c_void_p,c.c_uint,c.c_int,c.c_ulong]\n"
            "d=x.XOpenDisplay(None)\n"
            "def move(a,b):t.XTestFakeMotionEvent(d,-1,a,b,0);x.XFlush(d)\n"
            "def button(v):t.XTestFakeButtonEvent(d,1,v,0);x.XFlush(d)\n"
            "try:\n move(%1,%2);time.sleep(.08);button(1);time.sleep(.06)\n"
            " begin=time.monotonic()\n"
            " for i in range(1,121):\n  move(round(%1+(%3-%1)*i/120),round(%2+(%4-%2)*i/120));time.sleep(max(0,begin+i/120-time.monotonic()))\n"
            " time.sleep(.12)\n %5\n"
            "finally:\n button(0);x.XCloseDisplay(d)\n")
            .arg(qRound(from.x()*dpr)).arg(qRound(from.y()*dpr)).arg(qRound(to.x()*dpr)).arg(qRound(to.y()*dpr))
            .arg(cancel?"subprocess.run(['xdotool','key','Escape'],check=True);time.sleep(.05)":"pass")});
        QElapsedTimer elapsed;elapsed.start();while(mouse.state()!=QProcess::NotRunning&&elapsed.elapsed()<7000)settle(10);
        check(mouse.state()==QProcess::NotRunning&&mouse.exitCode()==0,"native mouse driver finishes and releases the button");
        settle(200);
    };
    drive(start,end,false);inspect.stop();
    if(!artifacts.isEmpty()){QDir().mkpath(artifacts);activeFrame.save(artifacts+"/drag-active.png");}
    check(samples>20 && maxAnchorError<=12,"drag follows the off-centre grab point continuously (bounded by one queued input event)");
    check(nativeWindows==0,"desktop layout drag never maps a compositor-managed drag preview");
    check(landingPositions.size()>=2,"release crosses intermediate positions instead of teleporting into the cell");
    check(moving->pos()==nativeTarget,"landing animation finishes at the previewed nearest cell");
    qInfo()<<"Gesture samples / maximum queued-input distance / landing positions:"<<samples<<maxAnchorError<<landingPositions.size();
    const auto incremental=QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage();canvas.update();settle(80);
    check(incremental==QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage(),"release leaves no stale pixels after landing");
    if(!artifacts.isEmpty())QApplication::primaryScreen()->grabWindow(0).save(artifacts+"/drag-released.png");
    const QPoint corner=moving->pos()+QPoint(3,3);
    auto cornerPixel=[&]{const QImage view=QApplication::primaryScreen()->grabWindow(canvas.winId()).toImage();return view.pixel(qRound(corner.x()*dpr),qRound(corner.y()*dpr));};
    const QRgb selectedCorner=cornerPixel();moving->hide();settle(80);const QRgb wallpaperCorner=cornerPixel();moving->show();settle(80);
    check(selectedCorner==wallpaperCorner,"selected icon has no dark full-cell rectangle outside its artwork and caption");
    const QPoint savedDrop=moving->pos();
    drive(moving->mapToGlobal(grab),canvas.mapToGlobal(savedDrop+grab+QPoint(190,-90)),true);
    check(!moving->isLocalDragging() && moving->pos()==savedDrop,"Escape returns the same icon to its saved cell");
    canvas.refreshAll();settle(200);
    check(moving->pos()==savedDrop,"cancel never changes the saved layout");
    file.open(QIODevice::ReadOnly);check(file.readAll()=="untouched contents","desktop repositioning preserves file contents");
    return failures?1:0;
}
