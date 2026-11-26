#include "DshCredentials.h"
#include <QApplication>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Resolve packages from the Harness installation, never from the chat workspace.
QString runtimeDirectory() {
    const QString managed=QDir::homePath()+"/.local/lib/liquid-cli/dsh-runtime";
    const auto cli=QStandardPaths::findExecutable("dsh");
    const auto npmPackage=QFileInfo(cli).canonicalPath()+"/..";
    for(const auto &dir:{managed,npmPackage}) {
        if(QFileInfo::exists(dir+"/node_modules/@deepseek-ai/dsh-credentials-local/package.json") ||
           QFileInfo::exists(dir+"/../dsh-credentials-local/package.json"))
            return QDir(dir).canonicalPath();
    }
    return {};
}
QString feedback(const QString &result) {
    if(result=="key-only")return "密钥已写入，ACP 入口保存失败。请检查配置文件权限后重试。";
    if(result=="balance")return "DeepSeek 官方账户余额不足（402）。";
    if(result=="wrong-key")return "这是阿里云 Token Plan 的 Key，请在阿里云入口使用。";
    if(result=="choose-model")return "请先选择模型。";
    if(result=="empty-list")return "接口没有返回可用的对话模型。";
    if(result=="empty")return "尚未配置密钥。";
    if(result=="empty-input")return "请先填写 API Key。";
    if(result=="invalid")return "密钥中包含空格或换行，请重新粘贴。";
    if(result=="saved")return "密钥已保存到 DeepSeek Harness。";
    if(result=="ok")return "测试成功，所选模型已接受请求。";
    if(result=="auth")return "认证失败（401），请检查 Key 和套餐入口是否匹配。";
    if(result=="denied")return "调用被拒绝（403），请核对套餐与模型权限。";
    if(result=="quota")return "当前套餐额度已用尽（429 insufficient_quota）。";
    if(result=="rate")return "请求受限（429），请稍后再试。";
    if(result=="model")return "模型或接口不存在（404）。";
    if(result=="timeout")return "连接超时，请稍后重试。";
    if(result.startsWith("http-"))return "请求失败（HTTP "+result.mid(5).left(3)+"）。";
    return "操作失败，请检查 Harness 安装、凭据文件权限或网络连接。";
}
}
void showDshCredentials(QWidget *owner,DshService service,const std::function<bool(const QString &)> &onSaved) {
    const bool official=service==DshService::Official;
    const QString dialogId=official?"dshOfficialCredentials":"dshAliCredentials";
    for(auto *open:QApplication::topLevelWidgets())
        if(open->objectName()==dialogId){open->show();open->raise();open->activateWindow();return;}
    // Keep the secret editor out of the settings bridge's exported widget tree.
    auto *dialog=new QDialog(nullptr,Qt::Dialog|Qt::WindowStaysOnTopHint);
    QObject::connect(owner,&QObject::destroyed,dialog,&QObject::deleteLater);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setObjectName(dialogId);dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(official?"DeepSeek Harness · DeepSeek 官方 API":"DeepSeek Harness · 阿里云 API Key");
    dialog->setMinimumWidth(450);
    auto *layout=new QVBoxLayout(dialog);
    auto *form=new QFormLayout;layout->addLayout(form);
    auto *route=new QComboBox(dialog);route->setObjectName("dshAliRoute");
    if(official)route->addItem("DeepSeek 官方","official");
    else {route->addItem("阿里云 Token Plan","plan");route->addItem("百炼通用 API（北京，按量计费）","general");}
    form->addRow("服务入口",route);
    auto *endpoint=new QLabel(dialog);
    endpoint->setTextInteractionFlags(Qt::TextSelectableByMouse);endpoint->setWordWrap(true);form->addRow("Base URL",endpoint);
    auto *key=new QLineEdit(dialog);key->setObjectName(official?"dshOfficialKey":"dshAliKey");key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText("粘贴新 Key，留空使用此入口已保存的 Key");key->setMaxLength(4096);form->addRow("API Key",key);
    auto *modelRow=new QWidget(dialog);auto *row=new QHBoxLayout(modelRow);row->setContentsMargins(0,0,0,0);
    auto *model=new QComboBox(modelRow);model->setObjectName("dshModel");model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto *fetchModels=new QPushButton("获取模型",modelRow);fetchModels->setObjectName("fetchDshModels");
    row->addWidget(model,1);row->addWidget(fetchModels);form->addRow("模型",modelRow);
    auto *message=new QLabel(dialog);message->setObjectName("dshAliStatus");message->setWordWrap(true);message->setTextFormat(Qt::PlainText);layout->addWidget(message);
    auto *buttons=new QDialogButtonBox(dialog);auto *close=buttons->addButton("关闭",QDialogButtonBox::RejectRole);
    auto *save=buttons->addButton("保存配置",QDialogButtonBox::ActionRole);save->setObjectName("saveDshAliKey");
    auto *test=buttons->addButton("测试连接",QDialogButtonBox::ActionRole);test->setObjectName("testDshAliKey");
    test->setToolTip("使用所选模型发起一次简短调用。测试不会保存配置。");
    layout->addWidget(buttons);QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::reject);

    auto showRoute=[=]{
        const auto type=route->currentData().toString();
        const QString url=official?"https://api.deepseek.com":type=="plan"?"https://token-plan.cn-beijing.maas.aliyuncs.com/compatible-mode/v1":"https://dashscope.aliyuncs.com/compatible-mode/v1";
        endpoint->setText(url);
        auto info=route->currentData(Qt::UserRole+1).toJsonObject();
        const auto chosen=info.value("current").toString(official?"deepseek-flash":"deepseek-v4.1-flash");
        const QSignalBlocker block(model);model->clear();
        for(const auto &item:info.value("models").toArray())model->addItem(item.toString());
        if(model->count()==0)model->addItem(chosen);
        model->setCurrentIndex(qMax(0,model->findText(chosen)));
        message->setText(info.value("configured").toBool()?"此入口已有密钥，可直接获取模型。":"填写此服务的 API Key 后可获取模型。");
    };
    QObject::connect(route,qOverload<int>(&QComboBox::currentIndexChanged),dialog,[=](int){showRoute();});
    QObject::connect(route,qOverload<int>(&QComboBox::activated),dialog,[=](int){
        route->setProperty("chosenManually",true);
    });
    QObject::connect(key,&QLineEdit::textEdited,dialog,[=](const QString &value){
        if(official||route->property("chosenManually").toBool()||value.trimmed().size()<=12)return;
        if(value.trimmed().startsWith("sk-sp-"))route->setCurrentIndex(0);
        else if(value.trimmed().startsWith("sk-"))route->setCurrentIndex(1);
    });
    QObject::connect(model,qOverload<int>(&QComboBox::activated),dialog,[=](int){
        auto info=route->currentData(Qt::UserRole+1).toJsonObject();info["current"]=model->currentText();
        route->setItemData(route->currentIndex(),info,Qt::UserRole+1);
    });
    showRoute();
    const QString runtime=runtimeDirectory(),bundledNode=runtime+"/node_modules/node/bin/node";
    const QString node=QFileInfo(bundledNode).isExecutable()?bundledNode:QStandardPaths::findExecutable("node");
    QFile resource(":/dsh/aliyun.mjs");resource.open(QIODevice::ReadOnly);
    const QString script=QString::fromUtf8(resource.readAll());
    auto *job=new QProcess(dialog);job->setWorkingDirectory(runtime);job->setStandardErrorFile(QProcess::nullDevice());
    auto *timer=new QTimer(job);timer->setSingleShot(true);QObject::connect(timer,&QTimer::timeout,job,&QProcess::kill);
    auto enable=[=](bool value){
        for(QWidget *w:QList<QWidget*>{key,model,save,test,fetchModels})w->setEnabled(value);
        route->setEnabled(value&&!official);
    };
    auto finish=[=](const QJsonObject &reply){
        timer->stop();enable(true);
        const auto result=reply.value("state").toString();
        if(result=="status"){
            const auto options=reply.value("options").toObject();
            for(int i=0;i<route->count();++i)route->setItemData(i,options.value(route->itemData(i).toString()).toObject(),Qt::UserRole+1);
            route->setCurrentIndex(qMax(0,route->findData(reply.value("active").toString())));showRoute();key->setFocus();return;
        }
        auto info=route->currentData(Qt::UserRole+1).toJsonObject();
        if(result=="models"){
            info["current"]=model->currentText();info["models"]=reply.value("models");
            route->setItemData(route->currentIndex(),info,Qt::UserRole+1);showRoute();
            message->setText(QString("已获取 %1 个模型，选择后可测试或保存。").arg(model->count()));return;
        }
        if(result=="saved"){
            key->clear();info["configured"]=true;info["current"]=model->currentText();
            route->setItemData(route->currentIndex(),info,Qt::UserRole+1);
            if(!onSaved(reply.value("acpModel").toString())){
                message->setText("Harness 配置已写入，小站模型选择保存失败。请检查小站配置文件权限后重试。");return;
            }
            message->setText("已保存，Harness 将使用 "+route->currentText()+" · "+model->currentText());return;
        }
        message->setText(feedback(result));
    };
    QObject::connect(job,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),dialog,[=](int code,QProcess::ExitStatus status){
        const auto data=job->readAllStandardOutput();
        finish(code==0&&status==QProcess::NormalExit?QJsonDocument::fromJson(data).object():QJsonObject());
    });
    QObject::connect(job,&QProcess::errorOccurred,dialog,[=](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish({});});
    auto run=[=](const QString &action){
        if(job->state()!=QProcess::NotRunning)return;
        enable(false);
        message->setText(action=="models"?"正在获取模型…":action=="test"?"正在测试所选模型…":action=="save"?"正在保存…":"正在读取配置…");
        job->start(node,{"--input-type=module","--eval",script,action,route->currentData().toString(),model->currentText()});
        if(action!="status")job->write(key->text().trimmed().toUtf8());
        job->closeWriteChannel();timer->start(40000);
    };
    QObject::connect(fetchModels,&QPushButton::clicked,dialog,[=]{run("models");});
    QObject::connect(save,&QPushButton::clicked,dialog,[=]{run("save");});
    QObject::connect(test,&QPushButton::clicked,dialog,[=]{run("test");});
    dialog->show();dialog->raise();dialog->activateWindow();
    if(runtime.isEmpty()||node.isEmpty()||script.isEmpty()){
        enable(false);message->setText("未找到 DeepSeek Harness 的凭据组件。");return;
    }
    run("status");
}
