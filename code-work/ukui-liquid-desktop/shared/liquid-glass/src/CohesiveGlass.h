#pragma once
#include "LiquidOpticsRenderer.h"
// One neutral optical family for the Dock, desktop cards, launcher and AI.
// Surfaces keep their own geometry and contrast policies; no rendering loop.
namespace CohesiveGlass {
constexpr qreal Refraction=2.6;
constexpr qreal Tint=.42;
constexpr qreal Highlight=.28;
constexpr qreal Chroma=.86;
constexpr qreal Clarity=.28;
constexpr qreal Strength=.75;
constexpr qreal CardRadius=22.;
constexpr qreal DockRadius=24.;
constexpr qreal DesktopInset=4.;
inline void configure(LiquidOpticsRenderer &renderer){
    renderer.setFinish(LiquidOpticsRenderer::Finish::Liquid);
    // Reading surfaces retain full diffusion and their established text contrast.
    renderer.setOptics(Refraction,.58,Highlight,Chroma);
    renderer.setMaterial(0,Strength);
}
}
