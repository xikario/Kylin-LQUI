#pragma once
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QJsonObject>
#include <QPalette>
#include <QIODevice>
#include <QList>

namespace PeonyCliAppearance {
// QTermWidget 0.14 forwards OSC color queries as ordinary receivedData bytes
// instead of replying. Keep only a possible query prefix (at most 7 bytes),
// so terminal output is never buffered or retained here.
class OscColorQueryResponder {
public:
    QList<QByteArray> consume(const QByteArray &bytes, const QColor &foreground, const QColor &background) {
        QList<QByteArray> replies;
        for (char byte : bytes) {
            pending.append(byte);
            const int slot = querySlot(pending);
            if (slot != 0) {
                replies.append(colorReply(slot, slot == 10 ? foreground : background));
                pending.clear();
                continue;
            }
            while (!pending.isEmpty() && !isQueryPrefix(pending)) pending.remove(0, 1);
        }
        return replies;
    }

    int bufferedByteCount() const { return pending.size(); }

private:
    static int querySlot(const QByteArray &candidate) {
        static const QByteArray osc10St("\x1b]10;?\x1b\\");
        static const QByteArray osc11St("\x1b]11;?\x1b\\");
        static const QByteArray osc10Bel("\x1b]10;?\x07");
        static const QByteArray osc11Bel("\x1b]11;?\x07");
        if (candidate == osc10St || candidate == osc10Bel) return 10;
        if (candidate == osc11St || candidate == osc11Bel) return 11;
        return 0;
    }

    static bool isQueryPrefix(const QByteArray &candidate) {
        static const QByteArray queries[] = {
            QByteArray("\x1b]10;?\x1b\\"), QByteArray("\x1b]11;?\x1b\\"),
            QByteArray("\x1b]10;?\x07"), QByteArray("\x1b]11;?\x07")
        };
        for (const auto &query : queries) if (query.startsWith(candidate)) return true;
        return false;
    }

    static QByteArray component(int value) {
        return QByteArray::number(qBound(0, value, 255) * 257, 16).rightJustified(4, '0');
    }

    static QByteArray colorReply(int slot, const QColor &color) {
        QByteArray reply("\x1b]");
        reply += QByteArray::number(slot) + ";rgb:";
        reply += component(color.red()) + "/" + component(color.green()) + "/" + component(color.blue());
        reply += "\x1b\\";
        return reply;
    }

    QByteArray pending;
};

inline QFont terminalFont(const QFont &system, const QJsonObject &config) {
    QFont font(system);
    font.setWeight(QFont::Normal);
    font.setItalic(false);
    if (!config.value("followSystemFont").toBool(true))
        font.setPointSize(qBound(8, config.value("fontSize").toInt(11), 24));
    else if (font.pointSizeF() <= 0) font.setPointSize(10);
    // UKUI returns its proportional UI font for QFontDatabase::FixedFont.
    // Select the CJK mono companion explicitly; ASCII=one cell, Chinese=two.
    const QStringList available = QFontDatabase().families();
    QStringList candidates{config.value("fontFamily").toString("Sarasa Fixed SC")};
    if (system.family().contains("Noto Sans CJK"))
        candidates << QString(system.family()).replace("Noto Sans CJK", "Noto Sans Mono CJK");
    candidates << "Sarasa Fixed SC" << "Noto Sans Mono CJK SC" << "Sarasa Mono SC" << "Source Han Mono SC" << "DejaVu Sans Mono";
    for (const auto &family : candidates) {
        if (!available.contains(family)) continue;
        font.setFamily(family);
        font.setStyleHint(QFont::TypeWriter);
        font.setFixedPitch(true);
        font.setKerning(false);
        // QTermWidget 0.14 forces integer metrics. At odd pixel sizes it would
        // round half-em ASCII and full-em CJK independently (e.g. 8 vs 17).
        const int pixels = QFontInfo(font).pixelSize();
        if (pixels > 0 && pixels % 2) font.setPixelSize(pixels + 1);
        font.setStyleStrategy(QFont::ForceIntegerMetrics);
        const QFontMetricsF metrics(font);
        const qreal cell = metrics.horizontalAdvance(QLatin1Char('M'));
        if (qAbs(cell - metrics.horizontalAdvance(QLatin1Char('i'))) < .2 &&
            qAbs(metrics.horizontalAdvance(QStringLiteral("中")) - 2 * cell) < 1.) return font;
    }
    // Installed system monospace remains the final fallback on other machines.
    font.setFamily("DejaVu Sans Mono");
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    font.setKerning(false);
    return font;
}

inline bool writeScheme(QIODevice &output, const QPalette &palette) {
    const QColor background = palette.color(QPalette::Active, QPalette::Base);
    const QColor foreground = palette.color(QPalette::Active, QPalette::Text);
    const bool dark = background.lightness() < 128;
    const QStringList colors = dark
        ? QStringList{"#202124", "#f28b82", "#81c995", "#fdd663", "#8ab4f8", "#d7aefb", "#78d9ec", "#e8eaed"}
        : QStringList{"#202124", "#b3261e", "#18733b", "#8a5700", "#185abc", "#8430a8", "#006a7c", "#e8eaed"};
    QByteArray data("[General]\nDescription=Peony system palette\nOpacity=1\n");
    auto add = [&](const QString &name, const QColor &color) {
        data += QString("[%1]\nColor=%2,%3,%4\nBold=false\n\n")
            .arg(name).arg(color.red()).arg(color.green()).arg(color.blue()).toUtf8();
    };
    add("Foreground", foreground);
    add("ForegroundIntense", foreground);
    add("Background", background);
    add("BackgroundIntense", background);
    for (int i = 0; i < colors.size(); ++i) {
        add(QString("Color%1").arg(i), QColor(colors[i]));
        add(QString("Color%1Intense").arg(i), QColor(colors[i]));
    }
    return output.write(data) == data.size();
}
}
