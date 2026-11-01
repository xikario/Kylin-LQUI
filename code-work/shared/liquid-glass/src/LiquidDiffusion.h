#pragma once

#include <QImage>
#include <QSize>

// Internal texture preparation shared by the renderer and its equivalence test.
namespace LiquidOpticsDetail {
QImage diffuse(const QImage &source, QSize logical, int radius=2, int divisor=3);
}
