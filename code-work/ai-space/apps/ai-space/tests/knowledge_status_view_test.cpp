#include "KnowledgeStatusView.h"
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    for (bool light : {true, false}) {
        KnowledgeStatusView view(light);
        view.resize(810, 680);
        view.show();
        auto text = [&](const char *key) {
            return view.findChild<QLabel *>(QStringLiteral("knowledgeStatus_") + key)->text();
        };
        KnowledgeStatusContext context;
        context.reserveGiB = 5;
        context.quotaGiB = 20;
        context.cpuPercent = 30;
        context.memoryMiB = 1024;
        context.currentDatabase = QStringLiteral("/data/knowledge/library.sqlite");
        context.newDirectory = QStringLiteral("/data/knowledge");
        view.updateStatus({}, context);
        require(text("documents") == QStringLiteral("—"),
                "loading must not claim an empty library");
        require(text("ocrPending") == QStringLiteral("待统计"), "unknown OCR is not zero");
        require(!view.ocrButton()->isEnabled(), "OCR waits for a loaded personal library");
        QJsonObject snapshot{
            {"path", "/data/knowledge/library.sqlite"},
            {"manifest", QJsonObject{{"format", "legacy-sqlite"}}},
            {"documents", 48182},
            {"chunks", 970188},
            {"dbBytes", 9824734412.0},
            {"failurePending", 157},
            {"ignoredDocuments", 12},
            {"retainedAbsent", 3},
            {"lastBuild", "2026-10-17T16:02:10+08:00"},
            {"extractionStates", QJsonObject{{"complete", 29867},
                                             {"partial", 1551},
                                             {"no_text", 16607},
                                             {"failed", 157},
                                             {"missing", 0}}},
            {"ocrPending", QJsonObject{{"complete", true},
                                       {"documents", 129},
                                       {"pdfPages", 27},
                                       {"images", 105},
                                       {"emptyOcrPages", 737},
                                       {"imagesWithoutText", 16538}}},
            {"failures",
             QJsonArray{QJsonObject{{"name", "示例材料.xlsx"},
                                    {"reason", "工作簿已加密；请解密后重试，已有正文保留。"}}}}};
        context.summary = QStringLiteral("增量更新已完成 · 复用未变化的资料");
        view.updateStatus(snapshot, context);
        app.processEvents();
        require(view.ocrButton()->isEnabled(), "pending personal OCR can be started");
        const auto pendingOcr = snapshot.value("ocrPending");
        snapshot["ocrPending"] = QJsonObject{{"complete", true}, {"documents", 0}};
        view.updateStatus(snapshot, context);
        require(!view.ocrButton()->isEnabled(), "known empty OCR work cannot be started");
        snapshot["ocrPending"] = pendingOcr;
        context.ocrActionBlocked = true;
        view.updateStatus(snapshot, context);
        require(!view.ocrButton()->isEnabled(), "other workers prevent duplicate OCR starts");
        context.ocrActionBlocked = false;
        view.updateStatus(snapshot, context);
        require(text("documents") == "48,182",
                "large counts are readable without losing precision");
        require(text("ocrUnits").contains(QStringLiteral("27 页")) &&
                    text("ocrUnits").contains(QStringLiteral("105 张")),
                "PDF pages and images have distinct units");
        auto *examples = view.findChild<QLabel *>("knowledgeStatus_failures");
        require(!examples->isVisible(), "failure examples stay collapsed after live refresh");
        for (auto *button : view.findChildren<QToolButton *>()) {
            if (button->text().startsWith(QStringLiteral("查看失败示例")))
                button->click();
        }
        require(examples->isVisible(), "failure examples can be opened");
        view.updateStatus(snapshot, context);
        require(examples->isVisible(), "live refresh preserves expansion state");
        for (auto *button : view.findChildren<QToolButton *>())
            if (button->isChecked())
                button->click();
        const auto artifacts = qEnvironmentVariable("AI_SPACE_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            app.processEvents();
            require(
                view.grab().save(artifacts + (light ? "/status-light.png" : "/status-dark.png")),
                "save synthetic preview");
        }
        snapshot["ocrPending"] = QJsonObject{{"complete", false}, {"documents", 0}};
        view.updateStatus(snapshot, context);
        require(text("ocrPending") == QStringLiteral("待统计"),
                "legacy incomplete counters are unknown");
        require(view.ocrButton()->isEnabled(),
                "legacy unknown counters do not prevent explicit OCR");
        context.busy = true;
        context.stage = "text";
        context.job = QJsonObject{{"processed", 500},
                                  {"total", 1000},
                                  {"reused", 450},
                                  {"updated", 50},
                                  {"path", QString(500, QLatin1Char('x'))}};
        view.updateStatus(snapshot, context);
        require(!view.ocrButton()->isEnabled(), "active indexing prevents overlapping OCR");
        context.job["currentPage"] = 9;
        view.updateStatus(snapshot, context);
        require(text("taskProgress").contains(QStringLiteral("正在识别第 9 页")),
                "current page is visible without opening task details");
        context.job["pages"] = 9;
        view.updateStatus(snapshot, context);
        require(text("taskProgress").contains(QStringLiteral("已处理 9 页")),
                "completed pages are distinct from in-flight pages");
        require(view.findChild<QProgressBar *>()->value() == 500,
                "live task progress is separate from OCR pending");
        require(text("taskCounts").contains(QStringLiteral("复用 450")),
                "incremental reuse remains visible");
        context.message = QStringLiteral("刷新失败，保留上次统计快照");
        view.updateStatus(snapshot, context);
        require(text("documents") == "48,182" && !text("message").isEmpty(),
                "refresh failure keeps prior counts and a warning");
        for (auto *button : view.findChildren<QToolButton *>())
            button->setChecked(true);
        view.resize(620, 430);
        app.processEvents();
        auto *scroll = view.findChild<QScrollArea *>();
        require(scroll->horizontalScrollBar()->maximum() == 0,
                "small screens and long paths do not overflow horizontally");
        require(scroll->verticalScrollBar()->maximum() > 0,
                "small screens retain access to lower sections");
    }
    std::cout << "Knowledge status cards passed" << std::endl;
}
