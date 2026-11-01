#pragma once
#include <QImage>
#include <QRect>
#include <QPainterPath>
#include <memory>

// GUI-thread only. One prepared backdrop and one GL backend per renderer.
// Each caller caches its final surface; hover never re-runs the shader.
class LiquidOpticsRenderer final
{
public:
    LiquidOpticsRenderer();
    ~LiquidOpticsRenderer();
    void setOptics(qreal refraction, qreal shade, qreal highlight, qreal chroma = 1.1);
    // Opt-in pale surface; zero preserves existing dark material exactly.
    void setLightSurface(qreal amount);
    void setWallpaper(const QImage &source);
    QImage renderPanel(const QRect &logicalRect, qreal radius, const QPainterPath &shape = {});
    bool usedGpu() const { return m_usedGpu; }
    int preparationCount() const { return m_preparationCount; }
private:
    class Backend;
    std::unique_ptr<Backend> m_backend;
    QImage m_source, m_body, m_clear;
    // Shape is local to the panel, so translating a fence must not rebuild it.
    QImage m_shapeField;
    QPainterPath m_cachedShape;
    QSize m_shapeSize;
    qreal m_shapeDpr = 0;
    int m_preparationCount = 0;
    bool m_usedGpu = false;
    qreal m_lightSurface=0;
    qreal m_refraction=3.5, m_shade=.58, m_highlight=.55, m_chroma=1.1;
};
