#pragma once
#include <QString>
#include <QJsonObject>

struct Settings {
    bool residentOnLogin=true;
    bool followLauncherMaterial=true;
    QString translationLanguage=QStringLiteral("简体中文");
    int width=460, height=720, animationMs=200, liquid=100, darken=26, hideDelay=700, fontSize=11, contextLimitK=256, idleStopMinutes=10;
    // animation: float (short vertical glide + fade), slide (full-height), none.
    // idleStopMinutes: stop Codex after this long without activity (0 = keep running).
    // permissions: config (follow ~/.codex/config.toml), ask, workspace, full.
    QString animation="float", directory, command, cliName="Codex", permissions="config";
    QString backend="codex",model,reasoningEffort,sessionMode;
    QString defaultReplyMode="normal"; // Codex startup mode; quick uses a separate conversation.
    QString quickModel="gpt-6-luna",quickReasoningEffort="low";
    bool normalFast=false,quickFast=false;
    QString serviceTierForTurn; // Runtime override only; never changes global CLI settings.
    QJsonObject backendProfiles; // Each backend keeps its command and model selection.
    static QString backendName(const QString &id);
    static QString defaultCommand(const QString &id);
    void selectBackend(const QString &id);
    QString file;
    static Settings load(const QString &file={});
    bool save(QString *error=nullptr) const;
    void normalize();
    QJsonObject json() const;
};
