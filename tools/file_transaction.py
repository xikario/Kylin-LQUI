"""Recoverable file publication for repository installers (no directory snapshots)."""
import contextlib
import fcntl
import json
import os
from pathlib import Path
import shutil
import tempfile


def atomic_bytes(path, data, mode=0o644):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    fd,temporary=tempfile.mkstemp(prefix='.'+path.name+'.',dir=str(path.parent))
    try:
        with os.fdopen(fd,'wb') as out:out.write(data);out.flush();os.fsync(out.fileno())
        os.chmod(temporary,mode);os.replace(temporary,path)
    finally:
        if os.path.lexists(temporary):os.unlink(temporary)


class FileTransaction:
    def __init__(self,directory):
        self.directory=Path(directory);self.lock=None;self.journal=None;self.rows=[]
    def __enter__(self):
        self.directory.mkdir(parents=True,exist_ok=True)
        self.lock=(self.directory/'.publish.lock').open('a');fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        try:
            for folder in self.directory.glob('.transaction-*'):
                journal=folder/'journal.json'
                if journal.exists() and json.loads(journal.read_text()):
                    raise RuntimeError('检测到中断安装，未覆盖现状；请先核对恢复记录：'+str(folder))
                shutil.rmtree(folder)
            self.journal=Path(tempfile.mkdtemp(prefix='.transaction-',dir=str(self.directory)))
            self.save()
            return self
        except BaseException:self.lock.close();raise
    def save(self):atomic_bytes(self.journal/'journal.json',json.dumps(self.rows).encode(),0o600)
    def watch(self,path):
        path=Path(path).absolute()
        if any(row['path']==str(path) for row in self.rows):return
        row={'path':str(path),'kind':'absent'}
        if path.is_symlink():row.update(kind='link',target=os.readlink(path))
        elif path.exists():
            if not path.is_file():raise RuntimeError('Refusing to replace non-file: '+str(path))
            backup=str(len(self.rows));shutil.copy2(path,self.journal/backup)
            row.update(kind='file',backup=backup,mode=path.stat().st_mode&0o7777)
        self.rows.append(row);self.save()
    @staticmethod
    def restore(folder):
        journal=folder/'journal.json'
        if not journal.exists():
            shutil.rmtree(folder);return
        rows=json.loads(journal.read_text())
        for row in reversed(rows):
            path=Path(row['path']);path.parent.mkdir(parents=True,exist_ok=True)
            if row['kind']=='file':atomic_bytes(path,(folder/row['backup']).read_bytes(),row['mode'])
            elif row['kind']=='link':
                fd,tmp=tempfile.mkstemp(prefix='.restore-',dir=str(path.parent));os.close(fd);os.unlink(tmp)
                try:os.symlink(row['target'],tmp);os.replace(tmp,path)
                finally:
                    if os.path.lexists(tmp):os.unlink(tmp)
            elif os.path.lexists(path):path.unlink()
        shutil.rmtree(folder)
    def __exit__(self,kind,error,traceback):
        try:
            if kind:self.restore(self.journal)
            else:
                # An empty durable journal is the commit record. A crash while
                # removing backups must not roll a completed publication back.
                self.rows=[]
                try:self.save()
                except BaseException:
                    self.restore(self.journal)
                    raise
                shutil.rmtree(self.journal,ignore_errors=True)
        finally:self.lock.close()
        return False
