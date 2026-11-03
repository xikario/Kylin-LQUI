#!/usr/bin/python3
"""Reproduce the user-local Rime font repair without touching the IM engine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
from urllib.request import urlopen
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / 'assets/fonts'
REQUIRED = (0x1FAE0, 0x1FAE5)


def manifest():
    return json.loads((ASSETS / 'noto-emoji.json').read_text())


def validate_font(data, spec):
    if (len(data) != spec['bytes'] or data[:4] != b'\x00\x01\x00\x00'
            or hashlib.sha256(data).hexdigest() != spec['sha256']):
        raise ValueError('字体大小、格式或 SHA-256 不符合固定版本，未安装。')


def command(*args):
    return subprocess.check_output(args, text=True, timeout=30).strip()


def charset_contains(charset, codepoint):
    return any(int(part.split('-')[0], 16) <= codepoint <= int(part.split('-')[-1], 16)
               for part in charset.split())


def emoji_rule(paths):
    return ('<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n'
            '<fontconfig><selectfont><rejectfont>'
            + ''.join('<glob>' + escape(str(p)) + '</glob>' for p in sorted(paths))
            + '</rejectfont></selectfont></fontconfig>\n')


def cjk_rule(primary, fallback):
    return ('<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n'
            '<fontconfig><match target="pattern">'
            '<test name="family" compare="eq"><string>' + escape(primary) + '</string></test>'
            '<edit name="family" mode="append" binding="strong"><string>'
            + escape(fallback) + '</string></edit></match></fontconfig>\n')


def xml_key(text):
    def key(element):
        return (element.tag, sorted(element.attrib.items()), (element.text or '').strip(),
                [key(child) for child in element])
    return key(ET.fromstring(text))


def guard_rule(path, text):
    if path.is_symlink():
        raise RuntimeError('已有符号链接字体规则，未覆盖：' + str(path))
    if path.exists() and xml_key(path.read_text()) != xml_key(text):
        raise RuntimeError('已有不同的用户字体规则，未覆盖：' + str(path))


def primary_font():
    from PyQt5.QtCore import QSettings
    config = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config')))
    return QSettings(str(config / 'fcitx/liquid-panel.conf'), QSettings.IniFormat).value(
        'Fonts/ChineseFamily', 'Noto Sans CJK SC', type=str)


def candidate_strings(paths):
    values = set()
    count = 0
    for path in paths:
        for line in path.read_text().splitlines():
            if line.startswith('#') or '\t' not in line:
                continue
            values.update(line.split('\t', 1)[1].split())
        count += 1
    dictionary_candidates = len(values)
    values.update({'消失', '🫠', '🫥', '😀', '𱍂'})
    return values, count, dictionary_candidates


def audit(family, dictionaries):
    # A fresh process is required: an existing Qt font database caches faces.
    if not os.environ.get('DISPLAY'):
        os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    from PyQt5.QtGui import QFont, QTextLayout
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    values, count, dictionary_candidates = candidate_strings(dictionaries)
    missing = set()
    for value in sorted(values):
        layout = QTextLayout(value, QFont(family, 15))
        layout.beginLayout()
        layout.createLine()
        layout.endLayout()
        runs = layout.glyphRuns()
        if not runs or any(0 in run.glyphIndexes() for run in runs):
            missing.add(' '.join('U+%04X' % ord(c) for c in value))
    return {'font': family, 'dictionariesChecked': count,
            'dictionaryCandidatesChecked': dictionary_candidates, 'uniqueCandidatesChecked': len(values),
            'missingCodepointSequences': sorted(missing), 'ok': not missing}


def install(offline, family, fallback):
    spec = manifest()
    home = Path.home()
    data_home = Path(os.environ.get('XDG_DATA_HOME', str(home / '.local/share')))
    config_home = Path(os.environ.get('XDG_CONFIG_HOME', str(home / '.config')))
    state_home = Path(os.environ.get('XDG_STATE_HOME', str(home / '.local/state')))
    target = data_home / 'fonts/noto-emoji-rime/NotoColorEmoji.ttf'
    if target.exists():
        # Do not silently replace a newer/different user-installed version.
        validate_font(target.read_bytes(), spec)
        data = target.read_bytes()
    elif offline:
        data = offline.read_bytes()
    else:
        with urlopen(spec['url'], timeout=40) as response:
            data = response.read(spec['bytes'] + 1)
    validate_font(data, spec)
    license_data = (ASSETS / 'OFL-NotoEmoji.txt').read_bytes()
    if hashlib.sha256(license_data).hexdigest() != spec['licenseSha256']:
        raise ValueError('字体许可证校验失败。')

    existing = set(command('fc-list', '-f', '%{file}\n').splitlines())
    existing.add('/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf')
    obsolete = []
    for name in sorted(existing):
        path = Path(name)
        if not path.is_file() or path.name != 'NotoColorEmoji.ttf' or path == target:
            continue
        charset = command('fc-query', '-f', '%{charset}', str(path))
        if not all(charset_contains(charset, cp) for cp in REQUIRED):
            obsolete.append(path)

    families = set(command('fc-list', ':charset=31342', '-f', '%{family[0]}\n').splitlines())
    if fallback and fallback not in families:
        raise ValueError('指定的已安装字体不包含 U+31342，未安装：' + fallback)
    if not fallback and families:
        fallback = 'SimSun-ExtG' if 'SimSun-ExtG' in families else sorted(families)[0]
    rules = {}
    if obsolete:
        rules[config_home / 'fontconfig/conf.d/70-noto-color-emoji-local.conf'] = emoji_rule(obsolete)
    if fallback:
        rules[config_home / 'fontconfig/conf.d/71-rime-cjk-extensions.conf'] = cjk_rule(family, fallback)
    for path, text in rules.items():
        guard_rule(path, text)

    tools = next((p / 'tools' for p in ROOT.parents if (p / 'tools/file_transaction.py').is_file()), ROOT)
    sys.path.insert(0, str(tools))
    from file_transaction import FileTransaction, atomic_bytes
    files = {target: data, target.parent / 'LICENSE': license_data,
             target.parent / 'kylin-lqui-source.json': (json.dumps(spec, indent=2) + '\n').encode()}
    files.update({p: t.encode() for p, t in rules.items() if not p.exists()})
    changed = [p for p, content in files.items() if not p.exists() or p.read_bytes() != content]
    refresh = target in changed or any(path in changed for path in rules)
    if changed:
        with FileTransaction(state_home / 'fcitx-liquid-panel/font-support') as transaction:
            for path in changed:
                transaction.watch(path)
                atomic_bytes(path, files[path])
            # Verify actual character coverage before enabling a rejection rule.
            charset = command('fc-query', '-f', '%{charset}', str(target))
            if not all(charset_contains(charset, cp) for cp in REQUIRED):
                raise ValueError('字体不含必需表情，已恢复原配置。')
            if refresh:
                command('fc-cache', '-f', str(target.parent))
    return {'fontSha256': spec['sha256'], 'changedFiles': len(changed),
            'cjkFallbackFamily': fallback, 'restartRequired': refresh,
            'note': '仅安装用户字体及回退规则；未重启候选窗、Fcitx 或 Rime。'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('check', 'install'))
    parser.add_argument('--font-family', default=None)
    parser.add_argument('--dictionary', action='append', type=Path, default=None)
    parser.add_argument('--emoji-font', type=Path, help='离线使用固定 SHA-256 的 Noto 字体文件')
    parser.add_argument('--cjk-family', help='已安装、包含 U+31342 的回退字体')
    args = parser.parse_args()
    family = args.font_family or primary_font()
    if args.action == 'install':
        result = install(args.emoji_font, family, args.cjk_family)
    else:
        config = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config')))
        dictionaries = args.dictionary if args.dictionary is not None else [
            p for p in (config / 'fcitx/rime/opencc').glob('*.txt')
            if p.name in ('emoji.txt', 'others.txt')]
        result = audit(family, dictionaries)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get('ok', True) else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, ET.ParseError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
