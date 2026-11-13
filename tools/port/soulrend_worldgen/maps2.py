"""SRG (Forge) member names -> Mojang's, for the classes given (fields; methods matched by descriptor): python3 maps2.py out.json class... -> {srg: name}"""
import re, sys, json
MOJ='/home/matt/.local/share/PrismLauncher/libraries/net/minecraft/client/1.20.1-20230612.114412/client-1.20.1-20230612.114412-mappings.txt'
TSRG='/home/matt/.local/share/PrismLauncher/libraries/de/oceanlabs/mcp/mcp_config/1.20.1-20230612.114412/mcp_config-1.20.1-20230612.114412-mappings-merged.txt'
want=set(sys.argv[2:])
lines=[l for l in open(MOJ) if not l.startswith('#')]
cls={}
for l in lines:
    if not l.startswith(' '): n,o=l.strip().rstrip(':').split(' -> '); cls[n]=o
PRIM={'void':'V','boolean':'Z','byte':'B','char':'C','short':'S','int':'I','long':'J','float':'F','double':'D'}
def desc(t):
    a=0
    while t.endswith('[]'): t=t[:-2]; a+=1
    d=PRIM.get(t) or 'L'+cls.get(t,t).replace('.','/')+';'
    return '['*a+d
moj={}; cur=None
for l in lines:
    if not l.startswith(' '):
        n,o=l.strip().rstrip(':').split(' -> '); cur={} if n in want else None
        if cur is not None: moj[o]=cur
        continue
    if cur is None: continue
    m=re.match(r'\s+(?:\d+:\d+:)?(\S+) ([\w$<>]+)\((.*)\)(?::\d+:\d+)? -> (\S+)$', l.rstrip())
    if m:
        ps=[p for p in m.group(3).split(',') if p]
        cur['m'+m.group(4)+'('+''.join(desc(p) for p in ps)+')'+desc(m.group(1))]=m.group(2); continue
    m=re.match(r'\s+\S+ ([\w$]+) -> (\S+)$', l.rstrip())
    if m: cur['f'+m.group(2)]=m.group(1)
out={}; cur=None
for l in open(TSRG):
    if not l.startswith('\t'): cur=moj.get(l.split(' ')[0]); continue
    if cur is None or l.startswith('\t\t'): continue
    p=l.split()
    k=('m'+p[0]+p[1]) if len(p)==3 else 'f'+p[0]
    if k in cur: out[p[-1]]=cur[k]
json.dump(out,open(sys.argv[1],'w')); print(len(out))
