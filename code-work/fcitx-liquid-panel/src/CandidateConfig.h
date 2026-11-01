#pragma once
#include <QString>
#include <QColor>
#include <QVariant>
#include <QVector>
#include <functional>
struct CandidateConfig {
    int pageSize = 6;
    int layoutDirection = 0; // 0 horizontal, 1 vertical, 2 backend preference
    bool showPreedit = true;
    bool showCandidatePinyin = true;
    int pinyinFontSize = 0;
    bool showAuxiliary = true;
    bool showLabels = true;
    bool showPagingButtons = true;
    bool scrollToPage = true;
    QString chineseFont = QStringLiteral("Noto Sans CJK SC");
    QString latinFont = QStringLiteral("Noto Sans");
    int fontSize = 15;
    int headerFontSize = 0;
    int candidateFontSize = 0;
    bool bold = false;
    bool italic = false;
    qreal letterSpacing = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    int minWindowWidth = 140;
    int maxWindowWidth = 880;
    int rowHeight = 0;
    int pinyinGap = 1;
    int headerHeight = 0;
    int windowPadding = 5;
    int horizontalPadding = 16;
    int itemSidePadding = 8;
    int itemHorizontalPadding = 28;
    int candidateGap = 2;
    int headerGap = 2;
    int shadowSize = 5;
    int cornerRadius = 16;
    qreal refraction = 3.5;
    qreal shade = .58;
    qreal highlight = .55;
    qreal chroma = 1.05;
    bool autoContrast = true;
    QColor textColor = QColor(241,245,252);
    QColor headerColor = QColor(241,245,252);
    QColor accentColor = QColor(151,216,255);
    QColor selectedTextColor = QColor(241,245,252);
    QColor selectionColor = QColor(166,211,255,40);
    QColor borderColor = QColor(255,255,255,70);
    QColor tintColor = QColor(20,30,45,0);
};


struct ConfigField {
    QString key, label, group;
    enum Kind { Number, Boolean, Font, Color, Choice } kind;
    QStringList choices;
    double minimum=0, maximum=0, step=1;
    std::function<QVariant(const CandidateConfig &)> get;
    std::function<void(CandidateConfig &,const QVariant &)> set;
};
const QVector<ConfigField> &candidateFields();
QString candidateConfigPath();
CandidateConfig loadCandidateConfig(const QString &path=candidateConfigPath());
bool saveCandidateConfig(const CandidateConfig &,const QString &path=candidateConfigPath());
