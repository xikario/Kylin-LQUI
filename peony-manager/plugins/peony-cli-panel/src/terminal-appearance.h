#pragma once
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QJsonObject>
#include <QPalette>
#include <QIODevice>

namespace PeonyCliAppearance {
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
