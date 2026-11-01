#include "Markdown.h"
#include <QRegularExpression>
#include <QStringList>

namespace Markdown {
namespace {
QString codeBlockStyle(bool light){return QString("background-color:%1;color:%2;font-family:'Sarasa Fixed SC','Noto Sans Mono CJK SC',monospace;white-space:pre-wrap;margin:6px 0;padding:8px;").arg(light?"#e1ece8":"#0b2119",light?"#203e32":"#e3f5ea");}
QString inlineCodeStyle(bool light){return QString("background-color:%1;color:%2;font-family:'Sarasa Fixed SC','Noto Sans Mono CJK SC',monospace;").arg(light?"#e1ece8":"#0b2119",light?"#203e32":"#c9f4dc");}

QString inlineSpans(const QString &text,bool light) {
    // Code spans are split out first so emphasis/link rules never touch them.
    QString result;const QStringList parts=text.split('`');
    for(int i=0;i<parts.size();++i) {
        const bool code=i%2==1&&i<parts.size()-1;
        if(code){result+="<span style=\""+inlineCodeStyle(light)+"\">&nbsp;"+escape(parts[i])+"&nbsp;</span>";continue;}
        QString plain=escape(i%2==1?"`"+parts[i]:parts[i]);
        static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\((https?://[^)\\s]+)\\)"));
        plain.replace(link,QString("<a style=\"color:%1\" href=\"\\2\">\\1</a>").arg(light?"#176855":"#9fe9c6"));
        static const QRegularExpression bold(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
        plain.replace(bold,QStringLiteral("<b>\\1</b>"));
        static const QRegularExpression italic(QStringLiteral("(^|[^*\\w])\\*([^*\\s][^*]*)\\*(?!\\*)"));
        plain.replace(italic,QStringLiteral("\\1<i>\\2</i>"));
        result+=plain;
    }
    return result;
}
bool tableRow(const QString &line){const auto t=line.trimmed();return t.startsWith('|')&&t.endsWith('|')&&t.size()>1;}
QStringList cells(const QString &line) {
    QString t=line.trimmed();t=t.mid(1,t.size()-2);QStringList out;
    for(const auto &c:t.split('|'))out<<c.trimmed();return out;
}
}

QString escape(const QString &text){return text.toHtmlEscaped();}

QString toHtml(const QString &markdown,bool light) {
    const QStringList lines=QString(markdown).replace("\r\n","\n").split('\n');
    QString html;QStringList paragraph;QString listTag;
    auto closeParagraph=[&]{if(!paragraph.isEmpty()){html+="<p style=\"margin:4px 0\">"+paragraph.join("<br>")+"</p>";paragraph.clear();}};
    auto closeList=[&]{if(!listTag.isEmpty()){html+="</"+listTag+">";listTag.clear();}};
    static const QRegularExpression heading(QStringLiteral("^(#{1,6})\\s+(.*)$"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*[-*+]\\s+(.*)$"));
    static const QRegularExpression ordered(QStringLiteral("^\\s*\\d+[.)]\\s+(.*)$"));
    static const QRegularExpression separator(QStringLiteral("^\\s*\\|?\\s*:?-{3,}"));
    for(int i=0;i<lines.size();++i) {
        const QString &line=lines[i];const QString trimmed=line.trimmed();
        if(trimmed.startsWith("```")) {
            closeParagraph();closeList();QStringList code;
            while(++i<lines.size()&&!lines[i].trimmed().startsWith("```"))code<<lines[i];
            html+="<pre style=\""+codeBlockStyle(light)+"\">"+escape(code.join('\n'))+"</pre>";continue;
        }
        if(trimmed.isEmpty()){closeParagraph();closeList();continue;}
        if(tableRow(line)&&i+1<lines.size()&&separator.match(lines[i+1]).hasMatch()) {
            closeParagraph();closeList();
            html+="<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" style=\"margin:6px 0;border-color:rgba(200,240,220,60)\" border=\"1\"><tr>";
            for(const auto &c:cells(line))html+="<th align=\"left\">"+inlineSpans(c,light)+"</th>";
            html+="</tr>";++i;
            while(i+1<lines.size()&&tableRow(lines[i+1])){html+="<tr>";for(const auto &c:cells(lines[++i]))html+="<td>"+inlineSpans(c,light)+"</td>";html+="</tr>";}
            html+="</table>";continue;
        }
        auto m=heading.match(trimmed);
        if(m.hasMatch()) {
            closeParagraph();closeList();const int level=m.captured(1).size();
            const int size=level<=1?17:level==2?16:15;
            html+=QString("<p style=\"margin:8px 0 4px 0;font-size:%1px;font-weight:600\">%2</p>").arg(size).arg(inlineSpans(m.captured(2),light));continue;
        }
        if(trimmed.startsWith('>')){closeParagraph();closeList();html+=QString("<p style=\"margin:4px 0;color:%1\">┃ ").arg(light?"#496171":"#b4c5d3")+inlineSpans(trimmed.mid(1).trimmed(),light)+"</p>";continue;}
        auto b=bullet.match(line);auto o=ordered.match(line);
        if(b.hasMatch()||o.hasMatch()) {
            closeParagraph();const QString tag=b.hasMatch()?"ul":"ol";
            if(listTag!=tag){closeList();listTag=tag;html+="<"+tag+" style=\"margin:2px 0 2px -18px\">";}
            html+="<li>"+inlineSpans((b.hasMatch()?b:o).captured(1),light)+"</li>";continue;
        }
        if(trimmed=="---"||trimmed=="***"){closeParagraph();closeList();html+="<hr>";continue;}
        closeList();paragraph<<inlineSpans(line,light);
    }
    closeParagraph();closeList();return html;
}
}
