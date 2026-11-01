#include "FenceGlassRenderer.h"
#include "../../../shared/liquid-glass/src/CohesiveGlass.h"
FenceGlassRenderer::FenceGlassRenderer(){CohesiveGlass::configure(m_renderer);}
FenceGlassRenderer::~FenceGlassRenderer()=default;
void FenceGlassRenderer::setWallpaper(const QImage &source){m_renderer.setWallpaper(source);}
void FenceGlassRenderer::setPreparedWallpaper(const LiquidMaterial::Prepared &material){m_renderer.setPreparedWallpaper(material);}
QImage FenceGlassRenderer::renderPanel(const QRect &rect,qreal radius,const QPainterPath &shape){return m_renderer.renderPanel(rect,radius,shape);}
