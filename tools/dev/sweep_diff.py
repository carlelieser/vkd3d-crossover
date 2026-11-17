import sys,re
def load(p):
    d={}
    for line in open(p):
        f=line.rstrip('\n').split('\t')
        if len(f)<5: continue
        m=re.search(r'(\d+) tests executed \((\d+) failures',f[1])
        d[f[0]]=dict(exe=int(m.group(1)) if m else None, fail=int(m.group(2)) if m else None,
                    mvk=int(f[2].split('=')[1]), lost=int(f[3].split('=')[1]), crash=int(f[4].split('=')[1]), raw=f[1])
    return d
a=load(sys.argv[1]); b=load(sys.argv[2])
worse=[];better=[]
for t,nb in b.items():
    na=a.get(t)
    if not na: continue
    def score(n): return ((n['fail'] if n['fail'] is not None else 10**9), n['mvk']+n['lost']+n['crash'])
    sa,sb=score(na),score(nb)
    if sb>sa and (sb[0]>sa[0] or sb[1]>sa[1]): worse.append((t,na['raw'],nb['raw'],na['mvk'],nb['mvk'],na['lost'],nb['lost'],na['crash'],nb['crash']))
    elif sb<sa: better.append((t,na['raw'],nb['raw']))
print(f"compared {sum(1 for t in b if t in a)} tests; regressions {len(worse)}; improvements {len(better)}")
for w in worse: print("WORSE", *w, sep=' | ')
for x in better: print("BETTER", *x, sep=' | ')
