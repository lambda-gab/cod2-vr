# helper: run REA queries; usage: python rq.py <bin> <workdir> '<json list>'
import json,subprocess,sys,os
binp,wd,qs=sys.argv[1],os.path.abspath(sys.argv[2]),json.loads(sys.argv[3])
q=[{"tool":"open_binary","args":{"path":"$BIN","provider_id":"ghidra"}}]
res_addrs=[a for a in qs.get('resolve',[])]
for a in res_addrs: q.append({"tool":"resolve_containing_procedure","args":{"address":a}})
for t in qs.get('raw',[]): q.append(t)
json.dump(q,open(os.path.join(wd,'_q.json'),'w'))
subprocess.run(['node',os.path.abspath('reacli.mjs'),binp,os.path.join(wd,'_q.json'),os.path.join(wd,'out','_r.json')],cwd=wd,stderr=subprocess.DEVNULL)
r=json.load(open(os.path.join(wd,'out','_r.json')))
procs=[]
for x in r[1:]:
    n=x['r']
    if isinstance(n,dict): n=n.get('normalized_result',n)
    if x['q']['tool']=='resolve_containing_procedure':
        p=(n or {}).get('procedure') or {}
        print('resolve',x['q']['args']['address'],'->',p.get('address')); 
        if p.get('address'): procs.append(p['address'])
    else:
        print(x['q']['tool'],json.dumps(x['q'].get('args'))[:100],'=>',json.dumps(n)[:qs.get('maxout',1500)])
if qs.get('dec') or procs and qs.get('decresolved',True):
    allp=list(dict.fromkeys(procs+qs.get('dec',[])))
    q2=[q[0],{"tool":"__dec","args":{"procs":allp}}]
    json.dump(q2,open(os.path.join(wd,'_q.json'),'w'))
    subprocess.run(['node',os.path.abspath('reacli.mjs'),binp,os.path.join(wd,'_q.json'),os.path.join(wd,'out','_r2.json')],cwd=wd,stderr=subprocess.DEVNULL)
    print('decompiled',allp)
