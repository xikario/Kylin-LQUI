#!/usr/bin/python3
"""Build the offline factual month/solar-term table from HKO year tables.

Manual maintenance tool only. No calendar display makes network requests.
Source: https://www.hko.gov.hk/tc/gts/time/conversion.htm
"""
import concurrent.futures,datetime as dt,hashlib,json,re,urllib.request,time,subprocess,argparse
from pathlib import Path

MONTHS=['正月','二月','三月','四月','五月','六月','七月','八月','九月','十月','十一月','十二月']
def fetch(year):
    url='https://www.hko.gov.hk/tc/gts/time/calendar/text/files/T%dc.txt'%year
    cache=Path.home()/'.cache/kylin-lqui/calendar-reference'/('T%dc.txt'%year)
    cache.parent.mkdir(parents=True,exist_ok=True)
    if cache.is_file():data=cache.read_bytes()
    else:
        # curl on older Kylin OpenSSL builds handles this server's TLS more
        # reliably than the bundled Python 3.8 HTTPS transport.
        data=subprocess.check_output(['curl','-fsSL','--retry','2','--max-time','25',url.replace('www.hko.gov.hk','www.weather.gov.hk')],timeout=85)
        cache.write_bytes(data)
    text=data.decode('utf-8-sig')
    rows=[]
    for line in text.splitlines():
        match=re.match(r'(\d{4})年(\d+)月(\d+)日\s+(\S+)\s+星期\S+\s*(\S*)',line)
        if match:
            y,m,d,lunar,term=match.groups();rows.append([dt.date(int(y),int(m),int(d)),lunar,term])
    expected=(dt.date(year+1,1,1)-dt.date(year,1,1)).days
    if len(rows)!=expected or any(row[0]!=dt.date(year,1,1)+dt.timedelta(days=i) for i,row in enumerate(rows)):raise ValueError('Incomplete HKO table: '+str(year))
    return year,rows,hashlib.sha256(data).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--first-year',type=int,default=1901);parser.add_argument('--last-year',type=int,default=2100);args=parser.parse_args()
    if not 1901<=args.first_year<=args.last_year<=2100:raise ValueError('Invalid reference range')
    tables={};hashes={}
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for year,rows,digest in pool.map(fetch,range(args.first_year,args.last_year+1)):
            tables[year]=rows;hashes[str(year)]=digest
            if year%20==0:print('Validated through',year,flush=True)
    days=['初'+x for x in '一二三四五六七八九十']+['十'+x for x in '一二三四五六七八九']+['二十']+['廿'+x for x in '一二三四五六七八九']+['三十']
    starts=[];terms={}
    lunar_year=args.first_year-1
    for year,rows in sorted(tables.items()):
        for date,lunar,term in rows:
            if '月' in lunar:
                leap='閏' in lunar;month=MONTHS.index(lunar.replace('閏',''))+1
                if month==1 and not leap:lunar_year=year
                if not starts or date.isoformat()!=starts[-1][0]:starts.append([date.isoformat(),month,leap,lunar_year])
            elif starts:
                expected=(date-dt.date.fromisoformat(starts[-1][0])).days
                if not 0<=expected<30 or days[expected]!=lunar:raise ValueError('Lunar continuity mismatch: '+str(date))
            if term:terms[date.isoformat()]=term.translate(str.maketrans('驚蟄穀滿種處','惊蛰谷满种处'))
    for a,b in zip(starts,starts[1:]):
        if (dt.date.fromisoformat(b[0])-dt.date.fromisoformat(a[0])).days not in (29,30):raise ValueError('Invalid lunar month length')
    output={'source':'Hong Kong Observatory Gregorian-Lunar conversion tables','url':'https://www.hko.gov.hk/tc/gts/time/conversion.htm','range':[starts[0][0],str(args.last_year)+'-12-31'],'predictionNote':'HKO flags potential future one-day differences for new moons near midnight in 2057, 2089 and 2097.','sha256ByYear':hashes,'monthStarts':starts,'solarTerms':terms}
    target=Path(__file__).with_name('lunar_hko_reference.json');temporary=target.with_suffix('.json.new');temporary.write_text(json.dumps(output,ensure_ascii=False,separators=(',',':'))+'\n');temporary.replace(target)
    print('Saved',len(starts),'months,',len(terms),'solar terms',flush=True)
if __name__=='__main__':main()
