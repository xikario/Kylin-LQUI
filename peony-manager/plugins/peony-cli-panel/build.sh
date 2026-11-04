#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$ROOT_DIR/build"
SDK_ROOT=""

usage() {
    cat <<'EOF'
Usage: build.sh [--sdk-root VENDOR_DIR] [--output-dir DIR]

Build the Peony CLI panel plugin, its terminal runtime, and session helper.
By default, dependencies are discovered from the host development packages.
--sdk-root selects an explicit SDK/vendor directory with the documented layout.
EOF
}

while (($#)); do
    case "$1" in
        --sdk-root)
            (($# >= 2)) && [[ -n "$2" ]] || { echo "build.sh: --sdk-root requires a directory" >&2; exit 2; }
            SDK_ROOT="$2"
            shift 2
            ;;
        --output-dir)
            (($# >= 2)) && [[ -n "$2" ]] || { echo "build.sh: --output-dir requires a directory" >&2; exit 2; }
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "build.sh: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

fail_dependency() {
    echo "build.sh: missing dependency: $*" >&2
    echo "Install the matching development/runtime packages or pass --sdk-root to a compatible extracted SDK." >&2
    exit 2
}

require_file() {
    [[ -f "$1" ]] || fail_dependency "$2 (expected $1)"
}

system_library() {
    local soname path
    soname="$1"
    path="$(ldconfig -p 2>/dev/null | awk -v name="$soname" '$1 == name { print $NF; exit }' || true)"
    [[ -n "$path" && -f "$path" ]] || fail_dependency "system runtime library $soname"
    printf '%s\n' "$path"
}

if ! command -v g++ >/dev/null 2>&1; then
    fail_dependency "a C++ compiler (g++)"
fi

COMMON=(-std=c++11 -O2 -fPIC -DQT_NO_DEBUG)
QT_CFLAGS=()
QT_LIBS=()
TERM_CFLAGS=()
TERM_LIBS=()
PEONY_CFLAGS=()
MOC=""
QTERM_LIB=""
UTF8PROC_LIB=""
QTERMWIDGET_DATA=""
QTERMWIDGET_DOC=""
QT_LIB_DIR=""

if [[ -n "$SDK_ROOT" ]]; then
    SDK_ROOT="$(cd "$SDK_ROOT" 2>/dev/null && pwd)" || fail_dependency "SDK directory $SDK_ROOT"
    MULTIARCH="$(g++ -print-multiarch 2>/dev/null || true)"
    [[ -n "$MULTIARCH" ]] || fail_dependency "compiler multiarch information"

    QT_USR="$SDK_ROOT/extract/qt/usr"
    PEONY_USR="$SDK_ROOT/extract/libpeony-dev/usr"
    TERM_USR="$SDK_ROOT/qtermwidget/extract/usr"
    QT_INC="$QT_USR/include/$MULTIARCH/qt5"
    QT_LIB_DIR="$QT_USR/lib/$MULTIARCH"
    MOC="$QT_USR/lib/qt5/bin/moc"
    PEONY_INC="$PEONY_USR/include/peony-qt"
    TERM_INC="$TERM_USR/include/qtermwidget5"
    QTERM_LIB="$TERM_USR/lib/$MULTIARCH/libqtermwidget5.so.0"
    UTF8PROC_LIB="$TERM_USR/lib/$MULTIARCH/libutf8proc.so.2"
    QTERMWIDGET_DATA="$TERM_USR/share/qtermwidget5"
    QTERMWIDGET_DOC="$TERM_USR/share/doc"

    for module in QtCore QtGui QtWidgets QtNetwork; do
        [[ -d "$QT_INC/$module" ]] || fail_dependency "Qt5 $module headers in SDK"
    done
    require_file "$QT_INC/QtCore/QObject" "Qt5 headers"
    require_file "$MOC" "Qt5 moc"
    require_file "$PEONY_INC/menu-plugin-iface.h" "Peony interface headers"
    require_file "$TERM_INC/qtermwidget.h" "QTermWidget development headers"
    require_file "$QTERM_LIB" "QTermWidget runtime library"
    require_file "$UTF8PROC_LIB" "utf8proc runtime library"

    QT_CFLAGS=(-DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_NETWORK_LIB
        -I"$QT_INC" -I"$QT_INC/QtCore" -I"$QT_INC/QtGui" -I"$QT_INC/QtWidgets" -I"$QT_INC/QtNetwork")
    TERM_CFLAGS=(-I"$TERM_INC")
    PEONY_CFLAGS=(-I"$PEONY_INC")
    QT_LIBS=("$(system_library libQt5Widgets.so.5)" "$(system_library libQt5Gui.so.5)"
        "$(system_library libQt5Core.so.5)" "$(system_library libQt5Network.so.5)")
    TERM_LIBS=("$QTERM_LIB")
else
    command -v pkg-config >/dev/null 2>&1 || fail_dependency "pkg-config"
    pkg-config --exists Qt5Widgets Qt5Network || fail_dependency "Qt5 Core/Gui/Widgets/Network development packages"
    pkg-config --exists qtermwidget5 || fail_dependency "QTermWidget development package (qtermwidget5.pc)"
    pkg-config --exists peony || fail_dependency "Peony development package (peony.pc)"

    read -r -a QT_CFLAGS <<<"$(pkg-config --cflags Qt5Widgets Qt5Network)"
    read -r -a QT_LIBS <<<"$(pkg-config --libs Qt5Widgets Qt5Network)"
    read -r -a TERM_CFLAGS <<<"$(pkg-config --cflags qtermwidget5)"
    read -r -a TERM_LIBS <<<"$(pkg-config --libs qtermwidget5)"
    read -r -a PEONY_CFLAGS <<<"$(pkg-config --cflags-only-I peony)"
    PEONY_INCLUDEDIR="$(pkg-config --variable=includedir peony)"
    require_file "$PEONY_INCLUDEDIR/menu-plugin-iface.h" "Peony interface headers"

    QT_LIB_DIR="$(pkg-config --variable=libdir qtermwidget5)"
    QTERMWIDGET_DATA="$(pkg-config --variable=prefix qtermwidget5)/share/qtermwidget5"
    QTERMWIDGET_DOC="$(pkg-config --variable=prefix qtermwidget5)/share/doc"
    if [[ -n "${QT_MOC:-}" ]]; then
        MOC="$QT_MOC"
    else
        HOST_BINS="$(pkg-config --variable=host_bins Qt5Core 2>/dev/null || true)"
        for candidate in "${HOST_BINS:+$HOST_BINS/moc}" /usr/lib/qt5/bin/moc /usr/local/qt5/bin/moc; do
            if [[ -n "$candidate" && -x "$candidate" ]]; then MOC="$candidate"; break; fi
        done
        if [[ -z "$MOC" ]] && command -v moc >/dev/null 2>&1; then MOC="$(command -v moc)"; fi
    fi
    [[ -n "$MOC" ]] || fail_dependency "Qt5 moc (set QT_MOC if it is outside the standard paths)"

    if [[ -n "$QT_LIB_DIR" ]]; then
        QTERM_LIB="$QT_LIB_DIR/libqtermwidget5.so.0"
    fi
    if [[ ! -f "$QTERM_LIB" ]]; then
        QTERM_LIB="$(ldconfig -p 2>/dev/null | awk '/libqtermwidget5\.so\.0 / { print $NF; exit }' || true)"
    fi
    [[ -n "$QTERM_LIB" && -f "$QTERM_LIB" ]] || fail_dependency "QTermWidget runtime library libqtermwidget5.so.0"
    QTERM_LIB="$(readlink -f "$QTERM_LIB")"
    UTF8PROC_LIB="$(dirname "$QTERM_LIB")/libutf8proc.so.2"
    if [[ ! -f "$UTF8PROC_LIB" ]]; then
        UTF8PROC_LIB="$(ldconfig -p 2>/dev/null | awk '/libutf8proc\.so\.2 / { print $NF; exit }' || true)"
    fi
    [[ -n "$UTF8PROC_LIB" && -f "$UTF8PROC_LIB" ]] || fail_dependency "utf8proc runtime library libutf8proc.so.2"
    UTF8PROC_LIB="$(readlink -f "$UTF8PROC_LIB")"
fi

[[ -d "$QTERMWIDGET_DATA/kb-layouts" ]] || fail_dependency "QTermWidget keyboard layout data"
[[ -d "$QTERMWIDGET_DATA/color-schemes" ]] || fail_dependency "QTermWidget color scheme data"

RUNTIME_DIR="$OUTPUT_DIR/peony-cli-panel"
mkdir -p -- "$OUTPUT_DIR" "$RUNTIME_DIR"
MOC_CPP="$OUTPUT_DIR/moc_cli-panel-plugin.cpp"

"$MOC" "${QT_CFLAGS[@]}" "${PEONY_CFLAGS[@]}" "$ROOT_DIR/src/cli-panel-plugin.h" -o "$MOC_CPP"

g++ "${COMMON[@]}" "${QT_CFLAGS[@]}" "${PEONY_CFLAGS[@]}" -shared \
    "$ROOT_DIR/src/cli-panel-plugin.cpp" "$MOC_CPP" \
    -o "$OUTPUT_DIR/libpeony-cli-panel.so" "${QT_LIBS[@]}" -ldl

g++ "${COMMON[@]}" "${QT_CFLAGS[@]}" "${TERM_CFLAGS[@]}" -shared \
    "$ROOT_DIR/src/terminal-runtime.cpp" -o "$RUNTIME_DIR/libpeony-cli-terminal.so" \
    "${TERM_LIBS[@]}" "${QT_LIBS[@]}" -ldl \
    -Wl,--disable-new-dtags,-rpath,'$ORIGIN'

g++ -std=c++11 -O2 "$ROOT_DIR/src/session-guard.cpp" -o "$RUNTIME_DIR/peony-cli-session"

cp -L "$QTERM_LIB" "$RUNTIME_DIR/libqtermwidget5.so.0"
cp -L "$UTF8PROC_LIB" "$RUNTIME_DIR/libutf8proc.so.2"
mkdir -p "$RUNTIME_DIR/data" "$RUNTIME_DIR/licenses"
cp -a "$QTERMWIDGET_DATA/kb-layouts" "$QTERMWIDGET_DATA/color-schemes" "$RUNTIME_DIR/data/"

QTERM_COPYRIGHT="$QTERMWIDGET_DOC/libqtermwidget5-0/copyright"
UTF8PROC_COPYRIGHT="$QTERMWIDGET_DOC/libutf8proc2/copyright"
[[ -f "$QTERM_COPYRIGHT" ]] && cp "$QTERM_COPYRIGHT" "$RUNTIME_DIR/licenses/qtermwidget-copyright"
[[ -f "$UTF8PROC_COPYRIGHT" ]] && cp "$UTF8PROC_COPYRIGHT" "$RUNTIME_DIR/licenses/utf8proc-copyright"

echo "Built plugin: $OUTPUT_DIR/libpeony-cli-panel.so"
echo "Built runtime and helper: $RUNTIME_DIR"
