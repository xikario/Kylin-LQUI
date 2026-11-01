#!/usr/bin/python3
"""Sample framework process trees without starting applications or reading chat data."""
import argparse
import datetime
import json
import os
from pathlib import Path
import time
from desktop_status import snapshot

def process_table():
    table={}
    for path in Path('/proc').iterdir():
        if not path.name.isdigit():continue
        try:
            if path.stat().st_uid!=os.getuid():continue
            fields=(path/'stat').read_text().rsplit(') ',1)[1].split()
            if fields[0]=='Z':continue
            memory={}
            for line in (path/'smaps_rollup').read_text().splitlines():
                key,_,value=line.partition(':')
                if key in ('Pss','Rss','SwapPss'):memory[key]=int(value.split()[0])
            table[int(path.name)]={'parent':int(fields[1]),'start':int(fields[19]),
                'ticks':sum(int(fields[i]) for i in (11,12,13,14)),'name':(path/'comm').read_text().strip(),**memory}
        except (OSError,ValueError,IndexError):pass
    return table

def measure(seconds=15,interval=1):
    inventory=snapshot();roots={c['id']:{p['pid'] for p in c['processes']} for c in inventory['components']}
    samples=[];started=time.monotonic();previous={};last=started
    while True:
        table=process_table();now=time.monotonic();groups=[]
        for component in inventory['components']:
            ids=set(roots[component['id']]) & table.keys()
            while True:
                added={pid for pid,p in table.items() if p['parent'] in ids}-ids
                if not added:break
                ids.update(added)
            total=sum(table[pid]['ticks'] for pid in ids)
            delta=max(0,total-previous.get(component['id'],total))
            groups.append({'id':component['id'],'name':component['name'],'pids':sorted(ids),
                'pssMiB':sum(table[p]['Pss'] for p in ids)/1024 if ids else 0,
                'swapPssMiB':sum(table[p]['SwapPss'] for p in ids)/1024 if ids else 0,
                'cpuPercent':100*delta/os.sysconf('SC_CLK_TCK')/(now-last) if samples else None})
            previous[component['id']]=total
        samples.append({'elapsed':now-started,'components':groups});last=now
        if now-started>=seconds:break
        time.sleep(min(interval,seconds-(now-started)))
    return {'measuredAt':datetime.datetime.now().astimezone().isoformat(),'durationSeconds':samples[-1]['elapsed'],
        'scope':'Current-user front processes and descendants; PSS includes shared pages proportionally. CPU 100% = one core. Detached model scopes are excluded. No application is started.',
        'samples':samples,'summary':[{**c,'cpuPercent':sum(s['components'][i]['cpuPercent'] for s in samples[1:])/max(1,len(samples)-1),
            'peakPssMiB':max(s['components'][i]['pssMiB'] for s in samples)} for i,c in enumerate(samples[-1]['components'])]}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--seconds',type=float,default=15);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.seconds<=0:parser.error('--seconds must be positive')
    result=measure(args.seconds);args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2))
    for c in result['summary']:print('%s\t%d\t%.1f MiB\t%.2f%%'%(c['name'],len(c['pids']),c['pssMiB'],c['cpuPercent']))
