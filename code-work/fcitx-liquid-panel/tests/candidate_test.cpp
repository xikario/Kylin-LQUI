#include "PanelAgent.h"
#include "PinyinTones.h"
#include "LiquidPopup.h"
#include <QtTest>
#include <QScreen>
#include <QPainter>
#include "SettingsDialog.h"
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTemporaryDir>
#include <QSettings>
#include <QProcess>
#include <QComboBox>
#include <QScrollArea>

class CandidateTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { LiquidPopup::install(*qApp); }
    void configurationAndPreview() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path=dir.path()+"/panel.ini";
        CandidateConfig config; config.bold=true; config.fontSize=22; config.autoContrast=false;
        config.textColor=QColor("#ffe2a34b"); config.selectionColor=QColor(150,60,90,70);
        QVERIFY(saveCandidateConfig(config,path));
        const auto loaded=loadCandidateConfig(path);
        QCOMPARE(loaded.fontSize,22); QVERIFY(loaded.bold); QVERIFY(!loaded.autoContrast);
        QCOMPARE(loaded.textColor,config.textColor); QCOMPARE(loaded.selectionColor,config.selectionColor);
        QSettings invalid(path,QSettings::IniFormat); invalid.setValue("Colors/Text","not-a-color");invalid.setValue("Fonts/Size",999);invalid.sync();
        const auto bounded=loadCandidateConfig(path);QCOMPARE(bounded.fontSize,48);QVERIFY(bounded.textColor.isValid());
        CandidateWindow live; const auto original=live.config();
        SettingsDialog dialog(live);dialog.show();
        auto *installHint=dialog.findChild<QLabel *>("rimeInstallHint");QVERIFY(installHint);
        QVERIFY(installHint->text().contains(QStringLiteral("安装 fcitx-rime")));
        QVERIFY(installHint->text().contains(QStringLiteral("添加 Rime")));
        QVERIFY(installHint->text().contains(QStringLiteral("切换到 Rime")));
        auto *presets=dialog.findChild<QComboBox *>("preset");QVERIFY(presets);
        QCOMPARE(presets->accessibleName(),QStringLiteral("候选窗预设"));
        QCOMPARE(presets->count(),4);QCOMPARE(presets->itemText(2),QStringLiteral("紧凑两行"));
        auto *background=dialog.findChild<QComboBox *>("previewBackground");QVERIFY(background);
        QCOMPARE(background->count(),5);QCOMPARE(background->itemText(4),QStringLiteral("黑色"));
        auto *preview=dialog.findChild<CandidateWindow *>("settingsPreview");QVERIFY(preview);
        QVERIFY(QMetaObject::invokeMethod(presets,"activated",Qt::DirectConnection,Q_ARG(int,3)));
        QCOMPARE(dialog.draft().fontSize,20);QVERIFY(dialog.draft().bold);
        QCOMPARE(preview->config().fontSize,20);
        auto *size=dialog.findChild<QDoubleSpinBox *>("Fonts/Size");QVERIFY(size);
        size->setValue(24);
        QCOMPARE(preview->config().fontSize,24);QCOMPARE(live.config().fontSize,original.fontSize);
        QVERIFY(!preview->isWindow());QVERIFY(preview->captureCount()<=1);
        const int captures=preview->captureCount();
        dialog.resetDefaults();
        QCOMPARE(preview->config().fontSize,CandidateConfig().fontSize);
        QCOMPARE(preview->config().selectionColor,CandidateConfig().selectionColor);
        QCOMPARE(preview->captureCount(),captures);
        background->setCurrentIndex(4);
        QCOMPARE(preview->captureCount(),captures+1);
        if(qEnvironmentVariableIsSet("FCITX_LIQUID_ISOLATED_TEST")) dialog.grab().save("settings-preview.png");
        dialog.reject();QCOMPARE(live.config().fontSize,original.fontSize);
    }
    void applyAndCancel() {
        if(!qEnvironmentVariableIsSet("FCITX_LIQUID_CONFIG_TEST")) QSKIP("Requires isolated HOME");
        QVERIFY(candidateConfigPath().startsWith(QDir::tempPath()+"/"));
        CandidateWindow live;SettingsDialog dialog(live);
        auto *size=dialog.findChild<QDoubleSpinBox *>("Fonts/Size");QVERIFY(size);size->setValue(21);
        QVERIFY(dialog.applySettings());QTRY_COMPARE_WITH_TIMEOUT(live.config().fontSize,21,5000);QCOMPARE(loadCandidateConfig().fontSize,21);
        size->setValue(30);dialog.reject();QTRY_COMPARE_WITH_TIMEOUT(live.config().fontSize,21,5000);QCOMPARE(loadCandidateConfig().fontSize,21);
        SettingsDialog reset(live);reset.resetDefaults();QSignalSpy saved(&reset,&SettingsDialog::settingsApplied);QVERIFY(reset.applySettings());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,5000);
        QCOMPARE(loadCandidateConfig().fontSize,CandidateConfig().fontSize);
        QCOMPARE(live.config().selectionColor,CandidateConfig().selectionColor);
    }
    void interfacePreferencesAndPreview() {
        CandidateConfig cfg;QCOMPARE(cfg.pageSize,6);QCOMPARE(cfg.layoutDirection,0);
        CandidateWindow window(nullptr,true);window.setConfiguration(cfg);window.showPreview();
        QCOMPARE(window.candidateRects().size(),6);
        QVERIFY(!window.headerRect().isEmpty());
        cfg.showPreedit=false;cfg.showAuxiliary=false;cfg.showLabels=false;cfg.showPagingButtons=false;
        window.setConfiguration(cfg);window.refresh();
        QVERIFY(window.headerRect().isEmpty());QVERIFY(window.nextRect().isEmpty());
        QCOMPARE(window.candidateRects().size(),6);QVERIFY(window.isVisible());
        cfg.layoutDirection=1;cfg.pageSize=9;window.setConfiguration(cfg);window.refresh();
        const auto rows=window.candidateRects();QCOMPARE(rows.size(),9);
        for(int i=1;i<rows.size();++i){QCOMPARE(rows[i].x(),rows[0].x());QVERIFY(rows[i].top()>rows[i-1].bottom());}
        cfg.layoutDirection=0;cfg.pageSize=6;window.setConfiguration(cfg);
        window.state.layout=1;window.refresh();
        QVERIFY(window.candidateRects()[1].x()>window.candidateRects()[0].x());
        cfg.layoutDirection=2;window.setConfiguration(cfg);window.refresh();
        QVERIFY(window.candidateRects()[1].top()>window.candidateRects()[0].bottom());
        cfg.layoutDirection=0;cfg.showPagingButtons=true;window.setConfiguration(cfg);window.setPreviewWidth(300);window.refresh();
        QVERIFY(window.width()<=300);
        for(const auto &r:window.candidateRects())QVERIFY(window.rect().contains(r));
        QVERIFY(window.rect().contains(window.nextRect()));
        for(const auto &r:window.candidateRects())QVERIFY(!r.intersects(window.nextRect()));
        // Hiding the preedit must not hide a valid lookup or change its index.
        QSignalSpy selected(&window,&CandidateWindow::selected);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,window.candidateRects()[2].center());
        QCOMPARE(selected.first().first().toInt(),2);
        CandidateWindow live;SettingsDialog dialog(live);dialog.show();QTest::qWait(100);
        QCOMPARE(dialog.windowTitle(),QStringLiteral("RIME液态输入法设置"));QVERIFY(!dialog.windowIcon().isNull());
        auto *direction=dialog.findChild<QComboBox *>("Interface/Layout");QVERIFY(direction);
        direction->setCurrentIndex(1);
        auto *preedit=dialog.findChild<QCheckBox *>("Interface/ShowPreedit");QVERIFY(preedit);preedit->setChecked(false);
        QTest::qWait(100);
        auto *preview=dialog.findChild<CandidateWindow *>("settingsPreview");QVERIFY(preview);
        QCOMPARE(preview->config().layoutDirection,1);QVERIFY(preview->headerRect().isEmpty());
        QVERIFY(live.config().showPreedit);
        QTemporaryDir dir;const QString path=dir.path()+"/settings.ini";
        QVERIFY(saveCandidateConfig(cfg,path));auto loaded=loadCandidateConfig(path);
        QCOMPARE(loaded.pageSize,6);QVERIFY(!loaded.showPreedit);QVERIFY(!loaded.showLabels);
        QSettings invalid(path,QSettings::IniFormat);invalid.setValue("Interface/PageSize",999);invalid.setValue("Interface/Layout",99);invalid.sync();
        loaded=loadCandidateConfig(path);QCOMPARE(loaded.pageSize,10);QCOMPARE(loaded.layoutDirection,2);
    }
    void pageSizeApplyAndCancel() {
        if(!qEnvironmentVariableIsSet("FCITX_LIQUID_CONFIG_TEST"))QSKIP("Requires isolated configuration root");
        QVERIFY(candidateConfigPath().startsWith(QDir::tempPath()+"/"));
        CandidateWindow live;CandidateConfig cfg;live.setConfiguration(cfg);
        SettingsDialog dialog(live);auto *count=dialog.findChild<QDoubleSpinBox *>("Interface/PageSize");QVERIFY(count);
        QSignalSpy redeploy(&live,&CandidateWindow::rimeSettingsChanged);
        count->setValue(9);QVERIFY(dialog.applySettings());QTRY_COMPARE_WITH_TIMEOUT(redeploy.size(),1,5000);
        QCOMPARE(live.config().pageSize,9);QCOMPARE(loadCandidateConfig().pageSize,9);
        QFile patch(QFileInfo(candidateConfigPath()).dir().filePath("rime/default.custom.yaml"));QVERIFY(patch.open(QIODevice::ReadOnly));
        const auto saved=patch.readAll();QVERIFY(saved.contains("menu/page_size: 9"));patch.close();
        count->setValue(3);dialog.reject();
        QCOMPARE(live.config().pageSize,9);QVERIFY(patch.open(QIODevice::ReadOnly));QCOMPARE(patch.readAll(),saved);
    }
    void candidatePinyinAndIndependentHeader() {
        CandidateWindow window;PanelAgent agent(window);CandidateConfig cfg;cfg.showPreedit=false;window.setConfiguration(cfg);
        window.capture=[](QRect r){QImage im(r.size(),QImage::Format_RGB32);im.fill(QColor(30,55,80));return im;};
        agent.SetSpotRect(20,20,1,20);agent.SetLookupTable({"1.","2.","3."},{QStringLiteral("视频［shi pin］ "),QStringLiteral("重庆［chong qing］"),QStringLiteral("普通［中文］")},{},false,false,0,0);
        agent.ShowLookupTable(true);window.refresh();
        QCOMPARE(window.state.texts[0],QStringLiteral("视频"));QCOMPARE(window.state.pinyins[0],QString::fromUtf8("shì pín"));
        QCOMPARE(window.state.texts[1],QStringLiteral("重庆"));QCOMPARE(window.state.pinyins[1],QString::fromUtf8("chóng qìng"));
        QCOMPARE(window.state.texts[2],QStringLiteral("普通［中文］"));QVERIFY(window.state.pinyins[2].isEmpty());
        QVERIFY(window.headerRect().isEmpty());const int annotatedHeight=window.candidateRects()[0].height();
        const auto hanzi=window.candidateTextRects(0),reading=window.pinyinRects(0);
        QCOMPARE(hanzi.size(),2);QCOMPARE(reading.size(),2);
        for(int i=0;i<2;++i) {
            QVERIFY(reading[i].bottom()<hanzi[i].top());
            QCOMPARE(reading[i].center().x(),hanzi[i].center().x());
            QVERIFY(window.candidateRects()[0].contains(reading[i]));
            QVERIFY(window.candidateRects()[0].contains(hanzi[i]));
        }
        cfg.showCandidatePinyin=false;window.setConfiguration(cfg);window.refresh();QVERIFY(window.candidateRects()[0].height()<annotatedHeight);QVERIFY(window.pinyinRects(0).isEmpty());
        QSignalSpy chosen(&window,&CandidateWindow::selected);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,window.candidateRects()[1].center());
        QCOMPARE(chosen.first().first().toInt(),1);
        cfg.showCandidatePinyin=true;cfg.showPreedit=true;window.setConfiguration(cfg);window.refresh();
        QCOMPARE(window.candidateRects()[0].height(),annotatedHeight);
        cfg.rowHeight=10;cfg.pinyinFontSize=32;window.setConfiguration(cfg);window.refresh();
        for(const auto &r:window.pinyinRects(0))QVERIFY(window.candidateRects()[0].contains(r));
        QVERIFY(window.pinyinRects(0)[0].bottom()<window.candidateTextRects(0)[0].top());
    }
    void pinyinGapSettingsAndGeometry() {
        CandidateWindow window;PanelAgent agent(window);CandidateConfig cfg;
        cfg.showPreedit=false;cfg.showCandidatePinyin=true;cfg.pinyinGap=0;
        window.setConfiguration(cfg);
        window.capture=[](QRect r){QImage im(r.size(),QImage::Format_RGB32);im.fill(QColor(30,55,80));return im;};
        agent.SetSpotRect(20,20,1,20);
        agent.SetLookupTable({"1."},{QStringLiteral("这个［zhe ge］")},{},false,false,0,0);
        agent.ShowLookupTable(true);window.refresh();
        const int compactHeight=window.candidateRects()[0].height();
        QCOMPARE(window.candidateTextRects(0)[0].top()-window.pinyinRects(0)[0].bottom()-1,0);
        cfg.pinyinGap=12;cfg.rowHeight=1;window.setConfiguration(cfg);window.refresh();
        QVERIFY(window.candidateRects()[0].height()>compactHeight);
        QVERIFY(window.candidateTextRects(0)[0].top()-window.pinyinRects(0)[0].bottom()-1>=12);
        for(const auto &r:window.pinyinRects(0))QVERIFY(window.candidateRects()[0].contains(r));
        SettingsDialog dialog(window);auto *gap=dialog.findChild<QDoubleSpinBox *>("Geometry/PinyinGap");QVERIFY(gap);
        gap->setValue(8);QTest::qWait(100);
        auto *preview=dialog.findChild<CandidateWindow *>("settingsPreview");QVERIFY(preview);
        QCOMPARE(preview->config().pinyinGap,8);QCOMPARE(window.config().pinyinGap,12);
        dialog.reject();QCOMPARE(window.config().pinyinGap,12);
        QTemporaryDir dir;const QString path=dir.path()+"/spacing.ini";
        QVERIFY(saveCandidateConfig(cfg,path));QCOMPARE(loadCandidateConfig(path).pinyinGap,12);
        QSettings invalid(path,QSettings::IniFormat);invalid.setValue("Geometry/PinyinGap",999);invalid.sync();
        QCOMPARE(loadCandidateConfig(path).pinyinGap,32);
    }
    void zeroPinyinGapUsesVisibleInk() {
        CandidateWindow window;PanelAgent agent(window);CandidateConfig cfg;
        cfg.showPreedit=false;cfg.showLabels=false;cfg.pinyinGap=0;
        cfg.chineseFont="Microsoft YaHei";cfg.latinFont="Microsoft YaHei";
        cfg.candidateFontSize=15;cfg.pinyinFontSize=11;
        cfg.autoContrast=false;cfg.textColor=Qt::red;cfg.selectedTextColor=Qt::red;
        cfg.headerColor=Qt::green;cfg.selectionColor=Qt::transparent;
        window.setConfiguration(cfg);
        window.capture=[](QRect r){QImage im(r.size(),QImage::Format_RGB32);im.fill(QColor(30,55,80));return im;};
        agent.SetSpotRect(20,20,1,20);
        agent.SetLookupTable({"1."},{QStringLiteral("这个［zhe ge］")},{},false,false,0,0);
        agent.ShowLookupTable(true);window.refresh();
        const auto inkBounds=[](const QImage &im,bool green) {
            QRect result;
            for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x) {
                const auto c=im.pixelColor(x,y);
                if(c.blue()<100 && (green?(c.green()>120 && c.red()<100):(c.red()>120 && c.green()<100)))
                    result=result.united(QRect(x,y,1,1));
            }
            return result;
        };
        const auto render=[&window] {
            QImage im(window.size(),QImage::Format_ARGB32_Premultiplied);
            im.setDotsPerMeterX(qRound(window.logicalDpiX()/0.0254));im.setDotsPerMeterY(qRound(window.logicalDpiY()/0.0254));
            im.fill(Qt::transparent);window.render(&im);return im;
        };
        const QImage compact=render();
        const QRect reading=inkBounds(compact,true),main=inkBounds(compact,false);
        QVERIFY(!reading.isEmpty());QVERIFY(!main.isEmpty());
        const int visibleGap=main.top()-reading.bottom()-1;
        QVERIFY2(visibleGap>=0 && visibleGap<=3,qPrintable(QString("Visible ink gap: %1").arg(visibleGap)));
        // QFontMetrics already includes font DPI; don't scale its pixel height again.
        QFont mainFont(cfg.chineseFont,15),readingFont(cfg.latinFont,11);
        QVERIFY(window.candidateTextRects(0)[0].height()<=QFontMetrics(mainFont,&window).height());
        QVERIFY(window.pinyinRects(0)[0].height()<=QFontMetrics(readingFont,&window).height());
        cfg.pinyinGap=8;window.setConfiguration(cfg);window.refresh();
        const QImage spaced=render();
        const QRect nextReading=inkBounds(spaced,true),nextMain=inkBounds(spaced,false);
        QVERIFY(nextMain.top()-nextReading.bottom()-1>=visibleGap+8);
        QCOMPARE(reading.height(),nextReading.height());QCOMPARE(main.height(),nextMain.height());
    }
    void candidateUpdateBeforePaintKeepsFrameConsistent() {
        CandidateWindow window;PanelAgent agent(window);CandidateConfig cfg;
        cfg.showPreedit=false;window.setConfiguration(cfg);
        window.capture=[](QRect r){QImage im(r.size(),QImage::Format_RGB32);im.fill(QColor(30,55,80));return im;};
        agent.SetSpotRect(20,20,1,20);
        agent.SetLookupTable({"1."},{QStringLiteral("实时预览［shi shi yu lan］")},{},false,false,0,0);
        agent.ShowLookupTable(true);window.refresh();
        const auto draw=[&window] {
            QImage image(window.size(),QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);window.render(&image);return image;
        };
        const QImage previous=draw();
        // New D-Bus contents arrive before the zero-delay layout timer runs.
        agent.SetLookupTable({"1."},{QStringLiteral("这个［zhe ge］")},{},false,false,0,0);
        QCOMPARE(draw(),previous);
        window.refresh();QCOMPARE(window.candidateTextRects(0).size(),2);
        const QImage current=draw();QVERIFY(current!=previous);
        agent.SetLookupTable({"1."},{QStringLiteral("这［zhe］")},{},false,false,0,0);
        QCOMPARE(draw(),current);
        window.refresh();QCOMPARE(window.candidateTextRects(0).size(),1);
        // Toggling annotations must not pair old per-character geometry with
        // a newly collapsed, single-string word while a repaint is pending.
        cfg.showCandidatePinyin=false;window.setConfiguration(cfg);draw();
        window.refresh();QVERIFY(window.pinyinRects(0).isEmpty());
        draw();
        for(int i=0;i<40;++i) {
            cfg.showCandidatePinyin=i%2;window.setConfiguration(cfg);
            const QString candidate=i%2?QStringLiteral("实时预览［shi shi yu lan］"):QStringLiteral("这个［zhe ge］");
            agent.SetLookupTable({"1."},{candidate},{},false,false,0,0);
            draw();window.refresh();draw();
        }
    }
    void tonesAndRubyPreview() {
        const QVector<QPair<QString,QString>> cases={
            {QStringLiteral("视频"),QStringLiteral("shì pín")},
            {QStringLiteral("重庆"),QStringLiteral("chóng qìng")},
            {QStringLiteral("银行"),QStringLiteral("yín háng")},
            {QStringLiteral("长大"),QStringLiteral("zhǎng dà")},
            {QStringLiteral("好奇"),QStringLiteral("hào qí")},
            {QStringLiteral("女儿"),QStringLiteral("nǚ ér")},
            {QStringLiteral("孩子"),QStringLiteral("hái zi")},
            {QStringLiteral("音乐"),QStringLiteral("yīn yuè")}};
        const QStringList hints={"shi pin","chong qing","yin hang","zhang da","hao qi","nv er","hai zi","yin yue"};
        for(int i=0;i<cases.size();++i)QCOMPARE(PinyinTones::annotate(cases[i].first,hints[i]),cases[i].second);
        QCOMPARE(PinyinTones::annotate(QStringLiteral("视频"),"unknown pin"),QStringLiteral("unknown pín"));
        QCOMPARE(PinyinTones::annotate(QStringLiteral("视频"),"shi"),QString("shi"));
        QCOMPARE(PinyinTones::annotate("Rime",""),QString());
        CandidateWindow preview(nullptr,true);CandidateConfig cfg;cfg.showPreedit=false;
        cfg.chineseFont=QStringLiteral("微软雅黑");cfg.latinFont=QStringLiteral("微软雅黑");cfg.candidateFontSize=15;
        preview.setConfiguration(cfg);preview.showPreview();QTest::qWait(100);
        const int baseline=preview.candidateTextRects(0)[0].top();
        for(int i=0;i<preview.candidateRects().size();++i) {
            QCOMPARE(preview.candidateTextRects(i)[0].top(),baseline);
            for(const auto &r:preview.pinyinRects(i))QVERIFY(preview.candidateRects()[i].contains(r));
        }
        preview.grab().save("pinyin-above-preview.png");
        // Tone work uses static dictionaries and doesn't cause a fresh background capture.
        const int captures=preview.captureCount();
        for(int i=0;i<20;++i){preview.state.cursor=i%6;preview.refresh();}
        QCOMPARE(preview.captureCount(),captures);
    }
    void menuCornerTransparencyAfterReopen() {
        LiquidPopup::theme().reducedMotion=true;
        LiquidPopup::setBackdropProvider([](QRect r,qreal d){QImage im(r.size(),QImage::Format_RGB32);im.fill(Qt::white);im.setDevicePixelRatio(d);return im;});
        QMenu menu;menu.addAction(QStringLiteral("RIME液态输入法设置…"));menu.addAction(QStringLiteral("测试菜单"));
        for(int n=0;n<3;++n) {
            menu.popup(QPoint(80,80));QTest::qWait(30);
            QVERIFY(menu.testAttribute(Qt::WA_TranslucentBackground));
            QImage im(menu.size()*menu.devicePixelRatioF(),QImage::Format_ARGB32_Premultiplied);
            im.setDevicePixelRatio(menu.devicePixelRatioF());im.fill(Qt::transparent);menu.render(&im);
            for(QPoint p:{QPoint(0,0),QPoint(im.width()-1,0),QPoint(0,im.height()-1),QPoint(im.width()-1,im.height()-1)})QCOMPARE(im.pixelColor(p).alpha(),0);
            if(n==2)im.save("menu-white-background.png");
            menu.hide();QTest::qWait(20);
        }
        LiquidPopup::setBackdropProvider({});LiquidPopup::theme().reducedMotion=false;
    }
    void menuWaitsForNativeRelease() {
        if(!qEnvironmentVariableIsSet("FCITX_LIQUID_ISOLATED_TEST") || qEnvironmentVariable("DISPLAY")==":0")
            QSKIP("Native pointer test requires isolated Xvfb");
        QMenu menu;menu.addAction("first");TrayMenu controller(menu);
        QCOMPARE(QProcess::execute("xdotool",{"mousedown","3"}),0);
        controller.request(QPoint(100,100));QTest::qWait(260);
        const bool waited=!menu.isVisible() && controller.pending();
        QCOMPARE(QProcess::execute("xdotool",{"mouseup","3"}),0);
        QVERIFY(waited);QTest::qWait(220);QVERIFY(menu.isVisible());
        QTest::qWait(250);QVERIFY(menu.isVisible());menu.close();
    }
    void deferredMenu() {
        QMenu menu;menu.addAction("first");TrayMenu controller(menu);
        controller.request(QPoint(100,100));controller.request(QPoint(200,200));
        QVERIFY(!menu.isVisible());QVERIFY(controller.pending());
        QTest::qWait(220);QVERIFY(menu.isVisible());
        controller.request(QPoint(300,300));QTest::qWait(180);QVERIFY(menu.isVisible());
        menu.close();QTest::qWait(150);QVERIFY(!menu.isVisible());
    }
    void menuSurvivesPropertyUpdates() {
        CandidateWindow window;PanelAgent agent(window);QMenu *menu=nullptr;
        for(auto *w:QApplication::topLevelWidgets()) if(w->objectName()=="liquidTrayMenu") menu=qobject_cast<QMenu *>(w);
        QVERIFY(menu);agent.RegisterProperties({"/Fcitx/im:Chinese:input-keyboard:Input method"});
        menu->popup(QPoint(100,100));QTest::qWait(50);QVERIFY(menu->isVisible());
        QVERIFY(menu->property("liquidPopupSkin").toBool());
        QVERIFY(menu->testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(!menu->mask().isEmpty());
        QVERIFY(!menu->styleSheet().contains("#253140"));
        const auto actions=menu->actions();
        agent.UpdateProperty("/Fcitx/im:Rime:input-keyboard:Input method");QTest::qWait(150);
        QVERIFY(menu->isVisible());QCOMPARE(menu->actions(),actions);
        menu->hide();QTest::qWait(50);
        QVERIFY(menu->styleSheet().isEmpty()); // common skin restores native state
        bool updated=false;for(auto *a:menu->actions()) if(a->text()=="Rime") updated=true;QVERIFY(updated);
    }
    void trayFollowsInputMode() {
        CandidateWindow window;PanelAgent agent(window);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QString("?"));
        const QString chinese="/Fcitx/rime-enzh:双拼:rime-im-double_pinyin:双拼:menu";
        const QString english="/Fcitx/rime-enzh:A:rime-latin:A:menu";
        agent.RegisterProperties({"/Fcitx/im:中州韻:fcitx-rime:中州韻:menu",chinese});
        agent.Enable(true);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("中"));
        agent.UpdateProperty(english);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QString("a"));
        QVERIFY(agent.trayStatus()["trayTooltip"].toString().contains(QStringLiteral("英文小写")));
        agent.UpdateProperty(chinese);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("中"));
        agent.Enable(false);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QString("a"));
        QVERIFY(agent.trayStatus()["trayTooltip"].toString().contains(QStringLiteral("输入法未启用")));
        agent.Enable(true);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("中"));
        // A different engine must not inherit Rime's stale ASCII status.
        agent.UpdateProperty(english);
        agent.UpdateProperty("/Fcitx/im:拼音:missing-pinyin-icon:拼音:menu");
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("拼"));
        // A fresh property registration replaces, rather than appends to, old modes.
        agent.RegisterProperties({"/Fcitx/im:中州韻:fcitx-rime:中州韻:menu",chinese});
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("中"));
        agent.UpdateProperty("malformed");
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("中"));
        agent.UpdateProperty("/Fcitx/rime-enzh:⌛:rime-disable:⌛:menu");
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QString("?"));
        agent.UpdateProperty(chinese);
        QCOMPARE(agent.trayStatus()["trayLabel"].toString(),QStringLiteral("中"));
    }
    void lightMaterialAndContrast() {
        LiquidOpticsRenderer optics;
        QImage white(440,100,QImage::Format_RGB32);white.fill(Qt::white);
        optics.setWallpaper(white);const QImage original=optics.renderPanel(white.rect(),16);
        optics.setLightSurface(1);const QImage pale=optics.renderPanel(white.rect(),16);
        QVERIFY(pale.pixelColor(220,50).lightness()>original.pixelColor(220,50).lightness()+30);
        QVERIFY(pale.pixelColor(220,50).lightness()>200);
        QCOMPARE(pale.pixelColor(0,0).alpha(),0);
        optics.setLightSurface(0);QCOMPARE(optics.renderPanel(white.rect(),16),original);
        QImage dark(440,100,QImage::Format_RGB32);dark.fill(QColor(35,35,35));
        optics.setWallpaper(dark);const auto unchanged=optics.renderPanel(dark.rect(),16);
        optics.setLightSurface(0);QCOMPARE(optics.renderPanel(dark.rect(),16),unchanged);
        CandidateWindow window(nullptr,true);CandidateConfig cfg;window.setConfiguration(cfg);window.showPreview();
        window.setPreviewBackground(1);
        const QRect r=window.candidateRects().first();
        QVERIFY(window.readableColor(cfg.textColor,r).lightness()<80);
        QVERIFY(window.selectionFill(r).alpha()>=100);
        const int captures=window.captureCount(),renders=window.renderCount();
        window.state.cursor=2;window.refresh();
        QCOMPARE(window.captureCount(),captures);QCOMPARE(window.renderCount(),renders);
        window.setPreviewBackground(3);
        QCOMPARE(window.readableColor(cfg.textColor,window.candidateRects().first()),cfg.textColor);
        QCOMPARE(window.selectionFill(window.candidateRects().first()),cfg.selectionColor);
        cfg.autoContrast=false;window.setConfiguration(cfg);window.setPreviewBackground(1);
        QCOMPARE(window.readableColor(cfg.textColor,window.candidateRects().first()),cfg.textColor);
        QCOMPARE(window.selectionFill(window.candidateRects().first()),cfg.selectionColor);
        // Dark custom tint is included in contrast decisions, not the untinted material.
        cfg.autoContrast=true;cfg.tintColor=QColor(0,0,0,245);window.setConfiguration(cfg);window.refresh();
        QCOMPARE(window.readableColor(cfg.textColor,window.candidateRects().first()),cfg.textColor);
        // The same window can choose opposite ink on a split background.
        cfg.tintColor=Qt::transparent;window.setConfiguration(cfg);window.clear();
        window.capture=[](QRect area){QImage im(area.size(),QImage::Format_RGB32);im.fill(Qt::white);QPainter p(&im);p.fillRect(QRect(0,0,area.width()/2,area.height()),QColor(25,25,25));return im;};
        window.state.hasSpot=true;window.state.lookupVisible=true;window.state.spot=QRect(0,0,1,20);
        window.state.labels=QStringList{"1.","2."};window.state.texts=QStringList{"a long sample first candidate","second long sample candidate"};window.refresh();
        QVERIFY(window.readableColor(cfg.textColor,QRect(20,20,30,25)).lightness()>180);
        QVERIFY(window.readableColor(cfg.textColor,QRect(window.width()-65,20,30,25)).lightness()<90);
    }
    void screenEdges() {
        const QRect area(-1920,0,1920,1040);
        for (const QPoint &point:{QPoint(-1920,0),QPoint(-3,1035),QPoint(-1000,500)}) {
            QRect r=CandidateWindow::place(QSize(680,140),QRect(point,QSize(1,25)),area,6);
            QVERIFY(area.contains(r));
            if (point.y()>1000) QVERIFY(r.bottom()<point.y());
        }
    }
    void unicodeCaret() {
        const QString s=QString::fromUtf8("中😀文");
        QCOMPARE(CandidateWindow::utf16Caret(s,2),3);
        QCOMPARE(CandidateWindow::utf16Caret(s,99),s.size());
    }
    void compositionAndCache() {
        CandidateWindow window;
        window.capture=[](QRect r){QImage im(r.size(),QImage::Format_RGB32); im.fill(QColor(75,95,115)); return im;};
        PanelAgent agent(window);
        agent.UpdatePreeditText("ni hao",""); agent.ShowPreedit(true);
        agent.SetLookupTable({"1.","2.","3."},{"你好","拟好","你号"},{},false,true,0,0);
        agent.ShowLookupTable(true); window.refresh();
        QVERIFY(!window.isVisible()); // no stale pointer-position fallback
        agent.SetSpotRect(50,50,1,25); window.refresh();
        QVERIFY(window.isVisible()); QCOMPARE(window.captureCount(),1);
        const int renders=window.renderCount();
        agent.UpdateLookupTableCursor(2); window.refresh();
        QCOMPARE(window.captureCount(),1); QCOMPARE(window.renderCount(),renders);
        agent.SetLookupTable({"1."},{"较长的候选词测试"},{},true,false,0,0); window.refresh();
        QCOMPARE(window.captureCount(),1); // no self-capture during resize
        QSignalSpy chosen(&window,&CandidateWindow::selected);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,window.candidateRects().first().center());
        QCOMPARE(chosen.size(),1); QCOMPARE(chosen.first().first().toInt(),0);
        QSignalSpy up(&window,&CandidateWindow::pageUp), down(&window,&CandidateWindow::pageDown);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,window.previousRect().center());
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,window.nextRect().center());
        QCOMPARE(up.size(),1); QCOMPARE(down.size(),0);
        agent.Enable(false); QVERIFY(!window.isVisible());
        agent.UpdateAux("中文",""); agent.ShowAux(true);
        agent.SetSpotRect(80,80,0,20); window.refresh();
        QVERIFY(window.isVisible()); QCOMPARE(window.captureCount(),2);
    }
    void reopenBeforeRefreshUsesNewBackground_data() {
        QTest::addColumn<bool>("emptyData");
        QTest::newRow("visibility-notifications")<<false;
        QTest::newRow("empty-content-notifications")<<true;
    }
    void reopenBeforeRefreshUsesNewBackground() {
        QFETCH(bool,emptyData);
        CandidateWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen);
        QColor page(185,35,25);
        bool selfCapture=false;
        window.capture=[&](QRect r){
            selfCapture|=window.isVisible();
            QImage image(r.size(),QImage::Format_RGB32); image.fill(page); return image;
        };
        PanelAgent agent(window);
        agent.SetSpotRect(50,50,1,25);
        agent.UpdatePreeditText("a",""); agent.ShowPreedit(true);
        agent.SetLookupTable({"1."},{"第一页面"},{},false,false,0,0);
        agent.ShowLookupTable(true); window.refresh();
        QVERIFY(window.isVisible()); QCOMPARE(window.captureCount(),1);
        const QImage previous=window.grab().toImage();
        // Fcitx closes and reopens in one event-loop turn. Geometry and text
        // can be identical, but the underlying application page has changed.
        if (emptyData) {
            agent.SetLookupTable({},{},{},false,false,0,0);
            agent.UpdatePreeditText("","");
        } else {
            agent.ShowLookupTable(false); agent.ShowPreedit(false);
        }
        QVERIFY(!window.isVisible());
        page=QColor(25,175,45);
        agent.UpdatePreeditText("a","");
        agent.SetLookupTable({"1."},{"第一页面"},{},false,false,0,0);
        agent.ShowPreedit(true); agent.ShowLookupTable(true);
        window.refresh();
        QVERIFY(window.isVisible()); QCOMPARE(window.captureCount(),2);
        const QImage current=window.grab().toImage();
        QVERIFY(current!=previous);
        const int captures=window.captureCount(),renders=window.renderCount();
        agent.UpdatePreeditCaret(1); window.refresh();
        QCOMPARE(window.captureCount(),captures); QCOMPARE(window.renderCount(),renders);
        QVERIFY(!selfCapture);
    }
    void wrappingAndCorners() {
        CandidateWindow window;
        window.capture=[](QRect r){QImage im(r.size(),QImage::Format_RGB32); im.fill(Qt::white); return im;};
        auto &s=window.state; s.spot=QRect(700,500,1,20); s.hasSpot=true; s.lookupVisible=true;
        s.labels=QStringList{"1.","2.","3.","4.","5."};
        s.texts=QStringList{"第一条非常长的候选词","第二条非常长的候选词","第三条非常长的候选词","第四条非常长的候选词","第五条非常长的候选词"};
        window.refresh();
        QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().contains(window.geometry()));
        for (auto r:window.candidateRects()) QVERIFY(window.rect().contains(r));
        QPixmap pixmap(window.size()); pixmap.fill(Qt::transparent); window.render(&pixmap);
        QCOMPARE(pixmap.toImage().pixelColor(0,0).alpha(),0);
        QVERIFY(window.testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(window.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    }
};
QTEST_MAIN(CandidateTest)
#include "candidate_test.moc"
