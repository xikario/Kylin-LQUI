#include "PanelSystemControls.h"
#include "ControlCenterLauncher.h"
#include <QAbstractButton>
#include <QDynamicPropertyChangeEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmapCache>
#include <QtMath>
#include <QWidget>
#include <dlfcn.h>
namespace {
using GridSize=QSize (*)(QLayout*);
using GridWidth=void (*)(QLayout*,int);
GridSize gridMinimum(){return reinterpret_cast<GridSize>(dlsym(RTLD_DEFAULT,"_ZNK4UKUi10GridLayout15cellMinimumSizeEv"));}
GridSize gridMaximum(){return reinterpret_cast<GridSize>(dlsym(RTLD_DEFAULT,"_ZNK4UKUi10GridLayout15cellMaximumSizeEv"));}
GridWidth gridFixed(){return reinterpret_cast<GridWidth>(dlsym(RTLD_DEFAULT,"_ZN4UKUi10GridLayout17setCellFixedWidthEi"));}
void restoreGrid(QLayout *layout,int low,int high){
    auto minimum=reinterpret_cast<GridWidth>(dlsym(RTLD_DEFAULT,"_ZN4UKUi10GridLayout19setCellMinimumWidthEi"));
    auto maximum=reinterpret_cast<GridWidth>(dlsym(RTLD_DEFAULT,"_ZN4UKUi10GridLayout19setCellMaximumWidthEi"));
    if(minimum&&maximum){minimum(layout,low);maximum(layout,high);}
}

QPixmap readableIcon(QAbstractButton *button,int pixels){
    const QString key=QString("lqui-system-%1-%2-%3").arg(button->icon().cacheKey()).arg(pixels).arg(button->isEnabled());QPixmap pixmap;
    if(QPixmapCache::find(key,&pixmap))return pixmap;
    QImage image=button->icon().pixmap(pixels,pixels,button->isEnabled()?QIcon::Normal:QIcon::Disabled).toImage().convertToFormat(QImage::Format_ARGB32);
    // Symbolic icons may arrive black from a light UKUI icon theme. Brighten
    // only dark neutral pixels; retain battery and notification status colors.
    for(int y=0;y<image.height();++y){auto *line=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x){const QRgb color=line[x];const int high=qMax(qRed(color),qMax(qGreen(color),qBlue(color))),low=qMin(qRed(color),qMin(qGreen(color),qBlue(color)));if(qAlpha(color)&&high<145&&high-low<45)line[x]=qRgba(217,228,239,qAlpha(color));}}
    pixmap=QPixmap::fromImage(image);QPixmapCache::insert(key,pixmap);return pixmap;
}
QString role(QWidget *widget){
    const QString identity=widget->accessibleName();
    if(identity.endsWith("_ukui-sidebar"))return "notifications";
    if(identity.endsWith("_kylin-nm"))return "wifi";
    if(identity.endsWith("_ukui-volume-control-applet-qt"))return "sound";
    if(identity.endsWith("_ukui-power-manager-tray"))return "power";
    return {};
}
class Capsule:public QWidget {
public:Capsule(QWidget *parent):QWidget(parent){setObjectName("liquidSystemCapsule");setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_TranslucentBackground);}
protected:void paintEvent(QPaintEvent*)override{
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(100,116,139,150),1));p.setBrush(QColor(10,16,23,135));p.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),11,11);
}
};
}
PanelSystemControls::PanelSystemControls(QObject *parent):QObject(parent){setObjectName("liquidSystemControls");pending.setSingleShot(true);connect(&pending,&QTimer::timeout,this,&PanelSystemControls::sync);qApp->installEventFilter(this);schedule();}
PanelSystemControls::~PanelSystemControls(){if(qApp)qApp->removeEventFilter(this);restore();}
void PanelSystemControls::schedule(){if(!applying&&!pending.isActive())pending.start(0);}
void PanelSystemControls::restore(){
    applying=true;if(capsule)delete capsule;
    for(const auto &saved:originals)if(saved.widget){saved.widget->setMinimumSize(saved.minimum);saved.widget->setMaximumSize(saved.maximum);saved.widget->setIconSize(saved.icon);saved.widget->setProperty("liquidSystemPage",QVariant());}
    for(const auto &saved:hosts)if(saved.widget&&saved.widget->layout())restoreGrid(saved.widget->layout(),saved.minimum,saved.maximum);
    for(const auto &saved:bounds)if(saved.widget){saved.widget->setMinimumSize(saved.minimum);saved.widget->setMaximumSize(saved.maximum);saved.widget->updateGeometry();}
    bounds.clear();hosts.clear();originals.clear();capsule.clear();
    // The set of icons can grow after the initial geometry was recorded.
    // Let the OEM rebuild its present-day extents when opting out.
    QWidget *area=home?home->parentWidget():nullptr;while(area&&!area->inherits("StatusNotiferScrollArea"))area=area->parentWidget();
    if(area){Dl_info info{};if(dladdr(area->metaObject(),&info)&&info.dli_fname){void *module=dlopen(info.dli_fname,RTLD_LAZY|RTLD_NOLOAD);if(module){using Realign=void (*)(QWidget*);auto realign=reinterpret_cast<Realign>(dlsym(module,"_ZN23StatusNotiferScrollArea7realignEv"));if(realign)realign(area);dlclose(module);}}}
    applying=false;
}
void PanelSystemControls::sync(){
    if(applying)return;
    if(!LquiControl::enabled()){if(!originals.isEmpty()||capsule)restore();return;}
    if(!home)for(auto *w:QApplication::allWidgets())if(w->objectName()=="StatusNotifier_outwidget"&&w->window()->inherits("UKUIPanel")){home=w;break;}
    if(!home)return;
    if(home->window()->width()<home->window()->height()){restore();return;}
    applying=true;
    qApp->setProperty("lquiControlSyncs",qApp->property("lquiControlSyncs").toInt()+1);
    for(auto it=originals.begin();it!=originals.end();){
        const auto saved=it.value();
        if(!saved.widget){it=originals.erase(it);continue;}
        if(saved.widget->window()!=home->window()){
            saved.widget->setMinimumSize(saved.minimum);saved.widget->setMaximumSize(saved.maximum);saved.widget->setIconSize(saved.icon);saved.widget->setProperty("liquidSystemPage",QVariant());it=originals.erase(it);
        }else ++it;
    }
    // Keep OEM parents, ordering and icon updates intact. Only the three core
    // button extents and their primary action change; wheel/context stay native.
    for(auto *button:home->findChildren<QAbstractButton*>())if((button->inherits("StatusNotifierButton")||button->inherits("StatusNotifierStorageArrow"))&&button->window()==home->window()){
        const auto page=role(button);
        if(!originals.contains(button))originals.insert(button,{button,button->minimumSize(),button->maximumSize(),button->iconSize()});
        if(button->property("liquidSystemPage").toString()!=page)button->setProperty("liquidSystemPage",page);
        if(button->minimumWidth()!=25||button->maximumWidth()!=25)button->setFixedWidth(25);

    }
    // Use the OEM layout's public cell constraint instead of fighting its
    // square size hint or replacing/reparenting native tray items.
    int total=0;
    auto widthFor=[this](QWidget *widget,int width){if(!bounds.contains(widget))bounds.insert(widget,{widget,widget->minimumSize(),widget->maximumSize()});if(widget->minimumWidth()!=width||widget->maximumWidth()!=width)widget->setFixedWidth(width);};
    for(const auto &name:QStringList{"StatusNotifier_statusNotifierWidget","StatusNotifier_fixedWidget"}){
        auto *row=home->findChild<QWidget*>(name);if(!row||!row->layout())continue;
        auto *layout=row->layout();const auto minimum=gridMinimum();const auto maximum=gridMaximum();const auto fixed=gridFixed();
        if(!layout->inherits("UKUi::GridLayout")||!minimum||!maximum||!fixed)continue;
        if(!hosts.contains(row))hosts.insert(row,{row,minimum(layout).width(),maximum(layout).width()});
        if(minimum(layout).width()!=25||maximum(layout).width()!=25)fixed(layout,25);
        const int width=layout->count()*25;widthFor(row,width);total+=width;
    }
    if(total>0){widthFor(home,total);QWidget *area=home->parentWidget();while(area&&!area->inherits("StatusNotiferScrollArea"))area=area->parentWidget();if(area)widthFor(area,total);}
    if(!capsule){capsule=new Capsule(home);capsule->lower();capsule->show();}
    const int height=qMin(36,home->height());const QRect area(0,(home->height()-height)/2,home->width(),height);
    if(capsule->geometry()!=area)capsule->setGeometry(area);
    applying=false;
}
bool PanelSystemControls::eventFilter(QObject *object,QEvent *event){
    // OEM paintEvent unconditionally resets every item to a square. Draw the
    // current native icon ourselves so compact geometry remains stable.
    if(event->type()==QEvent::Paint&&originals.contains(qobject_cast<QAbstractButton*>(object))&&LquiControl::enabled()){
        auto *button=qobject_cast<QAbstractButton*>(object);QPainter painter(button);painter.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
        if(button->isDown()){painter.setPen(Qt::NoPen);painter.setBrush(QColor(155,195,230,40));painter.drawRoundedRect(button->rect().adjusted(1,3,-1,-3),7,7);}
        const int size=qMin(20,qMax(16,button->iconSize().width()));const QPixmap icon=readableIcon(button,qCeil(size*button->devicePixelRatioF()));painter.drawPixmap(QRect((button->width()-size)/2,(button->height()-size)/2,size,size),icon);
        if(button->hasFocus()){painter.setPen(QColor(160,215,245,140));painter.setBrush(Qt::NoBrush);painter.drawRoundedRect(button->rect().adjusted(1,3,-2,-4),6,6);}
        return true;
    }
    if(applying)return false;
    if(object==qApp&&event->type()==QEvent::DynamicPropertyChange&&static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="liquidSystemControlsEnabled"){schedule();return false;}
    auto *widget=qobject_cast<QWidget*>(object);if(!widget)return false;
    const QString page=widget->property("liquidSystemPage").toString();
    if(!page.isEmpty()&&LquiControl::enabled()){
        auto *button=qobject_cast<QAbstractButton*>(widget);
        const auto activate=[&]{QWidget *anchor=home?home.data():widget;return LquiControl::open(page,QRect(anchor->mapToGlobal(QPoint()),anchor->size()));};
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease){
            auto *mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::LeftButton){
                if(event->type()==QEvent::MouseButtonPress)button->setDown(true);
                else {const bool click=button->isDown()&&button->rect().contains(mouse->pos());button->setDown(false);if(click)activate();}
                event->accept();return true;
            }
        }
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease){
            auto *key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter||key->key()==Qt::Key_Space){if(event->type()==QEvent::KeyRelease&&!key->isAutoRepeat())activate();event->accept();return true;}
        }
    }
    if(widget->inherits("StatusNotifierButton")&&event->type()==QEvent::Paint&&page.isEmpty()&&!role(widget).isEmpty())schedule();
    const bool related=widget==home||widget->inherits("StatusNotifierButton")||widget->inherits("UKUIPanel")||widget->objectName().startsWith("StatusNotifier_");
    if(related&&(event->type()==QEvent::Resize||event->type()==QEvent::LayoutRequest||event->type()==QEvent::Show||event->type()==QEvent::ChildPolished))schedule();
    if(widget->objectName()=="BackgroundWidget"&&event->type()==QEvent::Paint&&LquiControl::enabled())sync();
    return false;
}
