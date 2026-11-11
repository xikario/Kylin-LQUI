#include "Drawer.h"
#include "SettingsBridge.h"
#include "CodexClient.h"
#include "AcpClient.h"
#include "ChatView.h"
#include "Composer.h"
#include "Markdown.h"
#include "RegionCapture.h"
#include "CaptureEditor.h"
#include <QGraphicsView>
#include <QAction>
#include <QTextCursor>
#include "UiColors.h"
#include <QTextBrowser>
#include <QLineEdit>
#include <QAbstractTextDocumentLayout>
#include "LiquidPopup.h"
#include "StationDrop.h"
#include "ResidentStartup.h"
#include <QCheckBox>
#include <QMimeData>
#include <QTextBlock>
#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include <QClipboard>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QFileInfo>
#include <QScreen>
#include <QPainter>
#include <QProcess>
#include <QScrollArea>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QMessageBox>
#include <QTimer>
#include <QtMath>

QImage drawerWallpaper(const QRect &area,const QRect &screen,qreal dpr);

namespace {
QString quote(QString s){return "'"+s.replace("'","'\\''")+"'";}
QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
bool running(int pid){const auto data=read(QString("/proc/%1/stat").arg(pid));return !data.isEmpty()&&data.mid(data.lastIndexOf(')')+2,1)!="Z";}
bool allStopped(const QJsonArray &pids){for(const auto p:pids)if(running(p.toInt()))return false;return true;}
QList<QJsonObject> requests(const QString &marker,const QString &method={}) {
    QList<QJsonObject> out;
    for(const auto &line:read(marker+".requests").split('\n')){const auto o=QJsonDocument::fromJson(line).object();if(!o.isEmpty()&&(method.isEmpty()||o.value("method").toString()==method))out<<o;}
    return out;
}
QString allText(const Drawer &d){QStringList t;for(const auto &m:d.conversation().messages())t<<m.role+":"+m.text+"|"+m.meta;return t.join('\n');}
QFrame *notice(Drawer &d,const QString &kind={}) {
    for(auto *f:d.findChildren<QFrame*>("notice"))if(kind.isEmpty()?f->property("kind").toString().isEmpty():f->property("kind").toString()==kind)return f;
    return nullptr;
}
QPushButton *buttonText(QWidget *root,const QString &text){for(auto *b:root->findChildren<QPushButton*>())if(b->text()==text)return b;return nullptr;}
}
class CliTest:public QObject {
    Q_OBJECT
    QTemporaryDir root;
    int serial=0;
    Settings config(QString *marker=nullptr){
        Settings s;s.file=root.path()+QString("/case-%1.json").arg(++serial);s.animation="none";
        s.directory=root.path()+"/目录 含$()与'引号";QDir().mkpath(s.directory);
        const QString record=root.path()+QString("/session-%1.json").arg(serial);
        s.command=quote(qEnvironmentVariable("LIQUID_CLI_FIXTURE"))+" "+quote(record);
        if(marker)*marker=record;return s;
    }
    QString folder(){return root.path()+QString("/conversation-%1").arg(serial);}
private slots:
    void initTestCase(){
        qputenv("LQUI_SETTINGS_LEGACY","1");QVERIFY(root.isValid());qputenv("XDG_CONFIG_HOME",(root.path()+"/config").toUtf8());qputenv("XDG_DATA_HOME",(root.path()+"/data").toUtf8());qputenv("XDG_CACHE_HOME",(root.path()+"/cache").toUtf8());
        QDir().mkpath(root.path()+"/config/kyfences");
        QImage image(900,700,QImage::Format_RGB32);QPainter p(&image);QLinearGradient g(0,0,900,700);g.setColorAt(0,QColor("#73906d"));g.setColorAt(.4,QColor("#173f30"));g.setColorAt(1,QColor("#65846f"));p.fillRect(image.rect(),g);p.end();
        QVERIFY(image.save(root.path()+"/wallpaper.png"));QFile f(root.path()+"/config/kyfences/layout.json");QVERIFY(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(QJsonObject{{"wallpaperPath",root.path()+"/wallpaper.png"}}).toJson());
    }
    void wallpaperWithMismatchedSuffixMatchesTheDesktop(){
        const QString path=root.path()+"/下载壁纸.png",layoutPath=root.path()+"/config/kyfences/layout.json";
        QImage fixture(800,500,QImage::Format_RGB32);fixture.fill(QColor("#ca5030"));QVERIFY(fixture.save(path,"JPEG"));
        const auto before=read(layoutPath);QFile layout(layoutPath);QVERIFY(layout.open(QIODevice::WriteOnly|QIODevice::Truncate));
        layout.write(QJsonDocument(QJsonObject{{"wallpaperPath",path}}).toJson());layout.close();
        const auto tile=drawerWallpaper(QRect(100,100,160,90),QRect(0,0,800,500),1.5);
        QVERIFY(layout.open(QIODevice::WriteOnly|QIODevice::Truncate));layout.write(before);layout.close();
        QCOMPARE(tile.size(),QSize(240,135));QVERIFY(qAbs(tile.pixelColor(120,60).red()-202)<6);
    }
    void settingsRoundTripAndMigration(){
        auto s=config();s.width=90000;s.height=-10;s.liquid=900;s.animation="spring";s.permissions="bogus";s.contextLimitK=1;s.normalize();
        QCOMPARE(s.width,1000);QCOMPARE(s.height,420);QCOMPARE(s.liquid,100);QCOMPARE(s.animation,QString("float"));
        QCOMPARE(s.permissions,QString("config"));QCOMPARE(s.contextLimitK,32);
        s.idleStopMinutes=-5;s.normalize();QCOMPARE(s.idleStopMinutes,0);QCOMPARE(Settings().idleStopMinutes,10);
        QVERIFY(s.save());const auto loaded=Settings::load(s.file);QCOMPARE(loaded.json(),s.json());
        QVERIFY(!(QFile::permissions(s.file)&QFile::ReadOther));
        QCOMPARE(Settings::load(root.path()+"/missing.json").contextLimitK,256);
        s.contextLimitK=2000;s.normalize();QCOMPARE(s.contextLimitK,765);
        QCOMPARE(s.defaultReplyMode,QString("normal"));s.defaultReplyMode="quick";QVERIFY(s.save());
        QCOMPARE(Settings::load(s.file).defaultReplyMode,QString("quick"));
        s.defaultReplyMode="invalid";s.normalize();QCOMPARE(s.defaultReplyMode,QString("normal"));
        s.quickModel="fixture-fast";s.quickReasoningEffort="xhigh";s.quickFast=true;s.normalFast=true;s.serviceTierForTurn="priority";QVERIFY(s.save());
        const auto quick=Settings::load(s.file);QCOMPARE(quick.quickModel,s.quickModel);QCOMPARE(quick.quickReasoningEffort,s.quickReasoningEffort);QVERIFY(quick.quickFast);QVERIFY(quick.normalFast);QVERIFY(quick.serviceTierForTurn.isEmpty());
    }
    void backendProfilesAndConversationIsolation(){
        QString marker;auto s=config(&marker);s.model="codex-choice";const QString codexCommand=s.command;
        s.selectBackend("qwen");QCOMPARE(s.command,QString("qwen"));s.model="qwen-choice";s.reasoningEffort="high";
        s.selectBackend("deepseek");QCOMPARE(s.command,QString("dsh"));s.model="deepseek-choice";
        QVERIFY(s.save());s=Settings::load(s.file);s.selectBackend("codex");QCOMPARE(s.command,codexCommand);QCOMPARE(s.model,QString("codex-choice"));
        s.model.clear();Drawer d(s,folder());d.submit("Codex 原对话",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        const QString codexThread=d.conversation().threadId;d.composer()->editor()->setPlainText("Codex 草稿");
        auto qwen=d.configuration();qwen.selectBackend("qwen");qwen.model.clear();qwen.reasoningEffort.clear();qwen.command=quote(qEnvironmentVariable("LIQUID_ACP_FIXTURE"))+" "+quote(root.path()+"/isolated-qwen");
        d.applySettings(qwen);QCOMPARE(d.conversation().messages().size(),0);QVERIFY(d.composer()->text().isEmpty());QVERIFY(!d.client()->running());
        QVERIFY(d.conversation().folder().endsWith("/backends/qwen"));d.submit("千问原对话",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        QVERIFY(allText(d).contains("千问原对话"));QVERIFY(!allText(d).contains("Codex 原对话"));d.composer()->editor()->setPlainText("千问草稿");
        d.applySettings(s);QCOMPARE(d.conversation().threadId,codexThread);QCOMPARE(d.composer()->text(),QString("Codex 草稿"));QVERIFY(allText(d).contains("Codex 原对话"));
        d.applySettings(qwen);QCOMPARE(d.composer()->text(),QString("千问草稿"));QVERIFY(allText(d).contains("千问原对话"));
        d.clearConversation();QVERIFY(d.conversation().messages().isEmpty());d.applySettings(s);QCOMPARE(d.conversation().threadId,codexThread);QVERIFY(allText(d).contains("Codex 原对话"));
    }
    void acpCatalog_data(){QTest::addColumn<QString>("provider");QTest::newRow("qwen")<<QString("qwen");QTest::newRow("deepseek")<<QString("deepseek");}
    void backendSettingsPicker(){
        QString marker;auto s=config(&marker);
        for(const auto &provider:QStringList{"qwen","deepseek"}){s.selectBackend(provider);s.command=quote(qEnvironmentVariable("LIQUID_ACP_FIXTURE"))+" "+quote(marker+"-"+provider);}
        s.selectBackend("codex");Drawer d(s,folder());d.Show();d.Preferences();
        QDialog *dialog=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);QVERIFY(dialog);
        auto *picker=dialog->findChild<QComboBox*>("settingBackend");auto *model=dialog->findChild<QComboBox*>("settingModel");auto *mode=dialog->findChild<QComboBox*>("settingSessionMode");auto *buttons=dialog->findChild<QDialogButtonBox*>();QVERIFY(picker);QVERIFY(model);QVERIFY(mode);QVERIFY(buttons);
        for(const auto &provider:QStringList{"qwen","deepseek"}) {
            picker->setCurrentIndex(picker->findData(provider));QMetaObject::invokeMethod(picker,"activated",Q_ARG(int,picker->currentIndex()));
            QTRY_VERIFY_WITH_TIMEOUT(model->isEnabled()&&model->findData("fixture/deep")>=0,10000);
            QCOMPARE(mode->isHidden(),provider=="deepseek");buttons->button(QDialogButtonBox::Apply)->click();QCOMPARE(d.configuration().backend,provider);QVERIFY(!d.client()->running());
            QCOMPARE(Settings::load(s.file).backend,provider);QVERIFY(requests(marker+"-"+provider,"session/prompt").isEmpty());
        }
        dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void acpCatalog(){
        QFETCH(QString,provider);QString marker;auto s=config(&marker);s.selectBackend(provider);s.command=quote(qEnvironmentVariable("LIQUID_ACP_FIXTURE"))+" "+quote(marker);s.model="fixture/deep";
        AcpClient client;client.configure(s);QSignalSpy ready(&client,&AgentClient::modelsReady),failed(&client,&AgentClient::modelsFailed),modes(&client,&AgentClient::modesReady);
        client.discoverModels();QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),10000);QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first().first().toString()));
        QCOMPARE(ready.first()[0].toJsonArray().size(),2);QCOMPARE(ready.first()[1].toString(),QString("fixture/fast"));
        bool effortFound=false;for(const auto &v:ready.first()[0].toJsonArray())for(const auto &e:v.toObject().value("supportedReasoningEfforts").toArray())if(e.toObject().value("reasoningEffort")=="future-effort")effortFound=true;QVERIFY(effortFound);
        QCOMPARE(modes.size(),1);QCOMPARE(modes.first()[0].toJsonArray().size(),provider=="qwen"?2:0);
        QTRY_VERIFY_WITH_TIMEOUT(!client.running(),5000);QVERIFY(requests(marker,"session/prompt").isEmpty());QCOMPARE(requests(marker,"session/new").size(),1);
        const auto init=requests(marker,"initialize").first();QCOMPARE(init.value("jsonrpc").toString(),QString("2.0"));QVERIFY(init.value("params").toObject().value("clientCapabilities").toObject().isEmpty());
        const auto args=QJsonDocument::fromJson(read(marker)).object().value("arguments").toArray();QCOMPARE(args,QJsonArray::fromStringList(provider=="qwen"?QStringList{"--acp"}:QStringList{"--profile","acp"}));
    }
    void acpTurnsPermissionsAndResume_data(){acpCatalog_data();}
    void acpTurnsPermissionsAndResume(){
        QFETCH(QString,provider);QString marker;auto s=config(&marker);s.selectBackend(provider);s.command=quote(qEnvironmentVariable("LIQUID_ACP_FIXTURE"))+" "+quote(marker);s.model="fixture/deep";s.reasoningEffort="future-effort";if(provider=="qwen")s.sessionMode="edit";
        AcpClient client;client.configure(s);QSignalSpy finished(&client,&AgentClient::turnFinished),answers(&client,&AgentClient::answer),approval(&client,&AgentClient::approvalRequested),usage(&client,&AgentClient::contextUsage);
        QImage image(12,12,QImage::Format_RGB32);image.fill(Qt::red);const QString path=root.path()+"/acp-image.png";QVERIFY(image.save(path));
        client.send("APPROVE",{path});QTRY_COMPARE_WITH_TIMEOUT(approval.size(),1,10000);const auto id=approval.first()[0].toJsonValue();const auto choices=client.approvalOptions(id);QCOMPARE(choices.size(),2);
        client.answerApproval(id,"yes-once");QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,10000);QCOMPARE(finished.first()[0].toString(),QString("completed"));
        QVERIFY(answers.last()[0].toString().contains("图片 1 张"));QVERIFY(!answers.last()[0].toString().contains("SECRET_THOUGHT"));QVERIFY(finished.first()[2].toString().contains("运行 1 条命令"));QCOMPARE(usage.first()[0].toLongLong(),qint64(1200));
        const auto set=requests(marker,"session/set_config_option");QVERIFY(set.size()>=2);QCOMPARE(set[0].value("params").toObject().value("value").toString(),QString("fixture/deep"));QCOMPARE(set[1].value("params").toObject().value("value").toString(),QString("future-effort"));
        const auto prompt=requests(marker,"session/prompt").first().value("params").toObject().value("prompt").toArray();QCOMPARE(prompt[1].toObject().value("data").toString(),QString::fromLatin1(read(path).toBase64()));
        QCOMPARE(client.supportsCompaction(),provider=="qwen");const QString session=client.threadId();client.stop();
        AcpClient resumed;resumed.configure(s);resumed.setThreadId(session);QSignalSpy again(&resumed,&AgentClient::answer),done(&resumed,&AgentClient::turnFinished);
        resumed.send("续聊",{});QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,10000);QCOMPARE(resumed.threadId(),session);QVERIFY(!again.last()[0].toString().contains("REPLAY"));
        QCOMPARE(requests(marker,provider=="qwen"?"session/load":"session/resume").size(),1);
        QSignalSpy permission(&resumed,&AgentClient::approvalRequested);resumed.send("APPROVE SLOW",{});QTRY_COMPARE_WITH_TIMEOUT(permission.size(),1,10000);resumed.interrupt();QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,10000);QCOMPARE(done.last()[0].toString(),QString("interrupted"));
        bool cancelled=false;for(const auto &r:requests(marker))if(r.value("id")=="opaque-permission"&&r.value("result").toObject().value("outcome").toObject().value("outcome")=="cancelled")cancelled=true;QVERIFY(cancelled);
    }
    void markdownIsEscapedAndStructured(){
        const QString html=Markdown::toHtml("# 标题\n<script>x</script> **粗** `a<b`\n\n- 一\n- 二\n\n```\nint a<3;\n```\n\n| A | B |\n|---|---|\n| 1 | 2 |\n[链接](https://example.com)");
        QVERIFY(!html.contains("<script>"));QVERIFY(html.contains("&lt;script&gt;"));QVERIFY(html.contains("<b>粗</b>"));
        QVERIFY(html.contains("a&lt;b"));QVERIFY(html.contains("<ul"));QVERIFY(html.contains("<pre"));QVERIFY(html.contains("int a&lt;3;"));
        QVERIFY(html.contains("<table"));QVERIFY(html.contains("<td>2</td>"));QVERIFY(html.contains("href=\"https://example.com\""));
        QVERIFY(!Markdown::toHtml("[x](javascript:alert(1))").contains("href"));
        QVERIFY(Markdown::toHtml("`#9fe9c6`",true).contains("#9fe9c6"));
    }
    void lazyNoProcessAndGlassCache(){
        QString marker;Drawer d(config(&marker),folder());QVERIFY(!d.isVisible());QVERIFY(d.edgeWidget()->isVisible());QVERIFY(!d.client()->running());
        QVERIFY(d.windowType()!=Qt::Popup);QVERIFY(d.testAttribute(Qt::WA_ShowWithoutActivating));
        d.Show();QVERIFY(d.isVisible());QCOMPARE(d.geometry(),d.targetGeometry());QVERIFY(!d.client()->running());QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(d.findChild<QLabel*>("emptyState")->isVisible());
        QVERIFY(d.edgeWidget()->width()<=40);QCOMPARE(d.edgeWidget()->width(),d.edgeWidget()->height());
        const int builds=d.glassBuilds();QVERIFY(builds>=1);
        for(int i=0;i<10;i++){d.Hide();d.Show();QCoreApplication::processEvents();}
        QCOMPARE(d.glassBuilds(),builds);QVERIFY(!QWidget::mouseGrabber());
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty()){QDir().mkpath(artifacts);d.grab().save(artifacts+"/empty.png");}
    }
    void floatMotionReversesAndFades(){
        auto s=config();s.animation="float";s.animationMs=480;Drawer d(s,folder());d.Show();
        QTest::qWait(90);QVERIFY(d.progress()>0&&d.progress()<1);QVERIFY(d.windowOpacity()<1);
        QCOMPARE(d.x(),d.targetGeometry().x());QVERIFY(d.y()>d.targetGeometry().y());
        d.Hide();QTest::qWait(20);d.Show();
        QTRY_COMPARE_WITH_TIMEOUT(d.pos(),d.targetGeometry().topLeft(),1000);QTRY_COMPARE_WITH_TIMEOUT(d.progress(),1.,1000);QVERIFY(d.windowOpacity()>.99);
        d.Hide();QTRY_VERIFY_WITH_TIMEOUT(!d.isVisible(),1000);QVERIFY(d.windowOpacity()>.99);
        s.animation="slide";d.applySettings(s);d.Show();QTRY_COMPARE_WITH_TIMEOUT(d.pos(),d.targetGeometry().topLeft(),1000);
        s.width=680;s.height=850;s.liquid=35;const int builds=d.glassBuilds();d.applySettings(s);QCOMPARE(d.geometry(),d.targetGeometry());QVERIFY(d.glassBuilds()>builds);
    }
    void taskbarMotionKeepsGeometryAndCachedSurfaceWhileFading(){
        auto s=config();s.animation="float";Drawer d(s,folder());const QRect screen=QGuiApplication::primaryScreen()->availableGeometry();
        d.ToggleAt(screen.right()-45,screen.bottom()-35,32,32);const QRect target=d.targetGeometry();const int builds=d.glassBuilds();
        QTest::qWait(65);QCOMPARE(d.geometry(),target);QVERIFY(d.mask().isEmpty());QVERIFY(d.progress()>0);
        d.Hide();QTest::qWait(25);d.Show();QTRY_COMPARE_WITH_TIMEOUT(d.progress(),1.,1000);QCOMPARE(d.geometry(),target);QVERIFY(d.mask().isEmpty());QCOMPARE(d.glassBuilds(),builds);
        d.Hide();QTRY_VERIFY_WITH_TIMEOUT(!d.isVisible(),1000);QCOMPARE(d.glassBuilds(),builds);
    }
    void hiddenEdgeDoesNotPulseDuringBackgroundReply(){
        Drawer d(config(),folder());auto *pulse=d.edgeWidget()->findChild<QTimer*>("edgeBusyPulse");QVERIFY(pulse);
        d.submit("pulse fixture",{});QTRY_VERIFY(d.client()->busy());QVERIFY(pulse->isActive());
        d.edgeWidget()->hide();QVERIFY(!pulse->isActive());d.edgeWidget()->show();QVERIFY(pulse->isActive());d.client()->stop();
    }
    void hoverDoesNotOpenClickStays(){
        auto s=config();s.hideDelay=200;Drawer d(s,folder());QWidget workspace;workspace.show();QApplication::setActiveWindow(&workspace);
        QEvent enter(QEvent::Enter);QApplication::sendEvent(d.edgeWidget(),&enter);
        QTest::qWait(350);QVERIFY(!d.expanded());QCOMPARE(QApplication::activeWindow(),&workspace);
        QCursor::setPos(QGuiApplication::primaryScreen()->geometry().topLeft()+QPoint(1,1));
        QEvent leave(QEvent::Leave);QTest::mouseClick(d.edgeWidget(),Qt::LeftButton);QVERIFY(d.expanded());
        QTest::mouseClick(d.composer()->editor()->viewport(),Qt::LeftButton);
        QApplication::sendEvent(&d,&leave);QTest::qWait(320);QVERIFY(d.expanded());
        QTest::keyClick(d.composer()->editor(),Qt::Key_Escape);QVERIFY(!d.expanded());
        auto *pin=d.findChild<QPushButton*>("pinDrawer");d.Show();pin->setChecked(true);QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&d,&deactivate);QTest::qWait(320);QVERIFY(d.expanded());
    }
    void hiddenUnifiedSettingsDoNotBlockAutoHide(){
        auto s=config();s.hideDelay=200;Drawer d(s,folder());QWidget workspace;workspace.show();
        {LquiSettings::FormScope scope;QVERIFY(d.settingsBridgeRoot());}
        QVERIFY(!QJsonDocument::fromJson(d.State().toUtf8()).object().value("settingsVisible").toBool());d.Show();
        QCursor::setPos(QGuiApplication::primaryScreen()->geometry().topLeft()+QPoint(1,1));
        QApplication::setActiveWindow(&workspace);QEvent deactivate(QEvent::WindowDeactivate);QApplication::sendEvent(&d,&deactivate);
        QTRY_VERIFY_WITH_TIMEOUT(!d.expanded(),1200);
        d.Show();auto *pin=d.findChild<QPushButton*>("pinDrawer");pin->setChecked(true);
        QApplication::setActiveWindow(&workspace);QApplication::sendEvent(&d,&deactivate);QTest::qWait(300);QVERIFY(d.expanded());
        pin->setChecked(false);QTRY_VERIFY_WITH_TIMEOUT(!d.expanded(),1200);
    }
    void panelAnchorStaysOnScreen(){
        auto s=config();Drawer d(s,folder());const QRect screen=QGuiApplication::primaryScreen()->availableGeometry();
        d.ToggleAt(screen.right()-45,screen.bottom()-35,32,32);
        QVERIFY(d.expanded());QVERIFY(screen.contains(d.geometry()));QVERIFY(d.geometry().bottom()<screen.bottom()-35);
        QCOMPARE(d.windowTitle(),QString("AI小站"));QCOMPARE(d.findChild<QLabel*>("drawerTitle")->text(),QString("AI小站"));
        d.ToggleAt(screen.right()-45,screen.bottom()-35,32,32);QVERIFY(!d.expanded());
        d.ToggleAt(screen.left()+5,screen.bottom()-35,32,32);QVERIFY(screen.contains(d.geometry()));
        QVERIFY(!d.client()->running());
    }
    void chatStreamsAnswerHidesCommentaryAndPersists(){
        QString marker;auto s=config(&marker);const QString dir=folder();
        {
            Drawer d(s,dir);d.Show();auto *editor=d.composer()->editor();
            editor->insertPlainText("第一行");QTest::keyClick(editor,Qt::Key_Return,Qt::ShiftModifier);editor->insertPlainText("第二行");
            QVERIFY(!QFileInfo::exists(marker));QCOMPARE(editor->toPlainText(),QString("第一行\n第二行"));
            QTest::keyClick(editor,Qt::Key_Return);QVERIFY(editor->toPlainText().isEmpty());QVERIFY(d.chat()->typing());
            QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QVERIFY(!d.chat()->typing());
            const QString text=allText(d);QVERIFY2(text.contains("assistant:收到：第一行\n第二行 · 图片 0 张"),qPrintable(text));
            QVERIFY(!text.contains("我先看看目录"));QVERIFY(text.contains("运行 1 条命令"));
            const auto starts=requests(marker,"turn/start");QCOMPARE(starts.size(),1);
            const auto input=starts.first().value("params").toObject().value("input").toArray();QCOMPARE(input.first().toObject().value("text").toString(),QString("第一行\n第二行"));
            QCOMPARE(requests(marker,"thread/start").first().value("params").toObject().value("cwd").toString(),s.directory);
            QVERIFY(!requests(marker,"thread/start").first().value("params").toObject().contains("approvalPolicy"));
            const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty())d.grab().save(artifacts+"/chat.png");
        }
        const auto saved=QJsonDocument::fromJson(read(dir+"/conversation.json")).object();
        QVERIFY(saved.value("threadId").toString().startsWith("fixture-thread-"));QCOMPARE(saved.value("messages").toArray().size(),2);
        QVERIFY(!(QFile::permissions(dir+"/conversation.json")&QFile::ReadOther));
        QString second;auto next=config(&second);Drawer again(next,dir);QCOMPARE(again.chat()->rowCount(),2);
        again.submit("继续",{});QTRY_VERIFY_WITH_TIMEOUT(!again.client()->busy(),10000);
        const auto resume=requests(second,"thread/resume");QCOMPARE(resume.size(),1);
        QCOMPARE(resume.first().value("params").toObject().value("threadId").toString(),saved.value("threadId").toString());
        QVERIFY(requests(second,"thread/start").isEmpty());QCOMPARE(again.conversation().messages().size(),4);
    }
    void idleCodexStopsAndResumesSameThread(){
        QString marker;Drawer d(config(&marker),folder());d.Show();d.client()->setIdleTimeout(400);
        d.submit("第一句",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        const auto pids=QJsonDocument::fromJson(read(marker)).object().value("pids").toArray();QCOMPARE(pids.size(),3);
        QVERIFY(d.client()->running());QTRY_VERIFY_WITH_TIMEOUT(!d.client()->running(),3000);QTRY_VERIFY_WITH_TIMEOUT(allStopped(pids),10000);
        QVERIFY(d.findChild<QLabel*>("cliStatus")->text().contains("休眠"));
        const QString thread=d.conversation().threadId;QVERIFY(!thread.isEmpty());
        d.submit("第二句",{});QTRY_VERIFY_WITH_TIMEOUT(allText(d).contains("收到：第二句"),10000);QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),5000);
        const auto resume=requests(marker,"thread/resume");QCOMPARE(resume.size(),1);
        QCOMPARE(resume.first().value("params").toObject().value("threadId").toString(),thread);QCOMPARE(d.conversation().threadId,thread);
        d.client()->setIdleTimeout(0);QTest::qWait(600);QVERIFY(d.client()->running());
    }
    void screenshotCheckCopiesWithoutAttaching(){
        QString marker;Drawer d(config(&marker),folder());
        QImage shot(93,47,QImage::Format_RGB32);shot.fill(QColor("#438b68"));
        d.acceptCapture(shot);
        auto *editor=d.findChild<CaptureEditor*>();QVERIFY(editor);
        editor->findChild<QPushButton*>("captureCopy")->click();
        QTRY_COMPARE(QApplication::clipboard()->image().size(),shot.size());
        QCOMPARE(QApplication::clipboard()->image().pixelColor(20,20),shot.pixelColor(20,20));
        QVERIFY(d.composer()->images().isEmpty());QVERIFY(!d.expanded());QVERIFY(!QFileInfo::exists(marker));
    }
    void screenshotIsAttachedNotSent(){
        QString marker;Drawer d(config(&marker),folder());d.Show();
        QImage shot(93,47,QImage::Format_RGB32);shot.fill(QColor("#438b68"));d.acceptCapture(shot);
        QVERIFY(d.composer()->images().isEmpty());auto *capture=d.findChild<CaptureEditor*>();QVERIFY(capture);capture->findChild<QPushButton*>("captureInsert")->click();QTRY_COMPARE(d.composer()->images().size(),1);
        QCOMPARE(d.composer()->images().size(),1);const QString path=d.composer()->images().first();
        QVERIFY(path.startsWith(folder()));QCOMPARE(QImage(path).size(),shot.size());QVERIFY(!QFileInfo::exists(marker));QVERIFY(!(QFile::permissions(path)&QFile::ReadOther));
        d.composer()->editor()->insertPlainText("看图");QTest::keyClick(d.composer()->editor(),Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QVERIFY(d.composer()->images().isEmpty());
        const auto input=requests(marker,"turn/start").first().value("params").toObject().value("input").toArray();
        QCOMPARE(input.size(),2);QCOMPARE(input.at(1).toObject().value("type").toString(),QString("localImage"));QCOMPARE(input.at(1).toObject().value("path").toString(),path);
        QVERIFY(allText(d).contains("图片 1 张"));QCOMPARE(d.conversation().messages().first().images,QStringList{path});
    }
    void captureToolsKeepOriginalAndUndo(){
        QImage source(1200,800,QImage::Format_RGB32);source.fill(Qt::white);
        CaptureEditor editor(source);editor.show();QTest::qWait(80);
        auto *view=editor.findChild<QGraphicsView*>("captureCanvas");QVERIFY(view);
        editor.findChild<QLineEdit*>("captureText")->setText("测试文字");
        int count=0;
        for(const QString &tool:QStringList{"arrow","ellipse","rectangle","text","line","freehand"}) {
            editor.findChild<QPushButton*>("captureTool_"+tool)->click();
            auto a=view->mapFromScene(QPointF(100,100+count*80)),b=view->mapFromScene(QPointF(500,140+count*80));
            QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,a);
            QTest::mouseMove(view->viewport(),b);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,b);
            QCOMPARE(editor.annotationCount(),++count);
        }
        const QImage output=editor.renderedImage();QCOMPARE(output.size(),source.size());QVERIFY(output!=source);
        editor.findChild<QPushButton*>("captureUndo")->click();QCOMPARE(editor.annotationCount(),5);
        editor.findChild<QPushButton*>("captureRedo")->click();QCOMPARE(editor.annotationCount(),6);QCOMPARE(editor.renderedImage(),output);
        for(int i=0;i<6;++i)editor.findChild<QPushButton*>("captureUndo")->click();
        QCOMPARE(editor.renderedImage(),source.convertToFormat(QImage::Format_ARGB32_Premultiplied));
        editor.setProcessingAvailable(false);QVERIFY(!editor.findChild<QPushButton*>("captureAction_ocr")->isEnabled());
    }
    void captureActionsSendOnlyScreenshotAndPreserveDraft(){
        for(const QString &action:QStringList{"translate","ocr","summary"}) {
            QString marker;Drawer d(config(&marker),folder());d.Show();
            QImage shot(90,60,QImage::Format_RGB32);shot.fill(Qt::cyan);d.processCapture(shot,"insert");
            const auto draftImages=d.composer()->images();d.composer()->editor()->setPlainText("保留这段未发送的草稿");
            d.acceptCapture(shot);auto *capture=d.findChild<CaptureEditor*>();QVERIFY(capture);
            capture->findChild<QPushButton*>("captureAction_"+action)->click();
            QTRY_VERIFY_WITH_TIMEOUT(!requests(marker,"turn/start").isEmpty(),10000);
            QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
            QCOMPARE(d.composer()->text(),QString("保留这段未发送的草稿"));QCOMPARE(d.composer()->images(),draftImages);
            const auto input=requests(marker,"turn/start").first()["params"].toObject()["input"].toArray();
            QCOMPARE(input.size(),2);QVERIFY(!input.first().toObject()["text"].toString().contains("草稿"));
            QCOMPARE(input.first().toObject()["text"].toString(),CaptureEditor::actionPrompt(action,"简体中文"));
        }
    }
    void chatTextColorSurvivesOemPaletteAndThemeChanges(){
        ChatView chat;chat.resize(480,600);chat.setStyleSheet("QTextEdit{color:#202020;}");chat.show();
        ChatMessage user;user.role="user";user.text="用户文字 abc";ChatMessage reply;reply.role="assistant";reply.text="正文 **加粗**\n\n|标题|金额|\n|---|---|\n|测试|123元|";
        chat.append(user);chat.append(reply);
        for(bool light:{false,true,false}) {
            chat.setColors(light);QTest::qWait(50);const auto expected=QColor(UiColors::forSurface(light).text);
            for(auto *text:chat.findChildren<QTextBrowser*>("messageText")) {
                QCOMPARE(text->palette().color(QPalette::Text),expected);
                QTextCursor cursor(text->document());cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
                QCOMPARE(cursor.charFormat().foreground().color(),expected);
                text->clearFocus();QTest::qWait(10);QCOMPARE(text->palette().color(QPalette::Inactive,QPalette::Text),expected);
            }
        }
        reply.text+="\n新增的流式内容";chat.updateLast(reply);QTest::qWait(80);
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty())chat.grab().save(artifacts+"/richtext-contrast.png");
    }
    void captureToolbarStaysSmallAndAnchored(){
        const QRect screen=QGuiApplication::primaryScreen()->availableGeometry();
        const QRect selected(screen.center()-QPoint(120,60),QSize(240,120));QImage source(480,240,QImage::Format_RGB32);source.fill(Qt::white);
        CaptureEditor editor(source,nullptr,selected);editor.show();QTest::qWait(80);
        QVERIFY(editor.windowFlags()&Qt::FramelessWindowHint);QVERIFY(screen.contains(editor.geometry()));
        auto *canvas=editor.findChild<QGraphicsView*>("captureCanvas");QVERIFY(canvas);QVERIFY(canvas->width()<=selected.width()+2);QVERIFY(canvas->height()<=selected.height()+2);
        QVERIFY(editor.width()<600);QVERIFY(editor.height()<220);
        QVERIFY((canvas->mapToGlobal(canvas->rect().center())-selected.center()).manhattanLength()<8);
        editor.findChild<QPushButton*>("captureTool_text")->click();QTest::qWait(50);QVERIFY(screen.contains(editor.geometry()));
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty())editor.grab().save(artifacts+"/compact-capture.png");
    }
    void wideTablesAndLongIdentifiersStayInsideChat(){
        ChatView chat;chat.resize(340,480);chat.show();chat.setColors(false);
        ChatMessage message;message.role="assistant";
        message.text="| 厂商 | 产品规格 | 文档编号 | 价格 |\n| --- | --- | --- | --- |\n| 企业 | 嵌入式服务器 | doc-"+QString(180,'a')+" | ¥5500 |\n\n末尾可见内容";
        chat.append(message);
        for(int point:{8,11,20})for(int width:{300,480,900}) {
            chat.setTextPoint(point);chat.resize(width,480);QTest::qWait(100);
            QVERIFY2(chat.widget()->width()<=chat.viewport()->width(),"rich text must not widen the chat content");
            auto *text=chat.findChild<QTextBrowser*>("messageText");QVERIFY(text);QVERIFY(text->toPlainText().contains("末尾可见内容"));
            const auto position=text->mapTo(chat.viewport(),QPoint(text->width(),0));QVERIFY(position.x()<=chat.viewport()->width());
            QVERIFY(text->height()>=qCeil(text->document()->documentLayout()->documentSize().height()));
        }
        message.text+="\n\n"+QString(500,'z');chat.updateLast(message);QTest::qWait(120);QVERIFY(chat.widget()->width()<=chat.viewport()->width());
    }
    void contextPromptAndCompaction(){
        QString marker;Drawer d(config(&marker),folder());d.Show();d.submit("BIG",{});
        QTRY_VERIFY_WITH_TIMEOUT(notice(d,"compact"),10000);QCOMPARE(d.conversation().contextTokens,qint64(270000));
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty()){QTest::qWait(80);d.grab().save(artifacts+"/compact.png");}
        buttonText(notice(d,"compact"),"立即压缩")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!requests(marker,"thread/compact/start").isEmpty(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(allText(d).contains("已压缩上下文"),5000);QTRY_VERIFY(!notice(d,"compact"));QCOMPARE(d.conversation().contextTokens,qint64(0));
        d.submit("BIG",{});QTRY_VERIFY_WITH_TIMEOUT(notice(d,"compact"),10000);buttonText(notice(d,"compact"),"稍后")->click();
        QTRY_VERIFY(!notice(d,"compact"));QCOMPARE(requests(marker,"thread/compact/start").size(),1);
    }
    void approvalCardAnswersCodex(){
        auto s=config();QString marker;s=config(&marker);s.permissions="ask";Drawer d(s,folder());d.Show();d.submit("APPROVE",{});
        QTRY_VERIFY_WITH_TIMEOUT(notice(d),10000);QVERIFY(buttonText(notice(d),"允许"));buttonText(notice(d),"允许")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QVERIFY(allText(d).contains("审批结果：accept"));QTRY_VERIFY(!notice(d));
        const auto start=requests(marker,"thread/start").first().value("params").toObject();
        QCOMPARE(start.value("approvalPolicy").toString(),QString("on-request"));QCOMPARE(start.value("sandbox").toString(),QString("workspace-write"));
    }
    void stopInterruptsTurn(){
        QString marker;Drawer d(config(&marker),folder());d.Show();d.submit("SLOW",{});
        QTRY_VERIFY_WITH_TIMEOUT(allText(d).contains("收到：SLOW"),10000);
        d.findChild<QPushButton*>("sendButton")->click();
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),5000);QVERIFY(!requests(marker,"turn/interrupt").isEmpty());QVERIFY(allText(d).contains("已停止"));
        QVERIFY(!d.chat()->typing());
    }
    void stopBeforeTurnIdArrives(){
        QString marker;auto s=config(&marker);s.directory=folder()+"/stall-config";QDir().mkpath(s.directory);
        Drawer d(s,folder());d.Show();d.submit("waiting for configuration",{});
        QTRY_VERIFY_WITH_TIMEOUT(!requests(marker,"config/read").isEmpty(),10000);
        QVERIFY(d.client()->busy());d.client()->interrupt();
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),3000);
        QVERIFY(requests(marker,"turn/start").isEmpty());
        QTest::qWait(200);QVERIFY(!d.client()->busy());
    }
    void failedResumePreservesThread(){
        const QString dir=folder();QDir().mkpath(dir);QFile f(dir+"/conversation.json");QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{"threadId","gone"},{"messages",QJsonArray{QJsonObject{{"role","user"},{"text","旧消息"}}}}}).toJson());f.close();
        QString marker;Drawer d(config(&marker),dir);QCOMPARE(d.chat()->rowCount(),1);d.submit("新消息",{});
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        QCOMPARE(d.conversation().threadId,QString("gone"));QVERIFY(allText(d).contains("user:旧消息"));
        QVERIFY(requests(marker,"thread/start").isEmpty());QVERIFY(requests(marker,"turn/start").isEmpty());
    }
    void busyPermissionChangeIsRejectedBeforeSaving(){
        QString marker;auto s=config(&marker);s.permissions="full";Drawer d(s,folder());d.submit("SLOW",{});
        QTRY_VERIFY_WITH_TIMEOUT(!requests(marker,"turn/start").isEmpty(),10000);
        auto next=s;next.permissions="ask";d.applySettings(next);QCOMPARE(d.configuration().permissions,QString("full"));
        d.Preferences();auto *dialog=d.findChild<QDialog*>("cliPreferences");
        if(!dialog)for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);
        QVERIFY(dialog);auto *permissions=dialog->findChild<QComboBox*>("settingPermissions");QVERIFY(permissions);
        permissions->setCurrentIndex(permissions->findData("ask"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
        QCOMPARE(d.configuration().permissions,QString("full"));QVERIFY(!QFileInfo::exists(s.file));
        d.client()->interrupt();QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        d.applySettings(next);d.submit("second",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        const auto resumes=requests(marker,"thread/resume");QVERIFY(!resumes.isEmpty());
        QCOMPARE(resumes.last()["params"].toObject()["approvalPolicy"].toString(),QString("on-request"));
    }
    void oldApprovalCannotAuthorizeReusedRequestId(){
        QString marker;CodexClient client;client.configure(config(&marker));
        QSignalSpy pending(&client,&AgentClient::approvalRequested),closed(&client,&AgentClient::approvalClosed);
        client.send("APPROVE",{});QTRY_COMPARE_WITH_TIMEOUT(pending.size(),1,10000);
        const auto old=pending[0][0].toJsonValue();client.stop();QCOMPARE(closed.size(),1);
        client.send("APPROVE",{});QTRY_COMPARE_WITH_TIMEOUT(pending.size(),2,10000);
        QVERIFY(old!=pending[1][0].toJsonValue());client.answerApproval(old,"accept");QTest::qWait(100);
        int answers=0;for(const auto &r:requests(marker))if(r.value("id")=="approval-1"&&r.contains("result"))++answers;
        QCOMPARE(answers,0);client.answerApproval(pending[1][0].toJsonValue(),"decline");
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(),10000);
    }
    void bridgeDiscardRebuildsSavedPreferences(){
        auto settings=config();Drawer drawer(settings,folder());
        LquiSettings::Bridge bridge("ai",[&](const QString &){return drawer.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
        const auto first=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QString id,widthId;
        for(const auto &field:first["fields"].toArray()){
            const auto item=field.toObject();
            if(item["name"]=="settingDirectory")id=item["id"].toString();
            if(item["name"]=="settingWidth")widthId=item["id"].toString();
        }
        QVERIFY(!id.isEmpty());bridge.Set("general",id,QString::fromUtf8(QJsonDocument(QJsonObject{{"value","unsaved-path"}}).toJson()));
        QVERIFY(!widthId.isEmpty());const int liveWidth=settings.width+20;
        bridge.Set("general",widthId,QString::fromUtf8(QJsonDocument(QJsonObject{{"value",liveWidth}}).toJson()));
        QCOMPARE(drawer.configuration().width,liveWidth);QCOMPARE(Settings::load(settings.file).width,liveWidth);
        bridge.Discard("general");const auto second=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();
        bool found=false,widthFound=false;for(const auto &field:second["fields"].toArray()){
            const auto item=field.toObject();
            if(item["name"]=="settingDirectory"){QCOMPARE(item["value"].toString(),settings.directory);found=true;}
            if(item["name"]=="settingWidth"){QCOMPARE(item["value"].toInt(),liveWidth);widthFound=true;}
        }
        QVERIFY(found);QVERIFY(widthFound);
    }
    void dropBudgetUsesSerializedUtf8Bytes(){
        QMimeData mime;mime.setText(QString(1500000,QChar(0x4e2d)));QString error;
        QVERIFY(StationDrop::prepare(&mime,&error).isEmpty());QVERIFY(!error.isEmpty());
        mime.setText(QString(1000000,QChar(0x4e2d)));const auto path=StationDrop::prepare(&mime,&error);QVERIFY(!path.isEmpty());
        QCOMPARE(StationDrop::read(path)["text"].toString(),mime.text());StationDrop::discard(path);
    }
    void corruptConversationIsNeverOverwritten(){
        auto s=config();const QString dir=folder();QDir().mkpath(dir);const QString path=dir+"/conversation.json";
        const QByteArray original="{recoverable but incomplete";QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(original);f.close();
        {Drawer d(s,dir);d.submit("new text",{});QVERIFY(!d.client()->busy());}
        QCOMPARE(read(path),original);
        Conversation c(dir);QVERIFY(!c.load());QVERIFY(!c.save());QCOMPARE(read(path),original);
        QVERIFY(c.clear());QVERIFY(c.save());QVERIFY(!QDir(dir).entryList({"*.recovery-*"}).isEmpty());
    }
    void closingCaptureCancelsExactlyOnce(){
        int callbacks=0;auto windows=RegionCapture::begin([&](QImage image){++callbacks;QVERIFY(image.isNull());});
        QVERIFY(!windows.isEmpty());windows.first()->close();QTRY_COMPARE(callbacks,1);QTest::qWait(30);QCOMPARE(callbacks,1);
    }
    void acpApprovalPreservesLongParameters(){
        QString marker;auto s=config(&marker);s.selectBackend("qwen");s.command=quote(qEnvironmentVariable("LIQUID_ACP_FIXTURE"))+" "+quote(marker);
        AcpClient c;c.configure(s);QSignalSpy pending(&c,&AgentClient::approvalRequested);c.send("APPROVE",{});
        QTRY_COMPARE_WITH_TIMEOUT(pending.size(),1,10000);QVERIFY(pending[0][2].toString().contains("REVIEW_SUFFIX"));
        QVERIFY(pending[0][2].toString().size()>800);c.stop();
    }
    void clearStopsProcessesAndRemovesHistory(){
        QString marker;Drawer d(config(&marker),folder());d.Show();
        QImage shot(20,20,QImage::Format_RGB32);shot.fill(Qt::cyan);d.processCapture(shot,"insert");d.submit("你好",d.composer()->images());
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        const auto pids=QJsonDocument::fromJson(read(marker)).object().value("pids").toArray();QCOMPARE(pids.size(),3);
        QCOMPARE(QJsonDocument::fromJson(read(marker)).object().value("cwd").toString(),d.configuration().directory);
        d.clearConversation();QTRY_VERIFY_WITH_TIMEOUT(allStopped(pids),10000);
        QCOMPARE(d.chat()->rowCount(),0);QVERIFY(d.conversation().threadId.isEmpty());QVERIFY(!QDir(folder()+"/images").exists());
    }
    void settingsWindowApply(){
        auto s=config();Drawer d(s,folder());d.Show();d.Preferences();QDialog *dialog=nullptr;
        for(auto *w:QApplication::topLevelWidgets())if(w->windowTitle()=="AI小站设置")dialog=qobject_cast<QDialog*>(w);
        QVERIFY(dialog);QVERIFY(!dialog->windowIcon().isNull());QVERIFY(!(dialog->windowFlags()&Qt::WindowMaximizeButtonHint));
        QVERIFY(!dialog->parentWidget());
        auto *sharedMaterial=dialog->findChild<QCheckBox*>("settingFollowLauncherMaterial");QVERIFY(sharedMaterial);QVERIFY(sharedMaterial->isChecked());sharedMaterial->setChecked(false);
        QProcess identity;identity.start("xprop",{"-id",QString::number(dialog->winId()),"WM_CLASS","_KDE_NET_WM_DESKTOP_FILE","WM_WINDOW_ROLE"});QVERIFY(identity.waitForFinished(2000));
        const auto native=identity.readAllStandardOutput();QVERIFY(native.contains("_KDE_NET_WM_DESKTOP_FILE(UTF8_STRING) = \"liquid-cli\""));QVERIFY(native.contains("ai-station-settings"));
        auto *scroll=dialog->findChild<QScrollArea*>("cliSettingsScroll");QVERIFY(scroll);QVERIFY(!scroll->viewport()->autoFillBackground());QVERIFY(!scroll->widget()->autoFillBackground());
        QCOMPARE(dialog->findChild<QSlider*>("settingContextLimit")->maximum(),765);
        dialog->findChild<QSlider*>("settingWidth")->setValue(620);dialog->findChild<QSlider*>("settingContextLimit")->setValue(128);dialog->findChild<QSlider*>("settingIdleStop")->setValue(0);
        auto *permissions=dialog->findChild<QComboBox*>("settingPermissions");permissions->setCurrentIndex(permissions->findData("workspace"));
        auto *box=dialog->findChild<QDialogButtonBox*>();QVERIFY(box);box->button(QDialogButtonBox::Apply)->click();
        QTest::qWait(350);const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty()){
            dialog->grab().save(artifacts+"/settings.png");
            const auto area=QRect(dialog->mapToGlobal(QPoint()),dialog->size());
            const auto source=drawerWallpaper(area,QGuiApplication::primaryScreen()->geometry(),dialog->devicePixelRatioF());
            source.save(artifacts+"/settings-wallpaper.png");LiquidPopup::renderMenuMaterial(source,dialog->size(),dialog->devicePixelRatioF(),false).save(artifacts+"/settings-material.png");
        }
        QCOMPARE(d.configuration().width,620);const auto loaded=Settings::load(s.file);QCOMPARE(loaded.contextLimitK,128);QCOMPARE(loaded.idleStopMinutes,0);QCOMPARE(loaded.permissions,QString("workspace"));QVERIFY(!loaded.followLauncherMaterial);
        QVERIFY(d.findChild<QPushButton*>("contextButton")->text().contains("128K"));
        dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void immediateSettingsAndSafeDefaults(){
        QString marker;auto s=config(&marker);s.width=610;s.height=860;s.model="saved-model";QVERIFY(s.save());const auto chatFolder=folder();
        Conversation saved(chatFolder);saved.threadId="reset-must-preserve-thread";saved.append({"user","保留的历史",{}, {},0});QVERIFY(saved.save());
        Drawer d(s,chatFolder);d.composer()->editor()->setPlainText("保留的草稿");d.Preferences();
        QDialog *dialog=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);QVERIFY(dialog);
        auto *model=dialog->findChild<QComboBox*>("settingModel");QVERIFY(model);QTRY_VERIFY_WITH_TIMEOUT(model->findData("fixture-deep")>=0,10000);
        QTRY_COMPARE_WITH_TIMEOUT(requests(marker,"model/list").size(),2,10000);const int discoveriesBefore=requests(marker,"model/list").size();
        auto *width=dialog->findChild<QSlider*>("settingWidth");QVERIFY(width);width->setValue(680);
        QCOMPARE(d.configuration().width,680);QCOMPARE(Settings::load(s.file).width,680);
        width->setSliderDown(true);width->setValue(682);QTest::qWait(70);
        QCOMPARE(d.configuration().width,682);QCOMPARE(Settings::load(s.file).width,680);
        width->setSliderDown(false);QMetaObject::invokeMethod(width,"sliderReleased",Qt::DirectConnection);
        QCOMPARE(Settings::load(s.file).width,682);
        QCOMPARE(requests(marker,"model/list").size(),discoveriesBefore);QVERIFY(!d.client()->running());
        auto *command=dialog->findChild<QLineEdit*>("settingCommand");auto *directory=dialog->findChild<QLineEdit*>("settingDirectory");QVERIFY(command);QVERIFY(directory);
        auto *translation=dialog->findChild<QLineEdit*>("settingTranslationLanguage");QVERIFY(translation);
        command->setText("unsubmitted-command");directory->setText("unsubmitted-directory");translation->setText("未提交语言");width->setValue(690);
        QCOMPARE(d.configuration().command,s.command);QCOMPARE(d.configuration().directory,s.directory);
        QCOMPARE(Settings::load(s.file).command,s.command);QCOMPARE(Settings::load(s.file).directory,s.directory);
        QCOMPARE(Settings::load(s.file).translationLanguage,s.translationLanguage);
        auto *restore=dialog->findChild<QPushButton*>("restoreAppearanceDefaults");QVERIFY(restore);restore->click();
        const Settings defaults;
        QCOMPARE(d.configuration().width,defaults.width);QCOMPARE(d.configuration().height,defaults.height);
        QCOMPARE(d.configuration().command,s.command);QCOMPARE(d.configuration().directory,s.directory);QCOMPARE(d.configuration().model,s.model);
        const auto loaded=Settings::load(s.file);QCOMPARE(loaded.width,defaults.width);QCOMPARE(loaded.command,s.command);QCOMPARE(loaded.directory,s.directory);QCOMPARE(loaded.model,s.model);
        QCOMPARE(d.conversation().threadId,QString("reset-must-preserve-thread"));QCOMPARE(d.conversation().messages().size(),1);
        QCOMPARE(d.composer()->text(),QString("保留的草稿"));
        dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void preferencesAnchorAndDelayedHelp(){
        auto s=config();Drawer d(s,folder());const auto screen=QGuiApplication::primaryScreen()->availableGeometry();
        const QRect anchor(screen.right()-60,screen.bottom()-38,44,32);d.PreferencesAt(anchor.x(),anchor.y(),anchor.width(),anchor.height());
        QDialog *dialog=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);QVERIFY(dialog);QTest::qWait(350);
        QVERIFY(screen.contains(dialog->geometry()));QVERIFY(dialog->geometry().bottom()<anchor.top());QVERIFY(qAbs(dialog->geometry().right()-anchor.right())<=1);
        auto *help=dialog->findChild<QObject*>("cliSettingsHelpController");QVERIFY(help);QCOMPARE(help->property("delayMs").toInt(),1000);
        for(const auto &name:QStringList{"settingModel","settingEffort","settingWidth","settingHeight","settingAnimation","settingDuration","settingDelay","settingLiquid","settingDarken","settingFont","settingDirectory","settingCommand","settingPermissions","settingContextLimit","settingIdleStop"}) {
            auto *widget=dialog->findChild<QWidget*>(name);QVERIFY(widget);QVERIFY(!widget->property("settingsHelp").toString().isEmpty()||!widget->parentWidget()->property("settingsHelp").toString().isEmpty());
        }
        LiquidPopup::hideText();for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="liquidPopupShell")w->hide();
        auto *width=dialog->findChild<QSlider*>("settingWidth");auto *scroll=dialog->findChild<QScrollArea*>("cliSettingsScroll");scroll->ensureWidgetVisible(width);QTest::qWait(100);QTest::mouseMove(dialog,QPoint(5,5));QTest::qWait(30);
        QTest::mouseMove(width,width->rect().center());QTest::qWait(750);
        auto tip=[]()->QWidget*{for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="liquidPopupShell"&&w->isVisible())return w;return nullptr;};
        QVERIFY(!tip());QTRY_VERIFY_WITH_TIMEOUT(tip(),700);
        bool text=false;for(auto *label:tip()->findChildren<QLabel*>())text|=label->text().contains("窗口宽度");QVERIFY(text);
        QTest::qWait(250);const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty())tip()->grab().save(artifacts+"/settings-help.png");
        QTest::mouseMove(dialog,QPoint(5,5));QTest::qWait(220);QVERIFY(!tip());dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void clearConfirmationFitsAndCancelPreservesConversation(){
        auto s=config();s.fontSize=15;const auto path=folder();Conversation saved(path);
        saved.threadId="confirmation-preserve-thread";saved.append({"user","确认弹窗测试：保留记录",{}, {},0});QVERIFY(saved.save());
        Drawer d(s,path);d.composer()->editor()->setPlainText("保留草稿");const auto before=allText(d);
        const QRect screen=QGuiApplication::primaryScreen()->availableGeometry();
        d.PreferencesAt(screen.right()-50,screen.bottom()-35,44,32);QDialog *settings=nullptr;
        for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")settings=qobject_cast<QDialog*>(w);QVERIFY(settings);
        QTest::qWait(150);auto *clear=settings->findChild<QPushButton*>("clearConversation");QVERIFY(clear);
        bool found=false,withinScreen=false,bodyFits=false,controlsFit=false;QString detail;
        QTimer::singleShot(250,[&]{
            auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            found=dialog->objectName()=="liquidMessageDialog";auto *label=dialog->findChild<QLabel*>("liquidMessageText");
            auto *header=dialog->findChild<QWidget*>("liquidDialogTitlebar");auto *box=dialog->findChild<QDialogButtonBox*>();
            withinScreen=screen.adjusted(4,4,-4,-4).contains(dialog->geometry());
            if(label&&header&&box){
                bodyFits=label->height()>=label->heightForWidth(label->width())&&label->geometry().top()>header->geometry().bottom();
                controlsFit=dialog->rect().contains(box->geometry())&&box->geometry().top()>label->geometry().bottom();
                for(auto *button:box->findChildren<QPushButton*>())controlsFit&=box->rect().contains(button->geometry());
                detail=QString("window %1x%2 label %3x%4 needs %5 header bottom %6 body top %7").arg(dialog->width()).arg(dialog->height()).arg(label->width()).arg(label->height()).arg(label->heightForWidth(label->width())).arg(header->geometry().bottom()).arg(label->y());
            }
            const auto output=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!output.isEmpty())dialog->grab().save(output+"/clear-confirmation.png");
            // Only cancel a disposable fixture conversation; never accept clear.
            for(auto *button:dialog->findChildren<QPushButton*>())if(button->property("messageChoice").toInt()==QMessageBox::No){button->click();return;}
            dialog->reject();
        });
        clear->click();QVERIFY(found);QVERIFY2(withinScreen,qPrintable(detail));QVERIFY2(bodyFits,qPrintable(detail));QVERIFY2(controlsFit,qPrintable(detail));
        QCOMPARE(allText(d),before);QCOMPARE(d.conversation().threadId,QString("confirmation-preserve-thread"));QCOMPARE(d.composer()->text(),QString("保留草稿"));
        settings->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void settingsDropdownStaysReadableAtScreenEdges(){
        auto s=config();Drawer d(s,folder());const QRect screen=QGuiApplication::primaryScreen()->availableGeometry();
        d.PreferencesAt(screen.right()-50,screen.bottom()-35,44,32);QDialog *dialog=nullptr;
        for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);QVERIFY(dialog);
        auto *backend=dialog->findChild<QComboBox*>("settingBackend");auto *model=dialog->findChild<QComboBox*>("settingModel");QVERIFY(backend);QVERIFY(model);
        QTRY_VERIFY_WITH_TIMEOUT(model->isEnabled()&&model->findData("fixture-deep")>=0,10000);
        backend->setCurrentIndex(2);backend->showPopup();QTest::qWait(80);
        auto *view=backend->view();auto *popup=view->window();QVERIFY(screen.contains(popup->geometry()));
        QVERIFY(view->viewport()->rect().contains(view->visualRect(view->model()->index(2,0))));backend->hidePopup();
        model->clear();for(int i=0;i<40;++i)model->addItem(QString("Provider %1 / A very long model name with version and reasoning configuration %1").arg(i),QString::number(i));
        model->setCurrentIndex(39);
        for(const auto &position:QList<QPoint>{screen.topLeft()+QPoint(4,4),QPoint(screen.right()-dialog->width()-4,screen.bottom()-dialog->height()-4)}) {
            dialog->move(position);model->showPopup();QTest::qWait(80);view=model->view();popup=view->window();
            QVERIFY2(screen.contains(popup->geometry()),qPrintable(QString("screen %1,%2 %3x%4 popup %5,%6 %7x%8").arg(screen.x()).arg(screen.y()).arg(screen.width()).arg(screen.height()).arg(popup->x()).arg(popup->y()).arg(popup->width()).arg(popup->height())));
            QVERIFY(view->verticalScrollBar()->maximum()>0);
            QVERIFY(view->viewport()->rect().contains(view->visualRect(view->currentIndex())));
            QTest::keyClick(view,Qt::Key_Home);QTest::qWait(20);QCOMPARE(view->currentIndex().row(),0);
            QVERIFY(view->viewport()->rect().contains(view->visualRect(view->currentIndex())));
            QTest::keyClick(view,Qt::Key_End);QTest::qWait(20);QCOMPARE(view->currentIndex().row(),39);
            QVERIFY(view->viewport()->rect().contains(view->visualRect(view->currentIndex())));
            const auto output=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!output.isEmpty())popup->grab().save(output+"/model-popup.png");
            QTest::keyClick(view,Qt::Key_Escape);QVERIFY(!popup->isVisible());
        }
        dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void dockGlassBackingStoreIsComplete(){
        if(!qEnvironmentVariableIsSet("LIQUID_CLI_NATIVE_SURFACE"))QSKIP("Opt-in composited screen check");
        QApplication::setCursorFlashTime(0);
        auto s=config();s.animation="slide";s.width=495;s.height=680;
        s.hideDelay=10000;
        Conversation saved(folder());
        for(int i=0;i<26;++i){
            saved.append({"user",QString("测试问题 %1：查看资料中的说明").arg(i),{},{},0});
            saved.append({"assistant",QString("### 测试回答 %1\n\n").arg(i)+QString("这是一段用于检验玻璃底层是否完整的多行文本。\n\n").repeated(8),{},"运行 3 条命令",0});
        }
        QVERIFY(saved.save());Drawer d(s,folder());
        d.findChild<QPushButton*>("pinDrawer")->setChecked(true);
        const auto screen=QGuiApplication::primaryScreen()->availableGeometry();
        QWidget background(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
        background.setGeometry(screen);background.setStyleSheet("background:#d43abb;");background.show();
        QTest::qWait(150);
        auto capture=[&]{return QGuiApplication::primaryScreen()->grabWindow(0,d.x(),d.y(),d.width(),d.height()).toImage().convertToFormat(QImage::Format_RGB32);};
        for(int pass=0;pass<4;++pass){
            if(pass==3){s.animation="none";d.applySettings(s);}
            d.ToggleAt(screen.right()-55,pass==2?screen.top()+5:screen.bottom()-40,32,32);
            if(pass==1){QTest::qWait(90);d.Hide();QTest::qWait(45);d.Show();}
            QTRY_VERIFY_WITH_TIMEOUT(d.progress()==1,2000);QTest::qWait(500);
            if(pass==1){auto *bar=d.chat()->verticalScrollBar();bar->setValue(bar->value()-145);QTest::qWait(100);}
            if(pass==2){for(auto *bubble:d.findChildren<QFrame*>("assistantBubble"))bubble->update();QTest::qWait(100);}
            // Read the compositor BEFORE forcing any QWidget repaint/grab.
            const auto before=capture();d.repaint();QTest::qWait(80);const auto after=capture();
            QCOMPARE(before.size(),after.size());int changed=0,total=0;
            for(int y=12;y<before.height()-12;++y)for(int x=12;x<before.width()-12;++x){
                const auto a=before.pixelColor(x,y),b=after.pixelColor(x,y);
                if(qMax(qAbs(a.red()-b.red()),qMax(qAbs(a.green()-b.green()),qAbs(a.blue()-b.blue())))>12)++changed;
                ++total;
            }
            const QString output=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");
            if(!output.isEmpty()){before.save(output+QString("/surface-%1-before.png").arg(pass));after.save(output+QString("/surface-%1-after.png").arg(pass));}
            qInfo("Surface pass %d: changed %d / %d pixels",pass,changed,total);
            QVERIFY2(changed<double(total)*.005,"Settled glass changed after full repaint: backing store was incomplete");
            QVERIFY(!d.client()->running());d.Hide();QTRY_VERIFY_WITH_TIMEOUT(!d.isVisible(),2000);
        }
    }
    void residentWarmupKeepsBackendStopped(){
        QString marker;Drawer d(config(&marker),folder());
        d.prepareResident();QTRY_VERIFY_WITH_TIMEOUT(d.glassBuilds()>0,5000);
        QVERIFY(!d.expanded());QVERIFY(!d.client()->running());QVERIFY(!QFileInfo::exists(marker));
        const int builds=d.glassBuilds();d.Show();QCOMPARE(d.glassBuilds(),builds);
        QVERIFY(ResidentStartup::save(true));QVERIFY(ResidentStartup::enabled());
        const auto startup=read(ResidentStartup::path());QVERIFY(startup.contains("--resident"));QVERIFY(!startup.contains("--show"));
        d.Preferences();QDialog *dialog=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);
        QVERIFY(dialog);auto *resident=dialog->findChild<QCheckBox*>("settingResident");QVERIFY(resident&&resident->isChecked());
        dialog->close();QVERIFY(ResidentStartup::save(false));QVERIFY(!ResidentStartup::enabled());
    }
    void dropsPreserveDraftAndNeverSend(){
        QString marker;Drawer d(config(&marker),folder());d.composer()->editor()->setPlainText("已有草稿");
        const QString file=root.path()+"/资料 含空格$().xlsx";QFile sheet(file);QVERIFY(sheet.open(QIODevice::WriteOnly));sheet.write("fixture");sheet.close();
        QImage image(40,30,QImage::Format_RGB32);image.fill(Qt::cyan);const QString picture=root.path()+"/测试图片.png";QVERIFY(image.save(picture));
        QMimeData files;files.setUrls({QUrl::fromLocalFile(file),QUrl::fromLocalFile(root.path()),QUrl::fromLocalFile(picture)});const auto manifest=StationDrop::prepare(&files);QVERIFY(!manifest.isEmpty());
        const auto screen=QGuiApplication::primaryScreen()->availableGeometry();QVERIFY(d.ImportDropAt(manifest,screen.right()-50,screen.bottom()-34,44,32));
        QVERIFY(!QFile::exists(manifest));QVERIFY(d.composer()->text().startsWith("已有草稿\n"));QVERIFY(d.composer()->text().contains(file));QVERIFY(d.composer()->text().contains(root.path()));QCOMPARE(d.composer()->images().size(),1);
        QMimeData text;text.setText("新的说明\n第二行");QVERIFY(d.ImportDropAt(StationDrop::prepare(&text),0,0,0,0));QVERIFY(d.composer()->text().contains("新的说明\n第二行"));
        QMimeData pixels;pixels.setImageData(image);const auto raw=StationDrop::prepare(&pixels);const auto staged=StationDrop::read(raw).value("image").toString();QVERIFY(d.ImportDropAt(raw,0,0,0,0));
        QCOMPARE(d.composer()->images().size(),2);QVERIFY(!QFile::exists(staged));QVERIFY(d.expanded());QVERIFY(!d.client()->running());QVERIFY(!QFileInfo::exists(marker));QCOMPARE(d.conversation().messages().size(),0);
        Drawer restored(d.configuration(),d.conversation().folder());QCOMPARE(restored.composer()->text(),d.composer()->text());QCOMPARE(restored.composer()->images(),d.composer()->images());QVERIFY(!restored.client()->running());
    }
    void composerGrowsForWrappedPaths(){
        auto s=config();s.width=360;Drawer d(s,folder());d.Show();auto *editor=d.composer()->editor();const int line=editor->fontMetrics().lineSpacing();
        QTRY_VERIFY(editor->height()>=line*3+16);const int minimum=editor->height();
        editor->setPlainText(QString("/home/zhouzhang/下载/很长的资料文件名称/").repeated(12));QTRY_VERIFY(editor->height()>minimum);
        QVERIFY(editor->height()<=line*8+16);editor->clear();QTRY_COMPARE(editor->height(),minimum);
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty())d.grab().save(artifacts+"/larger-input.png");
    }
    void modelCatalogDoesNotOpenConversation(){
        QString marker;const auto s=config(&marker);CodexClient client;client.configure(s);
        QSignalSpy ready(&client,&CodexClient::modelsReady),failed(&client,&CodexClient::modelsFailed);
        client.discoverModels();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);QCOMPARE(failed.count(),0);
        const auto args=ready.first();QCOMPARE(args[0].toJsonArray().size(),2);QCOMPARE(args[1].toString(),QString("fixture-fast"));
        QCOMPARE(requests(marker,"model/list").size(),2);QVERIFY(requests(marker,"thread/start").isEmpty());QVERIFY(requests(marker,"turn/start").isEmpty());
        const auto pids=QJsonDocument::fromJson(read(marker)).object().value("pids").toArray();QTRY_VERIFY_WITH_TIMEOUT(allStopped(pids),10000);QVERIFY(!client.running());
    }
    void modelSettingsSaveImmediately(){
        QString marker;auto s=config(&marker);Drawer d(s,folder());d.Show();d.Preferences();
        QDialog *dialog=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);QVERIFY(dialog);
        auto *model=dialog->findChild<QComboBox*>("settingModel");auto *effort=dialog->findChild<QSlider*>("settingEffort");QVERIFY(model);QVERIFY(effort);
        QTRY_VERIFY_WITH_TIMEOUT(model->isEnabled()&&model->findData("fixture-deep")>=0,10000);
        model->setCurrentIndex(model->findData("fixture-deep"));QMetaObject::invokeMethod(model,"activated",Q_ARG(int,model->currentIndex()));
        QCOMPARE(effort->maximum(),3);effort->setValue(3);
        QCOMPARE(d.configuration().model,QString("fixture-deep"));QCOMPARE(Settings::load(s.file).reasoningEffort,QString("future-effort"));
        QVERIFY(!d.client()->running());QVERIFY(requests(marker,"thread/start").isEmpty());
        dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    void quickReplyKeepsNormalDefaultsAndSeparatesConversations(){
        QString marker;auto s=config(&marker);s.model="gpt-6-luna";s.reasoningEffort="xhigh";s.contextLimitK=340;
        QVERIFY(s.save());const auto settingsBefore=read(s.file);const auto storage=folder();
        QString normalThread,quickThread,normalImage,quickImage;
        {
            Drawer d(s,storage);d.Show();
            auto *toggle=d.findChild<QPushButton*>("quickReplyMode");QVERIFY(toggle);QVERIFY(!toggle->isChecked());
            d.submit("普通问题",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
            normalThread=d.conversation().threadId;
            auto params=requests(marker,"turn/start").last()["params"].toObject();
            QCOMPARE(params["model"].toString(),QString("gpt-6-luna"));QCOMPARE(params["effort"].toString(),QString("xhigh"));
            d.composer()->editor()->setPlainText("普通草稿");QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::green);
            normalImage=d.conversation().storeImage(image);QVERIFY(!normalImage.isEmpty());d.composer()->addImage(normalImage);
            const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");
            if(!artifacts.isEmpty()){QDir().mkpath(artifacts);QTest::qWait(80);d.grab().save(artifacts+"/reply-normal.png");}
            toggle->click();QVERIFY(toggle->isChecked());QVERIFY(!d.client()->running());
            QVERIFY(d.conversation().messages().isEmpty());QVERIFY(d.composer()->text().isEmpty());QVERIFY(d.composer()->images().isEmpty());
            QCOMPARE(d.conversation().folder(),storage+"/quick-reply");
            QCOMPARE(d.configuration().reasoningEffort,QString("xhigh"));QCOMPARE(read(s.file),settingsBefore);
            QVERIFY(d.findChild<QPushButton*>("contextButton")->text().contains("64K"));
            auto changed=s;changed.fontSize=12;d.applySettings(changed);
            d.submit("快速问题",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
            quickThread=d.conversation().threadId;QVERIFY(quickThread!=normalThread);
            params=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(params["model"].toString(),QString("gpt-6-luna"));QCOMPARE(params["effort"].toString(),QString("low"));
            QVERIFY(!allText(d).contains("普通问题"));d.composer()->editor()->setPlainText("快速草稿");
            quickImage=d.conversation().storeImage(image);d.composer()->addImage(quickImage);
            if(!artifacts.isEmpty()){QTest::qWait(80);d.grab().save(artifacts+"/reply-quick.png");}
            toggle->click();QVERIFY(!toggle->isChecked());QCOMPARE(d.conversation().threadId,normalThread);
            QCOMPARE(d.composer()->text(),QString("普通草稿"));QCOMPARE(d.composer()->images(),QStringList{normalImage});
            QVERIFY(allText(d).contains("普通问题"));QVERIFY(!allText(d).contains("快速问题"));
            d.submit("普通追问",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
            params=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(params["effort"].toString(),QString("xhigh"));
            QCOMPARE(d.conversation().threadId,normalThread);QVERIFY(d.setQuickReply(true));
            QCOMPARE(d.conversation().threadId,quickThread);QCOMPARE(d.composer()->text(),QString("快速草稿"));QCOMPARE(d.composer()->images(),QStringList{quickImage});
        }
        Drawer restored(s,storage);QCOMPARE(restored.conversation().threadId,normalThread);
        QVERIFY(!restored.findChild<QPushButton*>("quickReplyMode")->isChecked());
        QCOMPARE(read(s.file),settingsBefore);QVERIFY(restored.setQuickReply(true));QCOMPARE(restored.conversation().threadId,quickThread);
        QCOMPARE(restored.composer()->text(),QString("快速草稿"));
        restored.clearConversation();QVERIFY(!QFile::exists(quickImage));QVERIFY(QFile::exists(normalImage));
        QVERIFY(restored.setQuickReply(false));QCOMPARE(restored.conversation().threadId,normalThread);
        QVERIFY(allText(restored).contains("普通追问"));
    }
    void longStatusNeverWidensDrawerOrLeavesUnpaintedContent(){
        auto s=config();s.model="gpt-6-luna";s.reasoningEffort="xhigh";s.height=680;s.contextLimitK=340;
        const auto storage=folder();Conversation seed(storage);seed.contextTokens=179878;
        seed.append({"user","翻译成中文："+QString(160,'x'),{},{},0});
        seed.append({"assistant","这是用于验证窗口边界的长消息。"+QString(140,'y'),{},{},0});QVERIFY(seed.save());
        Drawer d(s,storage);d.Show();
        const QString message="请等待当前任务结束，再切换问答模式。";
        for(int width:QList<int>{360,495}){
            s.width=width;d.applySettings(s);d.client()->status(message.repeated(5));QTest::qWait(80);
            QCOMPARE(d.size(),d.targetGeometry().size());QCOMPARE(d.width(),width);
            auto *status=d.findChild<QLabel*>("cliStatus");QCOMPARE(status->text(),message.repeated(5));QCOMPARE(status->toolTip(),status->text());
            for(auto *child:QList<QWidget*>{d.chat(),d.composer(),d.findChild<QPushButton*>("collapseDrawer"),d.findChild<QPushButton*>("quickReplyMode"),d.findChild<QPushButton*>("newConversation")}){
                const QRect area(child->mapTo(&d,QPoint()),child->size());QVERIFY2(d.rect().contains(area),qPrintable(child->objectName()));
            }
        }
        d.submit("SLOW",{});QVERIFY(!d.findChild<QPushButton*>("quickReplyMode")->isEnabled());
        d.client()->interrupt();QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),5000);QVERIFY(d.findChild<QPushButton*>("quickReplyMode")->isEnabled());
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");
        if(!artifacts.isEmpty()){d.client()->status(message);QTest::qWait(80);QDir().mkpath(artifacts);d.grab().save(artifacts+"/bounded-long-status.png");}
    }
    void quickReplyRejectsBusyAndCompactingSwitches(){
        auto s=config();s.model="gpt-6-luna";s.reasoningEffort="xhigh";Drawer d(s,folder());
        auto *toggle=d.findChild<QPushButton*>("quickReplyMode");
        d.submit("SLOW",{});toggle->click();QVERIFY(!toggle->isChecked());QVERIFY(d.client()->busy());
        d.client()->interrupt();QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),5000);
        QVERIFY(d.setQuickReply(true));d.submit("quick",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        d.client()->compact();QVERIFY(!d.setQuickReply(false));QVERIFY(toggle->isChecked());
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->compacting(),10000);QVERIFY(d.setQuickReply(false));
    }
    void quickReplyProtectsCorruptHistoryAndUnsavedDraft(){
        auto s=config();const auto storage=folder();Drawer d(s,storage);d.composer()->editor()->setPlainText("保留草稿");
        QDir().mkpath(storage+"/quick-reply");QFile corrupt(storage+"/quick-reply/conversation.json");
        QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("broken");corrupt.close();
        QVERIFY(!d.setQuickReply(true));QCOMPARE(read(corrupt.fileName()),QByteArray("broken"));QCOMPARE(d.composer()->text(),QString("保留草稿"));
        QVERIFY(corrupt.remove());QDir().mkpath(storage+"/draft.json");
        QVERIFY(!d.setQuickReply(true));QCOMPARE(d.composer()->text(),QString("保留草稿"));QCOMPARE(d.conversation().folder(),storage);
        QDir(storage+"/draft.json").removeRecursively();QVERIFY(d.setQuickReply(true));
        auto other=s;other.backend="qwen";other.command=quote(qEnvironmentVariable("LIQUID_ACP_FIXTURE"))+" "+quote(storage+"/qwen-log");
        d.applySettings(other);QVERIFY(!d.setQuickReply(true));QVERIFY(d.findChild<QPushButton*>("quickReplyMode")->parentWidget()->isHidden());
        QVERIFY(!d.findChild<QPushButton*>("quickReplyMode")->isChecked());
        d.applySettings(s);QCOMPARE(d.conversation().folder(),storage);QCOMPARE(d.composer()->text(),QString("保留草稿"));
    }
    void quickModelEffortAndFastReachWireWithoutChangingNormalMode(){
        QString marker;auto s=config(&marker);s.model="gpt-6-luna";s.reasoningEffort="xhigh";
        s.quickModel="fixture-fast";s.quickReasoningEffort="xhigh";s.quickFast=true;s.defaultReplyMode="quick";
        Drawer d(s,folder());d.submit("quick fast",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        auto turns=requests(marker,"turn/start");QCOMPARE(turns.size(),1);auto p=turns.last()["params"].toObject();
        QCOMPARE(p["model"].toString(),QString("fixture-fast"));QCOMPARE(p["effort"].toString(),QString("xhigh"));QCOMPARE(p["serviceTierForTurn"].toString(),QString("priority"));QVERIFY(!p.contains("serviceTier"));
        const auto thread=d.conversation().threadId;s.quickFast=false;d.applySettings(s);d.submit("standard",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        p=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(p["serviceTierForTurn"].toString(),QString("default"));QCOMPARE(d.conversation().threadId,thread);
        d.client()->stop();s.quickFast=true;d.applySettings(s);d.submit("resume fast",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        p=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(p["serviceTierForTurn"].toString(),QString("priority"));QCOMPARE(d.conversation().threadId,thread);
        QVERIFY(d.setQuickReply(false));d.submit("ordinary",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        p=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(p["model"].toString(),QString("gpt-6-luna"));QCOMPARE(p["effort"].toString(),QString("xhigh"));QCOMPARE(p["serviceTierForTurn"].toString(),QString("default"));
        QVERIFY(d.setQuickReply(true));s.quickModel="fixture-deep";s.quickReasoningEffort="future-effort";d.applySettings(s);const int before=requests(marker,"turn/start").size();
        d.submit("unsupported fast",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QCOMPARE(requests(marker,"turn/start").size(),before);QVERIFY(allText(d).contains("未提供 Fast"));
        s.quickFast=false;d.applySettings(s);d.submit("supported standard",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        p=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(p["model"].toString(),QString("fixture-deep"));QCOMPARE(p["effort"].toString(),QString("future-effort"));
        s.quickReasoningEffort="xhigh";d.applySettings(s);const int valid=requests(marker,"turn/start").size();d.submit("unsupported effort",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QCOMPARE(requests(marker,"turn/start").size(),valid);QVERIFY(allText(d).contains("不支持思考强度"));
    }
    void newConversationArchivesOnlyCurrentModeAndPreservesDraft(){
        QString marker;auto s=config(&marker);s.quickModel="fixture-fast";const auto storage=folder();Drawer d(s,storage);
        d.submit("普通历史",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);const auto normalThread=d.conversation().threadId;
        QVERIFY(d.setQuickReply(true));QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::blue);const auto imagePath=d.conversation().storeImage(image);
        d.submit("快速历史",{imagePath});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);const auto oldThread=d.conversation().threadId;
        d.composer()->editor()->setPlainText("留给新对话的草稿");auto *button=d.findChild<QPushButton*>("newConversation");QVERIFY(button);button->click();
        QVERIFY(d.conversation().threadId.isEmpty());QVERIFY(d.conversation().messages().isEmpty());QVERIFY(!d.client()->running());QCOMPARE(d.composer()->text(),QString("留给新对话的草稿"));
        const auto archiveRoot=storage+"/quick-reply/archives";const auto names=QDir(archiveRoot).entryList(QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(names.size(),1);
        Conversation saved(archiveRoot+"/"+names.first());QVERIFY(saved.load());QCOMPARE(saved.threadId,oldThread);QVERIFY(saved.messages().first().text.contains("快速历史"));
        const auto copy=saved.messages().first().images.first();QVERIFY(copy!=imagePath);QVERIFY(QFile::exists(copy));
        d.submit("新快速对话",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QVERIFY(d.conversation().threadId!=oldThread);
        d.clearConversation();QVERIFY(QFile::exists(copy));QVERIFY(d.setQuickReply(false));QCOMPARE(d.conversation().threadId,normalThread);QVERIFY(allText(d).contains("普通历史"));
    }
    void newConversationRejectsActiveTurnAndArchiveFailure(){
        auto s=config();const auto storage=folder();Drawer d(s,storage);d.submit("SLOW",{});
        auto *button=d.findChild<QPushButton*>("newConversation");QVERIFY(!button->isEnabled());QVERIFY(!d.startNewConversation());QVERIFY(d.client()->busy());
        d.client()->interrupt();QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),5000);QVERIFY(button->isEnabled());
        QFile obstruction(storage+"/archives");QVERIFY(obstruction.open(QIODevice::WriteOnly));obstruction.write("keep");obstruction.close();
        const auto before=allText(d);const auto thread=d.conversation().threadId;QVERIFY(!d.startNewConversation());QCOMPARE(allText(d),before);QCOMPARE(d.conversation().threadId,thread);QCOMPARE(read(obstruction.fileName()),QByteArray("keep"));
    }
    void quickReplyPreservesExplicitSkillInstructions(){
        QString marker;auto s=config(&marker);s.defaultReplyMode="quick";s.quickModel="fixture-fast";s.quickReasoningEffort="xhigh";
        Drawer d(s,folder());const QString instruction="加载 AI 空间 skill（ai-space-index-query），按这个 skill 检索本地资料。";
        d.submit(instruction,{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        const auto params=requests(marker,"turn/start").last()["params"].toObject();QCOMPARE(params["input"].toArray().first().toObject()["text"].toString(),instruction);
        const auto thread=requests(marker,"thread/start").last()["params"].toObject();
        for(const auto &key:QStringList{"disabledPluginIds","tools","baseInstructions","developerInstructions"}){QVERIFY(!params.contains(key));QVERIFY(!thread.contains(key));}
    }
    void ordinaryFastIsIndependentAndUsesNativeTier(){
        QString marker;auto s=config(&marker);s.model="fixture-fast";s.reasoningEffort="xhigh";s.quickModel="fixture-fast";s.quickFast=true;QVERIFY(s.save());
        Drawer d(s,folder());d.submit("ordinary standard",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        QCOMPARE(requests(marker,"turn/start").last()["params"].toObject()["serviceTierForTurn"].toString(),QString("default"));
        LquiSettings::Bridge bridge("ai",[&](const QString &){return d.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"}}};});bridge.Page("general");
        auto *fast=d.settingsBridgeRoot()->findChild<QCheckBox*>("settingNormalFast");QVERIFY(fast);QTRY_VERIFY_WITH_TIMEOUT(fast->isEnabled(),10000);
        const auto page=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QString id;
        for(const auto &v:page["fields"].toArray())if(v.toObject()["name"]=="settingNormalFast")id=v.toObject()["id"].toString();QVERIFY(!id.isEmpty());
        bridge.Set("general",id,QString::fromUtf8(QJsonDocument(QJsonObject{{"value",true}}).toJson()));QVERIFY(Settings::load(s.file).normalFast);QVERIFY(Settings::load(s.file).quickFast);
        d.submit("ordinary fast",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QCOMPARE(requests(marker,"turn/start").last()["params"].toObject()["serviceTierForTurn"].toString(),QString("priority"));
        bridge.Set("general",id,QString::fromUtf8(QJsonDocument(QJsonObject{{"value",false}}).toJson()));QVERIFY(!Settings::load(s.file).normalFast);QVERIFY(Settings::load(s.file).quickFast);
        QVERIFY(d.setQuickReply(true));d.submit("quick still fast",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QCOMPARE(requests(marker,"turn/start").last()["params"].toObject()["serviceTierForTurn"].toString(),QString("priority"));
        QVERIFY(d.setQuickReply(false));d.submit("ordinary off again",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);QCOMPARE(requests(marker,"turn/start").last()["params"].toObject()["serviceTierForTurn"].toString(),QString("default"));
    }
    void quickChangesDuringConnectionOnlyAffectTheNextMessage(){
        QString marker;auto s=config(&marker);s.defaultReplyMode="quick";s.quickModel="fixture-fast";s.quickReasoningEffort="xhigh";s.quickFast=true;
        Drawer d(s,folder());d.submit("first",{});s.quickReasoningEffort="low";s.quickFast=false;d.applySettings(s);
        QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);auto p=requests(marker,"turn/start").last()["params"].toObject();
        QCOMPARE(p["effort"].toString(),QString("xhigh"));QCOMPARE(p["serviceTierForTurn"].toString(),QString("priority"));
        d.submit("second",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);p=requests(marker,"turn/start").last()["params"].toObject();
        QCOMPARE(p["effort"].toString(),QString("low"));QCOMPARE(p["serviceTierForTurn"].toString(),QString("default"));
    }
    void quickProfilePickerPersistsAndIsExposedToUnifiedSettings(){
        auto s=config();s.model="gpt-6-luna";s.reasoningEffort="xhigh";QVERIFY(s.save());Drawer d(s,folder());
        LquiSettings::Bridge bridge("ai",[&](const QString &){return d.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
        bridge.Page("general");QWidget *dialog=d.settingsBridgeRoot();
        auto *model=dialog->findChild<QComboBox*>("settingQuickModel");auto *effort=dialog->findChild<QSlider*>("settingQuickEffort");auto *fast=dialog->findChild<QCheckBox*>("settingQuickFast");QVERIFY(model&&effort&&fast);
        QTRY_VERIFY_WITH_TIMEOUT(model->isEnabled()&&model->findData("fixture-fast")>=0,10000);
        const auto page=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QHash<QString,QString> ids;
        for(const auto &v:page["fields"].toArray()){const auto f=v.toObject();ids[f["name"].toString()]=f["id"].toString();}
        for(const auto &name:QStringList{"settingQuickModel","settingQuickEffort","settingQuickFast"})QVERIFY(!ids[name].isEmpty());
        const auto set=[&](const QString &name,const QJsonValue &v){return QJsonDocument::fromJson(bridge.Set("general",ids[name],QString::fromUtf8(QJsonDocument(QJsonObject{{"value",v}}).toJson())).toUtf8()).object()["ok"].toBool();};
        QVERIFY(set("settingQuickModel",model->findData("fixture-fast")));QVERIFY(fast->isEnabled());
        QVERIFY(set("settingQuickEffort",effort->property("efforts").toStringList().indexOf("xhigh")));QVERIFY(set("settingQuickFast",true));
        auto saved=Settings::load(s.file);QCOMPARE(saved.quickModel,QString("fixture-fast"));QCOMPARE(saved.quickReasoningEffort,QString("xhigh"));QVERIFY(saved.quickFast);QCOMPARE(saved.model,QString("gpt-6-luna"));QCOMPARE(saved.reasoningEffort,QString("xhigh"));
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");
        if(!artifacts.isEmpty()){dialog->show();if(auto *scroll=dialog->findChild<QScrollArea*>("cliSettingsScroll"))scroll->ensureWidgetVisible(fast);QTest::qWait(80);QDir().mkpath(artifacts);dialog->grab().save(artifacts+"/quick-model-settings.png");}
        QVERIFY(d.setQuickReply(true));QVERIFY(d.findChild<QLabel*>("replyModeLabel")->text().contains("fixture-fast xhigh · Fast"));
        QVERIFY(set("settingQuickFast",false));QVERIFY(!Settings::load(s.file).quickFast);
        QVERIFY(set("settingQuickModel",model->findData("fixture-deep")));QVERIFY(!fast->isEnabled());QCOMPARE(effort->property("efforts").toStringList(),(QStringList{"","medium","high","future-effort"}));
        QCOMPARE(Settings::load(s.file).model,QString("gpt-6-luna"));QVERIFY(!d.client()->running());
    }
    void defaultReplyModeAvailableThroughUnifiedSettings(){
        auto s=config();s.model="gpt-6-luna";s.reasoningEffort="xhigh";QVERIFY(s.save());Drawer d(s,folder());
        LquiSettings::Bridge bridge("ai",[&](const QString &){return d.settingsBridgeRoot();},[]{return QJsonArray{QJsonObject{{"id","general"}}};});
        const auto page=QJsonDocument::fromJson(bridge.Page("general").toUtf8()).object();QString id;
        for(const auto &v:page["fields"].toArray()){
            const auto f=v.toObject();if(f["name"]!="settingDefaultReplyMode")continue;
            id=f["id"].toString();QCOMPARE(f["options"].toArray(),(QJsonArray{"普通思考","快问快答"}));QCOMPARE(f["value"].toInt(),0);
        }
        QVERIFY(!id.isEmpty());bridge.Set("general",id,QString::fromUtf8(QJsonDocument(QJsonObject{{"value",1}}).toJson()));
        QCOMPARE(Settings::load(s.file).defaultReplyMode,QString("quick"));QCOMPARE(Settings::load(s.file).reasoningEffort,QString("xhigh"));
        QVERIFY(!d.findChild<QPushButton*>("quickReplyMode")->isChecked());
        bridge.Discard("general");QCOMPARE(Settings::load(s.file).defaultReplyMode,QString("quick"));
    }
    void defaultReplyModeSavesWithoutChangingActiveConversation(){
        QString marker;auto s=config(&marker);s.model="gpt-6-luna";s.reasoningEffort="xhigh";QVERIFY(s.save());const auto storage=folder();
        {
            Drawer d(s,storage);d.Show();d.composer()->editor()->setPlainText("普通草稿");d.Preferences();
            QDialog *dialog=nullptr;for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="cliPreferences")dialog=qobject_cast<QDialog*>(w);QVERIFY(dialog);
            auto *choice=dialog->findChild<QComboBox*>("settingDefaultReplyMode");QVERIFY(choice);QCOMPARE(choice->count(),2);
            QCOMPARE(choice->currentData().toString(),QString("normal"));
            choice->setCurrentIndex(choice->findData("quick"));QMetaObject::invokeMethod(choice,"activated",Q_ARG(int,choice->currentIndex()));
            QCOMPARE(Settings::load(s.file).defaultReplyMode,QString("quick"));QCOMPARE(d.configuration().reasoningEffort,QString("xhigh"));
            QCOMPARE(d.composer()->text(),QString("普通草稿"));QVERIFY(!d.findChild<QPushButton*>("quickReplyMode")->isChecked());
            auto *font=dialog->findChild<QSlider*>("settingFont");font->setValue(font->value()+1);
            QCOMPARE(Settings::load(s.file).defaultReplyMode,QString("quick"));
            const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");
            if(!artifacts.isEmpty()){QDir().mkpath(artifacts);QTest::qWait(80);dialog->grab().save(artifacts+"/reply-default-settings.png");}
            dialog->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        }
        {
            Drawer d(Settings::load(s.file),storage);QVERIFY(d.findChild<QPushButton*>("quickReplyMode")->isChecked());
            QCOMPARE(d.conversation().folder(),storage+"/quick-reply");QVERIFY(d.composer()->text().isEmpty());
            d.submit("默认快问",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
            QCOMPARE(requests(marker,"turn/start").last()["params"].toObject()["effort"].toString(),QString("low"));
            QVERIFY(d.setQuickReply(false));QCOMPARE(d.composer()->text(),QString("普通草稿"));
            QCOMPARE(Settings::load(s.file).defaultReplyMode,QString("quick"));
        }
        auto normal=Settings::load(s.file);normal.defaultReplyMode="normal";QVERIFY(normal.save());
        Drawer normalLaunch(Settings::load(s.file),storage);QVERIFY(!normalLaunch.findChild<QPushButton*>("quickReplyMode")->isChecked());
        QCOMPARE(normalLaunch.composer()->text(),QString("普通草稿"));
    }
    void liveModelAndEffortSelection(){
        QString marker;auto s=config(&marker);Drawer d(s,folder());d.submit("first",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        const auto thread=d.conversation().threadId;
        s.model="fixture-deep";s.reasoningEffort="future-effort";d.applySettings(s);d.submit("second",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        QCOMPARE(d.conversation().threadId,thread);auto turns=requests(marker,"turn/start");QCOMPARE(turns.size(),2);
        QCOMPARE(turns.last()["params"].toObject()["model"].toString(),QString("fixture-deep"));QCOMPARE(turns.last()["params"].toObject()["effort"].toString(),QString("future-effort"));
        s.model.clear();s.reasoningEffort.clear();d.applySettings(s);d.submit("third",{});QTRY_VERIFY_WITH_TIMEOUT(!d.client()->busy(),10000);
        turns=requests(marker,"turn/start");QCOMPARE(turns.last()["params"].toObject()["model"].toString(),QString("fixture-fast"));QCOMPARE(turns.last()["params"].toObject()["effort"].toString(),QString("high"));
        QCOMPARE(requests(marker,"thread/start").size(),1);
    }
    void composerContrastInBothThemes(){
        auto s=config();Drawer d(s,folder());d.Show();auto *composer=d.composer();
        for(bool light:{false,true}) {
            composer->setColors(light);auto *editor=composer->editor();editor->ensurePolished();
            for(auto group:{QPalette::Active,QPalette::Inactive}) {
                const auto text=editor->palette().color(group,QPalette::Text),placeholder=editor->property("placeholderInk").value<QColor>();
                QVERIFY(light?text.lightness()<90:text.lightness()>210);
                QVERIFY2(light?placeholder.lightness()<130:placeholder.lightness()>150,qPrintable(QString("light=%1 group=%2 placeholder=%3 text=%4").arg(light).arg(group).arg(placeholder.name(),text.name())));
            }
            QCoreApplication::processEvents();const auto pixels=editor->viewport()->grab().toImage();
            const auto ink=editor->property("placeholderInk").value<QColor>();int readablePixels=0;
            for(int y=0;y<pixels.height();++y)for(int x=0;x<pixels.width();++x){const auto c=pixels.pixelColor(x,y);if(qAbs(c.red()-ink.red())<12&&qAbs(c.green()-ink.green())<12&&qAbs(c.blue()-ink.blue())<12)++readablePixels;}
            QVERIFY(readablePixels>12);
        }
    }
    void nativeModelCatalogReadOnly(){
        if(!qEnvironmentVariableIsSet("LIQUID_CLI_NATIVE_CATALOG"))QSKIP("Opt-in local CLI catalog check");
        Settings s;s.command="codex";s.directory=QDir::homePath();CodexClient client;client.configure(s);
        QSignalSpy ready(&client,&CodexClient::modelsReady),failed(&client,&CodexClient::modelsFailed);client.discoverModels();
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),20000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first()[0].toString()));
        QVERIFY(!ready.first()[0].toJsonArray().isEmpty());QVERIFY(client.threadId().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!client.running(),3000);
        const auto args=ready.first();QJsonObject result{{"models",args[0].toJsonArray()},{"configuredModel",args[1].toString()},{"configuredEffort",args[2].toString()}};
        const auto artifacts=qEnvironmentVariable("LIQUID_CLI_ARTIFACT_DIR");if(!artifacts.isEmpty()){QDir().mkpath(artifacts);QFile file(artifacts+"/native-model-catalog.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(result).toJson());}
    }
    void captureCancellationAndPixels(){
        QWidget desktop;desktop.setWindowFlags(Qt::FramelessWindowHint);desktop.setGeometry(QGuiApplication::primaryScreen()->geometry());
        QPalette colors;colors.setColor(QPalette::Window,QColor("#619a6b"));desktop.setPalette(colors);desktop.setAutoFillBackground(true);desktop.show();desktop.raise();QTest::qWait(50);
        bool done=false;QImage image;
        auto overlays=RegionCapture::begin([&](QImage result){image=result;done=true;});QVERIFY(!overlays.isEmpty());
        QTest::keyClick(overlays.first(),Qt::Key_Escape);QTRY_VERIFY(done);QVERIFY(image.isNull());QVERIFY(!QWidget::mouseGrabber());
        done=false;overlays=RegionCapture::begin([&](QImage result){image=result;done=true;});auto *overlay=overlays.first();
        const qreal scale=overlay->devicePixelRatioF();
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(30,40));QTest::mouseMove(overlay,QPoint(130,110));QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(130,110));
        QTRY_VERIFY(done);QVERIFY(!image.isNull());
        QVERIFY(qAbs(image.width()-qRound(101*scale))<=2);QVERIFY(!QWidget::mouseGrabber());
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor("#619a6b"));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        for(auto *w:QApplication::topLevelWidgets())QVERIFY(w->objectName()!="regionCapture");
    }
    void captureButtonHidesAndRestores(){
        QString marker;Drawer d(config(&marker),folder());d.Show();d.findChild<QPushButton*>("captureButton")->click();QVERIFY(!d.isVisible());QVERIFY(!d.edgeWidget()->isVisible());
        RegionCapture *overlay=nullptr;QElapsedTimer wait;wait.start();
        while(!overlay&&wait.elapsed()<2000){for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="regionCapture")overlay=static_cast<RegionCapture*>(w);QTest::qWait(20);}
        QVERIFY(overlay);QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(30,40));QTest::mouseMove(overlay,QPoint(130,110));QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,QPoint(130,110));
        QTRY_VERIFY(d.findChild<CaptureEditor*>());QVERIFY(!d.isVisible());d.findChild<CaptureEditor*>()->findChild<QPushButton*>("captureInsert")->click();
        QTRY_VERIFY(d.expanded());QVERIFY(d.edgeWidget()->isVisible());QCOMPARE(d.composer()->images().size(),1);QVERIFY(!QFileInfo::exists(marker));QVERIFY(!QWidget::mouseGrabber());
    }
    void crashCleanup(){
        QString marker;auto s=config(&marker);QVERIFY(s.save());QProcess child;
        child.start(QCoreApplication::applicationFilePath(),{"--crash-child",s.file,folder()});QVERIFY(child.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker),10000);QTRY_VERIFY_WITH_TIMEOUT(QJsonDocument::fromJson(read(marker)).object().value("pids").toArray().size()==3,5000);
        const auto pids=QJsonDocument::fromJson(read(marker)).object().value("pids").toArray();
        child.kill();QVERIFY(child.waitForFinished());QTRY_VERIFY_WITH_TIMEOUT(allStopped(pids),10000);
    }
};
int main(int argc,char **argv){
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    if(argc==4&&QString(argv[1])=="--crash-child"){Drawer d(Settings::load(QString::fromLocal8Bit(argv[2])),QString::fromLocal8Bit(argv[3]));d.Show();d.submit("SLOW",{});return app.exec();}
    CliTest test;return QTest::qExec(&test,argc,argv);
}
#include "test.moc"
