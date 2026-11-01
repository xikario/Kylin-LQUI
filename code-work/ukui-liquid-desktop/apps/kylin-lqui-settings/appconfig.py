import json
from pathlib import Path
_manifest=Path(__file__).with_name("runtime.json")
ROOT=Path(json.loads(_manifest.read_text())["sourceRoot"]) if _manifest.is_file() else Path(__file__).resolve().parents[2]
RUNTIME=Path(__file__).with_name('runtime')
if not RUNTIME.is_dir():RUNTIME=ROOT
