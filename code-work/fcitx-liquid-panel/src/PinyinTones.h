#pragma once
#include <QString>

// Local pronunciation data is loaded once; lookups never run external tools.
namespace PinyinTones {
void warmUp();
QString annotate(const QString &text, const QString &reading);
}
