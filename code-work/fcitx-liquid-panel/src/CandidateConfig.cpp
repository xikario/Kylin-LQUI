#include "CandidateConfig.h"
#include <QSettings>
#include <QDir>
#include <type_traits>
namespace {
template<typename T> ConfigField field(const char *key,const char *label,T CandidateConfig::*member,ConfigField::Kind kind) {
    ConfigField f; f.key=key; f.label=QString::fromUtf8(label); f.group=f.key.section('/',0,0); f.kind=kind;
    f.get=[member](const CandidateConfig &c){ return QVariant::fromValue(c.*member); };
    f.set=[member](CandidateConfig &c,const QVariant &v){
        if constexpr(std::is_same<T,QColor>::value) { QColor color(v.toString()); if(color.isValid()) c.*member=color; }
        else if constexpr(std::is_same<T,QString>::value) { if(!v.toString().trimmed().isEmpty()) c.*member=v.toString(); }
        else c.*member=v.value<T>();
    }; return f;
}
template<typename T> ConfigField number(const char *key,const char *label,T CandidateConfig::*member,double lo,double hi,double step) {
    auto f=field(key,label,member,ConfigField::Number); f.minimum=lo; f.maximum=hi; f.step=step;
    f.set=[member,lo,hi](CandidateConfig &c,const QVariant &v){ bool ok=false; double x=v.toDouble(&ok); if(ok) c.*member=T(qBound(lo,x,hi)); };
    return f;
}
ConfigField choice(const char *key,const char *label,int CandidateConfig::*member,const QStringList &choices) {
    auto f=number(key,label,member,0,choices.size()-1,1);
    f.kind=ConfigField::Choice;f.choices=choices;return f;
}
}
const QVector<ConfigField> &candidateFields() {
    static const QVector<ConfigField> fields={
        number("Interface/PageSize","每页候选词个数",&CandidateConfig::pageSize,1,10,1),
        choice("Interface/Layout","排版方向",&CandidateConfig::layoutDirection,{QStringLiteral("横排"),QStringLiteral("竖排"),QStringLiteral("跟随输入方案")}),
        field("Interface/ShowPreedit","显示顶部输入栏",&CandidateConfig::showPreedit,ConfigField::Boolean),
        field("Interface/ShowCandidatePinyin","显示候选词拼音",&CandidateConfig::showCandidatePinyin,ConfigField::Boolean),
        field("Interface/ShowAuxiliary","显示辅助提示",&CandidateConfig::showAuxiliary,ConfigField::Boolean),
        field("Interface/ShowLabels","显示候选序号",&CandidateConfig::showLabels,ConfigField::Boolean),
        field("Interface/ShowPagingButtons","显示翻页按钮",&CandidateConfig::showPagingButtons,ConfigField::Boolean),
        field("Interface/ScrollToPage","滚轮翻页",&CandidateConfig::scrollToPage,ConfigField::Boolean),
        field("Fonts/ChineseFamily","中文字体",&CandidateConfig::chineseFont,ConfigField::Font),
        field("Fonts/LatinFamily","英文 / 拼音字体",&CandidateConfig::latinFont,ConfigField::Font),
        number("Fonts/Size","通用字号（pt）",&CandidateConfig::fontSize,8,48,1),
        number("Fonts/HeaderSize","顶部输入栏字号（0=继承）",&CandidateConfig::headerFontSize,0,48,1),
        number("Fonts/PinyinSize","候选拼音字号（0=自动）",&CandidateConfig::pinyinFontSize,0,32,1),
        number("Fonts/CandidateSize","候选文字字号（0=继承）",&CandidateConfig::candidateFontSize,0,48,1),
        field("Fonts/Bold","粗体",&CandidateConfig::bold,ConfigField::Boolean),
        field("Fonts/Italic","斜体",&CandidateConfig::italic,ConfigField::Boolean),
        number("Fonts/LetterSpacing","字间距",&CandidateConfig::letterSpacing,-8,24,.05),
        number("Geometry/WindowWidth","窗口宽度（0=自动）",&CandidateConfig::windowWidth,0,4096,1),
        number("Geometry/WindowHeight","窗口高度（0=自动）",&CandidateConfig::windowHeight,0,2048,1),
        number("Geometry/MinWindowWidth","最小宽度",&CandidateConfig::minWindowWidth,80,4096,1),
        number("Geometry/MaxWindowWidth","最大宽度",&CandidateConfig::maxWindowWidth,160,4096,1),
        number("Geometry/RowHeight","候选整体行高（0=自动）",&CandidateConfig::rowHeight,0,256,1),
        number("Geometry/PinyinGap","拼音与汉字间距",&CandidateConfig::pinyinGap,0,32,1),
        number("Geometry/HeaderHeight","顶部输入栏高度（0=自动）",&CandidateConfig::headerHeight,0,256,1),
        number("Geometry/WindowPadding","上下留白",&CandidateConfig::windowPadding,0,64,1),
        number("Geometry/HorizontalPadding","顶部输入栏左右留白",&CandidateConfig::horizontalPadding,0,128,1),
        number("Geometry/ItemSidePadding","候选栏留白",&CandidateConfig::itemSidePadding,0,64,1),
        number("Geometry/ItemHorizontalPadding","候选项内边距",&CandidateConfig::itemHorizontalPadding,0,128,1),
        number("Geometry/CandidateGap","候选间距",&CandidateConfig::candidateGap,0,64,1),
        number("Geometry/HeaderGap","输入栏与候选栏间距",&CandidateConfig::headerGap,0,64,1),
        number("Geometry/ShadowSize","阴影大小",&CandidateConfig::shadowSize,0,64,1),
        number("Geometry/CornerRadius","圆角半径",&CandidateConfig::cornerRadius,0,128,1),
        number("Effects/Refraction","折射强度",&CandidateConfig::refraction,0,8,.05),
        number("Effects/Shade","底色深度",&CandidateConfig::shade,0,1,.05),
        number("Effects/Highlight","边缘高光",&CandidateConfig::highlight,0,1,.05),
        number("Effects/Chroma","色彩浓度",&CandidateConfig::chroma,0,1.3,.05),
        field("Colors/AutoContrast","浅色背景自动使用深色字",&CandidateConfig::autoContrast,ConfigField::Boolean),
        field("Colors/Text","候选文字",&CandidateConfig::textColor,ConfigField::Color),
        field("Colors/Header","输入栏 / 候选拼音文字",&CandidateConfig::headerColor,ConfigField::Color),
        field("Colors/Accent","序号与光标",&CandidateConfig::accentColor,ConfigField::Color),
        field("Colors/SelectedText","选中文字",&CandidateConfig::selectedTextColor,ConfigField::Color),
        field("Colors/Selection","选中框（支持透明度）",&CandidateConfig::selectionColor,ConfigField::Color),
        field("Colors/Border","边框高光（支持透明度）",&CandidateConfig::borderColor,ConfigField::Color),
        field("Colors/Tint","玻璃染色（支持透明度）",&CandidateConfig::tintColor,ConfigField::Color),
    }; return fields;
}
static QString configRoot() {
    return qEnvironmentVariable("XDG_CONFIG_HOME",QDir::homePath()+"/.config");
}
QString candidateConfigPath() { return configRoot()+"/fcitx/liquid-panel.conf"; }
CandidateConfig loadCandidateConfig(const QString &path) {
    CandidateConfig c;
    QSettings legacy(configRoot()+"/fcitx/conf/fcitx-classic-ui.config",QSettings::IniFormat);
    c.chineseFont=legacy.value("ClassicUI/Font",c.chineseFont).toString();
    c.fontSize=qBound(8,legacy.value("ClassicUI/FontSize",c.fontSize).toInt(),48);
    QSettings settings(path,QSettings::IniFormat); settings.setIniCodec("UTF-8");
    for(const auto &f:candidateFields()) if(settings.contains(f.key)) f.set(c,settings.value(f.key));
    c.maxWindowWidth=qMax(c.minWindowWidth,c.maxWindowWidth);
    return c;
}
bool saveCandidateConfig(const CandidateConfig &c,const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings settings(path,QSettings::IniFormat); settings.setIniCodec("UTF-8");
    for(const auto &f:candidateFields()) {
        QVariant v=f.get(c); if(f.kind==ConfigField::Color) v=v.value<QColor>().name(QColor::HexArgb);
        settings.setValue(f.key,v);
    }
    settings.sync(); return settings.status()==QSettings::NoError;
}
