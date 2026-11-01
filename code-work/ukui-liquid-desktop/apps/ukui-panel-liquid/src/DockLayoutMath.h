#pragma once
#include <algorithm>
#include <cmath>

// Adapted from NextKde AdaptiveMath.mjs / DockMagnification.mjs, GPL-3.0.
// Upstream: 3fa1d7006869bbd3404a05d362c6e6028e0fe3da.
// The native UKUI panel retains its height and fixed accessories. Only the
// application area participates in the proportional layout and hover field.
namespace DockLayout {
struct Metrics { int icon=0,cell=0,padding=0,content=0;bool overflow=false; };
inline Metrics solve(int count,int width,int preferredIcon) {
    if(count<=0)return {};
    constexpr double spacing=.09,corner=.30,clearance=.035;
    const double gap=std::max(clearance,corner-(corner-clearance)*std::sqrt(.5));
    const double pitch=1+2*gap+spacing;
    const int preferred=std::max(20,preferredIcon),minimum=1;
    int icon=std::clamp(int(std::floor((std::max(1,width)-4)/ (count*pitch+.24))),minimum,preferred);
    auto result=[&]{const int cell=int(std::ceil(icon*pitch)),pad=std::max(0,int(std::round(icon*.12)));return Metrics{icon,cell,pad,count*cell+2*pad,false};};
    Metrics m=result();
    while(m.content>width&&icon>minimum){--icon;m=result();}
    // At the integer-pixel floor, rounding a fractional gap up to a whole
    // pixel doubles the required width. Spend the final pixels on icons.
    if(m.content>width && count<=width) {
        m.padding=0;m.cell=std::max(1,width/count);m.icon=std::min(m.icon,m.cell);m.content=count*m.cell;
    }
    m.overflow=m.content>width;return m;
}
inline double influence(double distance,double radius) {
    const double t=std::clamp(std::abs(distance)/std::max(1.,radius),0.,1.);
    return 1-t*t*t*(10+t*(-15+6*t));
}
inline double advance(double value,double target,double dt) {
    if(std::abs(target-value)<=.0001)return target;
    // Ease each icon toward its current pointer target, including lateral
    // travel. A slightly softer response suits Qt5's raster-painted buttons.
    const double next=target-(target-value)*std::exp(-std::clamp(dt,0.,1./30)/.065);
    return std::abs(target-next)<=.0001?target:next;
}
}
