#include "PinyinTones.h"
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

static void initializePinyinData() { Q_INIT_RESOURCE(pinyin); }

namespace {
QString base(QString syllable) {
    syllable=syllable.toLower().normalized(QString::NormalizationForm_D);
    syllable.replace(QStringLiteral("u\u0308"),QStringLiteral("v"));
    syllable.replace(QStringLiteral("u:"),QStringLiteral("v"));
    QString result;
    for(QChar c:syllable)
        if(c.category()!=QChar::Mark_NonSpacing && !c.isDigit())result+=c;
    return result;
}
QStringList syllables(const QString &text) {
    static const QRegularExpression space(QStringLiteral("[\\s'’]+"));
    return text.split(space,QString::SkipEmptyParts);
}
struct Dictionary {
    QHash<uint,QStringList> characters;
    QHash<QString,QStringList> phrases;
    int maxPhrase=0;
    Dictionary() {
        initializePinyinData();
        QFile chars(QStringLiteral(":/fcitx-liquid/pinyin/characters.txt"));
        if(chars.open(QIODevice::ReadOnly))while(!chars.atEnd()) {
            const QString line=QString::fromUtf8(chars.readLine());
            if(!line.startsWith("U+"))continue;
            const int colon=line.indexOf(':');bool valid=false;
            const uint cp=line.mid(2,colon-2).toUInt(&valid,16);
            if(valid)characters.insert(cp,line.mid(colon+1).section('#',0,0).trimmed().split(','));
        }
        QFile words(QStringLiteral(":/fcitx-liquid/pinyin/phrases.txt"));
        if(words.open(QIODevice::ReadOnly))while(!words.atEnd()) {
            const QString line=QString::fromUtf8(words.readLine());
            if(line.startsWith('#'))continue;
            const int colon=line.indexOf(':');if(colon<1)continue;
            const QString word=line.left(colon);
            const auto reading=syllables(line.mid(colon+1).trimmed());
            const int length=word.toUcs4().size();
            if(length!=reading.size())continue;
            // The first upstream entry is the preferred pronunciation.
            if(!phrases.contains(word))phrases.insert(word,reading);
            maxPhrase=qMax(maxPhrase,length);
        }
    }
};
const Dictionary &dictionary() {static const Dictionary data;return data;}
}

void PinyinTones::warmUp() { (void)dictionary(); }

QString PinyinTones::annotate(const QString &text,const QString &reading) {
    const auto codepoints=text.toUcs4();
    const auto hints=syllables(reading);
    if(codepoints.isEmpty() || codepoints.size()!=hints.size())return reading;
    const auto &data=dictionary();
    QStringList result=hints;
    for(int pos=0;pos<codepoints.size();) {
        int matched=0;
        for(int size=qMin(data.maxPhrase,codepoints.size()-pos);size>=2;--size) {
            const auto it=data.phrases.constFind(QString::fromUcs4(codepoints.constData()+pos,size));
            if(it==data.phrases.cend())continue;
            bool compatible=true;
            for(int j=0;j<size;++j)if(base(it.value()[j])!=base(hints[pos+j])) {compatible=false;break;}
            if(!compatible)continue;
            for(int j=0;j<size;++j)result[pos+j]=it.value()[j];
            matched=size;break;
        }
        if(matched) {pos+=matched;continue;}
        const auto it=data.characters.constFind(codepoints[pos]);
        if(it!=data.characters.cend())for(const QString &sound:it.value()) {
            if(base(sound)==base(hints[pos])) {result[pos]=sound;break;}
        }
        ++pos;
    }
    return result.join(' ');
}
