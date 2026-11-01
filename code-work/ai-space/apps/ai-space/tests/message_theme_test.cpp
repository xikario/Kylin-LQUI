#include "UiMessages.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <cmath>
#include <iostream>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

static double luminance(const QColor &color)
{
    auto linear = [](double value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
}

static void verifyTheme(bool dark, const QString &artifact)
{
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    require(dialog && dialog->objectName() == "smartSpaceMessage", "use the shared dialog");
    dialog->ensurePolished();
    auto *text = dialog->findChild<QLabel *>("smartMessageText");
    require(text, "message text is present");
    const auto image = dialog->grab().toImage();
    if (!artifact.isEmpty())
        require(image.save(artifact), "save synthetic dialog preview");
    const auto background = image.pixelColor(8, 8);
    const auto foreground = text->palette().color(QPalette::WindowText);
    require((background.lightness() < 128) == dark, "painted background matches application theme");
    const double a = luminance(background), b = luminance(foreground);
    require((qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) >= 7.0, "message contrast is at least 7:1");
    for (auto *button : dialog->findChildren<QPushButton *>()) {
        require(button->isEnabled(), "task choices remain usable");
        require(button->text() != "Cancel", "cancel is localized");
        const auto buttonImage = button->grab().toImage();
        const auto bg = buttonImage.pixelColor(12, buttonImage.height() / 2);
        // Qt's palette does not expose the foreground from :focus style rules.
        // Inspect actual glyph pixels, excluding borders and the focus outline.
        const double x = luminance(bg);
        int readablePixels = 0;
        for (int row = buttonImage.height() / 4; row < buttonImage.height() * 3 / 4; ++row)
            for (int col = 18; col < buttonImage.width() - 18; ++col) {
                const double y = luminance(buttonImage.pixelColor(col, row));
                if ((qMax(x, y) + 0.05) / (qMin(x, y) + 0.05) >= 4.5)
                    ++readablePixels;
            }
        require(readablePixels >= 12, "button text remains readable including focused defaults");
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget parent;
    const auto artifacts = qEnvironmentVariable("AI_SPACE_TEST_ARTIFACT_DIR");
    if (!artifacts.isEmpty())
        QDir().mkpath(artifacts);
    for (bool dark : {false, true}) {
        auto palette = parent.palette();
        palette.setColor(QPalette::Window, QColor(dark ? "#0b1120" : "#f1f5f9"));
        palette.setColor(QPalette::WindowText, QColor(dark ? "#f1f5f9" : "#0f172a"));
        parent.setPalette(palette);
        parent.setStyleSheet(dark ? "QWidget {color:#f1f5f9;}" : "QWidget {color:#0f172a;}");
        parent.show();
        for (auto choice :
             {AiSpaceUi::TaskCloseChoice::Cancel, AiSpaceUi::TaskCloseChoice::Background,
              AiSpaceUi::TaskCloseChoice::PauseAndQuit}) {
            QTimer::singleShot(100, [&] {
                verifyTheme(dark,
                            artifacts.isEmpty() || choice != AiSpaceUi::TaskCloseChoice::Cancel
                                ? QString()
                                : artifacts + (dark ? "/exit-dark.png" : "/exit-light.png"));
                auto *dialog = QApplication::activeModalWidget();
                for (auto *button : dialog->findChildren<QPushButton *>()) {
                    if (button->property("messageChoice").toInt() == int(choice)) {
                        if (choice == AiSpaceUi::TaskCloseChoice::Cancel)
                            require(button->isDefault(), "cancel is the safe default");
                        button->click();
                        break;
                    }
                }
            });
            require(AiSpaceUi::smartTaskClose(&parent) == choice,
                    "each task action returns its own choice");
        }
        QTimer::singleShot(100, [&] {
            verifyTheme(dark, {});
            qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject();
        });
        require(AiSpaceUi::smartTaskClose(&parent) == AiSpaceUi::TaskCloseChoice::Cancel,
                "closing the window cancels without stopping work");
        QTimer::singleShot(100, [&] {
            verifyTheme(dark, {});
            for (auto *button : QApplication::activeModalWidget()->findChildren<QPushButton *>())
                if (button->isDefault()) {
                    require(button->property("messageChoice").toInt() == QMessageBox::No,
                            "destructive confirmation defaults to No");
                    button->click();
                    break;
                }
        });
        require(AiSpaceUi::smartQuestion(&parent, QStringLiteral("确认清理库内正文"),
                                         QStringLiteral("仅为隔离测试，不会删除文件。"),
                                         QMessageBox::Yes | QMessageBox::No,
                                         QMessageBox::No) == QMessageBox::No,
                "cleanup cancellation is preserved");
    }
    std::cout << "Message themes and safe choices passed" << std::endl;
}
