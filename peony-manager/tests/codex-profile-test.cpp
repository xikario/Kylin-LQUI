#include "../plugins/peony-cli-panel/src/codex-profile.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>

static void require(bool condition, const char *message) {
    if (!condition) qFatal("%s", message);
}
static QByteArray read(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "read profile");
    return file.readAll();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "private fixture directory");
    const QString path = directory.path() + "/codex/peony-cli.config.toml";
    QString error;
    QJsonObject settings{{"model", "gpt-6-luna"}, {"reasoningEffort", "xhigh"}};
    require(ensurePeonyCodexProfile(settings, &error, path), "create profile");
    const auto seeded = read(path);
    require(seeded.contains("model = \"gpt-6-luna\"")
        && seeded.contains("model_reasoning_effort = \"xhigh\""), "migrate old defaults");
    require(!(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)), "private profile permissions");
    const QByteArray chosen("model = \"gpt-6.1-sol\"\nmodel_reasoning_effort = \"medium\"\n[tui]\nscreen_reader_detection_done = true\n");
    QFile file(path); require(file.open(QIODevice::WriteOnly), "simulate CLI choice");
    file.write(chosen); file.close();
    require(ensurePeonyCodexProfile(settings, &error, path), "reopen profile");
    require(read(path) == chosen, "never overwrite /model choice or other preferences with old JSON");
    const QString blank = directory.path() + "/fresh/peony-cli.config.toml";
    require(ensurePeonyCodexProfile({}, &error, blank), "create initially inherited profile");
    require(!read(blank).contains("model ="), "do not invent a model before the first selection");
    const QString collision = directory.path() + "/collision";
    require(QDir().mkpath(collision), "fixture directory collision");
    require(!ensurePeonyCodexProfile(settings, &error, collision), "refuse unusable profile rather than launching global defaults");
    require(!ensurePeonyCodexProfile(settings, &error, "relative/profile"), "reject ambiguous relative Codex home");
    qInfo("PASS: native profile creation, migration, privacy, preservation and failure handling");
}
