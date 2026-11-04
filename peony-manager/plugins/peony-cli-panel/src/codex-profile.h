#pragma once

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryFile>
#include <cerrno>
#include <unistd.h>

// A named Codex profile is also the target of /model's "default" action.
// Do not pin -m/-c at launch: those would override later choices in the CLI.
static QString peonyCodexProfilePath() {
    const QString configured = QString::fromLocal8Bit(qgetenv("CODEX_HOME"));
    return QDir(configured.isEmpty() ? QDir::homePath() + "/.codex" : configured)
        .filePath("peony-cli.config.toml");
}

static bool ensurePeonyCodexProfile(const QJsonObject &validated, QString *error,
                                   const QString &path = peonyCodexProfilePath()) {
    if (!QFileInfo(path).isAbsolute()) {
        *error = "CODEX_HOME 需要使用绝对路径。";
        return false;
    }
    if (QFileInfo::exists(path)) {
        if (QFileInfo(path).isFile() && QFileInfo(path).isReadable()) return true;
        *error = "CLI 独立模型配置不可读取：" + path;
        return false;
    }
    const QString directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) {
        *error = "无法创建 CLI 独立模型配置目录。";
        return false;
    }
    QTemporaryFile temporary(directory + "/.peony-profile-XXXXXX");
    if (!temporary.open()) { *error = temporary.errorString(); return false; }
    QByteArray content("# Peony CLI defaults; /model saves choices here, independently of the desktop.\n");
    // Values have already passed managedLaunchArguments' strict identifier checks.
    const QString model = validated.value("model").toString();
    const QString effort = validated.value("reasoningEffort").toString();
    if (!model.isEmpty()) content += "model = \"" + model.toUtf8() + "\"\n";
    if (!effort.isEmpty()) content += "model_reasoning_effort = \"" + effort.toUtf8() + "\"\n";
    if (temporary.write(content) != content.size() || !temporary.flush()
        || ::fsync(temporary.handle()) != 0) {
        *error = "无法保存 CLI 独立模型配置。";
        return false;
    }
    // Publish without replacing: two simultaneous windows must never reset an
    // existing /model selection. The temporary file is private (0600).
    if (::link(QFile::encodeName(temporary.fileName()).constData(), QFile::encodeName(path).constData()) == 0)
        return true;
    if (errno == EEXIST && QFileInfo(path).isFile() && QFileInfo(path).isReadable()) return true;
    *error = "无法创建 CLI 独立模型配置：" + path;
    return false;
}
