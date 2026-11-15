#pragma once
#include <QLocale>
#include <QString>

inline bool useEnglishUi() {
    static const bool english = QLocale::system().language() != QLocale::Russian;
    return english;
}

inline QString uiText(const char *russian, const char *english) {
    return QString::fromUtf8(useEnglishUi() ? english : russian);
}
