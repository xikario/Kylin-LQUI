#include "PanelLayoutCompat.h"
#include "DockLayoutMath.h"
#include "DockIndicatorState.h"
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QAction>
#include <QEventLoop>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QMenu>
#include <QCheckBox>
#include <QSlider>
#include <QDialog>
#include <QVariantAnimation>
#include <cstdlib>
class UKUIPanel : public QWidget {
    Q_OBJECT
public slots:
    void realign() {
        const QRect r = QApplication::primaryScreen()->geometry();
        setFixedSize(r.width(), 48); move(r.left(), r.bottom() - 47);
    }
};
static void check(bool ok, const char *message) {
    if (!ok) { qCritical() << "FAIL:" << message; std::exit(1); }
    qInfo() << "PASS:" << message;
}
static void wait(int ms=100) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static QList<QRect> rectangleSamples() {
    QList<QRect> values;
    for (const auto &pair : {qMakePair(QRect(0,0,2,2),QRect(1,1,3,3)),
                            qMakePair(QRect(-7,-5,8,12),QRect(6,4,19,9))}) {
        QVariantAnimation animation;
        animation.setStartValue(pair.first);animation.setEndValue(pair.second);animation.setDuration(100);
        for (int t : {25,50,75}) { animation.setCurrentTime(t);values.append(animation.currentValue().toRect()); }
    }
    return values;
}
int main(int argc, char **argv) {
    check(DockIndicator::state({0},0).windows==0&&!DockIndicator::state({0},0).active,"pinned launcher without a window has no running or active marker");
    check(DockIndicator::state({0,42},0).windows==1&&!DockIndicator::state({0,42},0).active,"no active client never matches the zero-ID pinned placeholder");
    check(!DockIndicator::state({0,42},77).active&&DockIndicator::state({0,42},42).active,"only the real foreground window receives the blue marker");
    check(DockIndicator::state({0,42,77},77).windows==2&&DockIndicator::state({0,42,77},77).active,"grouped windows retain their count and active member");
    const auto dense=DockLayout::solve(40,50,32);
    check(!dense.overflow&&dense.content<=50&&dense.cell==1,"pixel-floor icons fit without rounded-up spacing overflow");
    QTemporaryDir temp; qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
    QApplication app(argc, argv);
    const auto originalRectangles=rectangleSamples();
    const QString file=temp.path()+"/ukui/liquid-panel.ini";
    QSettings native(temp.path()+"/ukui/panel.conf", QSettings::IniFormat);
    native.setValue("panel1/position", "Bottom");native.sync();
    UKUIPanel panel; panel.setObjectName("UKUIPanel panel1");panel.realign();panel.show();wait();
    auto *compat=panel.findChild<QObject *>("liquidPanelLayoutCompat");
    check(compat && panel.property("liquidPanelLayoutBackend").toString()=="widget", "missing native slots select widget backend");
    check(rectangleSamples()==originalRectangles,"layout adapter preserves Qt interpolation for unrelated rectangles");
    const QByteArray originalNative=[&]{QFile f(native.fileName());f.open(QIODevice::ReadOnly);return f.readAll();}();
    QMenu menu(&panel);menu.show();wait();menu.hide();
    auto *settings=menu.findChild<QAction *>("liquidPanelSettingsAction");
    check(settings, "OEM-like panel has appearance settings action");settings->trigger();wait();
    auto *dialog=panel.findChild<QDialog *>();
    auto *center=dialog?dialog->findChild<QCheckBox *>("liquidPanelCentered"):nullptr;
    auto *length=dialog?dialog->findChild<QSlider *>("liquidPanelCenteredLength"):nullptr;
    check(center && center->isEnabled() && !center->isChecked() && length, "fallback settings enabled and default off");
    check(!QFile::exists(file), "opening settings is read-only");
    center->click();wait();
    const QRect bounds=QApplication::primaryScreen()->geometry();
    for(int percent:{30,68,95}) {
        length->setValue(percent);wait();
        const int width=bounds.width()*percent/100;
        check(panel.width()==width && panel.x()==bounds.x()+(bounds.width()-width)/2, "setting changes actual width and center");
        panel.realign();wait();
        check(panel.width()==width && panel.x()==bounds.x()+(bounds.width()-width)/2, "native realign returns to saved target");
        const int hiddenY=bounds.bottom()-1;panel.move(panel.x(),hiddenY);wait();
        check(panel.y()==hiddenY, "compat preserves native hidden cross-axis position");
        panel.realign();wait();
    }
    const auto corrections=panel.property("liquidPanelLayoutCorrections").toULongLong();
    const auto batches=panel.property("liquidPanelLayoutBatches").toULongLong();wait(350);
    check(corrections==panel.property("liquidPanelLayoutCorrections").toULongLong()
        && batches==panel.property("liquidPanelLayoutBatches").toULongLong(), "idle has no layout timer or feedback loop");
    for(int i=0;i<100;++i) QMetaObject::invokeMethod(compat,"schedule");wait();
    check(panel.property("liquidPanelLayoutBatches").toULongLong()==batches+1, "event burst merges into one batch");
    dialog->close();wait();
    { UKUIPanel restarted;restarted.setObjectName("UKUIPanel panel1");restarted.realign();restarted.show();wait();
      check(restarted.width()==bounds.width()*95/100, "new panel restores actual saved geometry"); }
    bool accepted=false;
    QMetaObject::invokeMethod(compat,"setCentered",Q_RETURN_ARG(bool,accepted),Q_ARG(bool,false),Q_ARG(int,95));
    check(accepted, "disable accepted");wait();
    check(panel.width()==bounds.width() && panel.x()==bounds.x(), "disable releases constraints and restores native full width");
    QFile f(native.fileName());f.open(QIODevice::ReadOnly);
    check(f.readAll()==originalNative, "fallback never writes unsupported fields into native config");
    native.setValue("panel1/position","Top");native.sync();
    QMetaObject::invokeMethod(compat,"schedule");wait();
    check(panel.property("liquidPanelLayoutBackend").toString()=="unsupported", "unverified docking edge is explicitly unsupported");
    return 0;
}
#include "layout_compat_test.moc"
