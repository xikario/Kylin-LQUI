#include "StartQilinScenes.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
constexpr qreal Pi=3.14159265358979323846;
using StartQilinScenes::ramp;

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

qreal appear(qreal t,qreal in=.18,qreal out=.82) {
    return ramp(t,0,in)*(1-ramp(t,out,1));
}
qreal pulse(qreal t,qreal start,qreal end) {
    return std::pow(std::sin(Pi*ramp(t,start,end)),2);
}
QPen pen(QColor color,qreal width) {
    return QPen(color,width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
}
QTransform around(QPointF pivot,qreal scale,QPointF travel={}) {
    QTransform transform;
    transform.translate(pivot.x()+travel.x(),pivot.y()+travel.y());
    transform.scale(scale,scale);transform.translate(-pivot.x(),-pivot.y());
    return transform;
}

void pinwheel(QPainter &p,qreal t) {
    // The tail winds up to strike the lower blade; the toy coasts after contact.
    p.translate(134,40);
    p.setPen(pen("#e9c59a",3.8));p.drawLine(QPointF(0,0),QPointF(10,48));
    p.rotate(-20+510*ramp(t,.32,.96));
    const QColor paper[]={"#ff8271","#ffdb75","#86e8bf","#c9adff"};
    for(const auto &color:paper){
        p.setPen(Qt::NoPen);p.setBrush(color);
        p.drawPolygon(QPolygonF({{0,0},{-7,-13},{3,-24},{11,-10}}));
        p.setBrush(color.darker(138));p.drawPolygon(QPolygonF({{0,0},{3,-24},{11,-10}}));
        p.rotate(90);
    }
    p.setBrush(QColor("#fff6c9"));p.drawEllipse(QPointF(),3.6,3.6);
}
void hoop(QPainter &p,qreal t,bool front) {
    const qreal pass=ramp(t,.18,.85);
    const QRectF ring(109-91*pass,48,43,82);
    p.setBrush(Qt::NoBrush);p.setPen(pen("#ef7f9e",6));
    if(front)p.drawArc(ring,260*16,115*16);else p.drawEllipse(ring);
    p.setPen(pen("#ffe5a4",5));
    for(int angle=0;angle<360;angle+=60)
        if(!front||angle>=270||angle==0)p.drawArc(ring,angle*16,14*16);
}
void foldingFan(QPainter &p,qreal t) {
    p.translate(81,94);p.rotate(8-15*ramp(t,.16,.56));
    const qreal spread=10+108*ramp(t,.08,.48)*(1-ramp(t,.70,.98));
    constexpr int folds=6;
    QPointF tip(0,-40);
    for(int i=0;i<folds;++i){
        QTransform a,b;a.rotate(-spread/2+i*spread/folds);b.rotate(-spread/2+(i+1)*spread/folds);
        const auto left=a.map(tip),right=b.map(tip);
        p.setPen(pen("#854550",1.2));p.setBrush(i%2?QColor("#f6a49d"):QColor("#fff0d7"));
        p.drawPolygon(QPolygonF({{0,0},left,right}));
        p.setPen(pen("#bb5364",2.7));p.drawLine(left*.84,right*.84);
    }
    p.setPen(Qt::NoPen);p.setBrush(QColor("#f4c878"));p.drawEllipse(QPointF(),3,3);
}
void melody(QPainter &p,qreal t) {
    for(int i=0;i<2;++i){
        const qreal u=ramp(t,i*.19,.76+i*.16);
        p.save();p.setOpacity(std::sin(Pi*u));p.translate(130-58*u,36-15*std::sin(Pi*u)+12*i);
        p.rotate(9-17*u);p.setPen(pen("#e1b8ff",4));p.drawLine(QPointF(0,0),QPointF(0,-20));
        p.drawLine(QPointF(0,-20),QPointF(10,-16));p.setPen(Qt::NoPen);
        p.setBrush(QColor("#cd88fa"));p.drawEllipse(QPointF(-5,1),7,4.8);p.restore();
    }
}
void drum(QPainter &p,qreal t) {
    const qreal hit=pulse(t,.29,.43)+pulse(t,.60,.74);
    p.translate(34,125+3*hit);p.scale(1+.06*hit,1-.12*hit);
    p.setPen(pen("#9d4754",2));p.setBrush(QColor("#dc5852"));
    p.drawRoundedRect(QRectF(-22,0,44,21),5,5);
    p.setPen(pen("#ffce7c",2.4));
    for(int x=-17;x<=7;x+=12){
        p.drawLine(QPointF(x,7),QPointF(x+5,16));p.drawLine(QPointF(x+5,16),QPointF(x+10,7));
    }
    p.setBrush(QColor("#ffe9b5"));p.drawEllipse(QRectF(-22,-5,44,11));p.drawLine(QPointF(-15,20),QPointF(15,20));
    if(hit>.05){
        p.setOpacity(hit);p.setPen(pen("#ffe899",3));
        p.drawLine(QPointF(25,3),QPointF(32,-2));p.drawLine(QPointF(24,10),QPointF(31,11));
    }
}
void flower(QPainter &p,qreal t) {
    const qreal growth=ramp(t,.06,.48);
    const QPointF root(108,125),bud(104-21*growth,123-59*growth);
    QPainterPath stem;stem.moveTo(root);stem.quadTo(112,126-28*growth,bud.x(),bud.y());
    p.setBrush(Qt::NoBrush);p.setPen(pen("#81c77e",3.5));p.drawPath(stem);
    p.setPen(Qt::NoPen);p.setBrush(QColor("#9de088"));
    p.drawEllipse(QPointF(107,121-14*growth),8*growth,4*growth);
    p.translate(bud);const qreal bloom=.15+.85*ramp(t,.34,.62);p.scale(bloom,bloom);
    for(int i=0;i<5;++i){
        p.setBrush(i%2?QColor("#ff9cba"):QColor("#ffc9dc"));
        p.drawEllipse(QPointF(0,-8),6.2,10.1);p.rotate(72);
    }
    p.setBrush(QColor("#ffd56f"));p.drawEllipse(QPointF(),5,5);
}
void finishFlag(QPainter &p,qreal t) {
    p.translate(131-63*ramp(t,.14,.85),61);
    p.setPen(pen("#f2dcac",3));p.drawLine(QPointF(0,-23),QPointF(0,75));
    auto corner=[t](int x,int y){return QPointF(x*8,-22+y*8+2*std::sin(4*Pi*t+x)*x/3.);};
    for(int y=0;y<2;++y)for(int x=0;x<3;++x){
        p.setPen(Qt::NoPen);p.setBrush((x+y)%2?QColor("#333c58"):QColor("#fff6dd"));
        p.drawPolygon(QPolygonF({corner(x,y),corner(x+1,y),corner(x+1,y+1),corner(x,y+1)}));
    }
}
void butterfly(QPainter &p,qreal t,QPointF snout) {
    const qreal approach=ramp(t,.04,.46),depart=ramp(t,.64,.98);
    const QPointF land=snout+QPointF(4,-5);
    const QPointF at=(1-approach)*QPointF(137,20)+approach*land
        +QPointF(39*depart,-26*depart-14*std::sin(Pi*approach));
    p.translate(at);p.rotate(-18+18*approach-23*depart);
    const qreal wing=.38+.62*std::abs(std::cos(7*Pi*t));
    p.setPen(pen("#77424b",1.7));
    for(int side:{-1,1}){
        p.save();p.scale(side*wing,1);p.setBrush(QColor("#ffab4d"));p.drawEllipse(QRectF(0,-16,15,17));
        p.setBrush(QColor("#f57c57"));p.drawEllipse(QRectF(0,-1,11,10));
        p.setPen(Qt::NoPen);p.setBrush(QColor("#fff2ac"));p.drawEllipse(QPointF(9,-10),2.6,2.6);p.restore();
    }
    p.setPen(pen("#473752",3));p.drawLine(QPointF(0,-6),QPointF(0,6));
}
void echo(QPainter &p,qreal t,QPointF snout) {
    p.translate(snout);
    for(int i=0;i<3;++i){
        const qreal u=ramp(t,.19+i*.105,.62+i*.105);
        const qreal radius=10+34*u;
        p.setOpacity(std::sin(Pi*u));p.setBrush(Qt::NoBrush);
        p.setPen(pen(i==1?QColor("#ffbe72"):QColor("#ffecac"),3.9-1.4*u));
        p.drawArc(QRectF(-radius,-radius,radius*2,radius*2),-46*16,88*16);
    }
}
void constellation(QPainter &p,qreal t,QPointF tip) {
    // Draw the three links in order as if the horn were tracing the sky.
    const QPointF points[]={tip+QPointF(2,3),{91,14},{114,2},{137,23}};
    for(int i=0;i<3;++i){
        const qreal u=ramp(t,.06+i*.13,.32+i*.13);
        const auto end=points[i]+(points[i+1]-points[i])*u;
        p.setPen(pen("#ebd493",2.1));p.drawLine(points[i],end);
        sparkle(p,end,(i==2?10:6)*u,QColor("#ffe699"));
    }
}
void pond(QPainter &p,qreal t) {
    const qreal wave=ramp(t,.30,1);
    p.setOpacity(std::sin(Pi*wave));p.setPen(pen("#8beaff",3.5));p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(23,143),9+16*wave,3+3.5*wave);
    const qreal jump=ramp(t,.39,.84),arc=std::sin(Pi*jump);
    if(arc>.01){
        p.save();p.setOpacity(arc);p.translate(31+55*jump,139-31*arc);p.rotate(-45+88*jump);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#fc9261"));p.drawEllipse(QPointF(),11,6.4);
        p.drawPolygon(QPolygonF({{-7,0},{-18,-7},{-16,7}}));
        p.setBrush(QColor("#fff0d0"));p.drawEllipse(QPointF(-1,-1),3.3,5.4);
        p.setBrush(QColor("#563843"));p.drawEllipse(QPointF(6,-1),1.7,1.7);p.restore();
    }
    const qreal landing=ramp(t,.78,.99);
    p.setOpacity(std::sin(Pi*landing));p.setBrush(Qt::NoBrush);p.setPen(pen("#b2f3ff",3));
    p.drawEllipse(QPointF(84,140),6+14*landing,2+4*landing);
}
void lantern(QPainter &p,qreal t,QPointF snout) {
    const qreal light=ramp(t,.39,.57),rise=ramp(t,.61,.93);
    const QPointF center(111+8*rise,37-19*rise);
    p.save();p.translate(center);p.rotate(6*std::sin(3*Pi*t));
    p.setBrush(Qt::NoBrush);p.setPen(pen("#f1ca89",2.3));p.drawArc(QRectF(-6,-21,12,12),0,180*16);
    p.setBrush(QColor::fromRgbF(.42+.53*light,.20+.17*light,.28-.07*light));
    p.drawRoundedRect(QRectF(-16,-14,32,26),10,10);
    p.setPen(pen("#ffcf74",2));p.setBrush(Qt::NoBrush);p.drawEllipse(QRectF(-8,-13,16,25));
    p.setPen(pen("#ffcf74",3.5));p.drawLine(QPointF(-9,-14),QPointF(9,-14));
    p.drawLine(QPointF(-8,12),QPointF(8,12));p.drawLine(QPointF(0,13),QPointF(2,23));
    p.setOpacity(p.opacity()*light);p.setPen(Qt::NoPen);p.setBrush(QColor("#ffe699"));p.drawEllipse(QPointF(),4.6,7.5);p.restore();
    const qreal breath=pulse(t,.17,.68);
    if(breath<.01)return;
    p.translate(snout);p.scale(breath,breath);p.rotate(-8);p.setPen(Qt::NoPen);
    QPainterPath flame;flame.moveTo(1,1);flame.cubicTo(12,0,14,-12,26,-8);
    flame.quadTo(32,-6,41,-15);flame.cubicTo(35,1,29,10,16,7);flame.closeSubpath();
    p.setBrush(QColor("#f38c42"));p.drawPath(flame);
    p.scale(.68,.58);p.setBrush(QColor("#fff4a3"));p.drawPath(flame);
}

void rainShower(QPainter &p,qreal t) {
    const qreal shower=appear(t,.26,.66);
    p.save();p.setOpacity(shower);
    cloud(p,{110+10*std::sin(Pi*t),9},69,QColor("#829abb"),QColor("#c9dcec"));
    p.setPen(pen("#a7e7ff",3.9));
    for(int i=0;i<6;++i){
        const qreal u=std::fmod(qMax(0.,t-.09)*2.8+i*.37,1.);
        const QPointF drop(67+14*i-19*u,20+77*u);
        p.drawLine(drop,drop+QPointF(-3.5,9));
    }
    p.restore();
}
void umbrella(QPainter &p,qreal t) {
    const qreal open=ramp(t,.23,.46)*(1-ramp(t,.78,.98));
    if(open<=0)return;
    p.translate(55,23+8*(1-open));p.rotate(-9+3*std::sin(3*Pi*t));p.scale(.16+.84*open,.75+.25*open);
    p.setBrush(Qt::NoBrush);p.setPen(pen("#996148",3.7));
    QPainterPath handle;handle.moveTo(0,-10);handle.lineTo(0,28);handle.cubicTo(0,37,10,37,10,28);p.drawPath(handle);
    QPainterPath canopy;canopy.moveTo(-40,6);canopy.cubicTo(-31,-11,-19,-20,0,-20);
    canopy.cubicTo(19,-20,34,-8,40,6);canopy.quadTo(32,-1,24,6);
    canopy.quadTo(14,-1,5,7);canopy.quadTo(-4,-1,-12,6);canopy.quadTo(-24,-1,-31,6);canopy.quadTo(-36,3,-40,6);
    p.setPen(pen("#98583d",2.3));p.setBrush(QColor("#ffd16d"));p.drawPath(canopy);
    p.setBrush(Qt::NoBrush);p.setPen(pen("#e78e45",2.4));
    QPainterPath ribs;ribs.moveTo(-31,5);ribs.quadTo(-15,-16,0,-20);ribs.quadTo(14,-15,24,5);
    ribs.moveTo(0,-20);ribs.quadTo(-5,-11,-12,5);p.drawPath(ribs);
    p.setPen(pen("#fff0ae",3));p.drawLine(QPointF(0,-22),QPointF(0,-19));
}
void beachBall(QPainter &p,qreal t,QPointF snout) {
    // The nose volley and the second, lower rebound meet at fixed contact points.
    const QPointF contact=snout+QPointF(12,-2);
    QPointF from(139,117),to=contact;
    qreal u=qBound(0.,t/.37,1.),height=37;
    if(t>=.37&&t<.74){
        from=contact;to={131,134};u=(t-.37)/.37;height=58;
    }else if(t>=.74){
        from={131,134};to={145,132};u=(t-.74)/.26;height=36;
    }
    const QPointF center=from+(to-from)*u-QPointF(0,4*height*u*(1-u));
    const qreal impact=pulse(t,.704,.777);
    p.setBrush(Qt::NoBrush);p.setPen(pen("#ffda96",3));
    if(impact>.06)p.drawArc(QRectF(113,140,36,7),188*16,135*16);
    p.translate(center);p.scale(1+.20*impact,1-.22*impact);p.rotate(300*t);
    constexpr qreal radius=15;
    p.setPen(pen("#a6544f",1.9));p.setBrush(QColor("#fff0cb"));p.drawEllipse(QPointF(),radius,radius);
    for(const auto &color:{QColor("#f35f66"),QColor("#37b9b2"),QColor("#ffd270")}){
        QPainterPath panel;panel.moveTo(-3,-4);panel.quadTo(5,-12,14,-4);
        panel.arcTo(QRectF(-radius,-radius,2*radius,2*radius),16,-57);panel.quadTo(5,5,-3,-4);
        p.setPen(Qt::NoPen);p.setBrush(color);p.drawPath(panel);p.rotate(120);
    }
    p.setBrush(QColor("#fff8df"));p.drawEllipse(QPointF(-2,-3),2.8,2.8);
}
} // namespace

QTransform StartQilinScenes::bodyTransform(StartQilin::Move move,qreal t) {
    const qreal w=appear(t),arch=std::pow(std::sin(Pi*t),2);
    switch(move){
    case StartQilin::CloudRide:
        return around({75,75},1-.19*arch,{5*std::sin(2*Pi*t)*arch,-13*arch});
    case StartQilin::MistStep:{
        const qreal cover=ramp(t,.10,.40)*(1-ramp(t,.58,.90));
        return QTransform::fromTranslate((t<.5?-9:9)*cover,0);
    }
    case StartQilin::Leap:
        return around({70,101},1-.24*w,{(-13+26*ramp(t,.22,.68))*w,-11*arch});
    case StartQilin::Stamp:
        return around({42,18},1-.18*w,{7*w,0});
    case StartQilin::Trot:
        return around({58,133},1-.16*w,{10*arch,-3*arch});
    case StartQilin::Rain:
        return around({55,145},1-.21*w,{0,2*w});
    default:return {};
    }
}

void StartQilinScenes::paintBehind(QPainter &p,StartQilin::Move move,qreal t) {
    p.save();p.setOpacity(appear(t));
    switch(move){
    case StartQilin::Leap:hoop(p,t,false);break;
    case StartQilin::Tail:pinwheel(p,t);break;
    case StartQilin::Trot:finishFlag(p,t);break;
    case StartQilin::Rain:rainShower(p,t);break;
    default:break;
    }
    p.restore();
}
void StartQilinScenes::paintInFront(QPainter &p,StartQilin::Move move,qreal t,QPointF snout,QPointF horn) {
    const qreal arch=std::pow(std::sin(Pi*t),2);
    p.save();p.setOpacity(appear(t));
    switch(move){
    case StartQilin::Roar:echo(p,t,snout);break;
    case StartQilin::Leap:hoop(p,t,true);break;
    case StartQilin::Bow:foldingFan(p,t);break;
    case StartQilin::Listen:melody(p,t);break;
    case StartQilin::Stamp:drum(p,t);break;
    case StartQilin::Stretch:flower(p,t);break;
    case StartQilin::Blink:butterfly(p,t,snout);break;
    case StartQilin::CloudRide:
        p.setOpacity(appear(t,.24,.76));
        cloud(p,{73+9*std::sin(2*Pi*t),134-3*arch},109*(.65+.35*arch),QColor("#84cfea"),QColor("#e3fbff"));break;
    case StartQilin::MistStep:
        p.setOpacity(.88*appear(t,.28,.60));
        cloud(p,{35+49*t,124-55*arch},88,QColor("#99b8e1"),QColor("#e4f4ff"));
        cloud(p,{116-31*t,104-76*arch},65,QColor("#c3def0"),QColor("#effaff"));
        sparkle(p,{85,69},11*arch,QColor("#fff3b5"));break;
    case StartQilin::Star:constellation(p,t,horn);break;
    case StartQilin::Ripple:pond(p,t);break;
    case StartQilin::Flame:lantern(p,t,snout);break;
    case StartQilin::Rain:umbrella(p,t);break;
    case StartQilin::Ball:beachBall(p,t,snout);break;
    default:break;
    }
    p.restore();
}
