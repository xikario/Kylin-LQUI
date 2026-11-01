#!/usr/bin/python3
"""Read-only component inventory and current front-process PSS; never starts apps."""
import argparse
import datetime
import json
import os
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def processes(catalog):
    names={name for component in catalog for name in component['processNames']}
    suffixes=[component['scriptSuffix'] for component in catalog if component.get('scriptSuffix')]
    result=[]
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():continue
        try:
            if proc.stat().st_uid!=os.getuid():continue
            name=(proc/'comm').read_text().strip()
            if (proc/'stat').read_text().rsplit(') ',1)[1].split()[0]=='Z':continue
            if name not in names and not name.startswith('python'):continue
            record={'pid':int(proc.name),'name':name,'pssKiB':None,'rssKiB':None,'swapPssKiB':None}
            # Read the script argument only for Python launchers; never include
            # full command lines or environment variables in the report.
            if name.startswith('python'):
                args=(proc/'cmdline').read_bytes().split(b'\0')
                record['script']=os.fsdecode(args[1]) if len(args)>1 else ''
                if name not in names and not any(record['script'].endswith(suffix) for suffix in suffixes):continue
            try:
                for line in (proc/'smaps_rollup').read_text().splitlines():
                    if line.startswith('Pss:'):record['pssKiB']=int(line.split()[1])
                    elif line.startswith('Rss:'):record['rssKiB']=int(line.split()[1])
                    elif line.startswith('SwapPss:'):record['swapPssKiB']=int(line.split()[1])
            except OSError:pass
            result.append(record)
        except (OSError,ValueError):pass
    return result

def snapshot(source_root=None):
    catalog=json.loads((ROOT/'integration/components.json').read_text())
    current=processes(catalog['components'])
    try:
        from gi.repository import Gio,GLib
        bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
        def owner(name):
            try:return bus.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','NameHasOwner',GLib.Variant('(s)',(name,)),None,Gio.DBusCallFlags.NO_AUTO_START,500,None).unpack()[0]
            except GLib.Error:return None
    except Exception:
        owner=lambda name:None
    output=[]
    for component in catalog['components']:
        suffix=component.get('scriptSuffix')
        matches=[p for p in current if p['name'] in component['processNames'] or suffix and p.get('script','').endswith(suffix)]
        source=component.get('source');entry={k:component[k] for k in ('id','name','layer')}
        source_root=source_root or ROOT
        entry.update(source=str((source_root/source).resolve()) if source else None,sourceExists=(source_root/source).exists() if source else None,
            processes=[{k:p[k] for k in ('pid','name','pssKiB','rssKiB','swapPssKiB')} for p in matches],
            pssKiB=sum(p['pssKiB'] for p in matches) if matches and all(p['pssKiB'] is not None for p in matches) else None,
            swapPssKiB=sum(p['swapPssKiB'] for p in matches) if matches and all(p['swapPssKiB'] is not None for p in matches) else None,
            services={name:owner(name) for name in component.get('services',[])},
            configuration=[{'path':str(Path(path).expanduser()),'exists':Path(path).expanduser().exists()} for path in component.get('configuration',[])])
        output.append(entry)
    return {'schemaVersion':1,'measuredAt':datetime.datetime.now().astimezone().isoformat(),'components':output,
        'scope':'Current user front processes; excludes detached CLI scopes and browser helper children. PSS is proportional resident memory, not RSS sum.'}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--json',action='store_true');parser.add_argument('--output',type=Path);args=parser.parse_args()
    data=snapshot()
    if args.output:args.output.write_text(json.dumps(data,ensure_ascii=False,indent=2))
    if args.json:print(json.dumps(data,ensure_ascii=False,indent=2))
    else:
        print('组件\t进程数\tPSS MiB\tSwapPSS MiB\t源码')
        for item in data['components']:
            memory='未知' if item['pssKiB'] is None else '%.1f'%(item['pssKiB']/1024)
            swapped='未知' if item['swapPssKiB'] is None else '%.1f'%(item['swapPssKiB']/1024)
            print('%s\t%s\t%s\t%s\t%s'%(item['name'],len(item['processes']),memory,swapped,'已定位' if item['sourceExists'] else '系统组件' if item['source'] is None else '未找到'))

if __name__=='__main__':main()
