#pragma once
#include <QImage>
#include <QRect>
#include <QPainterPath>
#include <memory>
namespace LiquidMaterial { struct Prepared; }

// GUI-thread only. Each renderer owns its backdrop; live renderers share one
// GL program/context so restoring multiple widgets compiles the shader once.
// Each caller caches its final surface; hover never re-runs the shader.
class LiquidOpticsRenderer final
{
public:
    enum class Finish { Original, Liquid, Soft, Frosted };
    LiquidOpticsRenderer();
    ~LiquidOpticsRenderer();
    void setOptics(qreal refraction, qreal shade, qreal highlight, qreal chroma = 1.1);
    void setLightSurface(qreal amount){m_lightSurface=qBound(0.,amount,1.);}
    // Opt-in reading surfaces: adapt once per cached card, never per frame.
    void setAdaptiveSmoke(bool enabled){m_adaptiveSmoke=enabled;}
    qreal adaptiveSmokeAmount() const{return m_smokeAmount;}
    void setMaterial(qreal clarity = 0, qreal liquidStrength = 1);
    // Opt-in finish for the low-height Dock. Existing callers retain their
    // authored material until they explicitly select a finish.
    void setFinish(Finish finish) { m_finish=finish; }
    void setWallpaper(const QImage &source);
    void setPreparedWallpaper(const LiquidMaterial::Prepared &material);
    QImage renderPanel(const QRect &logicalRect, qreal radius, const QPainterPath &shape = {});
    QImage renderControl(const QRect &logicalRect, qreal radius, bool pressed=false);
    bool usedGpu() const { return m_usedGpu; }
    int preparationCount() const { return m_preparationCount; }
private:
    QImage renderSurface(const QRect &, qreal, const QPainterPath &, int control);
    class Backend;
    std::shared_ptr<Backend> m_backend;
    QImage m_source, m_body, m_clear;
    // Shape is local to the panel, so translating a fence must not rebuild it.
    QImage m_shapeField;
    QPainterPath m_cachedShape;
    QSize m_shapeSize;
    qreal m_shapeDpr = 0;
    int m_preparationCount = 0;
    bool m_usedGpu = false;
    bool m_adaptiveSmoke=false;
    qreal m_smokeAmount=0;
    Finish m_finish=Finish::Original;
    qreal m_clarity=0, m_liquidStrength=1,m_lightSurface=0;
    qreal m_refraction=3.5, m_shade=.58, m_highlight=.55, m_chroma=1.1;
};
