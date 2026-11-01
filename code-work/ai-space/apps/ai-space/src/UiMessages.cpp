#include "UiMessages.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVector>
namespace AiSpaceUi
{
namespace
{
struct MessageChoice {
    int value;
    QString caption;
    QDialogButtonBox::ButtonRole role;
};

int messageDialog(QWidget *parent, const QString &title, const QString &text,
                  QMessageBox::Icon icon, const QVector<MessageChoice> &choices, int defaultButton,
                  int cancelButton)
{
    QDialog dialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setObjectName("smartSpaceMessage");
    dialog.setWindowTitle(title);
    dialog.setWindowIcon(qApp->windowIcon());
    const QColor inherited =
        parent ? parent->palette().color(QPalette::WindowText) : QColor("#0f172a");
    const bool dark = inherited.lightness() > 128;
    const QString bg = dark ? "#131c31" : "#ffffff";
    const QString fg = dark ? "#f1f5f9" : "#0f172a";
    const QString border = dark ? "#26354d" : "#cbd5e1";
    const QString button = dark ? "#1e293b" : "#f1f5f9";
    const QString accent = "#2563eb";
    QPalette pal = dialog.palette();
    pal.setColor(QPalette::Window, QColor(bg));
    pal.setColor(QPalette::WindowText, QColor(fg));
    pal.setColor(QPalette::ButtonText, QColor(fg));
    dialog.setPalette(pal);
    dialog.setStyleSheet(
        QStringLiteral("QDialog#smartSpaceMessage { background: %1; color: %2; }"
                       "QDialog#smartSpaceMessage QLabel { color: %2; background: transparent; "
                       "font-size: 14px; }"
                       "QDialog#smartSpaceMessage QPushButton { color: %2; background: %3; border: "
                       "1px solid %4;"
                       "border-radius: 8px; padding: 7px 18px; min-width: 64px; font-weight: 500; }"
                       "QDialog#smartSpaceMessage QPushButton:focus, QDialog#smartSpaceMessage "
                       "QPushButton:hover {"
                       "border: 1px solid %5; background: %5; color: #ffffff; }")
            .arg(bg, fg, button, border, accent));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    auto *row = new QHBoxLayout;
    row->setSpacing(16);
    auto *symbol = new QLabel(&dialog);
    symbol->setPixmap(
        dialog.style()
            ->standardIcon(icon == QMessageBox::Question  ? QStyle::SP_MessageBoxQuestion
                           : icon == QMessageBox::Warning ? QStyle::SP_MessageBoxWarning
                                                          : QStyle::SP_MessageBoxInformation)
            .pixmap(32, 32));
    row->addWidget(symbol, 0, Qt::AlignTop);
    auto *label = new QLabel(text, &dialog);
    label->setObjectName("smartMessageText");
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(380);
    row->addWidget(label, 1);
    layout->addLayout(row);
    layout->addSpacing(18);
    auto *box = new QDialogButtonBox(&dialog);
    layout->addWidget(box);
    int result = cancelButton;
    for (const auto &choice : choices) {
        auto *buttonWidget = box->addButton(choice.caption, choice.role);
        buttonWidget->setProperty("messageChoice", choice.value);
        buttonWidget->setDefault(choice.value == defaultButton);
        if (choice.value == defaultButton)
            buttonWidget->setFocus();
        QObject::connect(buttonWidget, &QPushButton::clicked, &dialog, [&dialog, &result, choice] {
            result = choice.value;
            dialog.accept();
        });
    }
    dialog.resize(560, dialog.sizeHint().height());
    dialog.exec();
    return result;
}
} // namespace

QMessageBox::StandardButton smartMessage(QWidget *parent, const QString &title, const QString &text,
                                         QMessageBox::Icon icon,
                                         QMessageBox::StandardButtons buttons,
                                         QMessageBox::StandardButton defaultButton)
{
    QVector<MessageChoice> choices;
    for (auto choice : {QMessageBox::Yes, QMessageBox::No, QMessageBox::Ok, QMessageBox::Cancel}) {
        if (!buttons.testFlag(choice))
            continue;
        const bool startsTask =
            title == QStringLiteral("快速全量索引") || title == QStringLiteral("OCR 检测与补全");
        const QString caption = choice == QMessageBox::Yes  ? (startsTask ? "开始" : "确定")
                                : choice == QMessageBox::Ok ? "确定"
                                                            : "取消";
        choices.append({int(choice), caption,
                        choice == QMessageBox::Yes || choice == QMessageBox::Ok
                            ? QDialogButtonBox::AcceptRole
                            : QDialogButtonBox::RejectRole});
    }
    return static_cast<QMessageBox::StandardButton>(messageDialog(
        parent, title, text, icon, choices, int(defaultButton), int(QMessageBox::Cancel)));
}

TaskCloseChoice smartTaskClose(QWidget *parent)
{
    return static_cast<TaskCloseChoice>(messageDialog(
        parent, QStringLiteral("建库任务正在运行"),
        QStringLiteral("可以保留后台任务，或先暂停并保存已完成内容后退出。"), QMessageBox::Question,
        {{int(TaskCloseChoice::Background), QStringLiteral("最小化并继续"),
          QDialogButtonBox::AcceptRole},
         {int(TaskCloseChoice::PauseAndQuit), QStringLiteral("暂停并退出"),
          QDialogButtonBox::DestructiveRole},
         {int(TaskCloseChoice::Cancel), QStringLiteral("取消"), QDialogButtonBox::RejectRole}},
        int(TaskCloseChoice::Cancel), int(TaskCloseChoice::Cancel)));
}
QMessageBox::StandardButton smartQuestion(QWidget *p, const QString &title, const QString &text,
                                          QMessageBox::StandardButtons buttons,
                                          QMessageBox::StandardButton defaultButton)
{
    return smartMessage(p, title, text, QMessageBox::Question, buttons, defaultButton);
}
void smartInformation(QWidget *p, const QString &title, const QString &text)
{
    smartMessage(p, title, text, QMessageBox::Information, QMessageBox::Ok, QMessageBox::Ok);
}
void smartWarning(QWidget *p, const QString &title, const QString &text)
{
    smartMessage(p, title, text, QMessageBox::Warning, QMessageBox::Ok, QMessageBox::Ok);
}

} // namespace AiSpaceUi
