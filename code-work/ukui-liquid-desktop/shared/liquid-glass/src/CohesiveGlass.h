#pragma once
#include "LiquidOpticsRenderer.h"
// One neutral optical family for the Dock, desktop cards, launcher and AI.
// Surfaces keep their own geometry and contrast policies; no rendering loop.
namespace CohesiveGlass {
// Dock recommendation: clear glass and a continuous specular rim, no lens
// displacement by default. Advanced refraction remains opt-in.
constexpr qreal Refraction=0.;
constexpr qreal Tint=.30;
constexpr qreal Highlight=.55;
constexpr qreal Chroma=1.05;
constexpr qreal Clarity=.18;
constexpr qreal Strength=1.;
constexpr qreal CardRadius=22.;
constexpr qreal DockRadius=24.;
constexpr qreal DesktopInset=4.;
inline void configure(LiquidOpticsRenderer &renderer){
    // Full panels use the original Snell offset, chromatic rim and caustics.
    // The low-height Dock explicitly selects its own finish at its call site;
    // applying that finish here zeroed refractionOffsetStrength everywhere.
    renderer.setFinish(LiquidOpticsRenderer::Finish::Original);
    renderer.setOptics(3.5,.58,.55,1.1);
    renderer.setMaterial(0,1.0);
}
}
