#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPainter>
#include <utility>
#include <QRandomGenerator>
#include <QVector>
#include <algorithm>
#include <numeric>

// Shared hover/idle shuffle for one character. Row -1 is its original greeting.
class TaskbarPetDailies {
public:
    bool attempted=false;
    void load(const QString &avatar,const QString &id){
        attempted=true;
        if(avatar.isEmpty())return;
        const auto dir=QFileInfo(avatar).dir();
        QFile file(dir.filePath("../dailies.json"));
        if(!file.open(QIODevice::ReadOnly)||file.size()>128*1024)return;
        const auto pack=QJsonDocument::fromJson(file.readAll()).object().value(id).toObject();
        const QString name=pack.value("file").toString();
        const int tile=pack.value("tile").toInt();
        const auto moves=pack.value("moves").toArray();
        if(name.isEmpty()||QFileInfo(name).fileName()!=name||tile<32||tile>128||moves.isEmpty()||moves.size()>16)return;
        QVector<QVector<int>> timelines;
        int columns=0;
        for(const auto &move:moves){
            const auto holds=move.toObject().value("holdMs").toArray();
            if(holds.isEmpty()||holds.size()>40)return;
            QVector<int> times;
            for(const auto &hold:holds){
                const int ms=hold.toInt();if(ms<30||ms>1500)return;
                times.append(ms);
            }
            columns=qMax(columns,times.size());timelines.append(times);
        }
        const QSize size(tile*columns,tile*moves.size());
        QImageReader reader(dir.filePath(name),"png");
        if(reader.size()!=size||qint64(size.width())*size.height()>2*1024*1024)return;
        // QImageReader avoids retaining old roles' atlases in Qt's global pixmap cache.
        const auto image=reader.read();if(image.isNull())return;
        atlas=QPixmap::fromImage(image);if(atlas.isNull())return;
        delays=std::move(timelines);cell=tile;
        order.resize(delays.size()+1);std::iota(order.begin(),order.end(),-1);
        cursor=order.size();
    }
    int choose(){
        if(order.isEmpty())return -1;
        if(cursor==order.size()){
            std::shuffle(order.begin(),order.end(),*QRandomGenerator::global());
            if(order.first()==last||(last==-2&&order.first()==-1))
                std::swap(order.first(),order.last());
            cursor=0;
        }
        last=order[cursor++];return last;
    }
    const QVector<int> &holds(int row) const{return delays[row];}
    void paint(QPainter &p,const QRectF &target,int row,int frame) const {
        p.drawPixmap(target,atlas,QRectF(frame*cell,row*cell,cell,cell));
    }
private:
    QPixmap atlas;
    QVector<QVector<int>> delays;
    QVector<int> order;
    int cell=0,cursor=0,last=-2;
};
