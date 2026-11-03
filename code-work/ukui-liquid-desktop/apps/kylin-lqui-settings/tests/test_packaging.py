"""Import a disposable whitelist staging directory. Never call install()."""
import ast
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE=Path(__file__).resolve().parents[1]


class Packaging(unittest.TestCase):
    def test_whitelist_contains_ui_and_existing_audio_modules(self):
        tree=ast.parse((SOURCE/'install.py').read_text())
        files=next(ast.literal_eval(node.value) for node in tree.body if isinstance(node,ast.Assign)
                   and any(isinstance(target,ast.Name) and target.id=='FILES' for target in node.targets))
        self.assertTrue({'ui_tokens.py','ui_components.py','ui_schema.py','audio_events.py',
                         'volume_feedback.py','control_center.py','system_backend.py'}<=set(files))
        self.assertEqual(len(files),len(set(files)))
        with tempfile.TemporaryDirectory(prefix='settings-stage-') as folder:
            stage=Path(folder)
            for name in files:(stage/name).write_bytes((SOURCE/name).read_bytes())
            env=dict(os.environ,QT_QPA_PLATFORM='offscreen',PYTHONDONTWRITEBYTECODE='1')
            script='import sys;sys.path.insert(0,sys.argv[1]);import ui_tokens,ui_schema,ui_components,main,control_center;assert main.SettingsWindow;assert control_center.ControlCenter'
            result=subprocess.run([sys.executable,'-I','-c',script,str(stage)],env=env,cwd=stage,
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertFalse((stage/'current').exists());self.assertFalse((stage/'previous').exists())


if __name__=='__main__':unittest.main()
