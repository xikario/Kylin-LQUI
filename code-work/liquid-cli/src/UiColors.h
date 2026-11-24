#pragma once
#include <QColor>
#include <QPalette>
#include <QString>

// One contrast decision for the rendered glass, shared by all chat surfaces.
struct UiColors {
    QString text,muted,accent,field,border,bubble,userBubble;
    QString paper,wash;
    static UiColors chat(bool light,const QString &id) {
        if(id=="gemini")return {"#342A48","#695A78","#775194","#FFFFFF","#DDD3E5","#FFFCFF","#EBDFF6","#F9F6FC","#E9DDF6"};
        if(id=="claude")return {"#452E29","#775D53","#965332","#FFFDFC","#E4D4C9","#FFFCFA","#F8E3D5","#FFF9F4","#F9E1CD"};
        if(id=="fish")return {"#26384A","#536B80","#356486","#FCFEFF","#CEDCE7","#FCFEFF","#DCEDF9","#F5FAFD","#DCECF9"};
        if(id=="gpt")return {"#293D39","#536C64","#396B5F","#FEFFFD","#CEDFD7","#FDFFFD","#DFEFE6","#F6FAF7","#DDEDE1"};
        return forSurface(light);
    }
    static QColor color(const QString &css) {
        if(!css.startsWith("rgba("))return QColor(css);
        const auto parts=css.mid(5,css.size()-6).split(',');
        return QColor(parts.value(0).toInt(),parts.value(1).toInt(),parts.value(2).toInt(),parts.value(3).toInt());
    }
    static UiColors forSurface(bool light) {
        return light?UiColors{"#192d3c","#496171","#176855","rgba(243,248,252,220)","rgba(30,60,75,65)","rgba(246,250,252,230)","rgba(218,239,231,235)"}
                    :UiColors{"#edf5fc","#b4c5d3","#a0e7ce","rgba(10,20,30,195)","rgba(218,238,250,75)","rgba(7,19,29,190)","rgba(28,58,54,210)"};
    }
    QPalette palette(QPalette p) const {
        for(auto group:{QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
            for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})p.setColor(group,role,QColor(group==QPalette::Disabled?muted:text));
            p.setColor(group,QPalette::PlaceholderText,QColor(muted));
            p.setColor(group,QPalette::Highlight,QColor(paper.isEmpty()?QString("#397d73"):accent));p.setColor(group,QPalette::HighlightedText,Qt::white);
        }
        return p;
    }
};
