#pragma once
#include <QString>

// Qt 5.12 has no QTextDocument::setMarkdown. This covers what chat replies
// use: fenced code, inline code, emphasis, links, headings, lists, quotes and
// pipe tables. All source text is HTML-escaped before markup is added.
namespace Markdown {
QString toHtml(const QString &markdown,bool light=false);
QString escape(const QString &text);
}
