#include "PanelController.h"
#include "../../../shared/icon-style/OpticalIcon.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QProxyStyle>
#include <QStylePlugin>
#include <QStyleFactory>
#include <QTimer>
#include <QDebug>
#include <QMenu>
#include <QStyleOptionMenuItem>
#include <QStyleOptionToolButton>
#include <QPainter>
#include <QPixmapCache>
#include <QtMath>
class PanelStyle final : public QProxyStyle {
public:
    void drawComplexControl(ComplexControl element,const QStyleOptionComplex *option,QPainter *painter,const QWidget *widget=nullptr) const override {
        if(element==CC_ToolButton&&widget&&widget->property("liquidDockIconSize").toInt()>0) {
            if(const auto *tool=qstyleoption_cast<const QStyleOptionToolButton*>(option))if(tool->toolButtonStyle==Qt::ToolButtonIconOnly){
                // The native hover/checked plate is deliberately omitted. The
                // native task group still paints its running/active indicator.
                drawControl(CE_ToolButtonLabel,tool,painter,widget);return;
            }
        }
        QProxyStyle::drawComplexControl(element,option,painter,widget);
    }
    void drawPrimitive(PrimitiveElement element,const QStyleOption *option,QPainter *painter,const QWidget *widget=nullptr) const override {
        if(LiquidPopup::isEnabled() && qobject_cast<const QMenu *>(widget) &&
           widget->property("liquidPopupSkin").toBool() && LiquidPopup::drawMenuGlyph(element,option,painter))return;
        QProxyStyle::drawPrimitive(element,option,painter,widget);
    }
    void drawControl(ControlElement element,const QStyleOption *option,QPainter *painter,const QWidget *widget=nullptr) const override {
        painter->save();
        if(element==CE_ToolButtonLabel&&widget&&widget->property("liquidDockIconSize").toInt()>0) {
            if(const auto *tool=qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
                const int base=widget->property("liquidDockIconSize").toInt();
                const qreal scale=qBound(.80,widget->property("liquidDockScale").toDouble(),1.35);
                if(tool->toolButtonStyle==Qt::ToolButtonIconOnly&&!tool->icon.isNull()) {
                    const bool magnify=widget->window()->property("liquidDockHoverMagnification").toBool();
                    const int inset=magnify?1:qMax(1,qRound(base*.12));
                    const qreal size=qMax(1.,qMin(qreal(qMin(widget->width()-inset,widget->height()-(magnify?4:6))),base*scale));
                    // Request one stable raster at the largest displayed size.
                    // Asking the icon theme for a new integer size every frame
                    // switches raster variants and makes high-DPI icons jump.
                    const int pixels=qCeil(base*1.35*widget->devicePixelRatioF());
                    const auto mode=tool->state.testFlag(State_Enabled)?QIcon::Normal:QIcon::Disabled;
                    const auto state=tool->state.testFlag(State_On)?QIcon::On:QIcon::Off;
                    const QString key=QStringLiteral("liquid-dock-%1-%2-%3-%4-%5").arg(tool->icon.cacheKey()).arg(pixels).arg(int(mode)).arg(int(state)).arg(LquiIcon::revision());
                    QPixmap pixmap;
                    if(!QPixmapCache::find(key,&pixmap)) {
                        pixmap=LquiIcon::raster(tool->icon,pixels,mode,state);
                        QPixmapCache::insert(key,pixmap);
                    }
                    if(!pixmap.isNull()) {
                        QPointF center=QRectF(tool->rect).center();
                        center.ry()-=widget->property("liquidDockLift").toDouble();
                        if(tool->state.testFlag(State_Sunken))center+=QPointF(pixelMetric(PM_ButtonShiftHorizontal,tool,widget),pixelMetric(PM_ButtonShiftVertical,tool,widget));
                        const QSizeF extent=QSizeF(pixmap.size()).scaled(QSizeF(size,size),Qt::KeepAspectRatio);
                        painter->setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
                        painter->drawPixmap(QRectF(center-QPointF(extent.width()/2,extent.height()/2),extent),pixmap,QRectF(pixmap.rect()));
                        painter->restore();return;
                    }
                }
            }
        }
        if(LiquidPopup::isEnabled() && qobject_cast<const QMenu *>(widget))
            painter->setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
        QProxyStyle::drawControl(element,option,painter,widget);
        painter->restore();
    }
    PanelStyle():QProxyStyle(QStyleFactory::create("ukui-default")) {
        setObjectName("ukuiliquidpanel");
        // Qt requests the style during QApplication construction. Install only
        // once its event loop is ready and all UKUI startup attributes exist.
        QTimer::singleShot(0,qApp,[]{
            if (!qApp->property("liquidPanelController").toBool()) {
                qApp->setProperty("liquidPanelController",true);
                new PanelController(qApp);
            }
        });
    }
};
class PanelStylePlugin final : public QStylePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "style.json")
public:
    QStyle *create(const QString &key) override {
        if(key.compare("ukuiliquidpanel",Qt::CaseInsensitive)!=0) return nullptr;
        return new PanelStyle;
    }
};
#include "PanelStyle.moc"
