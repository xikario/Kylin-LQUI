#include "CandidateWindow.h"
#include "PinyinTones.h"
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSettings>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace {
QColor composite(QColor foreground,QColor background) {
    const qreal a=foreground.alphaF();
    return QColor::fromRgbF(foreground.redF()*a+background.redF()*(1-a),foreground.greenF()*a+background.greenF()*(1-a),foreground.blueF()*a+background.blueF()*(1-a));
}
qreal luminance(QColor c) {
    const auto linear=[](qreal x){return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);};
    return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());
}
qreal contrast(QColor foreground,QColor background) {
    const qreal a=luminance(composite(foreground,background)),b=luminance(background);
    return (qMax(a,b)+.05)/(qMin(a,b)+.05);
}
// A bounded grid reads existing pixels, never a new screen capture.
QColor sample(const QImage &image,QRect area) {
    area=area.intersected(image.rect()); if(area.isEmpty())return QColor(55,62,73);
    qreal r=0,g=0,b=0;
    for(int y=0;y<3;++y) for(int x=0;x<5;++x) {
        const QColor c=image.pixelColor(area.x()+qMin(area.width()-1,int((x+.5)*area.width()/5)),area.y()+qMin(area.height()-1,int((y+.5)*area.height()/3)));
        r+=c.redF();g+=c.greenF();b+=c.blueF();
    }
    return QColor::fromRgbF(r/15,g/15,b/15);
}
bool containsCjk(const QString &text)
{
    return text.contains(QRegularExpression(QStringLiteral("[\\x{2E80}-\\x{9FFF}]")));
}
}

CandidateWindow::CandidateWindow(QWidget *parent, bool preview)
    : QWidget(parent, preview ? Qt::Widget : (Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
              | Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint)), m_preview(preview) {
    PinyinTones::warmUp();
    setObjectName("FcitxLiquidCandidate");
    setWindowTitle(QStringLiteral("RIME液态输入法"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_X11DoNotAcceptFocus);
    setFocusPolicy(Qt::NoFocus);
    setConfiguration(loadCandidateConfig());
    m_update.setSingleShot(true);
    connect(&m_update,&QTimer::timeout,this,&CandidateWindow::refresh);
    m_optics.setOptics(m_config.refraction,m_config.shade,m_config.highlight,m_config.chroma);
    capture=[](const QRect &rect) {
        // High-DPI auto scaling is disabled: the protocol and root coordinates
        // are physical X11 pixels. Fonts still respect the screen's Xft DPI.
        QScreen *screen=QGuiApplication::screenAt(rect.center());
        if (!screen) return QImage();
        return screen->grabWindow(0, rect.x()-screen->geometry().x(),
                                   rect.y()-screen->geometry().y(),rect.width(),rect.height()).toImage();
    };
}

void CandidateWindow::setConfiguration(const CandidateConfig &config) {
    m_config=config;
    if(m_preview && state.lookupVisible) updatePreviewCandidates();
    m_chineseFont=QFont(m_config.chineseFont,m_config.candidateFontSize>0?m_config.candidateFontSize:m_config.fontSize);
    m_latinFont=QFont(m_config.latinFont,m_config.headerFontSize>0?m_config.headerFontSize:m_config.fontSize);
    for (QFont *font : {&m_chineseFont,&m_latinFont}) {
        font->setBold(m_config.bold);
        font->setItalic(m_config.italic);
        font->setLetterSpacing(QFont::AbsoluteSpacing,m_config.letterSpacing);
    }
    setFont(m_chineseFont);
    m_pinyinFont=m_latinFont;
    m_pinyinFont.setPointSize(m_config.pinyinFontSize>0?m_config.pinyinFontSize:qMax(8,qRound(m_chineseFont.pointSize()*.72)));
    m_optics.setOptics(m_config.refraction,m_config.shade,m_config.highlight,m_config.chroma);
    m_material={};
    if (state.hasSpot) schedule();
}
void CandidateWindow::reloadConfiguration() { setConfiguration(loadCandidateConfig()); }
void CandidateWindow::showPreview() {
    state.preedit="rime ye tai"; state.caret=11;
    state.preeditVisible=true; state.lookupVisible=true; state.hasSpot=true;
    updatePreviewCandidates();
    state.next=true; state.spot=QRect(0,0,1,20);
    setPreviewBackground(0);
}
void CandidateWindow::updatePreviewCandidates() {
    const QStringList samples={QStringLiteral("液态"),QStringLiteral("实时预览"),QStringLiteral("玻璃"),QStringLiteral("中文"),QStringLiteral("外观"),"Rime",QStringLiteral("拼音"),QStringLiteral("设置"),QStringLiteral("候选词"),QStringLiteral("输入法")};
    state.texts=samples.mid(0,qBound(1,m_config.pageSize,10));state.labels.clear();
    const QStringList pinyins={"ye tai","shi shi yu lan","bo li","zhong wen","wai guan","","pin yin","she zhi","hou xuan ci","shu ru fa"};
    state.pinyins.clear();
    for(int i=0;i<state.texts.size();++i)state.pinyins<<PinyinTones::annotate(state.texts[i],pinyins[i]);
    for(int i=0;i<state.texts.size();++i) state.labels<<QString::number((i+1)%10)+".";
    state.cursor=qBound(0,state.cursor,state.texts.size()-1);
}
void CandidateWindow::setPreviewWidth(int width) {
    if(!m_preview || m_previewWidth==width)return;
    m_previewWidth=qMax(80,width);schedule();
}
void CandidateWindow::setPreviewBackground(int mode) {
    if(!m_preview)return;
    capture=[mode](QRect rect) {
        QImage image(rect.size(),QImage::Format_RGB32); QPainter p(&image);
        if(mode==1 || mode==2) {p.fillRect(image.rect(),mode==1?QColor(250,250,250):QColor(35,35,35));return image;}
        QLinearGradient gradient(0,0,image.width(),image.height());
        gradient.setColorAt(0,QColor(47,87,121)); gradient.setColorAt(.48,QColor(154,113,158));
        gradient.setColorAt(1,QColor(38,116,133)); p.fillRect(image.rect(),gradient);
        p.setPen(Qt::NoPen); p.setBrush(QColor(242,205,155,100));
        p.drawEllipse(QPoint(image.width()/2,image.height()/2),100,85);
        return image;
    };
    m_backdrop={}; m_material={};
    refresh();
}

QFont CandidateWindow::fontForText(const QString &text, bool candidate) const
{
    QFont font=containsCjk(text)?m_chineseFont:m_latinFont;
    const int size=candidate
        ? (m_config.candidateFontSize>0?m_config.candidateFontSize:m_config.fontSize)
        : (m_config.headerFontSize>0?m_config.headerFontSize:m_config.fontSize);
    font.setPointSize(size);
    return font;
}

QFontMetrics CandidateWindow::metricsFor(const QFont &font) const {
    // Qt 5's metrics constructor takes a mutable device even for measurement.
    return QFontMetrics(font,const_cast<CandidateWindow *>(this));
}

int CandidateWindow::textWidth(const QString &text, bool candidate) const
{
    return metricsFor(fontForText(text,candidate)).horizontalAdvance(text);
}

int CandidateWindow::textHeight() const
{
    return qMax(metricsFor(m_chineseFont).height(),metricsFor(m_latinFont).height());
}

bool CandidateWindow::hasContent() const {
    return (state.lookupVisible && !state.texts.isEmpty())
        || !headerText().isEmpty();
}
QString CandidateWindow::headerText() const {
    QString text=m_config.showPreedit && state.preeditVisible?state.preedit:QString();
    if(m_config.showAuxiliary && state.auxiliaryVisible && !state.auxiliary.isEmpty()) {
        if(!text.isEmpty())text+="  ";text+=state.auxiliary;
    }
    return text;
}
void CandidateWindow::dismissComposition() {
    hide();
    m_drawState={};m_drawHeader.clear();
    m_items.clear();m_mainRects.clear();m_readingRects.clear();
    m_backdrop={}; m_material={};
    m_backdropRect={}; m_materialGeometry={};
    m_optics.setWallpaper({});
    m_pressed=-1; m_wheel=0;
}
void CandidateWindow::schedule() {
    // Close notifications can be followed by a new composition before the
    // coalesced refresh runs. Retire the old capture at the actual boundary,
    // rather than inferring that boundary from visibility at refresh time.
    if (!hasContent()) dismissComposition();
    if (!m_update.isActive()) m_update.start(0);
}
void CandidateWindow::warmUp() {
    QImage sample(64,40,QImage::Format_RGB32); sample.fill(QColor(72,88,105));
    m_optics.setWallpaper(sample); m_optics.renderPanel(sample.rect(),12);
    qInfo()<<"[liquid-candidate] warmed material backend:"<<(m_optics.usedGpu()?"GPU Snell refraction":"CPU blur fallback");
    m_optics.setWallpaper({});
}
void CandidateWindow::clear() {
    m_update.stop(); state=CandidateState(); dismissComposition();
}

QRect CandidateWindow::place(QSize size, QRect spot, QRect available, int gap) {
    size=size.boundedTo(available.size());
    int x=qBound(available.left(),spot.x(),qMax(available.left(),available.right()-size.width()+1));
    int y=spot.y()+qMax(spot.height(),1)+gap;
    if (y+size.height()>available.bottom()+1) y=spot.y()-size.height()-gap;
    y=qBound(available.top(),y,qMax(available.top(),available.bottom()-size.height()+1));
    return QRect(QPoint(x,y),size);
}

int CandidateWindow::utf16Caret(const QString &text,int codepoints) {
    int pos=0;
    for (int i=0;i<codepoints && pos<text.size();++i,++pos)
        if (text.at(pos).isHighSurrogate() && pos+1<text.size() && text.at(pos+1).isLowSurrogate()) ++pos;
    return pos;
}

QPair<QStringList,QStringList> CandidateWindow::rubyParts(const QString &text,const QString &reading) const {
    const auto points=text.toUcs4();
    const QStringList sounds=reading.split(' ',QString::SkipEmptyParts);
    if(!reading.isEmpty() && points.size()==sounds.size()) {
        QStringList glyphs;
        for(uint cp:points)glyphs<<QString::fromUcs4(&cp,1);
        return {glyphs,sounds};
    }
    return {QStringList{text},reading.isEmpty()?QStringList():QStringList{reading}};
}
QVector<int> CandidateWindow::rubyWidths(const QStringList &glyphs,const QStringList &sounds) const {
    QVector<int> widths;
    for(int i=0;i<glyphs.size();++i)
        widths<<qMax(textWidth(glyphs[i],true),sounds.isEmpty()?0:metricsFor(m_pinyinFont).horizontalAdvance(sounds[i]))
            +px(sounds.isEmpty()?2:4);
    return widths;
}

QSize CandidateWindow::layoutContent(int maxWidth) {
    const int pad=px(m_config.horizontalPadding), shadow=px(m_config.shadowSize);
    const int count=state.lookupVisible?qMin(state.texts.size(),33):0;
    QRect mainInk,readingInk;
    for(int i=0;i<count;++i) {
        const QString &text=state.texts[i];
        mainInk=mainInk.united(metricsFor(fontForText(text,true)).tightBoundingRect(text));
        if(m_config.showLabels) {
            const QString label=state.labels.value(i);
            mainInk=mainInk.united(metricsFor(fontForText(label,true)).tightBoundingRect(label));
        }
        if(m_config.showCandidatePinyin) {
            const QString reading=state.pinyins.value(i);
            readingInk=readingInk.united(metricsFor(m_pinyinFont).tightBoundingRect(reading));
        }
    }
    if(mainInk.isEmpty())mainInk=metricsFor(m_chineseFont).tightBoundingRect(QStringLiteral("国"));
    const bool annotated=!readingInk.isEmpty();
    // Font metrics are already physical pixels at the screen's font DPI.
    // Pack the actual ink, including tones/descenders, without scaling it twice.
    const int mainHeight=qMax(1,mainInk.height());
    const int annotationHeight=annotated?readingInk.height():0;
    m_mainBaselineOffset=-mainInk.top();m_pinyinBaselineOffset=-readingInk.top();
    const int pinyinGap=px(m_config.pinyinGap);
    const int naturalRow=textHeight()+px(6);
    const int requiredRow=mainHeight+annotationHeight+(annotated?pinyinGap:0)+px(4);
    const int row=qMax(m_config.rowHeight>0?px(m_config.rowHeight):requiredRow,requiredRow);
    const int gap=px(m_config.candidateGap);
    const int maxBody=qMax(px(100),maxWidth-2*shadow);
    m_items.clear(); m_mainRects.clear();m_readingRects.clear();m_header={}; m_previous={}; m_next={};
    const QString header=headerText();
    int y=shadow+px(m_config.windowPadding), width=qMin(px(m_config.minWindowWidth),maxBody);
    int x=shadow+px(m_config.itemSidePadding);
    if (!header.isEmpty()) {
        const int headerWidth=qMin(textWidth(header)+px(12),qMax(1,maxBody-2*pad));
        const int headerRow=m_config.headerHeight>0?px(m_config.headerHeight):naturalRow;
        m_header=QRect(shadow+pad,y,headerWidth,headerRow);
        width=qMax(width,headerWidth+pad+px(8));
        if (count) {
            y+=m_header.height()+px(m_config.headerGap);
            x=shadow+px(m_config.itemSidePadding);
        } else {
            y+=m_header.height();
        }
    }
    for (int i=0;i<count;++i) {
        const QString label=m_config.showLabels?state.labels.value(i):QString();
        const QString pinyin=m_config.showCandidatePinyin?state.pinyins.value(i):QString();
        const auto parts=rubyParts(state.texts[i],pinyin);
        const auto cells=rubyWidths(parts.first,parts.second);
        int wordWidth=0;for(int cell:cells)wordWidth+=cell;
        const int item=qMin(maxBody-px(16),textWidth(label,true)+wordWidth+px(qMax(25,m_config.itemHorizontalPadding)));
        const bool vertical=m_config.layoutDirection==1 || (m_config.layoutDirection==2 && state.layout==1);
        if (vertical && i>0) { x=shadow+px(m_config.itemSidePadding); y+=row+gap; }
        else if (x>shadow+px(m_config.itemSidePadding) && x+item>shadow+maxBody-px(m_config.itemSidePadding)) { x=shadow+px(m_config.itemSidePadding); y+=row+gap; }
        const QRect itemRect(x,y,item,row);m_items.append(itemRect);
        const int labelWidth=label.isEmpty()?0:textWidth(label,true)+px(5);
        const QRect content=itemRect.adjusted(px(10)+labelWidth,0,-px(10),0);
        // Leave the same top band on unannotated words to keep Hanzi baselines aligned.
        const int mainTop=y+(annotated?(row-mainHeight-annotationHeight-pinyinGap)/2+annotationHeight+pinyinGap:(row-mainHeight)/2);
        QVector<QRect> mainRects,readingRects;
        int cellX=content.x();
        for(int j=0;j<cells.size();++j) {
            const int cellWidth=qMin(cells[j],qMax(0,content.right()+1-cellX));
            mainRects<<QRect(cellX,mainTop,cellWidth,mainHeight);
            if(!pinyin.isEmpty())readingRects<<QRect(cellX,mainTop-pinyinGap-annotationHeight,cellWidth,annotationHeight);
            cellX+=cells[j];
        }
        m_mainRects<<mainRects;m_readingRects<<readingRects;
        width=qMax(width,x-shadow+item+px(m_config.itemSidePadding));
        x+=item+gap;
    }
    if (count) {
        y+=row;
    }
    if (count && m_config.showPagingButtons && (state.previous || state.next)) {
        const int button=px(26);
        const int navWidth=2*button+px(4);
        int navY=y-row;
        if(x+px(4)+navWidth>shadow+maxBody-px(m_config.itemSidePadding)) {x=shadow+px(m_config.itemSidePadding);navY=y+px(2);y+=px(22);}
        const int navX=qMax(x+px(4),shadow+width-px(m_config.itemSidePadding)-navWidth);
        m_previous=QRect(navX,navY,button,px(18));
        m_next=QRect(navX+button+px(4),navY,button,px(18));
        width=qMax(width,navX+navWidth-shadow+px(m_config.itemSidePadding));
    }
    return QSize(width+2*shadow,y+px(m_config.windowPadding)+shadow);
}

void CandidateWindow::refresh() {
    if (!hasContent() || !state.hasSpot) { dismissComposition(); return; }
    QScreen *screen=QGuiApplication::screenAt(state.spot.topLeft());
    if (!screen) screen=QGuiApplication::primaryScreen();
    if (!screen) return;
    m_scale=qBound(1.0,screen->logicalDotsPerInch()/96.0,3.0);
    QRect available=screen->availableGeometry().adjusted(px(3),px(3),-px(3),-px(3));
    int layoutWidth=m_config.windowWidth>0
        ? qMin(px(m_config.windowWidth),available.width())
        : qMin(px(m_config.maxWindowWidth),available.width());
    if(m_preview && m_previewWidth>0)layoutWidth=qMin(layoutWidth,m_previewWidth);
    m_drawState=state;
    m_drawHeader=headerText();
    m_drawPinyin=m_config.showCandidatePinyin;
    m_drawLabels=m_config.showLabels;
    QSize size=layoutContent(layoutWidth);
    if (m_config.windowWidth>0) size.setWidth(layoutWidth);
    if (m_config.windowHeight>0) size.setHeight(px(m_config.windowHeight));
    // Fcitx supplies at most 33 candidates; wrapped rows stay inside the workarea.
    const QRect target=place(size,state.spot,available,px(4));
    const bool opening=!isVisible();
    if (m_preview) setFixedSize(target.size()); else setGeometry(target);
    prepareMaterial(opening || (m_preview && m_backdrop.isNull()));
    updateReadability();
    if (opening) { m_pressed=-1; m_wheel=0; show(); }
    update();
}

void CandidateWindow::prepareMaterial(bool opening) {
    const int shadow=px(m_config.shadowSize);
    const QRect body=geometry().adjusted(shadow,shadow,-shadow,-shadow);
    if (opening) {
        QScreen *screen=QGuiApplication::screenAt(body.center());
        if (screen) {
            m_backdropRect=body.adjusted(-px(160),-px(100),px(400),px(160)).intersected(screen->geometry());
            m_backdrop=capture(m_backdropRect);
            ++m_captures;
            if (!m_backdrop.isNull()) {
                m_backdrop.setDevicePixelRatio(1);
                if (m_backdrop.size()!=m_backdropRect.size())
                    m_backdrop=m_backdrop.scaled(m_backdropRect.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            }
        }
        m_material={};
    }
    if (!m_material.isNull() && m_materialGeometry==body) return;
    // Never sample while the panel is visible: it would capture its own text.
    // A move/growth beyond this composition's cache uses a neutral continuation.
    QImage source(body.size(),QImage::Format_RGB32);
    QColor fill(55,62,73);
    if (!m_backdrop.isNull()) fill=m_backdrop.pixelColor(m_backdrop.width()/2,m_backdrop.height()/2);
    source.fill(fill);
    {
        QPainter p(&source);
        if (!m_backdrop.isNull()) p.drawImage(m_backdropRect.topLeft()-body.topLeft(),m_backdrop);
    }
    const QColor center=sample(source,source.rect().adjusted(source.width()/10,source.height()/5,-source.width()/10,-source.height()/5));
    const qreal brightness=.299*center.redF()+.587*center.greenF()+.114*center.blueF();
    qreal light=m_config.autoContrast?qBound(0.,(brightness-.65)/.25,1.):0.;
    light=light*light*(3-2*light);
    m_optics.setLightSurface(light);
    m_optics.setWallpaper(source);
    m_material=m_optics.renderPanel(QRect(QPoint(),body.size()),px(m_config.cornerRadius));
    m_materialGeometry=body; ++m_renders;
    m_light=luminance(backdropColor(rect()))>.35;
    if (m_renders==1) qInfo()<<"[liquid-candidate] material backend:"<<(m_optics.usedGpu()?"GPU Snell refraction":"CPU blur fallback");
}

QColor CandidateWindow::backdropColor(const QRect &area,QColor overlay) const {
    const int shadow=px(m_config.shadowSize);
    const QColor base=sample(m_material,area.translated(-shadow,-shadow));
    return composite(overlay,composite(m_config.tintColor,base));
}
QColor CandidateWindow::readableColor(QColor preferred,const QRect &area,QColor overlay) const {
    if(!m_config.autoContrast)return preferred;
    const QColor bg=backdropColor(area,overlay);
    if(contrast(preferred,bg)>=4.5)return preferred;
    if(preferred==m_config.accentColor && contrast(QColor(16,70,122),bg)>=4.5)return QColor(16,70,122);
    const QColor dark(17,25,35),light(245,248,253);
    return contrast(dark,bg)>contrast(light,bg)?dark:light;
}
QColor CandidateWindow::selectionFill(const QRect &area) const {
    if(!m_config.autoContrast || luminance(backdropColor(area))<.35)return m_config.selectionColor;
    // Keep the user's hue; mix with pale blue to separate selection from a
    // light body. Use a bounded alpha so custom near-transparent fills remain visible.
    const QColor c=m_config.selectionColor;
    return QColor(qRound(c.red()*.35+104*.65),qRound(c.green()*.35+171*.65),qRound(c.blue()*.35+237*.65),qMax(100,c.alpha()));
}

void CandidateWindow::updateReadability() {
    m_headerInk=readableColor(m_config.headerColor,m_header);
    m_caretInk=readableColor(m_config.accentColor,m_header);
    m_textInks.clear();m_labelInks.clear();m_pinyinInks.clear();m_selectionFills.clear();m_navInks.clear();
    for(int i=0;i<m_items.size();++i) {
        const QRect r=m_items[i];
        const QColor fill=i==state.cursor?selectionFill(r):QColor(Qt::transparent);
        m_selectionFills<<fill;
        m_labelInks<<readableColor(m_config.accentColor,r,fill);
        m_textInks<<readableColor(i==state.cursor?m_config.selectedTextColor:m_config.textColor,r,fill);
        m_pinyinInks<<readableColor(m_config.headerColor,r,fill);
    }
    for(const auto &r:{m_previous,m_next})m_navInks<<readableColor(m_config.textColor,r);
}

void CandidateWindow::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const int shadow=px(m_config.shadowSize);
    QRectF body=rect().adjusted(shadow,shadow,-shadow,-shadow);
    p.setPen(Qt::NoPen);
    for (int i=shadow;i>0;--i) {
        p.setBrush(QColor(0,0,0,qMax(1,12-i)));
        p.drawRoundedRect(body.adjusted(-i/2.,0,i/2.,i),px(m_config.cornerRadius)+i/2.,px(m_config.cornerRadius)+i/2.);
    }
    p.drawImage(QPoint(shadow,shadow),m_material);
    if (m_config.tintColor.alpha()>0) {
        p.setPen(Qt::NoPen); p.setBrush(m_config.tintColor);
        p.drawRoundedRect(body,px(m_config.cornerRadius),px(m_config.cornerRadius));
    }
    p.setBrush(Qt::NoBrush); p.setPen(QPen(m_config.autoContrast && m_light?QColor(66,87,110,80):m_config.borderColor,qMax(1.,m_scale*.65)));
    p.drawRoundedRect(body.adjusted(.7,.7,-.7,-.7),px(m_config.cornerRadius),px(m_config.cornerRadius));

    if (!m_header.isEmpty()) {
        const QString header=m_drawHeader;
        const QFont headerFont=fontForText(header);
        const QFontMetrics headerMetrics=metricsFor(headerFont);
        p.setFont(headerFont);
        p.setPen(m_headerInk);
        const QString shown=headerMetrics.elidedText(header,Qt::ElideRight,m_header.width());
        p.drawText(m_header,Qt::AlignVCenter|Qt::AlignLeft,shown);
        if (m_config.showPreedit && m_drawState.preeditVisible && !m_drawState.preedit.isEmpty() && shown==header) {
            const int x=m_header.x()+headerMetrics.horizontalAdvance(m_drawState.preedit.left(utf16Caret(m_drawState.preedit,m_drawState.caret)));
            p.setPen(QPen(m_caretInk,qMax(1.,m_scale)));
            p.drawLine(x,m_header.y()+px(3),x,m_header.bottom()-px(3));
        }
    }
    for (int i=0;i<m_items.size();++i) {
        const QRect r=m_items[i];
        const QColor fill=m_selectionFills.value(i);
        if (i==m_drawState.cursor) {
            p.setPen(Qt::NoPen); p.setBrush(fill);
            p.drawRoundedRect(r,px(9),px(9));
        }
        const QString label=m_drawLabels?m_drawState.labels.value(i):QString();
        const QString candidate=m_drawState.texts.value(i);
        const QFont labelFont=fontForText(label,true);
        const QFont candidateFont=fontForText(candidate,true);
        const int labelWidth=label.isEmpty()?0:metricsFor(labelFont).horizontalAdvance(label)+px(5);
        const auto mainRects=m_mainRects.value(i);
        const auto readingRects=m_readingRects.value(i);
        if(mainRects.isEmpty())continue;
        QRect labelRect(r.x()+px(10),mainRects[0].y(),labelWidth,mainRects[0].height());
        p.setFont(labelFont);p.setPen(m_labelInks.value(i));
        p.drawText(QPoint(labelRect.x(),labelRect.y()+m_mainBaselineOffset),label);
        const QString pinyin=m_drawPinyin?m_drawState.pinyins.value(i):QString();
        const auto parts=rubyParts(candidate,pinyin);
        for(int j=0;j<mainRects.size() && j<parts.first.size();++j) {
            const QRect main=mainRects[j];
            if(main.width()<=0)continue;
            p.setFont(candidateFont);p.setPen(m_textInks.value(i));
            const QFontMetrics metrics=metricsFor(candidateFont);
            const QString shown=metrics.elidedText(parts.first[j],Qt::ElideRight,main.width());
            const int textX=main.x()+(pinyin.isEmpty()?0:(main.width()-metrics.horizontalAdvance(shown))/2);
            p.drawText(QPoint(textX,main.y()+m_mainBaselineOffset),shown);
            if(j<readingRects.size() && j<parts.second.size() && !readingRects[j].isEmpty()) {
                p.setFont(m_pinyinFont);p.setPen(m_pinyinInks.value(i));
                const QRect reading=readingRects[j];const QFontMetrics metrics=metricsFor(m_pinyinFont);
                const QString shown=metrics.elidedText(parts.second[j],Qt::ElideRight,reading.width());
                p.drawText(QPoint(reading.x()+(reading.width()-metrics.horizontalAdvance(shown))/2,
                                 reading.y()+m_pinyinBaselineOffset),shown);
            }
        }
    }
    for (int i=0;i<2;++i) {
        const QRect r=i?m_next:m_previous;
        if (r.isEmpty()) continue;
        QColor c=m_navInks.value(i); c.setAlpha((i?m_drawState.next:m_drawState.previous)?210:65);
        p.setPen(QPen(c,qMax(1.5,m_scale*1.3),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const QPoint center=r.center(); const int d=px(3);
        QPolygonF line; line<<QPointF(center.x()+(i?-d:d),center.y()-d)
                           <<QPointF(center.x()+(i?d:-d),center.y())
                           <<QPointF(center.x()+(i?-d:d),center.y()+d);
        p.drawPolyline(line);
    }
}
int CandidateWindow::hit(QPoint pos) const {
    for (int i=0;i<m_items.size();++i) if (m_items[i].contains(pos)) return i;
    if (state.previous && m_previous.contains(pos)) return -2;
    if (state.next && m_next.contains(pos)) return -3;
    return -1;
}
void CandidateWindow::mousePressEvent(QMouseEvent *event) {
    m_pressed=event->button()==Qt::LeftButton?hit(event->pos()):-1; event->accept();
}
void CandidateWindow::mouseReleaseEvent(QMouseEvent *event) {
    const int target=hit(event->pos());
    if (event->button()==Qt::LeftButton && target==m_pressed) {
        if (target>=0) emit selected(target);
        else if (target==-2) emit pageUp();
        else if (target==-3) emit pageDown();
    }
    m_pressed=-1; event->accept();
}
void CandidateWindow::wheelEvent(QWheelEvent *event) {
    if(!m_config.scrollToPage){m_wheel=0;event->ignore();return;}
    m_wheel+=event->angleDelta().y();
    if (m_wheel>=120) { if (state.previous) emit pageUp(); m_wheel=0; }
    if (m_wheel<=-120) { if (state.next) emit pageDown(); m_wheel=0; }
    event->accept();
}
