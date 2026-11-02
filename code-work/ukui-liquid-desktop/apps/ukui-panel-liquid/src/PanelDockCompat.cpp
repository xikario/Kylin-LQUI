#include "PanelDockCompat.h"
#include "PanelLayoutCompat.h"
#include "DockIndicatorState.h"
#include <QApplication>
#include <QAbstractButton>
#include <QBoxLayout>
#include <QDir>
#include <QDynamicPropertyChangeEvent>
#include <QFileInfo>
#include <QLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QPainter>
#include <QWindow>
#include <QMetaMethod>
#include <QStyleOptionToolButton>
#include <QToolButton>
#include <QWidget>
#include <dlfcn.h>
#include <QtMath>

namespace {
using SetSize=void (*)(QLayout *,QSize);
SetSize gridSize(){return reinterpret_cast<SetSize>(dlsym(RTLD_DEFAULT,"_ZN4UKUi10GridLayout16setCellFixedSizeE5QSize"));}
bool gridMatches(QLayout *layout,QSize size) {
    using Get=QSize (*)(const QLayout *);
    static const auto minimum=reinterpret_cast<Get>(dlsym(RTLD_DEFAULT,"_ZNK4UKUi10GridLayout15cellMinimumSizeEv"));
    static const auto maximum=reinterpret_cast<Get>(dlsym(RTLD_DEFAULT,"_ZNK4UKUi10GridLayout15cellMaximumSizeEv"));
    return !minimum||!maximum||(minimum(layout)==size&&maximum(layout)==size);
}
void *native(QObject *object,const char *name) {
    if(void *symbol=dlsym(RTLD_DEFAULT,name))return symbol;
    Dl_info info{};if(!object||!dladdr(object->metaObject(),&info)||!info.dli_fname)return nullptr;
    void *module=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);if(!module)return nullptr;
    void *symbol=dlsym(module,name);dlclose(module);return symbol;
}
bool descendant(QWidget *w,QWidget *parent){for(;w;w=w->parentWidget())if(w==parent)return true;return false;}

}

PanelDockCompat::PanelDockCompat(QWidget *panel):QObject(panel),m_panel(panel) {
    setObjectName("liquidPanelDockCompat");
    m_pending.setSingleShot(true);m_frame.setSingleShot(true);
    m_frame.setTimerType(Qt::PreciseTimer);
    connect(&m_pending,&QTimer::timeout,this,&PanelDockCompat::apply);
    connect(&m_frame,&QTimer::timeout,this,&PanelDockCompat::frame);
    qApp->installEventFilter(this);
    connect(qApp,&QGuiApplication::focusWindowChanged,this,[this]{for(const auto &group:m_groups)for(auto button:group.buttons)if(button)button->update();});
    auto systemSelf=reinterpret_cast<QObject *(*)()>(dlsym(RTLD_DEFAULT,"_ZN13KWindowSystem4selfEv"));
    if(systemSelf)if(auto *system=systemSelf())for(int i=0;i<system->metaObject()->methodCount();++i){
        const auto signal=system->metaObject()->method(i);
        if(signal.methodType()==QMetaMethod::Signal&&signal.name()=="activeWindowChanged")
            connect(system,signal,this,metaObject()->method(metaObject()->indexOfSlot("updateActiveGroup()")),Qt::QueuedConnection);
    }
    schedule();
}
void PanelDockCompat::updateActiveGroup(){for(const auto &group:m_groups)for(auto button:group.buttons)if(button)button->update();}
PanelDockCompat::~PanelDockCompat(){if(qApp)qApp->removeEventFilter(this);}
bool PanelDockCompat::enabled() const {
    return m_panel&&m_panel->property("liquidDockEnabled").toBool()&&m_panel->width()>m_panel->height();
}
void PanelDockCompat::schedule(){if(!m_applying&&!m_pending.isActive())m_pending.start(0);}
void PanelDockCompat::remember(QWidget *w) {
    if(m_saved.contains(w))return;
    auto *b=qobject_cast<QAbstractButton*>(w);
    m_saved.insert(w,{w->minimumSize(),w->maximumSize(),b?b->iconSize():QSize(),w->style(),w->property("doNotAnimate"),w->property("useIconHighlightEffect")});
    connect(w,&QObject::destroyed,this,[this,w]{m_saved.remove(w);schedule();});
}
void PanelDockCompat::attach() {
    if(m_bar||!m_panel||!gridSize())return;
    for(auto *bar:m_panel->findChildren<QScrollArea*>()) {
        if(!bar->inherits("UKUITaskBar")||!bar->widget()||!bar->widget()->layout())continue;
        if(QByteArray(bar->widget()->layout()->metaObject()->className())!="UKUi::GridLayout")continue;
        auto *container=bar->parentWidget();auto *box=qobject_cast<QBoxLayout*>(container->layout());
        auto *pages=container->findChild<QWidget*>("TaskBar_KButtonBox");if(!box||!pages)continue;
        m_bar=bar;m_container=container;m_content=bar->widget();m_pages=pages;
        // The host Plugin wrapper receives panel::realigned(), not the
        // taskbar's private implementation object. Gate only this connection:
        // otherwise every compact-layout repair invokes the OEM layout again.
        auto *owner=container->parentWidget();
        if(owner&&owner->inherits("Plugin")&&m_panel->inherits("UKUIPanel")
            &&m_panel->metaObject()->indexOfSignal("realigned()")>=0) {
            auto fn=reinterpret_cast<void (*)(QWidget*)>(native(owner,"_ZN6Plugin7realignEv"));
            if(fn) {
                const auto connection=connect(m_panel,SIGNAL(realigned()),this,SLOT(nativePanelRealigned()));
                if(connection&&disconnect(m_panel,SIGNAL(realigned()),owner,nullptr)){m_nativeOwner=owner;m_nativeRealign=fn;}
                else disconnect(connection);
            }
        }
        m_containerSpacing=box->spacing();
        m_contentMinimum=m_content->minimumSize();m_contentMaximum=m_content->maximumSize();m_pagesMinimum=pages->minimumSize();m_pagesMaximum=pages->maximumSize();
        m_pagesOffscreen=pages->testAttribute(Qt::WA_DontShowOnScreen);m_pagesEnabled=pages->isEnabled();
        remember(bar);
        m_horizontalPolicy=bar->horizontalScrollBarPolicy();m_verticalPolicy=bar->verticalScrollBarPolicy();
        break;
    }
}
void PanelDockCompat::nativePanelRealigned() {
    if(!m_panel||!m_nativeOwner||!m_nativeRealign||m_nativeForwarding)return;
    const QSize panelSize=m_panel->size(),containerSize=m_container?m_container->size():QSize();
    const qreal dpr=m_panel->devicePixelRatioF();const int direction=int(m_panel->layoutDirection());
    const QVariant position=m_panel->property("position");
    if(m_managed&&panelSize==m_nativePanelSize&&containerSize==m_nativeContainerSize
        &&qFuzzyCompare(dpr,m_nativeDpr)&&direction==m_nativeDirection&&position==m_nativePosition)return;
    m_nativePanelSize=panelSize;m_nativeContainerSize=containerSize;m_nativeDpr=dpr;m_nativeDirection=direction;m_nativePosition=position;
    m_nativeForwarding=true;m_nativeRealign(m_nativeOwner);m_nativeForwarding=false;
    if(m_managed)apply();
}
void PanelDockCompat::apply() {
    if(!m_panel)return;
    if(!enabled()){restore();return;}
    attach();if(!m_bar||!m_content||!m_container||!m_pages)return;
    // Only a task-button interaction may own the native drag layout. A
    // launcher press can also trigger OEM realign(); deferring *all* mouse
    // presses lets native square-cell positions reach a visible paint frame.
    if(QApplication::mouseButtons()!=Qt::NoButton){
        for(auto *grab=QWidget::mouseGrabber();grab;grab=grab->parentWidget())if(grab->inherits("UKUITaskButton"))return;
        for(auto *button:m_bar->findChildren<QAbstractButton*>())if(button->inherits("UKUITaskButton")&&button->isDown())return;
    }
    QVector<Group> groups;int count=0,preferred=0;
    auto *layout=m_content->layout();
    for(int i=0;i<layout->count();++i) {
        auto *w=layout->itemAt(i)->widget();if(!w||!w->inherits("UKUITaskGroup")||w->isHidden())continue;
        Group group;group.widget=w;
        if(!w->layout()||QByteArray(w->layout()->metaObject()->className())!="UKUi::GridLayout")return;
        for(int j=0;j<w->layout()->count();++j) {
            auto *button=qobject_cast<QAbstractButton*>(w->layout()->itemAt(j)->widget());
            if(!button||!button->inherits("UKUITaskButton"))continue;
            const bool collapsed=button->property("liquidDockCollapsed").toBool();
            auto onDesktop=reinterpret_cast<bool (*)(QWidget*)>(native(button,"_ZN14UKUITaskButton18isOnCurrentDesktopEv"));
            if(button->isHidden()&&!collapsed)continue;
            if(collapsed&&onDesktop&&!onDesktop(button))continue;
            if(auto *tool=qobject_cast<QToolButton*>(button))if(tool->toolButtonStyle()!=Qt::ToolButtonIconOnly){restore();return;}
            remember(button);preferred=qMax(preferred,m_saved.value(button).icon.width());group.buttons.append(button);
        }
        if(!group.buttons.isEmpty()){
            // Keep the host's group and every window button alive: its hover
            // preview, context menu and window switching still own all IDs.
            auto windowId=reinterpret_cast<QVariant (*)(const QWidget*)>(native(w,"_ZNK14UKUITaskButton8windowIdEv"));
            QVector<qulonglong> ids;for(auto b:group.buttons)if(windowId)ids.append(windowId(b).toULongLong());
            const int windows=DockIndicator::state(ids,0).windows;
            auto leader=group.buttons.first();
            for(auto b:group.buttons){
                const bool hide=b!=leader;
                if(b->property("liquidDockCollapsed").toBool()!=hide)b->setProperty("liquidDockCollapsed",hide);
                if(hide&&!b->isHidden())b->hide();
            }
            if(leader->isHidden())leader->show();
            if(leader->property("liquidDockWindows").toInt()!=windows){leader->setProperty("liquidDockWindows",windows);leader->update();}
            group.buttons={leader};++count;remember(w);groups.append(group);
        }
    }
    const auto margins=m_container->layout()->contentsMargins();
    const int fullWidth=qMax(1,m_container->width()-margins.left()-margins.right());
    const int height=m_bar->height();preferred=qBound(20,preferred?preferred:32,qMax(20,int(height/1.4)));
    if(m_accessoryWidth<0||!m_panel->property("liquidDockResizing").toBool())m_accessoryWidth=m_panel->width()-fullWidth;
    if(auto *compat=m_panel->findChild<PanelLayoutCompat*>(QString(),Qt::FindDirectChildrenOnly))
        compat->setDockContentLength(m_accessoryWidth+DockLayout::solve(count,100000,preferred).content);
    auto metrics=DockLayout::solve(count,fullWidth,preferred);
    bool same=m_managed&&m_lastWidth==fullWidth&&m_lastHeight==height&&m_metrics.icon==metrics.icon&&m_metrics.content==metrics.content&&m_groups.size()==groups.size()
        &&gridMatches(layout,QSize(metrics.cell,height));
    if(same)for(int i=0;i<groups.size();++i){
        if(m_groups[i].widget!=groups[i].widget||m_groups[i].buttons!=groups[i].buttons){same=false;break;}
        if(groups[i].widget->width()!=groups[i].buttons.size()*metrics.cell){same=false;break;}
        if(!gridMatches(groups[i].widget->layout(),QSize(metrics.cell,height))){same=false;break;}
        for(auto b:groups[i].buttons)if(b->width()!=metrics.cell||b->iconSize().width()!=metrics.icon){same=false;break;}
    }
    if(same&&m_content->minimumWidth()==metrics.content&&m_bar->width()==fullWidth&&m_pages->maximumWidth()==0
        &&m_bar->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff&&m_bar->verticalScrollBarPolicy()==Qt::ScrollBarAlwaysOff)return;
    const bool topologyChanged=!m_managed||m_metrics.cell!=metrics.cell||m_lastHeight!=height||m_groups.size()!=groups.size();
    m_applying=true;if(topologyChanged)resetWave();m_groups=groups;m_metrics=metrics;m_lastWidth=fullWidth;m_lastHeight=height;m_managed=true;
    ++m_batches;
    m_bar->setProperty("liquidDockManaged",true);m_bar->setProperty("liquidDockReservedWidth",0);
    // Native spacing must not add extra gaps around the reserved page host.
    m_container->layout()->setSpacing(0);
    // Let OEM retain its logical page-arrow visibility. Calling hide() after
    // its Show creates a LayoutRequest -> realign -> Show loop once native
    // square cells overflow but our smaller cells still fit. Proportional
    // sizing keeps one row, so native paging needs no footprint or input.
    m_pages->setFixedWidth(0);m_pages->setAttribute(Qt::WA_DontShowOnScreen,true);m_pages->setEnabled(false);
    m_bar->setFixedWidth(fullWidth);m_bar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);m_bar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Preserve the native layout's item order and original buttons. GridLayout
    // is an exported public host API; no C++ private fields or object offsets.
    layout->setContentsMargins(metrics.padding,0,metrics.padding,0);
    const auto setSize=gridSize();
    setSize(layout,QSize(metrics.cell,height));
    for(const auto &g:groups) {
        auto *group=g.widget.data();if(!group)continue;
        group->setFixedSize(metrics.cell*g.buttons.size(),height);setSize(group->layout(),QSize(metrics.cell,height));
        for(auto b:g.buttons)if(b) {
            b->setFixedSize(metrics.cell,height);b->setIconSize(QSize(metrics.icon,metrics.icon));
            b->setProperty("liquidDockIconSize",metrics.icon);
            if(topologyChanged||!b->property("liquidDockScale").isValid()){b->setProperty("liquidDockScale",1.);b->setProperty("liquidDockLift",0.);}
            b->setProperty("doNotAnimate",true);b->setProperty("useIconHighlightEffect",false);
            b->setStyle(qApp->style());b->setMouseTracking(true);
        }
        group->layout()->invalidate();group->layout()->activate();
    }
    m_content->setMinimumSize(metrics.content,height);m_content->setMaximumSize(QWIDGETSIZE_MAX,height);
    m_content->resize(qMax(metrics.content,m_bar->viewport()->width()),height);
    layout->invalidate();layout->activate();
    m_bar->horizontalScrollBar()->setRange(0,0);
    m_bar->horizontalScrollBar()->setPageStep(m_bar->viewport()->width());
    m_bar->horizontalScrollBar()->setValue(0);
    m_panel->setProperty("liquidDockIconSize",metrics.icon);m_panel->setProperty("liquidDockCount",count);
    m_panel->setProperty("liquidDockOverflow",metrics.overflow);m_panel->setProperty("liquidDockLayoutBatches",qulonglong(m_batches));
    m_applying=false;updatePointer(QCursor::pos());
}
void PanelDockCompat::restore() {
    if(!m_managed)return;m_applying=true;resetWave();m_managed=false;
    if(auto *compat=m_panel->findChild<PanelLayoutCompat*>(QString(),Qt::FindDirectChildrenOnly))compat->setDockContentLength(0);
    for(auto it=m_saved.begin();it!=m_saved.end();++it){auto *w=it.key();w->setMinimumSize(it->minimum);w->setMaximumSize(it->maximum);
        w->setProperty("doNotAnimate",it->animation);w->setProperty("useIconHighlightEffect",it->highlight);
        if(w->property("liquidDockCollapsed").toBool()){w->setProperty("liquidDockCollapsed",QVariant());w->show();}
        if(auto *b=qobject_cast<QAbstractButton*>(w)){b->setProperty("liquidDockWindows",QVariant());b->setProperty("liquidDockIconSize",QVariant());b->setProperty("liquidDockScale",QVariant());b->setProperty("liquidDockLift",QVariant());b->setIconSize(it->icon);if(it->style)b->setStyle(it->style);}}
    for(const auto &group:m_groups)if(group.widget){
        auto refresh=reinterpret_cast<void (*)(QWidget*)>(native(group.widget,"_ZN13UKUITaskGroup21refreshButtonsVisibleEv"));
        if(refresh)refresh(group.widget);
    }
    if(m_content){m_content->setMinimumSize(m_contentMinimum);m_content->setMaximumSize(m_contentMaximum);m_content->layout()->setContentsMargins(0,0,0,0);}
    if(m_pages){m_pages->setAttribute(Qt::WA_DontShowOnScreen,m_pagesOffscreen);m_pages->setEnabled(m_pagesEnabled);m_pages->setMinimumSize(m_pagesMinimum);m_pages->setMaximumSize(m_pagesMaximum);}
    if(m_container&&m_container->layout())m_container->layout()->setSpacing(m_containerSpacing);
    if(m_bar){m_bar->setHorizontalScrollBarPolicy(m_horizontalPolicy);m_bar->setVerticalScrollBarPolicy(m_verticalPolicy);m_bar->setProperty("liquidDockManaged",false);m_bar->setProperty("liquidDockReservedWidth",0);
        using Realign=void (*)(QWidget*);auto realign=reinterpret_cast<Realign>(native(m_bar,"_ZN11UKUITaskBar7realignEv"));if(realign)realign(m_bar);}
    m_lastWidth=-1;m_panel->setProperty("liquidDockIconSize",0);m_applying=false;
}
void PanelDockCompat::resetWave() {
    m_frame.stop();m_target=0;m_clicked.clear();m_pressed=false;m_hoverActive=false;
    for(const auto &g:m_groups)for(auto b:g.buttons)if(b){b->setProperty("liquidDockScale",1.);b->setProperty("liquidDockLift",0.);b->update();}
    if(m_panel)m_panel->setProperty("liquidDockWaveRunning",false);
}
void PanelDockCompat::updatePointer(const QPoint &global,bool allowed) {
    const bool active=allowed&&m_managed&&m_bar&&m_panel->property("liquidDockHoverMagnification").toBool()
        &&m_bar->viewport()->rect().contains(m_bar->viewport()->mapFromGlobal(global));
    const bool changed=active!=m_hoverActive||(active&&global!=m_pointer);m_pointer=global;m_hoverActive=active;
    // Default-off hover never starts a frame timer or changes layout.
    if(changed){m_clock.restart();if(!m_frame.isActive())m_frame.start(0);}
}
void PanelDockCompat::feedback(QWidget *button,bool pressed) {
    if(!m_managed||!button||!m_panel->property("liquidDockClickFeedback").toBool())return;
    bool known=false;for(const auto &g:m_groups)for(auto b:g.buttons)known|=b==button;
    if(!known)return;
    m_clicked=button;m_pressed=pressed;m_feedbackClock.restart();m_clock.restart();
    if(!m_frame.isActive())m_frame.start(0);
}
void PanelDockCompat::frame() {
    if(!m_managed||!m_bar||!m_panel->isVisible()){resetWave();return;}
    const qreal dt=qMax(.001,m_clock.restart()/1000.);
    const qreal age=m_clicked?m_feedbackClock.elapsed()/1000.:1.;
    const qreal pulse=!m_clicked||m_pressed?0:qSin(M_PI*qBound(0.,age/.30,1.));
    const qreal clickedX=m_clicked?m_clicked->mapTo(m_bar,m_clicked->rect().center()).x():-100000.;
    const qreal pointerX=m_bar->mapFromGlobal(m_pointer).x();
    bool moving=false;
    for(const auto &g:m_groups)for(auto b:g.buttons)if(b) {
        const bool clicked=b==m_clicked;
        const qreal distance=qAbs(b->mapTo(m_bar,b->rect().center()).x()-clickedX);
        const qreal weight=clicked?1.:distance<=m_metrics.cell*1.1?.20:0.;
        const qreal hover=m_hoverActive?.28*DockLayout::influence(b->mapTo(m_bar,b->rect().center()).x()-pointerX,m_metrics.cell*1.6):0.;
        const qreal target=qMin(1.35,m_pressed?1.+hover-.10*weight:1.+hover+.18*pulse*weight);
        const qreal current=b->property("liquidDockScale").isValid()?b->property("liquidDockScale").toDouble():1.;
        const qreal scale=DockLayout::advance(current,target,dt),lift=hover*3+(m_pressed?-.8*weight:1.4*pulse*weight);
        moving|=scale!=target;
        if(current!=scale||b->property("liquidDockLift").toDouble()!=lift){b->setProperty("liquidDockScale",scale);b->setProperty("liquidDockLift",lift);b->update();}
    }
    ++m_frames;
    const bool running=moving||(m_clicked&&!m_pressed&&age<.30);
    m_panel->setProperty("liquidDockWaveFrames",qulonglong(m_frames));m_panel->setProperty("liquidDockWaveRunning",running);
    if(running)m_frame.start(16);else if(!m_pressed){if(!m_hoverActive)resetWave();else m_clicked.clear();}
}
bool PanelDockCompat::eventFilter(QObject *object,QEvent *event) {
    if(!m_panel)return false;
    if(object==m_panel&&event->type()==QEvent::DynamicPropertyChange){
        const auto name=static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
        if(name=="liquidDockEnabled"||name=="liquidDockClickFeedback"||name=="liquidDockHoverMagnification"){resetWave();schedule();}
    }
    auto *w=qobject_cast<QWidget*>(object);if(!w)return false;
    const bool inBar=m_bar&&descendant(w,m_bar);
    if(m_managed&&inBar&&w->inherits("UKUITaskButton")&&(event->type()==QEvent::UpdateLater||event->type()==QEvent::EnabledChange||event->type()==QEvent::DynamicPropertyChange)){
        if(w->property("liquidDockCollapsed").toBool())for(const auto &group:m_groups)
            if(group.widget==w->parentWidget())for(auto leader:group.buttons)if(leader)leader->update();
    }
    if(m_managed&&inBar&&event->type()==QEvent::Paint&&w->property("liquidDockIconSize").toInt()>0){
        if(auto *button=qobject_cast<QToolButton*>(w)){
            QStyleOptionToolButton option;option.initFrom(button);option.icon=button->icon();option.iconSize=button->iconSize();option.toolButtonStyle=Qt::ToolButtonIconOnly;
            if(button->isDown())option.state|=QStyle::State_Sunken;
            if(button->isChecked())option.state|=QStyle::State_On;
            QPainter painter(button);button->style()->drawComplexControl(QStyle::CC_ToolButton,&option,&painter,button);
            const int windows=button->property("liquidDockWindows").toInt();
            bool groupActive=false;
            if(windows>0){
                auto current=reinterpret_cast<WId (*)()>(native(button,"_ZN13KWindowSystem12activeWindowEv"));
                auto windowId=reinterpret_cast<QVariant (*)(const QWidget*)>(native(button,"_ZNK14UKUITaskButton8windowIdEv"));
                const WId active=current?current():0;
                // OEM isActiveWindow also compares the pinned launcher's ID 0
                // with activeWindow()==0. That lights every pinned group when
                // no real client has focus (for example, Show Desktop).
                QVector<qulonglong> ids;
                if(windowId)for(auto *member:button->parentWidget()->findChildren<QAbstractButton*>())
                    if(member->inherits("UKUITaskButton"))ids.append(windowId(member).toULongLong());
                groupActive=DockIndicator::state(ids,active).active;
                button->setProperty("liquidDockGroupActive",groupActive);
                painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);painter.setBrush(QColor(groupActive?"#3184d4":"#adb6bf"));
                const qreal width=windows>1?12:4;const QRectF mark((button->width()-width)/2.,button->height()-5,width,windows>1?3:4);
                if(windows>1)painter.drawRoundedRect(mark,1.5,1.5);else painter.drawEllipse(mark);
            }
            else button->setProperty("liquidDockGroupActive",false);
            return true;
        }
    }
    // This layout fits applications in a single row. Do not let OEM paging
    // move the content behind the hidden native arrows/scrollbars.
    if(m_managed&&inBar&&event->type()==QEvent::Wheel){event->accept();return true;}
    // OEM realign() rewrites GridLayout's preferred min/max cell sizes. A
    // zero-timer alone lets one expanded native frame reach the screen before
    // our compact cells return. Repair at layout/frame boundaries, before any
    // child painting; the unchanged fast path does not repaint or restart motion.
    if(m_managed&&!m_applying&&
       ((event->type()==QEvent::LayoutRequest&&inBar)||
        (event->type()==QEvent::UpdateRequest&&w==m_panel)||
        (event->type()==QEvent::Paint&&(w==m_panel||w->objectName()=="BackgroundWidget"))))apply();
    if(m_managed&&(inBar||w==m_panel)) {
        if(event->type()==QEvent::MouseMove)updatePointer(static_cast<QMouseEvent*>(event)->globalPos());
        else if(event->type()==QEvent::Enter||event->type()==QEvent::Leave)updatePointer(QCursor::pos());
        else if(event->type()==QEvent::MouseButtonPress&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton)feedback(w,true);
        else if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){feedback(w,false);schedule();}
        else if(event->type()==QEvent::DragEnter){resetWave();}
        else if(event->type()==QEvent::Drop)schedule();
    }
    if(w==m_panel&&event->type()==QEvent::Hide)resetWave();
    if(!m_applying&&(w==m_panel||w==m_container||inBar||w->inherits("UKUITaskBar")))
        switch(event->type()){
        case QEvent::Show:case QEvent::Hide:case QEvent::Resize:case QEvent::LayoutRequest:case QEvent::ChildPolished:case QEvent::StyleChange:schedule();break;
        default:break;}
    return false;
}
