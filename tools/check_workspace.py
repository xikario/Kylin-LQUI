#!/usr/bin/env python3
"""Validate source layout and parse Python without touching running components."""
import ast,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];main=root/'code-work/ukui-liquid-desktop';errors=[]
for component in json.loads((main/'integration/components.json').read_text())['components']:
    source=component.get('source')
    if source and not (main/source).exists():errors.append('Missing source: '+component['id'])
for source in root.rglob('*.py'):
    if any(part.startswith('build') or part in ('.git','__pycache__') for part in source.parts):continue
    try:ast.parse(source.read_text(),filename=str(source))
    except (SyntaxError,UnicodeError) as error:errors.append(str(error))
print(json.dumps({'ok':not errors,'errors':errors},ensure_ascii=False,indent=2));sys.exit(bool(errors))
