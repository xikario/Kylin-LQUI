"""Review regressions using synthetic documents and isolated storage only."""
import contextlib
import io
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS=Path(__file__).resolve().parents[1]/'scripts'
sys.path.insert(0,str(SCRIPTS))
import smart_space_indexer as indexer
import smart_space_knowledge as worker
import knowledge_library as lib
from local_path_policy import inside

class ReviewBoundaries(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.env=mock.patch.dict(os.environ,XDG_CONFIG_HOME=str(self.root/'config'),XDG_DATA_HOME=str(self.root/'data'),AI_SPACE_STORAGE_DIRECTORY=str(self.root/'storage'),AI_SPACE_RESERVE_GIB='0.01',AI_SPACE_MAX_GIB='32')
        self.env.start();self.allowed=self.root/'allowed';self.allowed.mkdir()
    def tearDown(self):self.env.stop();self.temp.cleanup()
    def run_script(self,name,*args):
        result=subprocess.run([sys.executable,str(SCRIPTS/name),*map(str,args)],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout);return result
    def build(self,snapshot,stream=False):
        path=self.root/'index.json';path.write_text(json.dumps(snapshot),encoding='utf-8')
        args=['build','--db',self.root/'storage/library.sqlite','--index-json',path,'--no-ocr']
        if stream:args+=['--index-stream',indexer.atomic_ui_stream_write(path,snapshot)]
        self.run_script('smart_space_knowledge.py',*args)
        with sqlite3.connect(str(self.root/'storage/library.sqlite')) as db:
            return list(db.execute('select path from documents')),list(db.execute('select content from chunks'))
    def test_alias_exclusion_applies_to_real_pipeline(self):
        private=self.allowed/'private';private.mkdir();(private/'secret.txt').write_text('EXCLUDED_SENTINEL')
        (self.allowed/'visible.txt').write_text('VISIBLE_SENTINEL');alias=self.root/'alias';alias.symlink_to(self.allowed,target_is_directory=True)
        for excluded in (alias/'private',private):
            index=self.root/'manifest.json'
            self.run_script('smart_space_indexer.py','--roots-json',json.dumps([str(alias)]),'--exclude-roots-json',json.dumps([str(excluded)]),'--output',index,'--metadata-only','--fast-full')
            snapshot=json.loads(index.read_text());docs,chunks=self.build(snapshot,True)
            self.assertTrue(any('visible.txt' in p[0] for p in docs));self.assertFalse(any('private' in p[0] for p in docs));self.assertNotIn('EXCLUDED_SENTINEL',str(chunks))
    def test_provider_never_reads_local_body_json_and_stream(self):
        outside=self.root/'outside.txt';outside.write_text('LOCAL_SENTINEL')
        local=self.allowed/'local.txt';local.write_text('LOCAL_SENTINEL')
        for stream in (False,True):
            with self.subTest(stream=stream):
                items=[dict(path=str(p),name=p.name,isDir=False,source='provider:http',content='PROVIDER_PREVIEW') for p in (outside,local)]
                snapshot=dict(roots=[str(self.allowed)],items=items)
                docs,chunks=self.build(snapshot,stream);self.assertEqual(len(docs),2);self.assertIn('PROVIDER_PREVIEW',str(chunks));self.assertNotIn('LOCAL_SENTINEL',str(chunks))
    def test_excluded_provider_removed_even_from_old_inventory(self):
        secret=self.allowed/'private.txt';secret.write_text('EXCLUDED_SENTINEL')
        visible=self.allowed/'visible.txt';visible.write_text('VISIBLE_SENTINEL')
        for stream in (False,True):
            snapshot=dict(roots=[str(self.allowed)],excludedRoots=[str(secret)],items=[dict(path=str(p),name=p.name,isDir=False,source='provider:http' if p==secret else 'filesystem') for p in (secret,visible)])
            docs,chunks=self.build(snapshot,stream);self.assertEqual(len(docs),1);self.assertNotIn('EXCLUDED_SENTINEL',str(chunks))
    def test_http_provider_cannot_bridge_excluded_or_external_files(self):
        from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
        from threading import Thread
        from provider_policy import load_policy
        private=self.root/'private';private.mkdir();secret=private/'secret.txt';secret.write_text('PRIVATE_SENTINEL')
        external=self.root/'external.txt';external.write_text('EXTERNAL_SENTINEL')
        (self.allowed/'visible.txt').write_text('VISIBLE_BODY')
        payload={'items':[{'path':str(secret),'name':'secret.txt'},{'path':str(external),'name':'external.txt'},{'path':'virtual://fixture','name':'virtual','content':'PROVIDER_CONTENT'},{'path':'relative.txt','name':'relative','content':'RELATIVE_CONTENT'}]}
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                self.rfile.read(int(self.headers.get('Content-Length','0')))
                data=json.dumps(payload).encode();self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
            def log_message(self,*args):pass
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            config=self.root/'providers.json';config.write_text(json.dumps({'providers':[{'name':'fixture','type':'http','url':'http://127.0.0.1:'+str(server.server_port),'includeRoots':False}]}))
            digest=load_policy(config)[1];manifest=self.root/'provider-manifest.json'
            self.run_script('smart_space_indexer.py','--roots-json',json.dumps([str(self.allowed)]),'--exclude-roots-json',json.dumps([str(private)]),'--output',manifest,'--metadata-only','--provider-config',config,'--provider-approval',digest)
            docs,chunks=self.build(json.loads(manifest.read_text()),True)
            self.assertFalse(any('secret.txt' in row[0] for row in docs));self.assertNotIn('PRIVATE_SENTINEL',str(chunks));self.assertNotIn('EXTERNAL_SENTINEL',str(chunks));self.assertIn('PROVIDER_CONTENT',str(chunks));self.assertIn('RELATIVE_CONTENT',str(chunks))
        finally:server.shutdown();server.server_close();thread.join(timeout=2)
    def test_compatibility_scan_keeps_image_when_ocr_language_is_missing(self):
        import base64,shutil
        if not shutil.which('tesseract'):self.skipTest('requires a Tesseract language probe')
        image=self.allowed/'image.png';image.write_bytes(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j6VQAAAAASUVORK5CYII='))
        output=self.root/'ocr.json'
        with mock.patch.dict(os.environ,AI_SPACE_OCR_LANGUAGES='fixture_missing_language'):
            self.run_script('smart_space_indexer.py','--roots-json',json.dumps([str(self.allowed)]),'--output',output,'--ocr-images')
        item=next(i for i in json.loads(output.read_text())['items'] if i['name']=='image.png')
        self.assertEqual(item['ocrStatus'],'unavailable');self.assertIn('ocr_languages_missing',item['ocrReason'])
    def test_canonical_boundaries_and_root(self):
        self.assertTrue(inside('/data','/'));self.assertTrue(inside('/data/a','/data'));self.assertFalse(inside('/database','/data'))
        alias=self.root/'link';alias.symlink_to(self.allowed,target_is_directory=True)
        self.assertTrue(indexer.path_is_excluded(self.allowed/'future/child',[str(alias/'future')]))
    def test_relaxed_chinese_accepts_later_chunk(self):
        database=self.root/'query.sqlite'
        with contextlib.redirect_stdout(io.StringIO()):db=worker.open_db(str(database))
        path=str(self.allowed/'neutral.txt');db.execute('INSERT INTO documents(id,path,name,content_hash) VALUES(1,?,?,?)',(path,'neutral.txt','hash'))
        for ident,content in ((1,'甲乙 '*30),(2,'甲乙 丙丁')):
            db.execute('INSERT INTO chunks(id,document_id,ordinal,content) VALUES(?,1,?,?)',(ident,ident,content))
            db.execute('INSERT INTO chunks_fts(rowid,name,path,content,locator,document_id,chunk_id) VALUES(?,?,?,?,?,1,?)',(ident,'neutral',path,worker.fts_text(content),'',ident))
        db.commit();db.close()
        result=lib.search_one(database,'甲乙丙丁',body_only=True)
        self.assertEqual(len(result['results']),1,result);self.assertEqual(result['results'][0]['chunk_id'],2)

if __name__=='__main__':unittest.main()
