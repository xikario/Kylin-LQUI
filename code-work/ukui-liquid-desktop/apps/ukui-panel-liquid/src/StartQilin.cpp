#include "StartQilin.h"
#include "TaskbarPetLayout.h"
#include "StartQilinScenes.h"
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <cmath>

namespace {
constexpr qreal Pi = 3.14159265358979323846;
constexpr int Tile = 112;
constexpr qreal SceneSide=TaskbarPetLayout::tileSide;
using StartQilinScenes::ramp;
qreal pulse(qreal t,qreal start,qreal end) {
    return std::pow(std::sin(Pi*ramp(t,start,end)),2);
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
        p.tail=19*pulse(t,.04,.30)-42*pulse(t,.24,.50)+16*pulse(t,.44,.70); p.hind=5*beat; break;
    case StartQilin::Leap:
        p.lift=-10*arch+2*std::sin(3*Pi*t); p.fore=15*arch; p.hind=-23*arch; p.tail=-21*arch; break;
    case StartQilin::Bow:
        p.head=26*arch; p.fore=-13*arch; p.lean=7*arch; p.blink=.85*arch; break;
    case StartQilin::Listen:
        p.ear=23*arch*std::sin(3*Pi*t); p.head=-8*arch; p.tail=10*beat; break;
    case StartQilin::Stamp:
        p.fore=-31*pulse(t,.06,.35)-26*pulse(t,.38,.66); p.head=8*beat; p.hind=4*arch; break;
    case StartQilin::Stretch:
        p.fore=16*arch; p.hind=-17*arch; p.head=17*arch; p.tail=-25*arch; p.lean=5*arch; break;
    case StartQilin::Trot:
        p.fore=22*arch*std::sin(4*Pi*t); p.hind=-p.fore;
        p.lift=-5*std::abs(p.fore)/22; p.tail=16*beat; break;
    case StartQilin::Blink:
        p.blink=pulse(t,.40,.60)+pulse(t,.65,.82); p.head=-9*pulse(t,.16,.54)+5*pulse(t,.58,.87); p.ear=11*beat; break;
    case StartQilin::CloudRide:
        p.fore=18*arch; p.hind=-20*arch; p.tail=13*beat; p.head=-4*arch; break;
    case StartQilin::MistStep:
        p.fore=-12*beat; p.hind=18*beat; p.head=-6*arch; break;
    case StartQilin::Star:
        p.ear=-13*beat; p.head=-7*arch; break;
    case StartQilin::Ripple:
        p.fore=-24*std::pow(std::sin(Pi*ramp(t,0,.53)),2); p.head=8*arch; break;
    case StartQilin::Flame:
        p.head=9*pulse(t,.05,.25)-10*pulse(t,.20,.67); p.ear=-7*arch; p.voice=pulse(t,.20,.69); break;
    case StartQilin::Rain:
        p.head=15*pulse(t,.13,.39)-7*pulse(t,.41,.78);
        p.ear=-15*pulse(t,.18,.42); p.blink=pulse(t,.22,.40); p.fore=-15*pulse(t,.31,.70); break;
    case StartQilin::Ball:
        p.head=11*pulse(t,.10,.31)-22*pulse(t,.29,.49);
        p.fore=-21*pulse(t,.25,.52); p.hind=7*pulse(t,.40,.62); p.tail=17*beat; break;
    case StartQilin::Flip:{
        const qreal tuck=ramp(t,.18,.39)*(1-ramp(t,.60,.78));
        p.head=19*tuck; p.fore=-35*tuck; p.hind=32*tuck; p.tail=-25*tuck; break;
    }
    case StartQilin::RibbonSpin:
        p.head=-9*arch; p.fore=-32*arch; p.hind=9*arch;
        p.tail=25*arch-13*beat; break;
    case StartQilin::DoubleBounce:{
        const qreal first=pulse(t,.17,.42),second=pulse(t,.47,.84);
        p.fore=-19*first+24*second; p.hind=16*first-29*second;
        p.tail=12*first-22*second; p.head=-8*second; break;
    }
    case StartQilin::Kickflip:
        p.fore=21*pulse(t,.20,.35)-27*pulse(t,.33,.70);
        p.hind=30*pulse(t,.27,.71); p.tail=-19*pulse(t,.31,.70); p.head=11*arch; break;
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

// Ground-level props need the hoof space. Let them take the lip's place
// during the action and settle the character back behind it on landing.
qreal perchDepth(StartQilin::Move move,qreal t) {
    switch(move){
    case StartQilin::Stamp: case StartQilin::Ripple: case StartQilin::Trot:
    case StartQilin::CloudRide: case StartQilin::MistStep: case StartQilin::RibbonSpin:
    case StartQilin::DoubleBounce: case StartQilin::Kickflip:
        return 1-ramp(t,0,.16)*(1-ramp(t,.84,1));
    default:return 1;
    }
}
QPainterPath perchRim(qreal depth) {
    const QRectF r=TaskbarPetLayout::restingMark();
    const qreal y=r.bottom()+10-depth*43;
    QPainterPath rim;rim.moveTo(-SceneSide,y);rim.lineTo(r.left(),y);
    rim.cubicTo(r.left()+r.width()*.29,y+9,r.right()-r.width()*.29,y+9,r.right(),y);
    rim.lineTo(2*SceneSide,y);
    return rim;
}
QPainterPath rimSide(QPainterPath rim,qreal farY) {
    rim.lineTo(2*SceneSide,farY);rim.lineTo(-SceneSide,farY);rim.closeSubpath();
    return rim;
}
void paintPerch(QPainter &p,const QPainterPath &rim) {
    static const auto outline=TaskbarPetLayout::bust(TaskbarPetLayout::restingMark());
    p.save();p.setClipPath(outline,Qt::IntersectClip);p.setClipPath(rimSide(rim,2*SceneSide),Qt::IntersectClip);
    QLinearGradient jade(15,112,127,152);
    jade.setColorAt(0,QColor("#65b1c4"));jade.setColorAt(.5,QColor("#377e9c"));jade.setColorAt(1,QColor("#27567b"));
    p.fillRect(outline.boundingRect(),jade);
    p.setPen(Qt::NoPen);p.setBrush(QColor(148,219,231,45));
    p.drawPolygon(QPolygonF({{0,114},{61,125},{42,150}}));
    p.setBrush(QColor(14,64,102,42));p.drawPolygon(QPolygonF({{87,151},{107,123},{151,116}}));
    p.setPen(QPen(QColor("#a6dce7"),2.2));p.setBrush(Qt::NoBrush);p.drawPath(rim);p.restore();
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

QRectF StartQilin::targetRect(const QRectF &button) {
    return TaskbarPetLayout::canvas(button,Qt::LeftEdge);
}

StartQilin::Timing StartQilin::timing(Move move) {
    static constexpr std::array<Timing,Count> times{{
        // Moving frames are rendered at 14–20 fps. A held pose needs no repaint.
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
        {39,69,190}, // Flame
        {49,57,140}, // Rain
        {51,55},     // Ball
        {54,51,110}, // Flip
        {52,52},     // RibbonSpin
        {57,50},     // DoubleBounce
        {52,54,90}   // Kickflip
    }};
    return times[move];
}

QPixmap StartQilin::render(Move move,qreal time,qreal extraLift) const {
    const Pose pose=poseAt(move,time);
    const Rig rig(pose);
    QPixmap image(Tile,extraLift>0?2*Tile:Tile);image.fill(Qt::transparent);
    QPainter p(&image);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
    p.translate(0,image.height()-Tile);
    p.scale(Tile/TaskbarPetLayout::tileSide,Tile/TaskbarPetLayout::tileSide);
    p.translate((TaskbarPetLayout::tileSide-TaskbarPetLayout::restSide)/2,TaskbarPetLayout::restTop);
    StartQilinScenes::paintBehind(p,move,time,extraLift);
    const QTransform body=StartQilinScenes::bodyTransform(move,time);
    const qreal depth=perchDepth(move,time);
    const auto rim=depth>0?perchRim(depth):QPainterPath();
    p.save();
    if(!rim.isEmpty())p.setClipPath(rimSide(rim,-2*SceneSide),Qt::IntersectClip);
    p.translate(0,-extraLift);p.setTransform(body,true);
    if(move==MistStep)p.setOpacity(1-ramp(time,.10,.40)*(1-ramp(time,.58,.90)));
    if(outline.isEmpty()){
        // Some themes have no vector mesh. Keep the bundled character and effects.
        const char *action=nullptr;
        switch(move){
        case Rest:break;
        case Roar:case Listen:case Flame:case Ball:action="roar";break;
        case Tail:case Blink:case MistStep:case RibbonSpin:action="tail";break;
        case Leap:case Trot:case Stamp:case CloudRide:case Ripple:
        case Flip:case DoubleBounce:case Kickflip:action="leap";break;
        default:action="bow";break;
        }
        const QPixmap strip=action?QPixmap(QString(":/start-qilin/%1.png").arg(action)):fallback;
        if(!strip.isNull()){
            const int side=strip.height(),n=strip.width()/side;
            p.drawPixmap(QRectF(-13,-15,176,176),strip,QRectF(qBound(0,int(time*n),n-1)*side,0,side,side));
        }
    } else {
        p.setPen(QPen(QColor("#9bdff2"),TaskbarPetLayout::outlineWidth,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
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
    }
    p.restore();
    if(!rim.isEmpty())paintPerch(p,rim);
    StartQilinScenes::paintInFront(p,move,time,body.map(rig.map(QPointF(67,45))),
                                 body.map(rig.map(QPointF(72,1))));
    return image;
}
