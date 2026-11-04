#pragma once
#include "FencesSettingsWindow.h"
#include "SystemMonitor.h"
#include "GlassFinish.h"
#include <QStandardPaths>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QSlider>
#include <QTabWidget>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QComboBox>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QWheelEvent>
#include <QFileDialog>
#include <QStandardPaths>
#include <QStyle>
static int runSettingsCenterTest(const QString &root) {
    qputenv("UKUI_FENCES_SMARTSPACE_ROOTS",root.toUtf8());
    QSettings s;s.setValue("smartSpace/autoStart",false);s.setValue("systemMonitor/autoStart",false);
    for(const QString key:{"clock","activity","music","calendar"})LiquidDesklet::setAutoStart(key,false);
    DesktopCanvas canvas;canvas.showAndActivate();settle(250);
    auto settingsWindow=[]()->FencesSettingsWindow *{for(auto *w:QApplication::topLevelWidgets())if(auto *f=qobject_cast<FencesSettingsWindow *>(w))return f;return nullptr;};
    canvas.showSettingsPage("appearance");settle(400);QPointer<FencesSettingsWindow> window=settingsWindow();
    check(window,"settings window opens");if(!window)return 1;
    check(!window->windowIcon().isNull() && !window->windowIcon().pixmap(32,32).isNull(),"settings has an embedded taskbar icon");
    check(window->windowRole()=="fences-settings","settings has its own window role");
    check(window->windowType()==Qt::Window && !window->parentWidget()
        && window->windowFlags().testFlag(Qt::FramelessWindowHint)
        && window->windowFlags().testFlag(Qt::WindowMinimizeButtonHint)
        && window->windowFlags().testFlag(Qt::WindowCloseButtonHint)
        && !window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint),
        "settings expose an independent normal frameless window with minimize and close but no maximize");
    auto *minimize=window->findChild<QPushButton *>("settingsMinimize");
    auto *closeButton=window->findChild<QPushButton *>("settingsClose");
    check(minimize && closeButton,"settings own titlebar exposes identifiable minimize and close controls");
    check(!window->findChild<QPushButton *>("liquidPopupPreviewButton"),"unused liquid preview entry is removed");
    canvas.showUnifiedSettings();check(settingsWindow()==window,"repeated entry reuses the same window");
    check(!canvas.globalEditMode(),"opening settings does not enable layout editing");
    check(!canvas.smartSpaceVisible() && !canvas.systemMonitorVisible(),"opening settings does not instantiate hidden components");
    settle(700); // Initial asynchronous wallpaper delivery may invalidate the first surface.
    const int materialBuilds=window->property("materialBuilds").toInt();
    for(int i=0;i<5;++i)window->grab();settle(250);
    check(materialBuilds>0 && materialBuilds==window->property("materialBuilds").toInt(),"settings repaint reuses cached glass");
    const QPoint beforeMove=window->pos();
    for(int i=1;i<=24;++i){window->move(beforeMove+QPoint(i*2,i));settle(8);}
    window->resize(window->size()+QSize(10,10));settle(180);
    check(materialBuilds==1 && window->property("materialBuilds").toInt()==1,"drag and resize reuse the one opening capture");
    check(window->styleSheet().isEmpty() && window->palette()==QApplication::palette(),"native dialog parent retains default style and palette");
    check(!window->windowFlags().testFlag(Qt::WindowStaysOnTopHint),"settings permits switching to another application");
    QProcess nativeIdentity;
    nativeIdentity.start("xprop",{"-id",QString::number(window->winId()),"WM_CLASS","_KDE_NET_WM_DESKTOP_FILE",
        "_NET_WM_WINDOW_TYPE","_NET_WM_STATE"});
    const bool readIdentity=nativeIdentity.waitForFinished(2000);
    const QByteArray nativeProperties=nativeIdentity.readAllStandardOutput();
    check(readIdentity && nativeProperties.count("ukui-fences-settings")>=3
        && nativeProperties.contains("_NET_WM_WINDOW_TYPE_NORMAL")
        && !nativeProperties.contains("_NET_WM_STATE_ABOVE")
        && !nativeProperties.contains("_NET_WM_STATE_SKIP_TASKBAR"),
        "settings native identity is a normal task-managed window without a permanent above state");
    {
        QFileDialog chooser(window,"选择 Fences 壁纸");
        check(!chooser.testOption(QFileDialog::DontUseNativeDialog),"wallpaper chooser permits the system native dialog");
        check(chooser.styleSheet().isEmpty() && chooser.palette()==QApplication::palette(),"native chooser does not inherit liquid content styling");
    }
    if(minimize)minimize->click();settle(60);check(window->isMinimized(),"settings titlebar button minimizes the actual window");
    window->showNormal();settle(60);check(window->isVisible() && !window->isMinimized(),"settings restores after explicit minimize");
    window->move(beforeMove);
    QMouseEvent outside(QEvent::MouseButtonPress,QPoint(3,3),canvas.mapToGlobal(QPoint(3,3)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&canvas,&outside);canvas.refreshAll();settle(1400);
    check(window->isVisible() && !window->isMinimized(),"outside desktop click and refresh do not hide settings");
    check(window->property("materialBuilds").toInt()==1,"wallpaper refresh does not replace frozen settings material");
    if(Display *display=XOpenDisplay(nullptr)){
        Window root,parent,*children=nullptr;unsigned int count=0;int desktopIndex=-1,settingsIndex=-1;
        XQueryTree(display,DefaultRootWindow(display),&root,&parent,&children,&count);
        for(unsigned int i=0;i<count;++i){if(children[i]==canvas.winId())desktopIndex=i;if(children[i]==window->winId())settingsIndex=i;}
        check(settingsIndex>desktopIndex && desktopIndex>=0,"desktop restacking stays below the settings window");
        if(children)XFree(children);XCloseDisplay(display);
    }
    for(const QString id:{"wallpaper","icons","layout","widgets","monitor","clock","activity","music","calendar","sync","session","help"}){window->openPage(id);settle(30);}
    check(!canvas.smartSpaceVisible() && !canvas.systemMonitorVisible(),"browsing all pages does not start hidden models");
    window->openPage("appearance");
    auto *glass=window->findChild<QCheckBox *>("fenceLiquidGlass");
    auto *scrim=window->findChild<QComboBox *>("glassScrim");
    auto *motion=window->findChild<QCheckBox *>("reduceMotion");
    auto *glassReset=window->findChild<QPushButton *>("fencesAppearanceResetDefaults");
    check(glass&&scrim&&motion&&glassReset,"liquid appearance page exposes its scoped default action");
    if(glass&&scrim&&motion&&glassReset){glass->setChecked(true);scrim->setCurrentIndex(3);motion->setChecked(true);glassReset->click();
        check(!canvas.fenceLiquidGlassEnabled()&&canvas.glassScrim()==int(GlassFinish::Scrim::Balanced)&&!canvas.reduceMotion(),
              "liquid appearance reset restores its real initial settings");}
    s.setValue("desklets/activity/recording",false);s.setValue("desklets/activity/metricsEnabled",false);
    s.setValue("desklets/activity/metricsInterval",30);s.setValue("desklets/activity/logRetentionDays",21);s.sync();
    window->openPage("activity");
    auto *activityRecording=window->findChild<QCheckBox *>("activityRecordingEnabled");
    auto *metrics=window->findChild<QCheckBox *>("activityMetricsEnabled");
    auto *metricsInterval=window->findChild<QSlider *>("activityMetricsInterval");
    auto *retention=window->findChild<QComboBox *>("activityLogRetention");
    auto *samplingReset=window->findChild<QPushButton *>("activitySamplingResetDefaults");
    check(activityRecording&&metrics&&metricsInterval&&retention&&samplingReset,"activity page exposes recording and sampling defaults");
    if(activityRecording&&metrics&&metricsInterval&&retention&&samplingReset){
        samplingReset->click();s.sync();
        check(s.value("desklets/activity/recording").toBool()&&s.value("desklets/activity/metricsEnabled").toBool()
                  &&s.value("desklets/activity/metricsInterval").toInt()==10,
              "activity reset restores recording and the 10 second resource sampling interval");
        check(s.value("desklets/activity/logRetentionDays").toInt()==21,
              "activity reset preserves the user's history retention period");
    }
    s.setValue("desklets/activity/recording",true);s.setValue("desklets/activity/metricsEnabled",true);
    s.setValue("desklets/activity/metricsInterval",10);s.setValue("desklets/activity/logRetentionDays",7);s.sync();
    for(auto *list:window->findChildren<QListWidget *>())
        for(int row=0;row<list->count();++row)
            check(list->item(row)->data(Qt::UserRole).toString()!="smart" && list->item(row)->data(Qt::UserRole).toString()!="cli", "settings navigation has no retired AI pages");
    window->openPage("smart");
    check(QSettings().value("settingsWindow/page").toString()!="smart", "retired Smart Space page restores to a valid Fences page");
    window->openPage("wallpaper");
    auto *wallpage=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
    auto *combo=wallpage->findChildren<QComboBox *>().last();const int oldIndex=combo->currentIndex();
    check(!combo->style()->styleHint(QStyle::SH_ComboBox_Popup,nullptr,combo),"combo uses a normal scrolling list instead of menu scrollers");
    combo->showPopup();settle(100);
    auto *popup=combo->view()->window();
    check(popup->isVisible() && popup->property("settingsComboPopupSurface").toBool(),"ordinary combo list opens with liquid surface");
    check(!combo->findChild<QToolButton *>("liquidWheelButton"),"settings no longer intercepts controls with a wheel");
    QDir().mkpath("artifacts");popup->grab().save(QString("artifacts/settings-picker-%1.png").arg(window->devicePixelRatioF()));
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(combo->view(),&escape);
    combo->hidePopup();check(combo->currentIndex()==oldIndex && window->isVisible(),"Escape closes list without closing settings");
    QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);QApplication::sendEvent(combo,&down);
    check(combo->currentIndex()==qMin(oldIndex+1,combo->count()-1),"ordinary combo keeps native keyboard selection");
    window->openPage("help");
    auto *desktopToggle=window->findChild<QPushButton *>("desktopVisibilityButton");
    check(desktopToggle && desktopToggle->text()=="切换到系统桌面","desktop switch starts with its current target");
    if(desktopToggle){
        canvas.setClockWidgetVisible(true);settle(100);
        auto *clock=canvas.findChild<ClockDesklet *>();
        const QRect before=clock?clock->geometry():QRect();
        desktopToggle->click();settle(80);
        check(!canvas.fencesDesktopVisible() && window->isVisible() && desktopToggle->text()=="切换回 Fences 桌面","switching to system desktop keeps a visible return action");
        if(minimize)minimize->click();settle(40);canvas.showUnifiedSettings();settle(60);
        check(!canvas.fencesDesktopVisible() && desktopToggle->text()=="切换回 Fences 桌面","reopening settings retains the return-to-Fences action");
        desktopToggle->click();settle(150);
        check(canvas.fencesDesktopVisible() && clock && clock->isVisible() && clock->geometry()==before,"return switch restores desktop and existing widget geometry");
        canvas.setClockWidgetVisible(false);
        canvas.hideFences();check(desktopToggle->text()=="切换回 Fences 桌面","external hide updates settings immediately");
        canvas.showAndActivate();
    }
    window->openPage("icons");
    auto *iconForm=window->findChild<QWidget *>("iconAppearanceDialog");
    check(iconForm,"icon editor exists as embedded QWidget");
    const int original=IconAppearance::load().strength;
    const int draftStrength=original==70?71:70;
    iconForm->findChild<QSlider *>("iconGlassStrength")->setValue(draftStrength);
    window->openPage("help");window->openPage("icons");
    check(iconForm==window->findChild<QWidget *>("iconAppearanceDialog"),"page navigation retains an unapplied draft");
    if(minimize)minimize->click();settle(60);
    check(window->isMinimized() && iconForm->property("settingsDirty").toBool(),
        "titlebar minimization retains the unapplied appearance draft");
    canvas.showUnifiedSettings();settle(60);
    check(settingsWindow()==window && window->isVisible() && !window->isMinimized()
        && iconForm==window->findChild<QWidget *>("iconAppearanceDialog")
        && iconForm->findChild<QSlider *>("iconGlassStrength")->value()==draftStrength
        && IconAppearance::load().strength==original,
        "restoring settings reuses the same controls and preserves the draft without saving it");
    QTimer::singleShot(30,window,[]{if(auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget()))box->reject();});
    window->close();check(window && window->isVisible(),"canceling close keeps the draft available");
    QTimer::singleShot(30,window,[]{if(auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget()))box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Discard)->click();});
    window->close();settle(100);
    // Model the outer event-loop boundary; nested settle() alone need not
    // deliver the WA_DeleteOnClose deferred deletion scheduled by close().
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(window.isNull(),"discard closes and destroys draft controls");
    check(IconAppearance::load().strength==original,"discard does not persist draft appearance");
    canvas.showSettingsPage("icons");window=settingsWindow();settle(80);
    iconForm=window->findChild<QWidget *>("iconAppearanceDialog");
    iconForm->findChild<QSlider *>("iconGlassStrength")->setValue(61);
    iconForm->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    check(IconAppearance::load().strength==61 && !iconForm->property("settingsDirty").toBool(),"apply persists appearance and clears draft state");
    window->openPage("widgets");
    QCheckBox *startup=nullptr,*visible=nullptr;
    for(auto *c:window->findChildren<QCheckBox *>()){
        if(c->property("startupKey").toString()=="clock")startup=c;
        if(c->property("visibilityKey").toString()=="clock")visible=c;
    }
    check(startup && visible,"component has distinct visibility and startup controls");
    startup->setChecked(true);check(LiquidDesklet::autoStartEnabled("clock") && !canvas.clockWidgetVisible(),"startup choice does not immediately show a component");
    visible->setChecked(true);check(canvas.clockWidgetVisible(),"visibility switch reveals component");
    visible->setChecked(false);check(!canvas.clockWidgetVisible() && LiquidDesklet::autoStartEnabled("clock"),"hiding does not erase startup choice");
    for(auto *c:window->findChildren<QCheckBox *>())
        check(c->property("visibilityKey").toString()!="cli"&&c->property("startupKey").toString()!="cli","independent AI station has no Fences visibility or startup switch");
    check(!window->findChild<QPushButton *>("cliOpenButton"),"independent AI station has no Fences launcher");

    s.setValue("systemMonitor/autoStart",false);s.setValue("systemMonitor/skin",int(SystemMonitor::Skin::Cyber));
    s.setValue("systemMonitor/scale",1.3);s.setValue("systemMonitor/compact",true);s.setValue("systemMonitor/title","测试标题");
    s.setValue("systemMonitor/fontFamily","DejaVu Sans");s.setValue("systemMonitor/fontSize",14);s.setValue("systemMonitor/opacity",65);
    s.setValue("systemMonitor/premiumAesthetics",true);s.setValue("systemMonitor/statInterval",17);
    s.setValue("systemMonitor/sortByMemory",true);s.setValue("systemMonitor/showFramework",false);
    s.setValue("systemMonitor/position",QPoint(120,140));s.sync();
    canvas.showSystemMonitorWidget();settle(400);
    auto *monitor=canvas.findChild<SystemMonitor *>();
    const QPoint monitorPosition=monitor?monitor->pos():QPoint();
    for(const QString id:{"monitor"}){
        window->openPage(id);settle(100);
        auto *current=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
        auto forms=current->findChildren<QDialogButtonBox *>();
        check(!forms.isEmpty(),"complex component form is embedded without a modal dialog");
        check(!QApplication::activeModalWidget(),"complex page leaves desktop interactive");
        if(id=="monitor" && !forms.isEmpty()){
            auto *resetVisual=current->findChild<QPushButton *>("monitorVisualResetDefaults");
            auto *samplingDraft=current->findChild<QSpinBox *>("monitorSampleInterval");
            auto *sortDraft=current->findChild<QComboBox *>("monitorSortBy");
            auto *frameworkDraft=current->findChild<QCheckBox *>("monitorFramework");
            QWidget *monitorForm=nullptr;for(auto *form:current->findChildren<QWidget *>())if(form->property("settingsForm").toBool())monitorForm=form;
            check(monitor&&resetVisual&&samplingDraft&&sortDraft&&frameworkDraft,"monitor page exposes a visual-only reset");
            if(monitor&&resetVisual&&samplingDraft&&sortDraft&&frameworkDraft&&monitorForm){
                samplingDraft->setValue(23);sortDraft->setCurrentIndex(sortDraft->findData("cpu"));frameworkDraft->setChecked(true);
                resetVisual->click();s.sync();
                check(monitor->skin()==SystemMonitor::Skin::Liquid&&s.value("systemMonitor/scale").toDouble()==1.0
                          &&!s.value("systemMonitor/compact").toBool()
                          &&s.value("systemMonitor/title").toString()=="桌面资源监控"
                          &&s.value("systemMonitor/fontFamily").toString().isEmpty()
                          &&s.value("systemMonitor/fontSize").toInt()==9&&s.value("systemMonitor/opacity").toInt()==100
                          &&!s.value("systemMonitor/premiumAesthetics").toBool(),
                      "monitor visual reset restores its shipped size, skin, title, font and opacity");
                check(monitor->pos()==monitorPosition&&s.value("systemMonitor/statInterval").toInt()==17
                          &&s.value("systemMonitor/sortByMemory").toBool()&&!s.value("systemMonitor/showFramework").toBool()
                          &&!s.value("systemMonitor/autoStart").toBool(),
                      "monitor visual reset preserves position, sampling, sorting, framework and startup state");
                check(samplingDraft->value()==23&&sortDraft->currentData().toString()=="cpu"&&frameworkDraft->isChecked()
                          &&monitorForm->property("settingsDirty").toBool(),
                      "monitor visual reset leaves unrelated functional drafts untouched");
                samplingDraft->setValue(17);sortDraft->setCurrentIndex(sortDraft->findData("memory"));frameworkDraft->setChecked(false);
                monitorForm->setProperty("settingsDirty",false);
            }
            auto *interval=current->findChildren<QSpinBox *>().last();
            const int value=interval->value();interval->stepUp();
            check(interval->buttonSymbols()!=QAbstractSpinBox::NoButtons,"spinbox retains normal step buttons");
            check(interval->value()==qMin(value+1,interval->maximum()),"normal step button updates the existing spinbox");interval->setValue(3);
            forms.first()->button(QDialogButtonBox::Apply)->click();
            check(s.value("systemMonitor/statInterval").toInt()==3,"monitor apply keeps sampling configuration persistence");
        }
        QDir().mkpath("artifacts");window->grab().save(QString("artifacts/settings-%1-%2.png").arg(id).arg(window->devicePixelRatioF()));
    }
    window->openPage("monitor");canvas.toggleDesklet();settle(150);window->openPage("monitor");
    check(window && window->isVisible(),"removing component while its settings exist is safe");
    auto *fence=canvas.createFence("设置测试",QRect(200,200,350,280));
    const QString keptFile=root+"/字体与网格重置保留.txt";QFile kept(keptFile);kept.open(QIODevice::WriteOnly);kept.write("keep");kept.close();
    fence->addItem(DesktopItem::fromPath(keptFile));
    const QRect fenceGeometry=fence->geometry();
    window->openPage("fence:"+fence->fenceId());
    auto *fencePage=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
    auto fontForms=fencePage->findChildren<QDialogButtonBox *>();
    check(fontForms.size()==2,"both per-fence font editors are embedded");
    if(!fontForms.isEmpty())fontForms.first()->button(QDialogButtonBox::Apply)->click();
    check(fence->hasLocalFont(),"per-fence font apply updates the original model");
    canvas.setFontFamily("DejaVu Sans");canvas.setFontSize(19);canvas.setFontColor(QColor("#223344"));
    canvas.setFontBold(true);canvas.setFontItalic(true);
    window->openPage("help");window->openPage("icons");settle(50);
    auto *fontReset=window->findChild<QPushButton *>("desktopFontResetDefaults");
    check(fontReset,"desktop font page exposes its standalone default action");
    if(fontReset)fontReset->click();settle(80);
    check(canvas.fontFamily().isEmpty()&&canvas.fontSize()==11&&canvas.fontColor()==QColor(Qt::white),
          "desktop font reset restores system-family, 11 px and white defaults");
    QWidget *desktopFontForm=nullptr;auto *iconsPage=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
    for(auto *form:iconsPage->findChildren<QWidget *>())if(form->windowTitle()=="桌面字体设置")desktopFontForm=form;
    check(desktopFontForm&&!desktopFontForm->findChildren<QCheckBox *>().isEmpty()
              &&!desktopFontForm->findChildren<QCheckBox *>().first()->isChecked()
              &&!desktopFontForm->findChildren<QCheckBox *>().last()->isChecked(),
          "desktop font form is refreshed to normal non-italic defaults after reset");
    QPointer<QWidget> oldFontForm=desktopFontForm;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    if(fontReset){fontReset->click();settle(80);}
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(oldFontForm.isNull(),"repeated font reset replaces the latest form after deferred deletion");
    int fontFormCount=0;
    for(auto *form:iconsPage->findChildren<QWidget *>())if(form->windowTitle()=="桌面字体设置")++fontFormCount;
    check(fontFormCount==1,"repeated font reset retains exactly one live font form");
    for(int i=0;i<4;++i){
        const QPoint local(120,120),global=canvas.mapToGlobal(local);
        QWheelEvent scaleWheel(QPointF(local),QPointF(global),QPoint(),QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&canvas,&scaleWheel);
    }
    window->openPage("layout");settle(50);
    auto *layoutPage=window->findChild<QStackedWidget *>("settingsPages")->currentWidget();
    QWidget *gridForm=nullptr;for(auto *form:layoutPage->findChildren<QWidget *>())if(form->windowTitle()=="桌面图标网格")gridForm=form;
    auto gridSpins=gridForm?gridForm->findChildren<QSpinBox *>():QList<QSpinBox *>{};
    if(gridSpins.size()>=3){gridSpins[0]->setValue(12);gridSpins[1]->setValue(7);gridSpins[2]->setValue(135);
        if(auto *box=gridForm->findChild<QDialogButtonBox *>())box->button(QDialogButtonBox::Apply)->click();}
    auto *gridReset=window->findChild<QPushButton *>("desktopGridResetDefaults");
    check(gridReset,"layout page exposes a grid-only default action");
    if(gridReset)gridReset->click();settle(80);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    if(gridReset){gridReset->click();gridReset->click();settle(80);}
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    int gridFormCount=0;
    for(auto *form:layoutPage->findChildren<QWidget *>())if(form->windowTitle()=="桌面图标网格")++gridFormCount;
    check(gridFormCount==1,"repeated and rapid grid resets retain exactly one live grid form");
    const QString layoutPath=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/kyfences/layout.json";
    QFile savedLayout(layoutPath);savedLayout.open(QIODevice::ReadOnly);const auto layoutData=QJsonDocument::fromJson(savedLayout.readAll()).object();
    check(layoutData.value("iconScale").toDouble()==1.0&&layoutData.value("desktopIconScale").toDouble()==1.0
              &&layoutData.value("gridColumns").toInt()==18&&layoutData.value("gridRows").toInt()==11,
          "grid reset writes the shipped 18 by 11, 100 percent defaults");
    check(canvas.findChildren<FenceWidget *>(QString(),Qt::FindDirectChildrenOnly).contains(fence)
              &&fence->geometry()==fenceGeometry&&fence->items().size()==1
              &&fence->items().first().filePath==keptFile&&QFileInfo::exists(keptFile),
          "font and grid resets preserve the existing partition, its files and its position");
    window->openPage("appearance");settle(300);QDir().mkpath("artifacts");
    window->grab().save(QString("artifacts/settings-center-%1.png").arg(window->devicePixelRatioF()));
    window->close();settle(100);
    return failures?1:0;
}
