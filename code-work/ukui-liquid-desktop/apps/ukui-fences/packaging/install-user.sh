#!/bin/sh
set -eu
/usr/bin/python3 -c 'import os,pwd,pathlib; assert pathlib.Path.home().resolve()==pathlib.Path(pwd.getpwuid(os.getuid()).pw_dir).resolve(), "HOME 与当前用户主目录不一致，未安装"'

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-"$PROJECT_DIR/build"}
USER_PREFIX=${UKUI_FENCES_USER_PREFIX:-"$HOME/.local"}

if [ ! -x "$BUILD_DIR/ukui-fences" ]; then
    echo "ukui-fences build not found: $BUILD_DIR/ukui-fences" >&2
    exit 2
fi

cmake --install "$BUILD_DIR" --prefix "$USER_PREFIX"

# XDG autostart is per-user and is not the same directory as prefix/etc.
# Keep it independent of the source/build tree so upgrades cannot leave a
# session entry pointing at an obsolete build-v10 executable.
install -d "$HOME/.config/autostart" "$HOME/.local/share/applications"
autostart_file="$HOME/.config/autostart/ukui-fences.desktop"
install -m 0644 "$PROJECT_DIR/packaging/ukui-fences-autostart.desktop" "$autostart_file"
# Desktop sessions do not consistently include ~/.local/bin in PATH. Resolve
# the launcher at install time so a reboot always starts this user installation.
sed -i "s|^Exec=.*|Exec=$USER_PREFIX/bin/ukui-fences-launcher --autostart|" "$autostart_file"
install -m 0644 "$PROJECT_DIR/packaging/ukui-fences.desktop" \
    "$HOME/.local/share/applications/ukui-fences.desktop"

# Some UKUI sessions expose a relative user icon search path. A resolved icon
# path lets the panel display this settings icon independently of its cwd/cache.
settings_file="$HOME/.local/share/applications/ukui-fences-settings.desktop"
awk -v launcher="$USER_PREFIX/bin/ukui-fences-launcher" \
    -v icon="$USER_PREFIX/share/icons/hicolor/scalable/apps/ukui-fences-settings.svg" \
    '/^Exec=/ { print "Exec=\"" launcher "\" --settings"; next }
     /^Icon=/ { print "Icon=" icon; next } { print }' \
    "$PROJECT_DIR/packaging/ukui-fences-settings.desktop" > "$settings_file"

# The standalone center is the single application-menu settings entry.
if [ -x "$HOME/.local/bin/kylin-lqui-settings" ]; then
    printf 'NoDisplay=true\nX-Kylin-Lqui-Settings-Alias=true\n' >> "$settings_file"
fi

# Hidden task identity for the local resource monitor.
monitor_file="$HOME/.local/share/applications/ukui-fences-monitor.desktop"
awk -v launcher="$USER_PREFIX/bin/ukui-fences-launcher" \
    -v icon="$USER_PREFIX/share/icons/hicolor/scalable/apps/ukui-fences-monitor.svg" \
    '/^Exec=/ { print "Exec=\"" launcher "\" --system-monitor"; next }
     /^Icon=/ { print "Icon=" icon; next } { print }' \
    "$PROJECT_DIR/packaging/ukui-fences-monitor.desktop" > "$monitor_file"

# This recovery entry also remains usable if the Fences desktop is unavailable.
python3 - "$USER_PREFIX" "$HOME/.local/share/applications/ukui-liquid-desktop-restore.desktop" <<'PYDESKTOP'
from pathlib import Path
import sys
helper = str(Path(sys.argv[1]) / "libexec/ukui-liquid-desktop/session_bridge.py")
def desktop_quote(value):
    value = value.replace("%", "%%")
    for char in ("\\", '"', "`", "$"):
        value = value.replace(char, "\\" + char)
    return '"' + value.replace("\\", "\\\\") + '"'
Path(sys.argv[2]).write_text("[Desktop Entry]\nType=Application\nName=恢复原生桌面服务\nExec=/usr/bin/python3 " + desktop_quote(helper) + " --restore\nIcon=user-desktop\nTerminal=false\nCategories=System;Settings;\n")
PYDESKTOP

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi

echo "Installed ukui-fences to $USER_PREFIX"
echo "Autostart: $HOME/.config/autostart/ukui-fences.desktop"
