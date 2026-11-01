#include "DesktopSessionSettings.h"
#include "DesktopCanvas.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

QWidget *createDesktopSessionSettings(DesktopCanvas *canvas,QWidget *parent) {
    auto *page=new QWidget(parent);page->setObjectName("desktopSessionSettings");auto *layout=new QVBoxLayout(page);layout->setContentsMargins(0,0,0,0);
    auto *intro=new QLabel("现在这项是“限时体验”，不是永久替换系统。点击后暂时关闭原生桌面，由 Fences 单独显示文件和组件；已打开的应用和文件管理窗口继续使用。",page);intro->setWordWrap(true);layout->addWidget(intro);
    auto *recovery=new QLabel("60 秒后会自动恢复，也可以提前点“立即恢复”。这不会卸载 UKUI、不会注销，也不会改变下次开机的启动方式。",page);recovery->setWordWrap(true);layout->addWidget(recovery);
    auto *login=new QLabel("正在读取登录桌面配置…",page);login->setObjectName("desktopLoginProvider");login->setWordWrap(true);login->setTextFormat(Qt::PlainText);layout->addWidget(login);
    auto *query=new QProcess(page);
    QObject::connect(query,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),page,[=](int code,QProcess::ExitStatus result){
        const QString provider=QString::fromUtf8(query->readAllStandardOutput()).trimmed();
        if(code||result!=QProcess::NormalExit||provider.isEmpty())login->setText("登录桌面配置暂时无法读取；试运行不会修改它。");
        else if(provider=="'peony-desktop'")login->setText("登录配置：仍由原生 Peony 负责桌面启动。Fences 使用自启动入口，登录时可能先看到原生桌面。直进 Fences 需要正式会话接管与故障回退，当前尚未启用。");
        else login->setText("当前必需桌面组件："+provider+"。是否直进 Fences 需结合该启动项检查。");
    });
    QObject::connect(query,&QProcess::errorOccurred,page,[=](QProcess::ProcessError){login->setText("登录桌面配置暂时无法读取。");});
    query->start("/usr/bin/gsettings",{"get","org.ukui.session.required-components","filemanager"});
    QTimer::singleShot(3000,query,[query]{if(query->state()!=QProcess::NotRunning)query->kill();});
    auto *status=new QLabel("当前：正常使用模式，尚未开始限时体验",page);status->setObjectName("desktopSessionStatus");status->setWordWrap(true);status->setTextFormat(Qt::PlainText);layout->addWidget(status);
    auto *trial=new QPushButton("体验 Fences 单独桌面 · 60 秒",page);trial->setObjectName("desktopSessionTrial");layout->addWidget(trial);
    auto *restore=new QPushButton("立即恢复原生桌面",page);restore->setObjectName("desktopSessionRestore");layout->addWidget(restore);
    const QString installed=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../libexec/ukui-liquid-desktop/session_bridge.py");
    const QString helper=QFileInfo::exists(installed)?installed:QStringLiteral(UKUI_LIQUID_SESSION_HELPER_PATH);
    const QString directory=qEnvironmentVariable("XDG_STATE_HOME",QDir::homePath()+"/.local/state")+"/ukui-liquid-desktop";
    const QString path=directory+"/desktop-trial-"+QString::fromLatin1(QCryptographicHash::hash(qgetenv("DISPLAY"),QCryptographicHash::Sha256).toHex().left(12))+".json";
    auto refresh=[=]{
        if(!QFileInfo::exists(helper)){status->setText("接管工具未安装，请先更新 Fences。");trial->setEnabled(false);restore->setEnabled(false);return;}
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return;const auto record=QJsonDocument::fromJson(file.readAll()).object();
        const QString phase=record.value("phase").toString();const int pid=record.value("guardPid").toInt();const bool alive=pid>0&&QFileInfo::exists("/proc/"+QString::number(pid)+"/stat");
        const bool active=record.value("active").toBool()&&alive;
        if(active)status->setText(QString("正在由 Fences 单独显示桌面，约 %1 秒后自动恢复。").arg(qMax(0,qRound(record.value("deadline").toDouble()-QDateTime::currentMSecsSinceEpoch()/1000.))));
        else if(phase=="preparing"&&alive)status->setText("正在检查并切换桌面…");
        else if(phase=="restoring"&&alive)status->setText("正在恢复原生桌面…");
        else if(!record.value("error").toString().isEmpty())status->setText(record.value("error").toString());
        else if(phase=="restored")status->setText("已恢复试运行前的桌面运行方式，Fences 可以继续使用。");
        else if(record.value("active").toBool())status->setText("试运行已中断，可以点击恢复。");
        trial->setEnabled(!active&&!(alive&&(phase=="preparing"||phase=="restoring")));
    };
    auto launch=[=](const QStringList &arguments){
        QDir().mkpath(directory);QProcess process;process.setProgram("/usr/bin/python3");process.setArguments(QStringList{helper}+arguments);process.setProcessChannelMode(QProcess::MergedChannels);process.setStandardOutputFile(directory+"/desktop-trial.log",QIODevice::Append);
        if(!process.startDetached())status->setText("无法启动接管工具，请检查安装。");else status->setText("正在处理…");
    };
    QObject::connect(trial,&QPushButton::clicked,page,[=]{canvas->showAndActivate();launch({"--trial","60"});});
    QObject::connect(restore,&QPushButton::clicked,page,[=]{launch({"--restore"});});
    auto *poll=new QTimer(page);poll->setInterval(1000);QObject::connect(poll,&QTimer::timeout,page,[=]{if(page->isVisible()&&!page->window()->isMinimized())refresh();});poll->start();refresh();
    return page;
}
