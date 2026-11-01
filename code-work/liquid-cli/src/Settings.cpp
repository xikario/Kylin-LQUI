#include "Settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

Settings Settings::load(const QString &path) {
    Settings s;
    s.file=path.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/liquid-cli/settings.json":path;
    s.directory=QDir::homePath();
    s.command="codex";
    // Read only the user's existing launcher. Never change Peony configuration.
    QFile peony(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/peony-manager/cli-profiles.json");
    if(peony.open(QIODevice::ReadOnly)) {
        const auto p=QJsonDocument::fromJson(peony.readAll()).object();
        s.command=p.value("command").toString(s.command);
    }
    QFile f(s.file);
    if(f.open(QIODevice::ReadOnly)) {
        const auto o=QJsonDocument::fromJson(f.readAll()).object();
        s.width=o.value("width").toInt(s.width);s.height=o.value("height").toInt(s.height);
        s.animationMs=o.value("animationMs").toInt(s.animationMs);s.animation=o.value("animation").toString(s.animation);
        s.liquid=o.value("liquid").toInt(s.liquid);s.darken=o.value("darken").toInt(s.darken);
        s.hideDelay=o.value("hideDelay").toInt(s.hideDelay);s.fontSize=o.value("fontSize").toInt(s.fontSize);
        s.directory=o.value("directory").toString(s.directory);s.command=o.value("command").toString(s.command);
        s.permissions=o.value("permissions").toString(s.permissions);s.contextLimitK=o.value("contextLimitK").toInt(s.contextLimitK);
        s.idleStopMinutes=o.value("idleStopMinutes").toInt(s.idleStopMinutes);
        s.followLauncherMaterial=o.value("followLauncherMaterial").toBool(true);
        s.residentOnLogin=o.value("residentOnLogin").toBool(s.residentOnLogin);
        s.translationLanguage=o.value("translationLanguage").toString(s.translationLanguage);
        s.model=o.value("model").toString();s.reasoningEffort=o.value("reasoningEffort").toString();
        s.backend=o.value("backend").toString("codex");s.sessionMode=o.value("sessionMode").toString();
        s.backendProfiles=o.value("backendProfiles").toObject();
    }
    s.normalize();return s;
}
void Settings::normalize() {
    if(backend!="qwen"&&backend!="deepseek")backend="codex";
    cliName=backendName(backend);
    width=qBound(360,width,1000);height=qBound(420,height,1400);animationMs=qBound(80,animationMs,500);
    liquid=qBound(0,liquid,100);darken=qBound(0,darken,65);hideDelay=qBound(200,hideDelay,3000);fontSize=qBound(8,fontSize,20);
    contextLimitK=qBound(32,contextLimitK,765);idleStopMinutes=qBound(0,idleStopMinutes,240);
    // 0.1.0 terminal drawer values migrate to the closest new motion.
    if(animation=="spring"||animation=="smooth")animation="float";
    if(animation!="none"&&animation!="float"&&animation!="slide")animation="float";
    if(permissions!="config"&&permissions!="ask"&&permissions!="workspace"&&permissions!="full")permissions="config";
    if(!QDir(directory).exists())directory=QDir::homePath();
    if(command.trimmed().isEmpty()||command.contains(QChar(0)))command=defaultCommand(backend);
    translationLanguage=translationLanguage.trimmed().left(80);if(translationLanguage.isEmpty())translationLanguage="简体中文";
    model=model.trimmed();reasoningEffort=reasoningEffort.trimmed();sessionMode=sessionMode.trimmed();
}
QString Settings::backendName(const QString &id) {return id=="qwen"?QString("千问 Qwen Code"):id=="deepseek"?QString("DeepSeek Harness"):QString("Codex");}
QString Settings::defaultCommand(const QString &id) {return id=="qwen"?QString("qwen"):id=="deepseek"?QString("dsh"):QString("codex");}
void Settings::selectBackend(const QString &id) {
    if(id==backend)return;
    backendProfiles[backend]=QJsonObject{{"command",command},{"directory",directory},{"model",model},{"reasoningEffort",reasoningEffort},{"sessionMode",sessionMode},{"permissions",permissions}};
    backend=id;const auto p=backendProfiles.value(id).toObject();
    command=p.value("command").toString(defaultCommand(id));directory=p.value("directory").toString(directory);model=p.value("model").toString();
    reasoningEffort=p.value("reasoningEffort").toString();sessionMode=p.value("sessionMode").toString();permissions=p.value("permissions").toString("config");normalize();
}
QJsonObject Settings::json() const {
    auto profiles=backendProfiles;profiles[backend]=QJsonObject{{"command",command},{"directory",directory},{"model",model},{"reasoningEffort",reasoningEffort},{"sessionMode",sessionMode},{"permissions",permissions}};
    return {{"version",3},{"followLauncherMaterial",followLauncherMaterial},{"translationLanguage",translationLanguage},{"residentOnLogin",residentOnLogin},{"backend",backend},{"backendProfiles",profiles},{"sessionMode",sessionMode},{"width",width},{"height",height},{"animation",animation},{"animationMs",animationMs},
        {"liquid",liquid},{"darken",darken},{"hideDelay",hideDelay},{"fontSize",fontSize},{"directory",directory},
        {"command",command},{"permissions",permissions},{"contextLimitK",contextLimitK},{"idleStopMinutes",idleStopMinutes},
        {"model",model},{"reasoningEffort",reasoningEffort}};
}
bool Settings::save(QString *error) const {
    QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile f(file);
    if(!f.open(QIODevice::WriteOnly)){if(error)*error=f.errorString();return false;}
    f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    if(f.write(QJsonDocument(json()).toJson())<0||!f.commit()){if(error)*error=f.errorString();return false;}
    return true;
}
