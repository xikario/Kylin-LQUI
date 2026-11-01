#include "LiquidDialog.h"
#include "../../../shared/async-work/BackgroundTask.h"
#include "PanelController.h"
#include "CohesiveGlass.h"
#include "SettingsBridge.h"
#include "PanelAiStation.h"
#include "PanelBehavior.h"
#include "PanelLayoutCompat.h"
#include "PanelDockCompat.h"
#include "../../../shared/icon-style/IconPolicy.h"
#include "PanelSystemControls.h"
#include "PanelNotificationHistory.h"
#include "PanelLauncherBridge.h"
#include "LiquidMaterialPreparation.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QWidget>
#include <QMenu>
#include <QEvent>
#include <QDynamicPropertyChangeEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFormLayout>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QScreen>
#include <QFile>
#include <QMouseEvent>
#include <QPushButton>
#include <QFileInfo>
#include <QStyle>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QMetaMethod>
#include <QLayout>
#include "PanelNative.h"
#include "WindowEffectsSettings.h"
#include <QTabBar>
#include <QJsonArray>
#include <QJsonObject>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>

static bool sameMaterialRect(const QRect &a,const QRect &b) {
    // Fractional X11 scaling round-trips an OEM realign between adjacent
    // logical sizes (e.g. 1124x48 and 1125x49). This is not a new backdrop.
    return a.isValid()&&b.isValid()&&qAbs(a.x()-b.x())<=1&&qAbs(a.y()-b.y())<=1
        &&qAbs(a.width()-b.width())<=1&&qAbs(a.height()-b.height())<=1;
}
#include <QButtonGroup>
#include <QSignalBlocker>

namespace {
void addPanelLayoutSettings(QFormLayout *form, QWidget *dialog, QWidget *panel,
                            const QString &preferences)
{
    auto *centered=new QCheckBox("任务栏居中",dialog);
    centered->setObjectName("liquidPanelCentered");
    form->addRow(centered);
    auto *row=new QWidget(dialog);
    auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
    auto *length=new QSlider(Qt::Horizontal,row);
    length->setObjectName("liquidPanelCenteredLength");length->setRange(30,95);
    length->setToolTip("默认保持这个居中宽度。应用增多时，只缩小应用图标和间距；不会改变总长度。");
    auto *number=new QLabel(row);number->setMinimumWidth(42);
    layout->addWidget(length);layout->addWidget(number);form->addRow("基础长度（%）",row);
    auto *expand=new QCheckBox("应用增多时自动延长",dialog);expand->setObjectName("liquidDockAutoExpand");form->addRow(expand);
    expand->setToolTip("默认关闭。只有主动开启本项，任务栏才随应用数量增长至指定上限。");
    auto *maximum=new QSlider(Qt::Horizontal,dialog);maximum->setObjectName("liquidDockMaximumLength");maximum->setRange(30,98);
    maximum->setToolTip("最大长度不小于基础长度。达到上限后，应用图标与间距持续按比例缩小，保持单行；不显示更多按钮或滚动条，保留系统信息区。");
    form->addRow("延长上限（%）",maximum);
    auto *hint=new QLabel("居中整个任务栏；取消后铺满屏幕边缘。图标较多时，系统会保留容纳图标所需的最小长度。",dialog);
    hint->setWordWrap(true);form->addRow(hint);

    auto *compat=panel->findChild<PanelLayoutCompat *>(QString(),Qt::FindDirectChildrenOnly);
    if(!compat) { centered->setEnabled(false);row->setEnabled(false);expand->setEnabled(false);maximum->setEnabled(false);return; }
    centered->setChecked(compat->centered());
    expand->setChecked(compat->autoExpand());maximum->setValue(compat->maximumLength());
    expand->setEnabled(compat->supported()&&centered->isChecked());maximum->setEnabled(expand->isEnabled()&&expand->isChecked());
    QSettings saved(preferences,QSettings::IniFormat);
    const QString panelName=panel->objectName().mid(QStringLiteral("UKUIPanel ").size());
    const int remembered=saved.value("layout/"+panelName+"/centeredLength",68).toInt();
    length->setValue(qBound(30,compat->centered()?compat->length():remembered,95));
    number->setText(QString::number(length->value())+"%");
    row->setEnabled(compat->supported() && centered->isChecked());
    centered->setEnabled(compat->supported());
    hint->setText(compat->explanation());
    QObject::connect(compat,&PanelLayoutCompat::changed,dialog,[=]{
        centered->setEnabled(compat->supported());
        row->setEnabled(compat->supported() && centered->isChecked());
        expand->setEnabled(compat->supported()&&centered->isChecked());maximum->setEnabled(expand->isEnabled()&&expand->isChecked());
        hint->setText(compat->explanation());
    });
    const QPointer<PanelLayoutCompat> guard(compat);
    auto apply=[guard,centered,length,hint]{
        if(guard && !guard->setCentered(centered->isChecked(),length->value()))
            hint->setText("任务栏未能应用或保存布局，请检查支持范围及配置文件权限。");
    };
    QObject::connect(centered,&QCheckBox::toggled,dialog,[row,apply](bool on){row->setEnabled(on);apply();});
    // Dragging previews only the number; commit once on release to avoid
    // repeated native dock relayout/material rebuilds. Keyboard changes apply.
    QObject::connect(length,&QSlider::valueChanged,dialog,[length,number,apply](int value){
        number->setText(QString::number(value)+"%");if(!length->isSliderDown())apply();
    });
    QObject::connect(length,&QSlider::sliderReleased,dialog,apply);
    auto applyExpansion=[guard,expand,maximum]{if(guard)guard->setAutoExpand(expand->isChecked(),maximum->value());};
    QObject::connect(expand,&QCheckBox::toggled,dialog,[=](bool on){maximum->setEnabled(on&&centered->isChecked());applyExpansion();});
    QObject::connect(centered,&QCheckBox::toggled,dialog,[=](bool on){expand->setEnabled(on);maximum->setEnabled(on&&expand->isChecked());});
    QObject::connect(maximum,&QSlider::valueChanged,dialog,[=]{if(!maximum->isSliderDown())applyExpansion();});
    QObject::connect(maximum,&QSlider::sliderReleased,dialog,applyExpansion);
}
}

QString PanelController::configFile() const {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/ukui/liquid-panel.ini";
}
PanelController::PanelController(QObject *parent):QObject(parent) {
    auto *settingsBridge=new LquiSettings::Bridge("panel",[this](const QString &page)->QWidget* {
        QWidget *owner=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->inherits("UKUIPanel")){owner=w;break;}if(!owner)return nullptr;
        settingsDialog(owner);if(!m_dialog)return nullptr;
        auto *tabs=m_dialog->findChild<QTabBar*>("liquidPanelSettingsTabs");auto *stack=m_dialog->findChild<QStackedWidget*>("liquidPanelSettingsStack");
        const int index=page=="layout"?1:page=="effects"?2:0;if(tabs)tabs->setCurrentIndex(index);return stack?stack->widget(index):nullptr;
    },[]{return QJsonArray{QJsonObject{{"id","material"},{"title","任务栏材质"},{"group","任务栏"}},QJsonObject{{"id","layout"},{"title","Dock 与托盘"},{"group","任务栏"}},QJsonObject{{"id","effects"},{"title","窗口与动画"},{"group","窗口"}}};},this);
    settingsBridge->registerOnSession();
    WindowEffectsSettings::recoverPendingTrial();
    QSettings s(configFile(),QSettings::IniFormat);
    m_refraction=qBound(0.,s.value("appearance/refraction",3.5).toDouble(),8.);
    m_clarity=qBound(0.,s.value("appearance/clarity",.65).toDouble(),1.);
    m_liquidStrength=qBound(0.,s.value("appearance/liquidStrength",1.).toDouble(),2.);
    m_transparency=qBound(0.,s.value("appearance/transparency",.35).toDouble(),.65);
    m_seeThrough=s.value("appearance/seeThrough",false).toBool();
    m_chroma=qBound(0.,s.value("appearance/chroma",.82).toDouble(),1.3);
    m_materialStyle=s.value("appearance/materialStyle","liquid").toString();
    if(m_materialStyle!="liquid"&&m_materialStyle!="soft"&&m_materialStyle!="frosted")m_materialStyle="liquid";
    m_pointerHighlight=qBound(0.,s.value("appearance/pointerHighlight",.16).toDouble(),1.);
    m_enabled=s.value("appearance/enabled",true).toBool();
    m_followWallpaper=s.value("appearance/followWallpaper",true).toBool();
    m_reducedMotion=s.value("appearance/reducedMotion",false).toBool();
    m_dockAdaptive=s.value("dock/adaptive",true).toBool();m_dockClickFeedback=s.value("dock/clickFeedback",true).toBool();
    m_dockMagnification=s.value("dock/hoverMagnification",false).toBool();
    m_surface.opacity=qBound(.15,s.value("appearance/opacity",.58).toDouble(),.95);
    m_surface.radius=qBound(0.,s.value("appearance/radius",16.).toDouble(),24.);
    m_surface.highlight=qBound(0.,s.value("appearance/highlight",.32).toDouble(),1.);
    LiquidPopup::install(*qApp);
    LiquidPopup::setEnabled(m_enabled);
    LiquidPopup::theme().reducedMotion=m_reducedMotion;
    LiquidPopup::theme().highlight=m_surface.highlight;
    QTimer::singleShot(0, this, [this] { refreshBackdrop(true); });
    m_wallpaperRefresh.setSingleShot(true);m_wallpaperRefresh.setInterval(250);
    connect(&m_wallpaperRefresh,&QTimer::timeout,this,[this]{if(m_followWallpaper)refreshBackdrop(false);});
    connect(&m_wallpaperWatcher,&QFileSystemWatcher::fileChanged,this,[this]{m_wallpaperRefresh.start();});
    connect(&m_wallpaperWatcher,&QFileSystemWatcher::directoryChanged,this,[this]{m_wallpaperRefresh.start();});
    updateWallpaperWatchers();
    m_hoverRefresh.setSingleShot(true);m_hoverRefresh.setInterval(32);
    m_previewRefresh.setSingleShot(true);m_previewRefresh.setInterval(16);
    connect(&m_previewRefresh,&QTimer::timeout,this,&PanelController::flushPreview);
    m_configSave.setSingleShot(true);m_configSave.setInterval(180);
    connect(&m_configSave,&QTimer::timeout,this,&PanelController::saveConfiguration);
    connect(qApp,&QCoreApplication::aboutToQuit,this,[this]{if(m_configSave.isActive())saveConfiguration();});
    connect(&m_hoverRefresh,&QTimer::timeout,this,[this]{
        const auto damage=m_pointerDamage;
        m_pointerDamage.clear();
        for(auto i=damage.cbegin();i!=damage.cend();++i)
            if(i.key()->isVisible()) i.key()->update(i.value());
    });
    qApp->installEventFilter(this);
    qApp->setProperty("liquidSystemControlsEnabled",s.value("controls/compact",true).toBool());
    new PanelSystemControls(this);
    qApp->setProperty("liquidNotificationHistory",s.value("controls/notificationHistory",true).toBool());
    new PanelNotificationHistory(this);
    new PanelBehavior(this);
    new PanelAiStation(this);
    new PanelLauncherBridge(this);
    if(!qEnvironmentVariable("UKUI_LIQUID_PANEL_INSPECT").isEmpty()) {
        QTimer::singleShot(6000,this,[]{
            std::function<QJsonObject(QWidget *)> item=[&](QWidget *w){
                QJsonObject data{{"class",w->metaObject()->className()},{"name",w->objectName()},
                    {"visible",w->isVisible()},{"x",w->x()},{"y",w->y()},{"w",w->width()},{"h",w->height()},
                    {"gx",w->mapToGlobal(QPoint()).x()},{"gy",w->mapToGlobal(QPoint()).y()},
                    {"dpr",w->devicePixelRatioF()}};
                QJsonArray methods,properties,children,layout;
                for(int i=w->metaObject()->methodOffset();i<w->metaObject()->methodCount();++i)
                    methods.append(QString::fromLatin1(w->metaObject()->method(i).methodSignature()));
                for(const QMetaObject *m=w->metaObject();m && QByteArray(m->className())!="QWidget";m=m->superClass())
                    for(int i=m->propertyOffset();i<m->propertyCount();++i) {
                        auto p=m->property(i);properties.append(QJsonObject{{"name",p.name()},{"value",QJsonValue::fromVariant(p.read(w))}});
                    }
                for(auto *child:w->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly))children.append(item(child));
                if(w->layout())for(int i=0;i<w->layout()->count();++i)
                    if(auto *child=w->layout()->itemAt(i)->widget())layout.append(QJsonObject{{"class",child->metaObject()->className()},{"name",child->objectName()},{"visible",child->isVisible()}});
                data.insert("methods",methods);data.insert("properties",properties);data.insert("children",children);data.insert("layout",layout);return data;
            };
            QJsonArray all;for(auto *w:QApplication::topLevelWidgets())if(w->inherits("UKUIPanel"))all.append(item(w));
            QFile out(qEnvironmentVariable("UKUI_LIQUID_PANEL_INSPECT"));if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(all).toJson());
            if(qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_INSPECT_QUIT"))qApp->quit();
        });
    }
    for(QWidget *w:QApplication::topLevelWidgets()) attach(w);
    if (qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_PROFILE")) {
        auto *stats=new QTimer(this);stats->setInterval(5000);
        connect(stats,&QTimer::timeout,this,[this,stats,samples=6]() mutable {
            qInfo()<<"[LiquidPanelProfile] 5s paints"<<m_paints<<"cache builds"<<m_builds<<"dirty pixels"<<m_dirtyPixels;
            m_paints=0;m_builds=0;m_dirtyPixels=0;
            if (--samples==0) stats->stop();
        });stats->start();
    }
    qInfo()<<"[LiquidPanel] v2 optics loaded on original executable; theme enabled:"<<m_enabled;
}
void PanelController::attach(QWidget *w) {
    if (!w || !w->inherits("UKUIPanel") || w->property("liquidPanelAttached").toBool()) return;
    w->setProperty("liquidPanelAttached",true);
    w->setProperty("liquidPanelEnabled",m_enabled);
    w->setProperty("liquidDockEnabled",m_enabled&&m_dockAdaptive);w->setProperty("liquidDockClickFeedback",m_dockClickFeedback&&!m_reducedMotion);
    w->setProperty("liquidDockReducedMotion",m_reducedMotion);
    w->setProperty("liquidDockHoverMagnification",m_dockMagnification&&!m_reducedMotion);
    new PanelLayoutCompat(w,configFile(),w);
    new PanelDockCompat(w);
    m_cache.insert(w,{});
    m_preparations.insert(w,new LiquidMaterial::Preparation(this));
    m_originalMasks.insert(w,w->mask());
    connect(w,&QObject::destroyed,this,[this,w]{delete m_preparations.take(w);m_cache.remove(w);m_materialDirty.remove(w);m_pointers.remove(w);m_pointerDamage.remove(w);m_originalMasks.remove(w);});
    syncOutline(w);
    w->setMouseTracking(true);
    for(QWidget *child:w->findChildren<QWidget *>())child->setMouseTracking(true);
    qInfo()<<"[LiquidPanel] attached"<<w->metaObject()->className()<<w->size()
           <<"translucent:"<<w->testAttribute(Qt::WA_TranslucentBackground);
    w->update();
}
void PanelController::apply(bool materialChanged) {
    LiquidPopup::theme().reducedMotion=m_reducedMotion;
    LiquidPopup::theme().highlight=m_surface.highlight;
    LiquidPopup::setEnabled(m_enabled);
    m_pendingMaterial|=materialChanged;
    if(!m_previewRefresh.isActive())m_previewRefresh.start();
    m_configSave.start();
}
void PanelController::flushPreview() {
    m_previewRefresh.stop();
    const bool rebuild=m_pendingMaterial;m_pendingMaterial=false;
    qApp->setProperty("liquidPanelPreviewBatches",qApp->property("liquidPanelPreviewBatches").toInt()+1);
    for(auto it=m_cache.begin();it!=m_cache.end();++it) {
        if(rebuild)invalidateMaterial(it.key());
        it.key()->setProperty("liquidPanelEnabled",m_enabled);syncOutline(it.key());it.key()->update();
        it.key()->setProperty("liquidDockEnabled",m_enabled&&m_dockAdaptive);it.key()->setProperty("liquidDockClickFeedback",m_dockClickFeedback&&!m_reducedMotion);
        it.key()->setProperty("liquidDockReducedMotion",m_reducedMotion);
        it.key()->setProperty("liquidDockHoverMagnification",m_dockMagnification&&!m_reducedMotion);
    }
}
void PanelController::saveConfiguration() {
    m_configSave.stop();
    QSettings s(configFile(),QSettings::IniFormat);
    s.setValue("appearance/refraction",m_refraction);
    s.setValue("appearance/chroma",m_chroma);
    s.setValue("appearance/clarity",m_clarity);
    s.setValue("appearance/liquidStrength",m_liquidStrength);
    s.setValue("appearance/transparency",m_transparency);
    s.setValue("appearance/seeThrough",m_seeThrough);
    s.setValue("appearance/enabled",m_enabled);
    s.setValue("appearance/followWallpaper",m_followWallpaper);
    s.setValue("appearance/opacity",m_surface.opacity);
    s.setValue("appearance/radius",m_surface.radius);
    s.setValue("appearance/highlight",m_surface.highlight);
    s.setValue("appearance/materialStyle",m_materialStyle);
    s.setValue("appearance/pointerHighlight",m_pointerHighlight);
    s.setValue("appearance/reducedMotion",m_reducedMotion);s.sync();
    s.setValue("dock/adaptive",m_dockAdaptive);s.setValue("dock/clickFeedback",m_dockClickFeedback);s.setValue("dock/hoverMagnification",m_dockMagnification);s.sync();
    qApp->setProperty("liquidPanelConfigWrites",qApp->property("liquidPanelConfigWrites").toInt()+1);
}
void PanelController::addMenu(QMenu *menu) {
    if (!menu || menu->property("liquidPanelMenu").toBool()) return;
    auto *owner=menu->parentWidget();
    if (!owner || !owner->inherits("UKUIPanel")) return;
    menu->setProperty("liquidPanelMenu",true);
    menu->addSeparator();
    auto *appearance=menu->addMenu("外观与特效");appearance->setObjectName("liquidPanelAppearanceMenu");
    auto *toggle=appearance->addAction(m_enabled?"液态主题（已开启）":"液态主题（已关闭）");
    toggle->setObjectName("liquidPanelEnabledAction");toggle->setCheckable(true);toggle->setChecked(m_enabled);
    connect(toggle,&QAction::triggered,this,[this,toggle](bool on){
        m_enabled=on;toggle->setText(on?"液态主题（已开启）":"液态主题（已关闭）");
        apply();flushPreview();saveConfiguration();
    });
    auto *refresh=appearance->addAction("刷新背景材质");
    connect(refresh,&QAction::triggered,this,[this]{refreshBackdrop();});
    auto *settings=appearance->addAction("液态外观设置…");settings->setObjectName("liquidPanelSettingsAction");
    connect(settings,&QAction::triggered,this,[this,owner]{settingsDialog(owner);});
}
void PanelController::applyMaterialPreset(const QString &style) {
    m_materialStyle=style;
    m_refraction=3.5;m_seeThrough=true;
    if(style=="soft") {
        m_clarity=.22;m_liquidStrength=.55;m_surface.opacity=.58;
        m_surface.highlight=.28;m_chroma=.72;m_transparency=.18;m_pointerHighlight=.10;
    } else if(style=="frosted") {
        m_clarity=0;m_liquidStrength=0;m_surface.opacity=.64;
        m_surface.highlight=.22;m_chroma=.62;m_transparency=.10;m_pointerHighlight=.06;
    } else {
        m_materialStyle="liquid";m_refraction=CohesiveGlass::Refraction;m_clarity=CohesiveGlass::Clarity;m_liquidStrength=CohesiveGlass::Strength;m_surface.opacity=CohesiveGlass::Tint;
        m_surface.highlight=CohesiveGlass::Highlight;m_chroma=CohesiveGlass::Chroma;m_transparency=.18;m_pointerHighlight=0.;m_surface.radius=CohesiveGlass::DockRadius;
    }
    if(m_dialog)if(auto *radius=m_dialog->findChild<QSlider*>("liquidPanelRadius")){const QSignalBlocker blocker(radius);radius->setValue(qRound(m_surface.radius));}
    apply();
}
void PanelController::settingsDialog(QWidget *owner) {
    if(LquiSettings::open("panel:material"))return;
    if(m_dialog){if(!LquiSettings::buildingForm())LiquidDialog::reopen(m_dialog);return;}
    LiquidDialog::Options options;const QPointer<QWidget> anchor(owner);
    options.placement=[anchor](QSize size){
        const QPoint point=anchor?anchor->mapToGlobal(anchor->rect().center()):QCursor::pos();
        auto *screen=QGuiApplication::screenAt(point);if(!screen)screen=QGuiApplication::primaryScreen();
        const QRect available=screen->availableGeometry().adjusted(12,12,-12,-12);
        size=size.boundedTo(available.size());
        const int top=anchor?anchor->mapToGlobal(QPoint()).y()-size.height()-12:point.y()-size.height()/2;
        return QRect(QPoint(qBound(available.left(),point.x()-size.width()/2,available.right()-size.width()+1),
            qBound(available.top(),top,available.bottom()-size.height()+1)),size);
    };
    auto *dialog=new LiquidDialog::Dialog(owner,Qt::Dialog,options);m_dialog=dialog;
    dialog->setWindowFlag(Qt::WindowStaysOnTopHint,false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Kylin-LQUI · 任务栏外观");
    dialog->setObjectName("liquidPanelAppearanceDialog");dialog->setWindowRole("liquid-panel-settings");
    dialog->setWindowIcon(QIcon::fromTheme("preferences-desktop",dialog->style()->standardIcon(QStyle::SP_DesktopIcon)));
    auto *screen=QGuiApplication::screenAt(owner->mapToGlobal(owner->rect().center()));if(!screen)screen=QGuiApplication::primaryScreen();
    const QSize available=screen->availableGeometry().size()-QSize(24,24);
    dialog->setMinimumSize(qMin(480,available.width()),qMin(400,available.height()));
    dialog->resize(qMin(570,available.width()),qMin(700,available.height()));
    dialog->setStyleSheet(dialog->styleSheet()+QStringLiteral(
        "QTabWidget::pane{border:0;background:transparent;} QTabBar::tab{background:transparent;color:#b4c3cc;padding:9px 13px;border:0;border-bottom:2px solid transparent;}"
        "QTabBar::tab:selected{color:#edf5ff;border-bottom-color:#8ed9ce;background:rgba(150,205,195,15);}"
        "QScrollArea{background:transparent;border:0;} QScrollBar:vertical{background:transparent;width:8px;margin:2px 0;}"
        "QScrollBar::handle:vertical{background:rgba(160,185,195,95);border:0;border-radius:3px;min-height:28px;margin:0 1px;}"
        "QScrollBar::handle:vertical:hover{background:rgba(160,210,200,155);} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        "QSlider:horizontal{min-height:26px;background:transparent;} QSlider::groove:horizontal{height:4px;background:rgba(140,170,185,65);border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:#8ed9ce;border-radius:2px;} QSlider::handle:horizontal{width:14px;margin:-5px 0;border-radius:7px;background:#edf5ff;border:1px solid rgba(210,235,240,90);}"
        "QSlider::handle:horizontal:focus{border:2px solid #8ed9ce;}"
        "QPushButton[materialPreset=true]:checked{background:rgba(120,205,190,45);border-color:#8ed9ce;}"
        "QLabel[settingsSection=true]{color:#c6d9de;font-weight:600;padding-top:8px;}"));
    auto *outer=new QFormLayout(dialog);outer->setContentsMargins(18,14,18,14);outer->setSpacing(12);
    auto *intro=new QLabel("轻薄反光，柔和折射。修改后立即生效并保存。",dialog);intro->setWordWrap(true);outer->addRow(intro);
    // Use a plain tab bar + stack: the OEM style unconditionally attaches an
    // opaque screenshot slide animator to QTabWidget, even with doNotAnimate.
    auto *body=new QWidget(dialog);body->setObjectName("liquidPanelSettingsBody");
    auto *bodyLayout=new QVBoxLayout(body);bodyLayout->setContentsMargins(0,0,0,0);bodyLayout->setSpacing(0);
    auto *tabs=new QTabBar(body);tabs->setObjectName("liquidPanelSettingsTabs");tabs->setExpanding(false);
    auto *stack=new QStackedWidget(body);stack->setObjectName("liquidPanelSettingsStack");stack->setStyleSheet("QStackedWidget{background:transparent;border:0;}");
    bodyLayout->addWidget(tabs);bodyLayout->addWidget(stack,1);outer->addRow(body);
    connect(tabs,&QTabBar::currentChanged,stack,&QStackedWidget::setCurrentIndex);
    auto page=[&](const QString &title,const QString &name){
        auto *scroll=new QScrollArea(stack);scroll->setObjectName(name);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *body=new QWidget(scroll);auto *form=new QFormLayout(body);form->setContentsMargins(4,12,12,10);form->setVerticalSpacing(12);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        scroll->setWidget(body);stack->addWidget(scroll);tabs->addTab(title);
        for(auto *surface:{body,scroll->viewport()}){surface->setAutoFillBackground(false);surface->setStyleSheet("background:transparent;");}
        return form;
    };
    auto *material=page("液态材质","liquidPanelMaterialScroll");
    auto *layoutForm=page("布局与交互","liquidPanelLayoutScroll");
    auto *effects=page("窗口动画","liquidPanelEffectsScroll");
    connect(tabs,&QTabBar::currentChanged,dialog,[effects](int index){
        if(index==2&&effects->rowCount()==0){effects->addRow(new WindowEffectsSettings(effects->parentWidget()));LiquidPopup::installControls(effects->parentWidget());}
    });
    auto heading=[](QFormLayout *form,const QString &title){auto *label=new QLabel(title,form->parentWidget());label->setProperty("settingsSection",true);form->addRow(label);};
    auto slider=[&](QFormLayout *form,const QString &name,const QString &id,int low,int high,int value,const QString &unit,const QString &help,auto change,bool rebuild=true){
        auto *row=new QWidget(form->parentWidget());auto *line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);line->setSpacing(10);
        auto *bar=new QSlider(Qt::Horizontal,row);bar->setObjectName(id);bar->setProperty("settingsSuffix",unit);bar->setRange(low,high);bar->setValue(value);bar->setMinimumWidth(100);bar->setToolTip(help);
        auto *number=new QLabel(QString::number(value)+unit,row);number->setProperty("sliderValue",true);number->setFixedWidth(62);number->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
        line->addWidget(bar,1);line->addWidget(number);form->addRow(name,row);
        connect(bar,&QSlider::valueChanged,dialog,[this,number,unit,change,rebuild](int v){number->setText(QString::number(v)+unit);change(v);apply(rebuild);});
        connect(bar,&QSlider::sliderReleased,dialog,[this]{if(m_previewRefresh.isActive())flushPreview();if(m_configSave.isActive())saveConfiguration();});
        return bar;
    };
    auto *presets=new QWidget(material->parentWidget());auto *presetRow=new QHBoxLayout(presets);presetRow->setContentsMargins(0,0,0,0);presetRow->setSpacing(8);
    auto *group=new QButtonGroup(dialog);group->setExclusive(true);
    const QStringList styles{"liquid","soft","frosted"},names{"清透液态","柔和玻璃","磨砂玻璃"};
    for(int i=0;i<styles.size();++i){auto *button=new QPushButton(names[i],presets);button->setCheckable(true);button->setProperty("materialPreset",true);button->setProperty("materialStyle",styles[i]);button->setObjectName("liquidMaterialPreset_"+styles[i]);button->setChecked(styles[i]==m_materialStyle);group->addButton(button);presetRow->addWidget(button,1);}
    material->addRow(presets);
    auto *description=new QLabel("选择材质会应用推荐参数，也可继续微调。",material->parentWidget());description->setWordWrap(true);material->addRow(description);
    heading(material,"玻璃质感");
    auto *clarity=slider(material,"背景清晰度","liquidPanelClarity",0,100,qRound(m_clarity*100),"%","向左更磨砂，向右保留更多壁纸细节。",[this](int v){m_clarity=v/100.;});
    auto *strength=slider(material,"折射强度","liquidPanelStrength",0,200,qRound(m_liquidStrength*100),"%","调整玻璃边缘的弯折；磨砂材质不使用折射。",[this](int v){m_liquidStrength=v/100.;});
    auto *highlight=slider(material,"边缘反光","liquidPanelHighlight",0,100,qRound(m_surface.highlight*100),"%","只调细边反光，避免角落过亮。",[this](int v){m_surface.highlight=v/100.;});
    auto *shade=slider(material,"背景压暗","liquidPanelShade",15,95,qRound(m_surface.opacity*100),"%","增加压暗，让图标和文字更突出。",[this](int v){m_surface.opacity=v/100.;});
    auto *chroma=slider(material,"壁纸色彩","liquidPanelChroma",0,130,qRound(m_chroma*100),"%","控制材质保留多少壁纸颜色。",[this](int v){m_chroma=v/100.;});
    heading(material,"通透与光感");
    auto *through=new QCheckBox("透出后方窗口",material->parentWidget());through->setObjectName("liquidPanelSeeThrough");through->setChecked(m_seeThrough);through->setToolTip("只改变底板透明度，图标和文字保持清晰。");material->addRow(through);
    auto *transparency=slider(material,"窗口透视","liquidPanelTransparency",0,65,qRound(m_transparency*100),"%","越向右越通透，后方窗口自然透出。",[this](int v){m_transparency=v/100.;},false);transparency->setEnabled(m_seeThrough);
    connect(through,&QCheckBox::toggled,dialog,[this,transparency](bool on){m_seeThrough=on;transparency->setEnabled(on);apply(false);});
    auto *pointer=slider(material,"鼠标追光","liquidPanelPointerHighlight",0,100,qRound(m_pointerHighlight*100),"%","鼠标附近的细边亮光；设为 0 可关闭。",[this](int v){m_pointerHighlight=v/100.;},false);
    auto *wallpaperRow=new QWidget(material->parentWidget());auto *wallpaperLine=new QHBoxLayout(wallpaperRow);wallpaperLine->setContentsMargins(0,0,0,0);
    auto *follow=new QCheckBox("跟随壁纸",wallpaperRow);follow->setObjectName("liquidPanelFollowWallpaper");follow->setChecked(m_followWallpaper);follow->setToolTip("壁纸变化时更新材质，不持续刷新。");wallpaperLine->addWidget(follow);wallpaperLine->addStretch();
    auto *refresh=new QPushButton("刷新材质",wallpaperRow);refresh->setObjectName("liquidPanelRefreshMaterial");wallpaperLine->addWidget(refresh);material->addRow(wallpaperRow);
    connect(follow,&QCheckBox::toggled,dialog,[this](bool on){m_followWallpaper=on;updateWallpaperWatchers();if(on)refreshBackdrop();apply(false);});
    connect(refresh,&QPushButton::clicked,dialog,[this]{refreshBackdrop();});
    auto syncPreset=[=]{
        clarity->setValue(qRound(m_clarity*100));strength->setValue(qRound(m_liquidStrength*100));highlight->setValue(qRound(m_surface.highlight*100));shade->setValue(qRound(m_surface.opacity*100));chroma->setValue(qRound(m_chroma*100));
        transparency->setValue(qRound(m_transparency*100));pointer->setValue(qRound(m_pointerHighlight*100));through->setChecked(m_seeThrough);transparency->setEnabled(m_seeThrough);strength->setEnabled(m_materialStyle!="frosted");
        description->setText(m_materialStyle=="soft"?"柔和扩散，淡化轮廓；适合更安静的桌面。":m_materialStyle=="frosted"?"细腻磨砂，突出内容；不弯折背景。":"清晰底色配细边折射，保持轻薄通透。");
    };
    strength->setEnabled(m_materialStyle!="frosted");
    for(auto *button:group->buttons())connect(button,&QAbstractButton::clicked,dialog,[this,button,syncPreset]{applyMaterialPreset(button->property("materialStyle").toString());syncPreset();});
    auto *reset=new QPushButton("恢复当前材质推荐值",material->parentWidget());reset->setObjectName("liquidPanelResetMaterial");material->addRow(reset);
    connect(reset,&QPushButton::clicked,dialog,[this,syncPreset]{applyMaterialPreset(m_materialStyle);syncPreset();});

    heading(layoutForm,"任务栏布局");addPanelLayoutSettings(layoutForm,dialog,owner,configFile());
    slider(layoutForm,"底板圆角","liquidPanelRadius",0,24,qRound(m_surface.radius)," px","调整底板轮廓，不改变图标排列。",[this](int v){m_surface.radius=v;});
    heading(layoutForm,"系统控制区");
    auto *systemControls=new QCheckBox("紧凑系统控制区（网络、声音、电源和日历）",layoutForm->parentWidget());systemControls->setObjectName("liquidSystemControlsEnabled");systemControls->setChecked(qApp->property("liquidSystemControlsEnabled").toBool());layoutForm->addRow(systemControls);
    connect(systemControls,&QCheckBox::toggled,dialog,[this](bool on){qApp->setProperty("liquidSystemControlsEnabled",on);QSettings s(configFile(),QSettings::IniFormat);s.setValue("controls/compact",on);s.sync();});
    auto *history=new QCheckBox("保留最近通知（最多 200 条）",layoutForm->parentWidget());history->setObjectName("liquidNotificationHistory");history->setChecked(qApp->property("liquidNotificationHistory").toBool());history->setToolTip("用于控制中心的通知历史；关闭后不再记录新通知，已有记录可在控制中心清空。");layoutForm->addRow(history);
    connect(history,&QCheckBox::toggled,dialog,[this](bool on){qApp->setProperty("liquidNotificationHistory",on);QSettings s(configFile(),QSettings::IniFormat);s.setValue("controls/notificationHistory",on);s.sync();});
    heading(layoutForm,"应用图标");
    auto *uniformIcons=new QCheckBox("统一应用图标与两端小球的主题",layoutForm->parentWidget());
    uniformIcons->setObjectName("liquidUniformAppIcons");uniformIcons->setChecked(LquiIcon::uniform());
    uniformIcons->setToolTip("任务栏、桌面、开始菜单共用图标主题，麒麟和 AI 小球同步配色。自动适配圆角、画布和视觉大小并缓存；保留原始标识和文件，关闭后恢复原始图形。");
    layoutForm->addRow(uniformIcons);
    connect(uniformIcons,&QCheckBox::toggled,dialog,[uniformIcons](bool on){
        if(!LquiIcon::setUniform(on)){QSignalBlocker guard(uniformIcons);uniformIcons->setChecked(LquiIcon::uniform());}
    });
    auto *iconTheme=new QComboBox(layoutForm->parentWidget());iconTheme->setObjectName("liquidIconTheme");
    iconTheme->addItem("原彩圆角", "color");iconTheme->addItem("轻透玻璃", "glass");iconTheme->addItem("柔和雾面", "mist");
    iconTheme->setCurrentIndex(int(LquiIcon::theme()));iconTheme->setEnabled(LquiIcon::uniform());
    iconTheme->setToolTip("全桌面图标与开始菜单球、AI 小球即时同步；原彩保留鲜明品牌色，玻璃增加清透反光，雾面使用柔和的低对比光感。");
    layoutForm->addRow("图标主题",iconTheme);connect(uniformIcons,&QCheckBox::toggled,iconTheme,&QWidget::setEnabled);
    connect(iconTheme,qOverload<int>(&QComboBox::currentIndexChanged),dialog,[iconTheme](int index){
        if(!LquiIcon::setTheme(LquiIcon::parseTheme(iconTheme->itemData(index).toString()))){QSignalBlocker guard(iconTheme);iconTheme->setCurrentIndex(int(LquiIcon::theme()));}
    });
    auto *dockAdaptive=new QCheckBox("应用图标自适应排列",layoutForm->parentWidget());dockAdaptive->setObjectName("liquidDockAdaptive");dockAdaptive->setChecked(m_dockAdaptive);layoutForm->addRow(dockAdaptive);
    auto *dockHover=new QCheckBox("点击图标轻反馈",layoutForm->parentWidget());dockHover->setObjectName("liquidDockClickFeedback");dockHover->setChecked(m_dockClickFeedback);dockHover->setEnabled(m_dockAdaptive);layoutForm->addRow(dockHover);
    auto *magnify=new QCheckBox("鼠标悬停时放大图标",layoutForm->parentWidget());magnify->setObjectName("liquidDockHoverMagnification");magnify->setChecked(m_dockMagnification);magnify->setEnabled(m_dockAdaptive);layoutForm->addRow(magnify);
    magnify->setToolTip("默认关闭。开启后放大经过的应用图标，任务栏总宽度与信息区保持不变；减弱动画时停用。");
    connect(magnify,&QCheckBox::toggled,dialog,[this](bool on){m_dockMagnification=on;apply(false);});
    connect(dockAdaptive,&QCheckBox::toggled,dialog,[this,dockHover,magnify](bool on){m_dockAdaptive=on;dockHover->setEnabled(on);magnify->setEnabled(on);apply(false);});
    connect(dockHover,&QCheckBox::toggled,dialog,[this](bool on){m_dockClickFeedback=on;apply(false);});
    QSettings tray(configFile(),QSettings::IniFormat);
    slider(layoutForm,"隐藏图标每行","liquidTrayMaxColumns",3,8,qBound(3,tray.value("tray/maxColumns",3).toInt(),8)," 个","设置隐藏托盘每行的图标数量。",[this](int v){QSettings s(configFile(),QSettings::IniFormat);s.setValue("tray/maxColumns",v);s.sync();qApp->setProperty("liquidTrayMaxColumns",v);},false);
    auto *reduced=new QCheckBox("减弱菜单与 Dock 动画",layoutForm->parentWidget());reduced->setObjectName("liquidPanelReducedMotion");reduced->setChecked(m_reducedMotion);layoutForm->addRow(reduced);
    connect(reduced,&QCheckBox::toggled,dialog,[this](bool on){m_reducedMotion=on;apply(false);});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,dialog);buttons->button(QDialogButtonBox::Close)->setText("关闭");outer->addRow(buttons);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    connect(dialog,&QDialog::finished,this,[this]{if(m_previewRefresh.isActive())flushPreview();if(m_configSave.isActive())saveConfiguration();});
    if(!LquiSettings::buildingForm()){dialog->show();dialog->raise();dialog->activateWindow();}
}
bool PanelController::eventFilter(QObject *obj,QEvent *e) {
    auto *w=qobject_cast<QWidget *>(obj);if(!w)return false;
    if(m_enabled&&m_dockAdaptive&&w->window()->property("liquidPanelAttached").toBool()
        &&(e->type()==QEvent::MouseButtonPress||e->type()==QEvent::MouseButtonDblClick||e->type()==QEvent::MouseButtonRelease)
        &&static_cast<QMouseEvent*>(e)->button()==Qt::LeftButton) {
        bool taskBackground=false,button=false;
        for(auto *parent=w;parent&&parent!=w->window();parent=parent->parentWidget()) {
            taskBackground|=parent->inherits("UKUITaskBar");
            button|=qobject_cast<QAbstractButton*>(parent)!=nullptr;
        }
        // Interactive children handle their own events before propagation.
        // An otherwise empty Dock click has no action; letting the OEM panel
        // treat it as a panel drag repeatedly resets its fractional-DPI size.
        if(!button&&(taskBackground||w==w->window()||w->objectName()=="BackgroundWidget")){e->accept();return true;}
    }
    if(e->type()==QEvent::Polish || e->type()==QEvent::Show) {
        attach(w);
        if(auto *menu=qobject_cast<QMenu *>(w))addMenu(menu);
    }
    if(m_enabled && (e->type()==QEvent::MouseMove || e->type()==QEvent::Enter || e->type()==QEvent::Leave)) {
        QWidget *panel=w;
        while(panel && !m_cache.contains(panel))panel=panel->parentWidget();
        if(panel && m_cache.contains(panel)) {
            const QPoint global = e->type()==QEvent::MouseMove
                ? static_cast<QMouseEvent *>(e)->globalPos() : QCursor::pos();
            updatePointer(panel,panel->mapFromGlobal(global));
        }
    }
    if (!w->property("liquidPanelAttached").toBool())return false;
    if(e->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(e)->propertyName()=="liquidDockResizing") {
        if(qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_MATERIAL_TRACE"))qInfo()<<"material resize-state"<<w->property("liquidDockResizing");
        invalidateMaterial(w);syncOutline(w);w->update();
    }
    if(e->type()==QEvent::Hide) {
        m_pointers.remove(w);
        m_pointerDamage.remove(w);
        if(m_pointerDamage.isEmpty())m_hoverRefresh.stop();
    }
    if(e->type()==QEvent::Move || e->type()==QEvent::Resize || e->type()==QEvent::PaletteChange || e->type()==QEvent::StyleChange || e->type()==QEvent::Show) {
        // OEM mouse presses temporarily realign to a native rectangle before
        // the centered compatibility rectangle is restored. Inspect only the
        // settled geometry; those transient resizes must not replace material.
        if(!w->property("liquidMaterialGeometryQueued").toBool()) {
            w->setProperty("liquidMaterialGeometryQueued",true);QPointer<QWidget> guard(w);
            QTimer::singleShot(0,this,[this,guard]{
                if(!guard)return;guard->setProperty("liquidMaterialGeometryQueued",false);
                const QRect geometry(guard->mapToGlobal(QPoint()),guard->size());
                const bool light=guard->palette().color(QPalette::Active,QPalette::Base).lightness()>150;
                if(!guard->property("liquidDockResizing").toBool()
                    &&(!sameMaterialRect(guard->property("liquidMaterialRect").toRect(),geometry)
                    ||!qFuzzyCompare(guard->property("liquidMaterialDpr").toDouble(),guard->devicePixelRatioF())
                    ||guard->property("liquidMaterialLight").toBool()!=light)) {
                    if(qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_MATERIAL_TRACE"))qInfo()<<"material settled geometry"<<geometry<<guard->property("liquidMaterialRect")<<guard->devicePixelRatioF()<<guard->property("liquidMaterialDpr")<<light;
                    invalidateMaterial(guard);guard->update();
                }
                syncOutline(guard);
            });
        }
    }
    if(e->type()==QEvent::Paint && m_enabled) {
        ++m_paints;
        const auto region=static_cast<QPaintEvent *>(e)->region();
        for (const QRect &r:region) m_dirtyPixels+=quint64(r.width())*r.height();
        QImage &image=m_cache[w];
        const bool resizing=w->property("liquidDockResizing").toBool();
        if((m_materialDirty.contains(w)&&!resizing) || image.isNull() || image.devicePixelRatio()!=w->devicePixelRatioF()) {
            if(qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_MATERIAL_TRACE"))qInfo()<<"material build"<<w->geometry()<<image.size()<<image.devicePixelRatio()<<w->devicePixelRatioF()<<m_materialDirty.contains(w);
            m_materialDirty.remove(w);
            ++m_builds;
            w->setProperty("liquidMaterialBuilds",w->property("liquidMaterialBuilds").toInt()+1);
            QScreen *screen=QGuiApplication::screenAt(w->mapToGlobal(w->rect().center()));
            if(!screen)screen=QGuiApplication::primaryScreen();
            const QImage source=screen?m_wallpaper.sample(QRect(w->mapToGlobal(QPoint()),w->size()),screen->geometry(),w->devicePixelRatioF()):QImage();
            auto style=m_surface;style.light=w->palette().color(QPalette::Active,QPalette::Base).lightness()>150;
            w->setProperty("liquidMaterialRect",QRect(w->mapToGlobal(QPoint()),w->size()));
            w->setProperty("liquidMaterialDpr",w->devicePixelRatioF());
            w->setProperty("liquidMaterialLight",style.light);
            // Keep the previous finished optical image while preparing a new
            // size. A placeholder is only necessary for the very first frame.
            if(image.isNull()){
                image=LiquidPopup::renderSurface(w->size(),w->devicePixelRatioF(),style);
                w->setProperty("liquidOpticalGpu",false);
                w->setProperty("liquidMaterialFallbacks",w->property("liquidMaterialFallbacks").toInt()+1);
            }
            if(!source.isNull()) {
                const QPointer<QWidget> guard(w);
                const QRect requested(w->mapToGlobal(QPoint()),w->size());
                const qreal dpr=w->devicePixelRatioF();
                m_preparations[w]->request(source,[this,guard,requested,dpr](const LiquidMaterial::Prepared &material){
                    if(!guard || !m_enabled)return;
                    if(!sameMaterialRect(requested,QRect(guard->mapToGlobal(QPoint()),guard->size())) || !qFuzzyCompare(dpr,guard->devicePixelRatioF())){
                        if(qEnvironmentVariableIsSet("UKUI_LIQUID_PANEL_MATERIAL_TRACE"))qInfo()<<"material stale completion"<<requested<<guard->geometry();
                        invalidateMaterial(guard);guard->update();return;
                    }
                    m_optics.setFinish(m_materialStyle=="soft"?LiquidOpticsRenderer::Finish::Soft:
                        m_materialStyle=="frosted"?LiquidOpticsRenderer::Finish::Frosted:LiquidOpticsRenderer::Finish::Liquid);
                    m_optics.setOptics(m_refraction,m_surface.opacity,m_surface.highlight,m_chroma);
                    m_optics.setMaterial(m_clarity,m_liquidStrength);
                    m_optics.setPreparedWallpaper(material);
                    m_cache[guard]=m_optics.renderPanel(QRect(QPoint(),requested.size()),m_surface.radius);
                    guard->setProperty("liquidOpticalGpu",m_optics.usedGpu());
                    guard->setProperty("liquidMaterialReady",true);
                    syncOutline(guard);guard->update();
                });
            }
        }
        // Match the native outline to physical optical coverage, including AA.
        if(!resizing&&w->property("liquidNativeOutlineKey").toLongLong()!=image.cacheKey())
            setPanelNativeOutline(w,image);
        QPainter p(w);p.setRenderHint(QPainter::Antialiasing);p.setClipRegion(region);p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(w->rect(),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        // Fade the premultiplied material only; child icons/text paint normally.
        // KWin composites windows behind us, with no screenshots or blur request.
        p.setOpacity(m_seeThrough?1.-m_transparency:1.);
        if(resizing||image.size()/image.devicePixelRatio()!=w->size())p.drawImage(w->rect(),image);else p.drawImage(QPoint(),image);
        if(m_pointerHighlight>0 && m_pointers.contains(w)) {
            const QPointF pos=m_pointers.value(w);
            const QRectF body=QRectF(w->rect()).adjusted(.75,.75,-.75,-.75);
            const qreal radius=qMin(m_surface.radius,qMin(body.width(),body.height())/2);
            QPainterPath rim;rim.setFillRule(Qt::OddEvenFill);
            rim.addRoundedRect(body,radius,radius);
            rim.addRoundedRect(body.adjusted(1.8,1.8,-1.8,-1.8),qMax(0.,radius-1.8),qMax(0.,radius-1.8));
            QRadialGradient light(QPointF(pos.x(),pos.y()<w->height()/2.?0:w->height()),95);
            light.setColorAt(0,QColor(244,250,255,qRound(90*m_pointerHighlight)));
            light.setColorAt(.3,QColor(210,230,245,qRound(35*m_pointerHighlight)));
            light.setColorAt(1,Qt::transparent);p.fillPath(rim,light);
        }
        // QWidget's child traversal still paints original buttons and plugins.
        return true;
    }
    return false;
}

void PanelController::invalidateMaterial(QWidget *w) {
    m_materialDirty.insert(w);
    if(auto *preparation=m_preparations.value(w))preparation->invalidate();
    w->setProperty("liquidMaterialReady",false);
}

void PanelController::syncOutline(QWidget *w) {
    if(!m_enabled) {
        w->setProperty("liquidNativeShapeReset",true);
        const QRegion original=m_originalMasks.value(w);
        if(original.isEmpty())w->clearMask();else w->setMask(original);
    } else {
        if(!w->property("liquidDockResizing").toBool()&&!m_cache.value(w).isNull()
            &&m_cache.value(w).size()/m_cache.value(w).devicePixelRatio()==w->size()) {
            setPanelNativeOutline(w,m_cache.value(w));
            setPanelNativeBackdrop(w,true);
            return;
        }
        QPainterPath outline;
        const qreal radius=qMin(m_surface.radius,qMin(w->width(),w->height())/2.);
        // Temporary paint envelope until the physical coverage is rendered.
        // Keep AA pixels; the cached alpha surface supplies the exact outline.
        outline.addRoundedRect(QRectF(w->rect()).adjusted(-2,-2,2,2),radius+2,radius+2);
        const QRegion logical=QRegion(outline.toFillPolygon().toPolygon()) & w->rect();
        if(w->mask()!=logical)w->setMask(logical);
    }
    setPanelNativeBackdrop(w,m_enabled);
}
void PanelController::updateWallpaperWatchers() {
    if(!m_wallpaperWatcher.files().isEmpty())m_wallpaperWatcher.removePaths(m_wallpaperWatcher.files());
    if(!m_wallpaperWatcher.directories().isEmpty())m_wallpaperWatcher.removePaths(m_wallpaperWatcher.directories());
    if(!m_followWallpaper){m_wallpaperRefresh.stop();return;}
    const QString cfg=QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const auto paths=m_wallpaper.sourceWatchPaths()+QStringList{
        cfg+"/kyfences/layout.json",QSettings(QSettings::NativeFormat,QSettings::UserScope,"kylin","ukui-fences").fileName(),cfg+"/dconf/user"};
    for(const QString &path:paths) {
        if(path.isEmpty())continue;
        if(QFile::exists(path))m_wallpaperWatcher.addPath(path);
        QString parent=QFileInfo(path).absolutePath();
        // A missing poster's cache directory may itself be created later.
        // Watch the nearest existing ancestor, then rebind after its creation.
        while(!QDir(parent).exists()) {
            const QString next=QFileInfo(parent).absolutePath();
            if(next==parent)break;
            parent=next;
        }
        // Atomic rename replaces the watched inode. Its directory survives.
        if(QDir(parent).exists() && !m_wallpaperWatcher.directories().contains(parent))m_wallpaperWatcher.addPath(parent);
    }
}
void PanelController::refreshBackdrop(bool force) {
    m_wallpaperForce = m_wallpaperForce || force;
    if (m_wallpaperLoading) { m_wallpaperPending = true; return; }
    m_wallpaperLoading = true; m_wallpaperPending = false;
    auto old = m_wallpaper;
    BackgroundTask::run(this, [old]() mutable {
        const bool changed = old.reload();
        return qMakePair(old, changed);
    }, [this](const QPair<WallpaperBackdrop, bool> &result) {
        m_wallpaperLoading = false;
        if (m_wallpaperPending) { refreshBackdrop(m_wallpaperForce); return; }
        m_wallpaper = result.first; updateWallpaperWatchers();
        const bool invalidate = result.second || m_wallpaperForce;
        m_wallpaperForce = false;
        if (invalidate)
            for (auto i=m_cache.begin();i!=m_cache.end();++i) { invalidateMaterial(i.key()); i.key()->update(); }
    });
}
void PanelController::updatePointer(QWidget *panel,const QPointF &pos) {
    const QPointF old=m_pointers.value(panel,QPointF(-1000,-1000));
    const bool inside=panel->rect().contains(pos.toPoint()) && panel->isVisible();
    if((inside && old==pos) || (!inside && !m_pointers.contains(panel)))return;
    QRegion &damage=m_pointerDamage[panel];
    if(m_pointers.contains(panel))damage |= QRect(qRound(old.x())-120,0,240,panel->height());
    if(inside) {
        m_pointers.insert(panel,pos);
        damage |= QRect(qRound(pos.x())-120,0,240,panel->height());
    } else m_pointers.remove(panel);
    // Coalesce old and new footprints once per frame, including pointer leave.
    if(!m_hoverRefresh.isActive())m_hoverRefresh.start();
}
