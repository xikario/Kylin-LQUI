#pragma once
#include "LiquidOpticsRenderer.h"
#include "CandidateConfig.h"
#include <QWidget>
#include <QTimer>
#include <QFont>
#include <functional>

struct CandidateState {
    QString preedit, auxiliary;
    QStringList labels, texts, pinyins;
    bool preeditVisible=false, auxiliaryVisible=false, lookupVisible=false;
    bool previous=false, next=false;
    int cursor=0, caret=0, layout=0;
    QRect spot;
    bool hasSpot=false;
};

class CandidateWindow : public QWidget {
    Q_OBJECT
public:
    explicit CandidateWindow(QWidget *parent=nullptr, bool preview=false);
    CandidateState state;
    void setConfiguration(const CandidateConfig &config);
    void reloadConfiguration();
    void showPreview();
    void setPreviewBackground(int mode);
    QColor readableColor(QColor preferred, const QRect &area, QColor overlay=Qt::transparent) const;
    QColor selectionFill(const QRect &area) const;
    // Injection used by isolated tests; production captures only before mapping.
    std::function<QImage(const QRect &)> capture;
    void schedule();
    void clear();
    void warmUp();
    int captureCount() const { return m_captures; }
    int renderCount() const { return m_renders; }
    bool usedGpu() const { return m_optics.usedGpu(); }
    const CandidateConfig &config() const { return m_config; }
    QVector<QRect> candidateRects() const { return m_items; }
    QVector<QRect> candidateTextRects(int index) const { return m_mainRects.value(index); }
    QVector<QRect> pinyinRects(int index) const { return m_readingRects.value(index); }
    QRect previousRect() const { return m_previous; }
    QRect nextRect() const { return m_next; }
    QRect headerRect() const { return m_header; }
    void setPreviewWidth(int width);
    static QRect place(QSize size, QRect spot, QRect available, int gap);
    static int utf16Caret(const QString &text, int codepoints);
public slots:
    void refresh();
signals:
    void selected(int index);
    void pageUp();
    void pageDown();
    void rimeSettingsChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    bool hasContent() const;
    QString headerText() const;
    void updatePreviewCandidates();
    void dismissComposition();
    QSize layoutContent(int maxWidth);
    void prepareMaterial(bool opening);
    QTimer m_update;
    LiquidOpticsRenderer m_optics;
    QImage m_backdrop, m_material;
    QRect m_backdropRect, m_materialGeometry;
    QVector<QRect> m_items;
    QVector<QVector<QRect>> m_mainRects,m_readingRects;
    QPair<QStringList,QStringList> rubyParts(const QString &text,const QString &reading) const;
    QVector<int> rubyWidths(const QStringList &glyphs,const QStringList &sounds) const;
    QRect m_header, m_previous, m_next;
    qreal m_scale=1;
    int m_captures=0, m_renders=0, m_pressed=-1, m_wheel=0;
    QColor backdropColor(const QRect &area, QColor overlay=Qt::transparent) const;
    bool m_light=false;
    bool m_preview=false;
    int m_previewWidth=0;
    CandidateConfig m_config;
    // D-Bus can update state after layout and before the next paint event.
    CandidateState m_drawState;
    QString m_drawHeader;
    bool m_drawPinyin=false,m_drawLabels=false;
    int m_mainBaselineOffset=0,m_pinyinBaselineOffset=0;
    QFont m_chineseFont, m_latinFont, m_pinyinFont;
    QColor m_headerInk,m_caretInk;
    QVector<QColor> m_textInks,m_labelInks,m_pinyinInks,m_selectionFills,m_navInks;
    void updateReadability();
    int px(qreal n) const { return qRound(n*m_scale); }
    QFont fontForText(const QString &text, bool candidate=false) const;
    QFontMetrics metricsFor(const QFont &font) const;
    int textWidth(const QString &text, bool candidate=false) const;
    int textHeight() const;
    int hit(QPoint point) const;
};
