#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QProcessEnvironment>
#include <QTimer>
#include <qtermwidget.h>
#include "../plugins/peony-cli-panel/src/terminal-appearance.h"

static void require(bool condition, const char *message) {
    if (!condition) qFatal("FAIL: %s", message);
}

static QColor schemeColor(const QByteArray &scheme, const QByteArray &group) {
    const QByteArray key = "[" + group + "]\nColor=";
    const int start = scheme.indexOf(key);
    require(start >= 0, "colorscheme section exists");
    const int valueStart = start + key.size();
    const int valueEnd = scheme.indexOf('\n', valueStart);
    const QList<QByteArray> channels = scheme.mid(valueStart, valueEnd - valueStart).split(',');
    require(channels.size() == 3, "colorscheme has an RGB triple");
    return QColor(channels.at(0).toInt(), channels.at(1).toInt(), channels.at(2).toInt());
}

static void verifyScheme(const QPalette &palette, const QColor &expectedForeground, const QColor &expectedBackground) {
    QByteArray bytes;
    QBuffer output(&bytes);
    require(output.open(QIODevice::WriteOnly), "colorscheme buffer opens");
    require(PeonyCliAppearance::writeScheme(output, palette), "colorscheme is written");
    output.close();

    require(schemeColor(bytes, "Foreground") == expectedForeground, "default foreground follows palette");
    require(schemeColor(bytes, "Background") == expectedBackground, "default background follows palette");
    require(schemeColor(bytes, "Color0") == QColor("#202124"), "explicit ANSI black remains unchanged");
    require(schemeColor(bytes, "Color0Intense") == QColor("#202124"), "explicit ANSI dark gray remains unchanged");
}

static QByteArray expectedReply(int slot, const QColor &color) {
    const auto component = [](int value) {
        return QByteArray::number(value * 257, 16).rightJustified(4, '0');
    };
    QByteArray reply("\x1b]");
    reply += QByteArray::number(slot) + ";rgb:";
    reply += component(color.red()) + "/" + component(color.green()) + "/" + component(color.blue());
    reply += "\x1b\\";
    return reply;
}

static void verifyPtyProbe(QApplication &app, const QString &fixture, const QString &workDirectory,
                           const QString &name, const QPalette &palette) {
    const QColor foreground = palette.color(QPalette::Active, QPalette::Text);
    const QColor background = palette.color(QPalette::Active, QPalette::Base);
    const QString schemePath = workDirectory + "/" + name + ".colorscheme";
    QFile schemeFile(schemePath);
    require(schemeFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "PTY colorscheme opens");
    require(PeonyCliAppearance::writeScheme(schemeFile, palette), "PTY colorscheme is written");
    schemeFile.close();

    const QString markerPath = workDirectory + "/" + name + ".json";
    QFile::remove(markerPath);
    QFile::remove(markerPath + ".error");

    QTermWidget terminal(0);
    terminal.setAutoClose(true);
    terminal.setScrollBarPosition(QTermWidget::NoScrollBar);
    terminal.setColorScheme(schemePath);
    terminal.setKeyBindings(QApplication::applicationDirPath() + "/peony-cli-panel/data/kb-layouts/default");
    terminal.setPalette(palette);
    terminal.setShellProgram("/usr/bin/python3");
    terminal.setArgs({fixture, name, markerPath});
    terminal.setWorkingDirectory(workDirectory);
    terminal.resize(900, 420);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("TERM", "xterm-256color");
    environment.insert("COLORTERM", "truecolor");
    environment.insert("COLORFGBG", background.lightness() < 128 ? "15;0" : "0;15");
    terminal.setEnvironment(environment.toStringList());

    PeonyCliAppearance::OscColorQueryResponder responder;
    bool previewSaved = false;
    const QString previewPath = workDirectory + "/" + name + "-synthetic-preview.png";
    QFile::remove(previewPath);
    QObject::connect(&terminal, &QTermWidget::receivedData, &terminal,
        [&terminal, &responder, &previewSaved, previewPath](const QString &chunk) {
            const QPalette current = terminal.palette();
            const auto replies = responder.consume(
                chunk.toLatin1(),
                current.color(QPalette::Active, QPalette::Text),
                current.color(QPalette::Active, QPalette::Base));
            for (const auto &reply : replies)
                terminal.sendText(QString::fromLatin1(reply.constData(), reply.size()));
            if (!previewSaved && chunk.contains(QStringLiteral("PREVIEW-READY"))) {
                QTimer::singleShot(80, &terminal, [&terminal, &previewSaved, previewPath]() {
                    previewSaved = terminal.grab().save(previewPath);
                });
            }
        });

    QEventLoop loop;
    bool finished = false;
    bool timedOut = false;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&terminal, &QTermWidget::finished, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });
    terminal.show();
    terminal.startShellProgram();
    timeout.start(8000);
    loop.exec();
    timeout.stop();
    require(!timedOut && finished, "private PTY fixture exits before timeout");
    require(previewSaved && QFileInfo(previewPath).size() > 0, "synthetic ANSI preview image is captured");

    QFile error(markerPath + ".error");
    if (error.open(QIODevice::ReadOnly)) qFatal("PTY fixture failed: %s", error.readAll().constData());
    QFile marker(markerPath);
    require(marker.open(QIODevice::ReadOnly), "PTY fixture records query replies");
    const QJsonObject record = QJsonDocument::fromJson(marker.readAll()).object();
    const QJsonArray actual = record.value("replies").toArray();
    const QList<QByteArray> expected{
        expectedReply(10, foreground), expectedReply(11, background),
        expectedReply(10, foreground), expectedReply(11, background)
    };
    require(actual.size() == expected.size(), "only four exact color queries receive replies");
    for (int i = 0; i < expected.size(); ++i)
        require(actual.at(i).toString().toLatin1() == expected.at(i).toHex(), "PTY reply matches current palette and OSC slot");
    require(record.value("extra").toString().isEmpty(), "unrelated OSC sequences receive no reply");
    qInfo("PASS: %s OSC 10/11 replies match the terminal palette over the real QTermWidget PTY; synthetic preview saved", qPrintable(name));
    terminal.hide();
    app.processEvents();
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    require(argc == 3, "fixture and artifact directory arguments");
    const QString fixture = QString::fromLocal8Bit(argv[1]);
    const QString workDirectory = QString::fromLocal8Bit(argv[2]);

    QPalette light;
    light.setColor(QPalette::Active, QPalette::Base, QColor("#ffffff"));
    light.setColor(QPalette::Active, QPalette::Text, QColor("#202124"));
    verifyScheme(light, QColor("#202124"), QColor("#ffffff"));
    verifyPtyProbe(app, fixture, workDirectory, "light", light);

    QPalette dark;
    dark.setColor(QPalette::Active, QPalette::Base, QColor("#23272f"));
    dark.setColor(QPalette::Active, QPalette::Text, QColor("#ecedf0"));
    verifyScheme(dark, QColor("#ecedf0"), QColor("#23272f"));
    verifyPtyProbe(app, fixture, workDirectory, "dark", dark);
    return 0;
}
