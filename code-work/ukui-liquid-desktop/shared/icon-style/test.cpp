#include "OpticalIcon.h"
#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QTemporaryDir>
#include <QProcess>
#include <QEventLoop>
#include <cstdlib>
static void check(bool ok,const char *message){if(!ok){qCritical()<<message;std::exit(1);}}
static QRect ink(const QPixmap &pix){const auto im=pix.toImage();QRect r;for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)if(im.pixelColor(x,y).alpha()>100)r=r.united(QRect(x,y,1,1));return r;}
int main(int argc,char **argv){
 if(argc==2 && QByteArray(argv[1])=="--external-disable") {QCoreApplication app(argc,argv);QSettings s(LquiIcon::Policy::path(),QSettings::IniFormat);s.setValue("icons/uniform",false);s.sync();return s.status()==QSettings::NoError?0:1;}
 QTemporaryDir config;qputenv("XDG_CONFIG_HOME",config.path().toUtf8());
 QApplication app(argc,argv);
 auto source=[](int size,int inset){QPixmap p(size,size);p.fill(Qt::transparent);QPainter g(&p);g.setPen(Qt::NoPen);g.setBrush(QColor("#30b8a5"));g.drawRect(inset,inset,size-inset*2,size-inset*2);return QIcon(p);};
 QPixmap circle(128,128);circle.fill(Qt::transparent);{QPainter p(&circle);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor("#ed6732"));p.drawEllipse(2,2,124,124);}
 QPixmap wide(128,64);wide.fill(Qt::cyan);
 QPixmap glyph(128,128);glyph.fill(Qt::transparent);{QPainter p(&glyph);p.setPen(QPen(Qt::white,10));p.drawLine(20,64,108,64);p.drawLine(64,20,64,108);}
 QPixmap tile(128,128);tile.fill(Qt::transparent);{QPainter p(&tile);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(QColor("#9056c9"));p.drawRoundedRect(QRectF(2,2,124,124),25,25);}
 for(int size:{24,32,40,48,64,96,128}) {
  const auto a=LquiIcon::raster(source(128,4),size),b=LquiIcon::raster(source(128,35),size);
  check(ink(a)==ink(b),"transparent source padding does not change visible icon size");
  check(a.size()==QSize(size,size)&&ink(a).width()<size,"safe margins remain inside every DPR raster");
  for(const QIcon &art:{QIcon(circle),QIcon(wide),QIcon(glyph),QIcon(tile)}) {
   const QPixmap adapted=LquiIcon::raster(art,size);
   check(ink(a)==ink(adapted),"circle, wide glyph and existing tile share the same canvas bounds");
   check(adapted.toImage().pixelColor(0,0).alpha()==0,"rounded tile retains transparent antialiased corners");
  }
 }
 const auto normalized=LquiIcon::render(QIcon(wide),128,true);const auto im=normalized.toImage();QRect coloured;
 for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x){const auto c=im.pixelColor(x,y);if(c.red()<10&&c.green()>240&&c.blue()>240)coloured=coloured.united(QRect(x,y,1,1));}
 check(std::abs(double(coloured.width())/coloured.height()-2.)<.1,"adapted artwork preserves source aspect ratio");
 check(LquiIcon::raster(QIcon(tile),64).toImage().pixelColor(32,32)==QColor("#9056c9"),"existing tile retains its original centre colour");
 const auto white=LquiIcon::raster(QIcon(glyph),128).toImage();check(white.pixelColor(64,64).lightness()>240&&white.pixelColor(25,25).lightness()<100,"white artwork gets a contrasting dark canvas");
 QPixmap empty(64,64);empty.fill(Qt::transparent);check(LquiIcon::raster(QIcon(empty),32).isNull(),"transparent artwork is handled without division by zero");
 const auto icon=source(128,30);check(LquiIcon::raster(icon,48).cacheKey()==LquiIcon::raster(icon,48).cacheKey(),"repeated paints reuse the optical raster");
 check(LquiIcon::setUniform(true),"policy can be persisted");const quint64 revision=LquiIcon::revision();
 check(QProcess::execute(QCoreApplication::applicationFilePath(),{"--external-disable"})==0,"separate process changes shared preference");
 QEventLoop loop;QTimer::singleShot(250,&loop,&QEventLoop::quit);loop.exec();
 check(!LquiIcon::uniform()&&LquiIcon::revision()>revision,"file watcher updates live rendering after an atomic external preference change");
 const auto original=ink(LquiIcon::raster(QIcon(wide),64));check(double(original.width())/original.height()>1.9,"disabled policy restores original silhouette");
 check(LquiIcon::setUniform(true)&&LquiIcon::uniform(),"uniform style can be restored without restarting");
 const QPixmap originalTheme=LquiIcon::raster(QIcon(circle),64);
 check(LquiIcon::setTheme(LquiIcon::Theme::Glass),"glass theme saves");const QPixmap glassTheme=LquiIcon::raster(QIcon(circle),64);
 check(LquiIcon::setTheme(LquiIcon::Theme::Mist),"mist theme saves");const QPixmap mistTheme=LquiIcon::raster(QIcon(circle),64);
 check(originalTheme.toImage()!=glassTheme.toImage()&&glassTheme.toImage()!=mistTheme.toImage(),"three themes produce distinct cached surfaces");
 check(ink(originalTheme)==ink(glassTheme)&&ink(glassTheme)==ink(mistTheme),"theme changes preserve icon geometry");
 check(LquiIcon::parseTheme("invalid")==LquiIcon::Theme::Color,"unknown future theme safely falls back to colour");
 LquiIcon::setTheme(LquiIcon::Theme::Color);
 QIcon::setThemeName("ukui-icon-theme-default");
 QDir().mkpath("artifacts/icon-unification");QPixmap preview(960,300);preview.fill(QColor("#30333b"));QPainter p(&preview);p.setPen(Qt::white);
 const QStringList names{"google-chrome","wechat","wps-office-wps","org.ukui.peony","utilities-terminal","firefox","preferences-system","text-x-generic"};
 for(int i=0;i<names.size();++i){QIcon art=QIcon::fromTheme(names[i],source(128,10+i*3));p.drawPixmap(QRect(i*120+26,36,68,68),art.pixmap(128,128));p.drawPixmap(i*120+26,180,LquiIcon::raster(art,68));}
 p.drawText(20,22,"原图标");p.drawText(20,163,"统一画布与圆角 · 保留品牌图形 · 自动补齐不规则图标");p.end();preview.save("artifacts/icon-unification/comparison.png");
 qInfo()<<"PASS uniform silhouettes, optical sizing, source aspect/colour, white glyph contrast, cache, live preference and original mode";
}
