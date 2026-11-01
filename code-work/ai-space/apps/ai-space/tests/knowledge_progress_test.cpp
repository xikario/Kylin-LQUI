#include "SmartSpaceWidget.h"
#include <QApplication>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QTemporaryDir>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

class SmartSpaceStartupTestAccess
{
  public:
    static void run()
    {
        SmartSpaceWidget widget;
        widget.m_libraryJobBusy = true;
        widget.m_knowledgeProcess = new QProcess(&widget);
        widget.setProperty("knowledgeStage", "ocr");
        auto send = [&](const QJsonObject &event) {
            widget.m_knowledgeOutputBuffer +=
                QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
            widget.handleKnowledgeOutput();
        };
        send({{"task", "build"},
              {"processed", 80},
              {"total", 100},
              {"currentPage", 9},
              {"path", "/data/scan.pdf"}});
        require(widget.m_indexProgress->value() == 80, "list progress is retained");
        require(widget.m_statusLabel->text().contains(QStringLiteral("正在识别第 9 页")),
                "inflight OCR page is visible");
        send({{"task", "build"}, {"phase", "extraction-error"}, {"error", "failed page"}});
        require(widget.m_indexProgress->value() == 80,
                "partial errors cannot reset progress to zero");
        require(widget.m_knowledgeLastObject.value("path").toString() == "/data/scan.pdf",
                "partial events retain file context");
        send({{"task", "build"}, {"processed", 80}, {"total", 100}, {"pages", 9}});
        require(widget.m_statusLabel->text().contains(QStringLiteral("已处理 9 页")),
                "finished page is distinguished from an inflight page");
        send({{"task", "build"}, {"processed", 81}, {"total", 100}, {"path", "/data/next.png"}});
        require(!widget.m_knowledgeLastObject.contains("pages") &&
                    !widget.m_knowledgeLastObject.contains("currentPage"),
                "a new file clears old page counters");
        send({{"done", true}, {"paused", true}, {"error", "内存不足，任务已暂停"}});
        widget.finishKnowledgeTask(1, QProcess::NormalExit);
        require(widget.property("knowledgeJobOutcome").toString() == QStringLiteral("已暂停"),
                "resource pauses are reported as pauses");
        widget.m_libraryJobBusy = true;
        widget.m_knowledgeProcess = new QProcess(&widget);
        send({{"done", true}, {"error", "protocol error"}});
        widget.finishKnowledgeTask(0, QProcess::NormalExit);
        require(widget.property("knowledgeJobOutcome").toString() != QStringLiteral("已完成"),
                "an error payload cannot be reported as success even with exit code zero");
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir home;
    require(home.isValid(), "temporary home");
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (home.path() + "/cache").toUtf8());
    QApplication app(argc, argv);
    app.setOrganizationName("xikario");
    app.setApplicationName("ai-space");
    SmartSpaceStartupTestAccess::run();
    std::cout << "Knowledge progress and pause outcomes passed" << std::endl;
}
