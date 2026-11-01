#pragma once
#include <QColor>
#include <QString>

namespace LquiIcon {
enum class Theme { Color, Glass, Mist };
inline QString themeKey(Theme theme){return theme==Theme::Glass?"glass":theme==Theme::Mist?"mist":"color";}
inline Theme parseTheme(const QString &key){return key=="glass"?Theme::Glass:key=="mist"?Theme::Mist:Theme::Color;}
struct ThemeColors {QColor lightTop,lightBottom,darkTop,darkBottom,orbTop,orbMiddle,orbBottom,orbGlow;};
inline ThemeColors colors(Theme theme) {
    if(theme==Theme::Glass)return {QColor(229,245,255,204),QColor(145,182,218,205),QColor(62,92,120,220),QColor(24,43,67,230),QColor("#d5ffff"),QColor("#699bc9"),QColor("#293c68"),QColor("#93d7e8")};
    if(theme==Theme::Mist)return {QColor("#eee8e1"),QColor("#d9ddd9"),QColor("#606b71"),QColor("#404b55"),QColor("#d4e6e1"),QColor("#9aabbf"),QColor("#72758e"),QColor("#c4b4c8")};
    return {QColor("#f5f6f8"),QColor("#e2e5ec"),QColor("#45515f"),QColor("#2a3441"),QColor("#b4ffff"),QColor("#68b9f5"),QColor("#302b69"),QColor("#ee81f0")};
}
}
