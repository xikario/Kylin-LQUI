#include "KnowledgeStatusView.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
QString number(double value)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(qint64(value));
}

QString stageName(const QString &stage)
{
    if (stage == QLatin1String("manifest"))
        return QStringLiteral("① 全量扫描");
    if (stage == QLatin1String("text"))
        return QStringLiteral("② 文本建库");
    if (stage == QLatin1String("ocr"))
        return QStringLiteral("③ OCR 补全");
    if (stage == QLatin1String("retry"))
        return QStringLiteral("失败项重试");
    return QStringLiteral("库文件操作");
}

QString dateText(const QString &text)
{
    const auto date = QDateTime::fromString(text, Qt::ISODate);
    return date.isValid() ? date.toLocalTime().toString("yyyy-MM-dd HH:mm") : text;
}
} // namespace

QLabel *KnowledgeStatusView::label(const QString &key, const QString &role)
{
    auto *result = new QLabel;
    result->setObjectName(QStringLiteral("knowledgeStatus_") + key);
    result->setProperty("role", role);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_labels.insert(key, result);
    return result;
}

QWidget *KnowledgeStatusView::card(const QString &title, QVBoxLayout **layout)
{
    auto *frame = new QFrame;
    frame->setProperty("statusCard", true);
    *layout = new QVBoxLayout(frame);
    (*layout)->setContentsMargins(18, 16, 18, 16);
    (*layout)->setSpacing(10);
    if (!title.isEmpty()) {
        auto *heading = new QLabel(title);
        heading->setProperty("role", "title");
        (*layout)->addWidget(heading);
    }
    return frame;
}

QWidget *KnowledgeStatusView::disclosure(const QString &title, QWidget *content)
{
    auto *container = new QWidget;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *button = new QToolButton;
    button->setText(title);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setArrowType(Qt::RightArrow);
    button->setCheckable(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *reveal = new QWidget;
    auto *revealLayout = new QVBoxLayout(reveal);
    revealLayout->setContentsMargins(0, 0, 0, 0);
    revealLayout->addWidget(content);
    reveal->hide();
    connect(button, &QToolButton::toggled, reveal, [button, reveal](bool expanded) {
        button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        reveal->setVisible(expanded);
    });
    layout->addWidget(button);
    layout->addWidget(reveal);
    return container;
}

KnowledgeStatusView::KnowledgeStatusView(bool light, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("knowledgeStatusDetails"));
    setStyleSheet(
        QStringLiteral(
            "QWidget#knowledgeStatusDetails{background:%1;color:%2;}"
            "QFrame[statusCard=true]{background:%3;border:1px solid %4;border-radius:12px;}"
            "QLabel{background:transparent;color:%2;border:0;}"
            "QLabel[role=secondary]{color:%5;font-size:12px;}"
            "QLabel[role=title]{font-size:14px;font-weight:600;}"
            "QLabel[role=metric]{font-size:28px;font-weight:700;}"
            "QLabel[role=accent]{font-size:28px;font-weight:700;color:%6;}"
            "QLabel[role=warning]{font-size:24px;font-weight:700;color:%7;}"
            "QLabel[role=badge]{color:%6;font-weight:600;font-size:13px;}"
            "QToolButton{border:0;background:transparent;color:%5;padding:6px 0;text-align:left;}"
            "QToolButton:hover{color:%6;}"
            "QProgressBar{border:0;border-radius:3px;background:%4;height:6px;}"
            "QProgressBar::chunk{background:%6;border-radius:3px;}")
            .arg(light ? "#f3f5fa" : "#141923", light ? "#192337" : "#edf1fa",
                 light ? "#ffffff" : "#1d2533", light ? "#e1e6ef" : "#303c50",
                 light ? "#526078" : "#a9b6cb", light ? "#6750c6" : "#b6a4ff",
                 light ? "#ad6510" : "#f3bf73"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        QStringLiteral("QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;}"));
    outer->addWidget(scroll);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(12);
    scroll->setWidget(content);
    layout->addWidget(label("message", "badge"));

    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    const QStringList keys{"documents", "chunks", "size"};
    const QStringList titles{QStringLiteral("入库文档 · 份"), QStringLiteral("正文片段 · 段"),
                             QStringLiteral("库文件大小 · GiB")};
    for (int i = 0; i < keys.size(); ++i) {
        QVBoxLayout *box;
        auto *tile = card(QString(), &box);
        box->addWidget(label(keys[i] + "Title"));
        setText(keys[i] + "Title", titles[i]);
        box->addWidget(label(keys[i], "metric"));
        metrics->addWidget(tile, 1);
    }
    layout->addLayout(metrics);

    QVBoxLayout *task;
    auto *taskCard = card(QString(), &task);
    auto *taskHeading = new QHBoxLayout;
    taskHeading->addWidget(label("taskTitle", "title"), 1);
    taskHeading->addWidget(label("taskBadge", "badge"), 1, Qt::AlignRight);
    setText("taskTitle", QStringLiteral("当前任务"));
    task->addLayout(taskHeading);
    task->addWidget(label("taskSummary"));
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    task->addWidget(m_progress);
    task->addWidget(label("taskProgress"));
    task->addWidget(label("taskCounts", "badge"));
    m_liveDetails = new QWidget;
    auto *details = new QVBoxLayout(m_liveDetails);
    details->setContentsMargins(0, 0, 0, 0);
    details->addWidget(label("taskPath"));
    details->addWidget(label("taskNote"));
    task->addWidget(disclosure(QStringLiteral("查看任务明细"), m_liveDetails));
    layout->addWidget(taskCard);

    auto *columns = new QHBoxLayout;
    columns->setSpacing(12);
    QVBoxLayout *ocr;
    auto *ocrCard = card(QStringLiteral("OCR 待办"), &ocr);
    ocr->addWidget(label("ocrPending", "accent"));
    ocr->addWidget(label("ocrUnits", "badge"));
    ocr->addWidget(label("ocrState"));
    ocr->addWidget(label("ocrNote"));
    ocr->addStretch();
    m_ocr = new QPushButton(QStringLiteral("开始处理"));
    m_ocr->setObjectName(QStringLiteral("knowledgeProcessOcr"));
    m_ocr->setShortcut(QKeySequence("Alt+O"));
    m_ocr->setEnabled(false);
    ocr->addWidget(m_ocr, 0, Qt::AlignLeft);
    columns->addWidget(ocrCard, 1);
    QVBoxLayout *quality;
    auto *qualityCard = card(QStringLiteral("正文提取情况"), &quality);
    const QStringList states{"complete", "partial", "no_text", "failed", "missing"};
    const QStringList names{QStringLiteral("完整提取"), QStringLiteral("部分提取"),
                            QStringLiteral("无正文"), QStringLiteral("提取失败"),
                            QStringLiteral("来源缺失")};
    for (int i = 0; i < states.size(); ++i) {
        auto *row = new QHBoxLayout;
        row->addWidget(label(states[i] + "Title"), 1);
        setText(states[i] + "Title", names[i]);
        row->addWidget(label(states[i], "title"), 1, Qt::AlignRight);
        quality->addLayout(row);
    }
    quality->addStretch();
    quality->addWidget(label("lastBuild"));
    columns->addWidget(qualityCard, 1);
    layout->addLayout(columns);

    QVBoxLayout *actions;
    auto *actionsCard = card(QStringLiteral("需要关注"), &actions);
    auto *failureRow = new QHBoxLayout;
    failureRow->addWidget(label("failurePending", "warning"), 1);
    m_failures = new QPushButton(QStringLiteral("处理失败 / 已忽略…"));
    m_failures->setObjectName(QStringLiteral("knowledgeManageFailures"));
    m_failures->setShortcut(QKeySequence("Alt+F"));
    m_failures->setToolTip(QStringLiteral("处理失败项与已忽略项（Alt+F）"));
    failureRow->addWidget(m_failures);
    actions->addLayout(failureRow);
    actions->addWidget(label("ignored"));
    actions->addWidget(disclosure(QStringLiteral("查看失败示例（最多 20 项）"), label("failures")));
    auto *retainedRow = new QHBoxLayout;
    retainedRow->addWidget(label("retained"), 1);
    m_cleanup = new QPushButton(QStringLiteral("清理缺失正文…"));
    m_cleanup->setObjectName(QStringLiteral("knowledgePruneAbsent"));
    m_cleanup->setShortcut(QKeySequence("Alt+D"));
    m_cleanup->setToolTip(QStringLiteral("按保留天数预览清理，不删除原文件（Alt+D）"));
    retainedRow->addWidget(m_cleanup);
    actions->addLayout(retainedRow);
    layout->addWidget(actionsCard);

    QVBoxLayout *storage;
    auto *storageCard = card(QString(), &storage);
    auto *storageDetails = new QWidget;
    auto *storageLayout = new QVBoxLayout(storageDetails);
    storageLayout->setContentsMargins(0, 0, 0, 0);
    storageLayout->setSpacing(8);
    for (const auto &key : {"database", "directory", "budget", "checkpoint", "ocrEmpty", "ocrEta"})
        storageLayout->addWidget(label(key));
    storage->addWidget(disclosure(QStringLiteral("存储位置、资源限制与统计说明"), storageDetails));
    layout->addWidget(storageCard);
    layout->addStretch();
}

void KnowledgeStatusView::setText(const QString &key, const QString &text)
{
    auto *target = m_labels.value(key);
    if (target->text() != text)
        target->setText(text);
    target->setVisible(!text.isEmpty());
}

void KnowledgeStatusView::updateStatus(const QJsonObject &snapshot,
                                       const KnowledgeStatusContext &context)
{
    const bool loaded = !snapshot.isEmpty();
    for (const auto &key : {"documents", "chunks"})
        setText(key, loaded ? number(snapshot.value(key).toDouble()) : QStringLiteral("—"));
    setText("size",
            loaded ? QString::number(snapshot.value("dbBytes").toDouble() / 1073741824.0, 'f', 2)
                   : QStringLiteral("—"));
    setText("taskBadge", context.busy ? QStringLiteral("运行中 · ") + stageName(context.stage)
                                      : QStringLiteral("空闲"));
    setText("taskSummary",
            context.summary.isEmpty() ? QStringLiteral("暂无正在运行的任务") : context.summary);
    const bool progressKnown =
        context.busy && (context.job.contains("processed") || context.job.contains("progress"));
    const double done =
        context.job.value(context.job.contains("processed") ? "processed" : "progress").toDouble();
    const double total = context.job.value("total").toDouble();
    m_progress->setVisible(progressKnown && total > 0);
    m_progress->setValue(total > 0 ? qBound(0, int(qMin(1.0, done / total) * 1000), 1000) : 0);
    QString progress;
    if (progressKnown)
        progress = total > 0
                       ? QStringLiteral("清单遍历  %1 / %2 项").arg(number(done), number(total))
                       : QStringLiteral("本轮已处理 %1 项 · 总量统计中").arg(number(done));
    if (context.busy && context.started.isValid())
        progress +=
            QStringLiteral("%1已用时 %2 分 %3 秒")
                .arg(progress.isEmpty() ? "" : "   ·   ")
                .arg(qMax(qint64(0), context.started.secsTo(QDateTime::currentDateTime())) / 60)
                .arg(qMax(qint64(0), context.started.secsTo(QDateTime::currentDateTime())) % 60);
    if (!context.busy && !context.outcome.isEmpty())
        progress = QStringLiteral("最近任务：") + context.outcome;
    setText("taskProgress", progress);
    QStringList counts;
    if (context.busy) {
        for (const auto &entry :
             {qMakePair(QStringLiteral("updated"), QStringLiteral("更新")),
              qMakePair(QStringLiteral("reused"), QStringLiteral("复用")),
              qMakePair(QStringLiteral("retainedOcr"), QStringLiteral("保留 OCR"))})
            if (context.job.contains(entry.first))
                counts << QStringLiteral("%1 %2").arg(
                    entry.second, number(context.job.value(entry.first).toDouble()));
    }
    setText("taskCounts", counts.join(QStringLiteral("   ·   ")));
    const auto path = context.job.value("path").toString();
    setText("taskPath", context.busy && !path.isEmpty() ? QStringLiteral("当前文件：") + path
                                                        : QStringLiteral("暂无正在处理的文件"));
    QString note = QStringLiteral("清单遍历包含目录、跳过项和复用项，不能作为待 OCR 数量。");
    if (context.busy && context.job.contains("blocks"))
        note.prepend(QStringLiteral("当前文件已提取 %1 个文本块（不是页数）。\n")
                         .arg(number(context.job.value("blocks").toDouble())));
    if (context.busy && (context.job.contains("currentPage") || context.job.contains("pages"))) {
        const int current = context.job.value("currentPage").toInt();
        const int pages = context.job.value("pages").toInt();
        progress += QStringLiteral("\n") +
                    (current > pages ? QStringLiteral("正在识别第 %1 页").arg(current)
                                     : QStringLiteral("当前文件已处理 %1 页").arg(pages));
        setText("taskProgress", progress);
    }
    setText("taskNote", note + QStringLiteral("\n百分比按清单项计算，不代表 OCR "
                                              "页数或剩余耗时；多页 PDF 处理时可长时间保持不变。"));

    const auto states = snapshot.value("extractionStates").toObject();
    for (const auto &key : {"complete", "partial", "no_text", "failed", "missing"})
        setText(key, loaded ? number(states.value(key).toDouble()) : QStringLiteral("—"));
    const auto lastBuild = snapshot.value("lastBuild").toString();
    setText("lastBuild",
            QStringLiteral("最近成功建库\n%1")
                .arg(lastBuild.isEmpty() ? QStringLiteral("暂无记录") : dateText(lastBuild)));
    const auto pending = snapshot.value("ocrPending").toObject();
    const bool known =
        loaded && pending.contains("documents") && pending.value("complete").toBool(true);
    const bool personal = !snapshot.value("path").toString().isEmpty() &&
                          snapshot.value("manifest").toObject().value("format").toString() ==
                              QLatin1String("legacy-sqlite");
    const bool blocked = context.busy || context.ocrActionBlocked;
    m_ocr->setEnabled(personal && !blocked &&
                      (!known || pending.value("documents").toDouble() > 0));
    m_ocr->setText(blocked ? QStringLiteral("任务运行中")
                   : known && pending.value("documents").toDouble() == 0
                       ? QStringLiteral("暂无待办")
                       : QStringLiteral("开始处理"));
    m_ocr->setToolTip(
        !loaded     ? QStringLiteral("请等待库统计加载完成")
        : !personal ? QStringLiteral("此入口用于当前个人库的 OCR 补全")
        : blocked   ? QStringLiteral("请先等待当前任务结束或暂停任务")
        : known && pending.value("documents").toDouble() == 0
            ? QStringLiteral("当前已提交记录中没有明确待 OCR 的文档")
            : QStringLiteral("执行③ OCR 补全，复用已有清单和正文，沿用当前资源限制（Alt+O）"));
    setText("ocrPending",
            known ? number(pending.value("documents").toDouble()) + QStringLiteral(" 份文档")
                  : QStringLiteral("待统计"));
    setText("ocrUnits", known ? QStringLiteral("PDF %1 页    ·    图片 %2 张")
                                    .arg(number(pending.value("pdfPages").toDouble()),
                                         number(pending.value("images").toDouble()))
                              : QStringLiteral("待 OCR 数量暂未知"));
    setText("ocrState", context.busy && context.stage == "ocr" ? QStringLiteral("正在进行 OCR 补全")
                        : context.busy && context.stage == "retry" && context.retryOcr
                            ? QStringLiteral("重试任务正在按需补全 OCR")
                            : QStringLiteral("OCR 当前未运行"));
    setText("ocrNote",
            known ? QStringLiteral("仅统计已检测、已提交的明确待办。无正文不一定需要 OCR。")
            : loaded ? QStringLiteral("旧库统计索引不完整；下次显式建库会升级。此处不会全库扫描。")
                     : QStringLiteral("正在读取已提交的库统计…"));
    setText("failurePending", loaded ? number(snapshot.value("failurePending").toDouble()) +
                                           QStringLiteral(" 项待处理")
                                     : QStringLiteral("— 项待处理"));
    setText("ignored", loaded ? QStringLiteral("失败 / 来源缺失 · 已忽略 %1 项，已有正文仍保留")
                                    .arg(number(snapshot.value("ignoredDocuments").toDouble()))
                              : QStringLiteral("正在读取异常记录…"));
    setText("retained", loaded ? QStringLiteral("清单外保留正文 %1 份\n默认保留，可按天数预览清理")
                                     .arg(number(snapshot.value("retainedAbsent").toDouble()))
                               : QStringLiteral("清单外保留正文 · 待统计"));
    QStringList failures;
    for (const auto &item : snapshot.value("failures").toArray()) {
        const auto row = item.toObject();
        failures << QStringLiteral("• %1\n  %2")
                        .arg(row.value("name").toString(), row.value("reason").toString());
    }
    setText("failures", failures.isEmpty() ? QStringLiteral("暂无记录") : failures.join("\n\n"));
    setText("database", QStringLiteral("当前个人库：") + context.currentDatabase);
    setText("directory", QStringLiteral("新建库位置：") + context.newDirectory);
    setText("budget", QStringLiteral("磁盘至少保留 %1 GiB · 单库上限 %2 GiB\nOCR 单核上限 %3% · "
                                     "工作进程地址空间上限 %4 MiB（非实际内存占用）")
                          .arg(context.reserveGiB)
                          .arg(context.quotaGiB)
                          .arg(context.cpuPercent)
                          .arg(context.memoryMiB));
    const auto checkpoint = snapshot.value("checkpoint").toObject();
    setText("checkpoint", checkpoint.isEmpty()
                              ? QString()
                              : QStringLiteral("已完成检查点：%1 · %2")
                                    .arg(stageName(checkpoint.value("stage").toString()),
                                         dateText(checkpoint.value("completed_at").toString())));
    setText("ocrEmpty", known ? QStringLiteral("OCR 后仍无文字：PDF %1 页；无正文图片 %2 "
                                               "张。它们可能本身没有文字，不自动计入待办。")
                                    .arg(number(pending.value("emptyOcrPages").toDouble()),
                                         number(pending.value("imagesWithoutText").toDouble()))
                              : QString());
    setText("ocrEta", QStringLiteral("OCR "
                                     "数量不含尚未检测的文件，正在处理的文档提交后才更新。剩余总页"
                                     "数和预计耗时暂无法可靠估算。"));
    setText("message", context.message);
}
