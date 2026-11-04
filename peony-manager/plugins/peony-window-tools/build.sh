#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$(cd "$ROOT/../.." && pwd)"
ARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
SDK="${PEONY_SDK_ROOT:-$PROJECT}"
if [ ! -d "$SDK/vendor" ] && [ -d "$PROJECT/../../peony-manager/vendor" ]; then SDK="$PROJECT/../../peony-manager"; fi
QT="$SDK/vendor/extract/qt/usr"
INC="$QT/include/$ARCH/qt5"
MOC="$QT/lib/qt5/bin/moc"
PEONY="$SDK/vendor/extract/libpeony-dev/usr/include/peony-qt"
if [ -d "/usr/include/$ARCH/qt5" ]; then INC="/usr/include/$ARCH/qt5"; MOC="$(command -v moc)"; fi
if [ -d /usr/include/peony-qt ]; then PEONY=/usr/include/peony-qt; fi
mkdir -p "$ROOT/build"
COMMON=(-std=c++11 -O2 -fPIC -I"$INC" -I"$INC/QtCore" -I"$INC/QtGui" -I"$INC/QtWidgets" -I"$PEONY")
LIBS=("/usr/lib/$ARCH/libQt5Widgets.so.5" "/usr/lib/$ARCH/libQt5Gui.so.5" "/usr/lib/$ARCH/libQt5Core.so.5")
"$MOC" "${COMMON[@]:3}" "$ROOT/src/window-tools-plugin.h" -o "$ROOT/build/moc_window-tools.cpp"
g++ "${COMMON[@]}" -shared "$ROOT/src/window-tools-plugin.cpp" "$ROOT/build/moc_window-tools.cpp" -o "$ROOT/build/libpeony-window-tools.so" "${LIBS[@]}"
