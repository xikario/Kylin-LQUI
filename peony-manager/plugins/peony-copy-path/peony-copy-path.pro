QT += core gui widgets
CONFIG += c++11 plugin release
TEMPLATE = lib
TARGET = peony-copy-path

PEONY_HEADERS = /usr/include/peony-qt
exists($$PWD/../../vendor/extract/libpeony-dev/usr/include/peony-qt) {
    PEONY_HEADERS = $$PWD/../../vendor/extract/libpeony-dev/usr/include/peony-qt
}

INCLUDEPATH += $$PEONY_HEADERS
LIBS += -lpeony

DESTDIR = $$PWD/build

SOURCES += src/copy-path-plugin.cpp
HEADERS += src/copy-path-plugin.h

DISTFILES += src/peony-copy-path.json
