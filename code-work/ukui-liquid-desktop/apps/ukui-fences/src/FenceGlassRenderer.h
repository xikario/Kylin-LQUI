#pragma once
#include "../../../shared/liquid-glass/src/LiquidOpticsRenderer.h"
#include "../../../shared/liquid-glass/src/LiquidMaterial.h"
// Desktop fences and monitor use the same renderer as launcher and desklets.
// Callers retain their geometry, wallpaper preparation and final image caches.
class FenceGlassRenderer final {
public:
    FenceGlassRenderer();
    ~FenceGlassRenderer();
    void setWallpaper(const QImage &source);
    void setPreparedWallpaper(const LiquidMaterial::Prepared &material);
    QImage renderPanel(const QRect &logicalRect,qreal radius,const QPainterPath &shape={});
    bool usedGpu() const{return m_renderer.usedGpu();}
    int preparationCount() const{return m_renderer.preparationCount();}
private:LiquidOpticsRenderer m_renderer;
};
