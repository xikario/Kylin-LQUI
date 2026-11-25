#include "StartQilin.h"
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <cmath>

namespace {
constexpr qreal Pi = 3.14159265358979323846;
constexpr int Tile = 112;
qreal ramp(qreal value, qreal lo, qreal hi) {
    const qreal t = qBound<qreal>(0, (value-lo)/(hi-lo), 1);
    return t*t*(3-2*t);
}

struct Pose {
    qreal head=0, tail=0, ear=0, fore=0, hind=0, lift=0, lean=0, blink=0, voice=0;
};
Pose poseAt(StartQilin::Move move, qreal t) {
    // A soft arrival and landing let each one-shot return to exactly the logo.
    const qreal arch=std::pow(std::sin(Pi*t),2);
    const qreal beat=std::sin(2*Pi*t)*arch;
    Pose p;
    switch (move) {
    case StartQilin::Roar:
        p.head=9*beat-18*arch; p.ear=-8*arch; p.voice=ramp(t,.22,.42)*(1-ramp(t,.65,.92)); break;
    case StartQilin::Tail:
        p.tail=47*arch*std::sin(3*Pi*t); p.hind=5*beat; break;
    case StartQilin::Leap:
        p.lift=-10*arch+2*std::sin(3*Pi*t); p.fore=15*arch; p.hind=-23*arch; p.tail=-21*arch; break;
    case StartQilin::Bow:
        p.head=26*arch; p.fore=-13*arch; p.lean=7*arch; p.blink=.85*arch; break;
    case StartQilin::Listen:
        p.ear=23*arch*std::sin(3*Pi*t); p.head=-8*arch; p.tail=10*beat; break;
    case StartQilin::Stamp:
        p.fore=-29*arch*std::abs(std::sin(2*Pi*t)); p.head=8*beat; p.hind=4*arch; break;
    case StartQilin::Stretch:
        p.fore=16*arch; p.hind=-17*arch; p.head=17*arch; p.tail=-25*arch; p.lean=5*arch; break;
    case StartQilin::Trot:
        p.fore=22*arch*std::sin(4*Pi*t); p.hind=-p.fore;
        p.lift=-5*std::abs(p.fore)/22; p.tail=16*beat; break;
    case StartQilin::Blink:
        p.blink=arch*std::pow(std::sin(2*Pi*t),2)*1.7; p.head=-4*arch; p.ear=7*beat; break;
    case StartQilin::CloudRide:
        p.fore=18*arch; p.hind=-20*arch; p.tail=13*beat; p.head=-4*arch; break;
    case StartQilin::MistStep:
        p.fore=-12*beat; p.hind=18*beat; p.head=-6*arch; break;
    case StartQilin::Star:
        p.ear=-13*beat; p.head=-7*arch; break;
    case StartQilin::Ripple:
        p.fore=-24*std::pow(std::sin(Pi*ramp(t,0,.53)),2); p.head=8*arch; break;
    case StartQilin::Flame:
        p.head=7*beat-8*arch; p.ear=-7*arch; p.voice=.7*arch; break;
    default: break;
    }
    return p;
}

// Rotate around anatomical joints. Identical vertices on adjacent facets use
// identical weights, so the coloured facets stay joined even at full flexion.
struct Joint {
    QPointF pivot; qreal sine, cosine;
    Joint(qreal x,qreal y,qreal degrees):pivot(x,y),sine(std::sin(degrees*Pi/180)),cosine(std::cos(degrees*Pi/180)) {}
    QPointF delta(QPointF point) const {
        const auto v=point-pivot;
        return {v.x()*(cosine-1)-v.y()*sine, v.x()*sine+v.y()*(cosine-1)};
    }
};
struct Rig {
    Pose pose;
    Joint head, tail, ear, fore, hind;
    explicit Rig(Pose p):pose(p),head(36,61,p.head),tail(105,88,p.tail),
        ear(46,29,p.ear),fore(24,101,p.fore),hind(91,106,p.hind) {}
    QPointF map(QPointF v) const {
        const auto x=v.x(),y=v.y();
        return v+head.delta(v)*(1-ramp(y,48,80))
            +ear.delta(v)*(1-ramp(y,8,32))
            +tail.delta(v)*(ramp(x,102,131)*(1-ramp(y,82,100)))
            +fore.delta(v)*(ramp(y,94,137)*(1-ramp(x,39,65)))
            +hind.delta(v)*(ramp(y,99,140)*ramp(x,52,79))
            +QPointF(0,pose.lift+pose.lean*(1-ramp(y,40,145)));
    }
    QPolygonF map(const QPolygonF &poly) const {
        QPolygonF result;result.reserve(poly.size());
        for (const auto &point:poly) result<<map(point);
        return result;
    }
};
// Effects share the icon's local coordinates; only the current short action is painted.
void cloud(QPainter &p, QPointF center, qreal width, QColor fill, QColor edge) {
    p.save();p.translate(center);p.scale(width/100.,width/100.);
    QPainterPath plume;
    plume.moveTo(-49,9);
    plume.cubicTo(-39,6,-45,-3,-31,-2);
    plume.cubicTo(-26,-18,-7,-17,0,-5);
    plume.cubicTo(10,-17,24,-12,27,-3);
    plume.cubicTo(35,-6,41,2,50,-4);
    plume.cubicTo(48,7,38,14,24,13);
    plume.cubicTo(9,14,-4,10,-17,14);
    plume.cubicTo(-29,19,-41,17,-49,9);
    p.setPen(QPen(edge,2.8));p.setBrush(fill);p.drawPath(plume);
    QPainterPath curl;curl.moveTo(-26,5);curl.cubicTo(-11,9,-7,-5,-16,-4);
    curl.moveTo(3,7);curl.cubicTo(14,13,22,5,28,4);
    p.setPen(QPen(edge,2.5,Qt::SolidLine,Qt::RoundCap));p.setBrush(Qt::NoBrush);p.drawPath(curl);p.restore();
}
void sparkle(QPainter &p,QPointF center,qreal radius,QColor color) {
    QPolygonF points;
    for(int i=0;i<8;++i){
        const qreal angle=i*Pi/4, length=(i%2?.27:1)*radius;
        points<<center+QPointF(std::cos(angle)*length,std::sin(angle)*length);
    }
    p.setPen(Qt::NoPen);p.setBrush(color);p.drawPolygon(points);
}
void foreground(QPainter &p,StartQilin::Move move,qreal t,const Rig &rig) {
    const qreal arch=std::pow(std::sin(Pi*t),2);
    p.save();
    if(move==StartQilin::CloudRide){
        p.setOpacity(ramp(t,0,.24)*(1-ramp(t,.76,1)));
        cloud(p,{73+9*std::sin(2*Pi*t),134-3*arch},109* (.65+.35*arch),QColor("#84cfea"),QColor("#e3fbff"));
    } else if(move==StartQilin::MistStep){
        p.setOpacity(.88*ramp(t,0,.28)*(1-ramp(t,.60,1)));
        cloud(p,{35+49*t,124-55*arch},88,QColor("#99b8e1"),QColor("#e4f4ff"));
        cloud(p,{116-31*t,104-76*arch},65,QColor("#c3def0"),QColor("#effaff"));
        sparkle(p,{85,69},11*arch,QColor("#fff3b5"));
    } else if(move==StartQilin::Star){
        p.setOpacity(arch);const auto tip=rig.map(QPointF(72,1));
        sparkle(p,tip+QPointF(5+21*t,6-4*arch),11*arch,QColor("#ffdf80"));
        sparkle(p,tip+QPointF(16+25*t,12+6*t),5*arch,QColor("#f4faff"));
    } else if(move==StartQilin::Ripple&&t>.41){
        const qreal wave=ramp(t,.41,1);
        p.setOpacity(std::sin(Pi*wave));p.setPen(QPen(QColor("#8beaff"),3.8));p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(19,145),10+18*wave,3+4*wave);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#cdfaff"));
        p.drawEllipse(QPointF(10-9*wave,142-12*std::sin(Pi*wave)),2.3,3.5);
        p.drawEllipse(QPointF(31+10*wave,144-10*std::sin(Pi*wave)),2.5,3.5);
    } else if(move==StartQilin::Flame){
        const QPointF snout=rig.map(QPointF(67,45));
        p.translate(snout);p.setOpacity(arch);p.scale(.4+.6*arch,.5+.5*arch);
        QPainterPath flame;flame.moveTo(2,0);flame.cubicTo(13,-2,14,-12,26,-8);
        flame.quadTo(32,-6,41,-17+3*std::sin(6*Pi*t));flame.cubicTo(36,0,31,9,19,8);
        flame.quadTo(11,8,2,0);p.setPen(Qt::NoPen);p.setBrush(QColor("#f28b49"));p.drawPath(flame);
        QPainterPath core;core.moveTo(3,0);core.quadTo(12,0,19,-4);core.quadTo(20,1,29,-3);
        core.quadTo(20,10,3,0);p.setBrush(QColor("#ffe79b"));p.drawPath(core);
    }
    p.restore();
}

}

StartQilin::StartQilin() {
    for (const auto &theme:{QIcon::themeName(),QString("ukui-icon-theme-fashion"),QString("ukui-icon-theme-classical")}) {
        QFile file("/usr/share/icons/"+theme+"/scalable/apps/kylin-startmenu.svg");
        if(!file.open(QIODevice::ReadOnly))continue;
        QXmlStreamReader xml(&file);
        const QRegularExpression separator("[,\\s]+");
        while(!xml.atEnd()){
            xml.readNext();
            if(!xml.isStartElement()||xml.name()!="polygon")continue;
            const auto a=xml.attributes();
            const auto values=a.value("points").toString().split(separator,QString::SkipEmptyParts);
            const QColor color(a.value("fill").toString());
            if(values.size()<6||values.size()%2||!color.isValid())continue;
            QPolygonF poly;
            for(int i=0;i<values.size();i+=2)poly<<QPointF(values[i].toDouble(),values[i+1].toDouble());
            if(outline.isEmpty()){
                if(poly.size()<15)continue;
                outline=poly;
            }
            facets.append({poly,color});
        }
        if(!outline.isEmpty())return;
    }
    fallback.load(":/start-qilin/idle.png");
}

StartQilin::Timing StartQilin::timing(Move move) {
    static constexpr std::array<Timing,Count> times{{
        // Moving frames are rendered at 14–18 fps. A held pose needs no repaint.
        {1,0},       // Rest
        {34,63,230}, // Roar
        {39,59},     // Tail
        {34,58,180}, // Leap
        {32,68,230}, // Bow
        {34,62},     // Listen
        {36,60},     // Stamp
        {33,70,300}, // Stretch
        {40,56},     // Trot
        {31,65},     // Blink
        {42,66,260}, // CloudRide
        {46,64},     // MistStep
        {35,67,180}, // Star
        {40,61},     // Ripple
        {39,69,190}  // Flame
    }};
    return times[move];
}

QPixmap StartQilin::render(Move move,qreal time) const {
    const Pose pose=poseAt(move,time);
    const Rig rig(pose);
    QPixmap image(Tile,Tile);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
    p.scale(Tile/176.,Tile/176.);p.translate(13,15);
    p.save();
    const qreal arch=std::pow(std::sin(Pi*time),2);
    if(move==CloudRide){
        p.translate(75+5*std::sin(2*Pi*time)*arch,75-13*arch);
        p.scale(1-.19*arch,1-.19*arch);p.translate(-75,-75);
    } else if(move==MistStep){
        const qreal cover=ramp(time,.10,.40)*(1-ramp(time,.58,.90));
        p.setOpacity(1-cover);p.translate((time<.5?-9:9)*cover,0);
    }
    if(outline.isEmpty()){
        // Some themes have no vector mesh. Keep the bundled character and effects.
        const char *action=nullptr;
        switch(move){
        case Rest:break;
        case Roar:case Listen:case Flame:action="roar";break;
        case Tail:case Blink:case MistStep:action="tail";break;
        case Leap:case Trot:case Stamp:case CloudRide:case Ripple:action="leap";break;
        default:action="bow";break;
        }
        const QPixmap strip=action?QPixmap(QString(":/start-qilin/%1.png").arg(action)):fallback;
        if(!strip.isNull()){
            const int side=strip.height(),n=strip.width()/side;
            p.drawPixmap(QRectF(-13,-15,176,176),strip,QRectF(qBound(0,int(time*n),n-1)*side,0,side,side));
        }
    } else {
        p.setPen(QPen(QColor("#9bdff2"),2.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        p.setBrush(QColor("#0078bc"));p.drawPolygon(rig.map(outline));
        for (const auto &facet:facets) {
            p.setPen(QPen(facet.color,.34,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            p.setBrush(facet.color);p.drawPolygon(rig.map(facet.points));
        }
        auto stroke=[&](QPointF a,QPointF b,QColor ink,qreal width){
            p.setPen(QPen(ink,width,Qt::SolidLine,Qt::RoundCap));p.drawLine(rig.map(a),rig.map(b));
        };
        const QPointF eye=rig.map(QPointF(53,38));
        const qreal open=1-qBound<qreal>(0,pose.blink,1);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#07385b"));p.drawEllipse(eye,4.8,1.2+3.8*open);
        if(open>.3){
            p.setBrush(Qt::white);p.drawEllipse(eye+QPointF(1,-1.1*open),1.8,1.7*open);
        }
        stroke({57,47},{64,45},QColor("#075684"),1.6+3*pose.voice);
        if(move==Roar && pose.voice>.08){
            QColor glint("#b8f3ff");glint.setAlphaF(pose.voice);
            stroke({74,40},{81,36},glint,2.8);stroke({76,46},{84,46},glint,2.6);
        }
    }
    p.restore();
    foreground(p,move,time,rig);
    return image;
}
