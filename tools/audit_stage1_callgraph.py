"""Codex: граф прямых вызовов финального LTO; диагностическая линковка не заменяет игру."""
from pathlib import Path
import hashlib,json,re,subprocess,sys,zipfile
from compile_probe import ROOT,TC
from prepare_stage1 import BASE
OUT=BASE.parent/'fix2/callgraph'

def main():
    OUT.mkdir(parents=True,exist_ok=True);src=BASE/'module'
    if '--reuse' not in sys.argv:
        args=(src/'link.rsp').read_text()
        for name in ['HMM2.raw.ELF','HMM2.map']:
            args=args.replace(str(src/name).replace('\\','/'),str(OUT/name).replace('\\','/'))
        args+='\n"-fcallgraph-info=su"\n'
        (OUT/'link.rsp').write_text(args)
        with (OUT/'link.log').open('wb') as log:
            subprocess.run([str(TC/'xtensa-esp32s3-elf-g++.exe'),'@'+str(OUT/'link.rsp')],cwd=OUT,stdout=log,stderr=subprocess.STDOUT,check=True)
    raw=(src/'HMM2.raw.ELF').read_bytes();assert (OUT/'HMM2.raw.ELF').read_bytes()==raw
    nodes={};edges={}
    for f in OUT.glob('*.ci'):
        for line in f.read_text().splitlines():
            m=re.search(r'node: \{ title: "([^"]+)" label: "([^"]+)"',line)
            if m and (m[1] not in nodes or 'bytes' in m[2]):nodes[m[1]]=m[2]
            m=re.search(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"',line)
            if m:edges.setdefault(m[1],set()).add(m[2])
    sys.setrecursionlimit(30000)
    indices={};low={};stack=[];active=set();cycles=[]
    def visit(v):
        indices[v]=low[v]=len(indices);stack.append(v);active.add(v)
        for w in sorted(edges.get(v,[])):
            if w not in indices:visit(w);low[v]=min(low[v],low[w])
            elif w in active:low[v]=min(low[v],indices[w])
        if low[v]==indices[v]:
            component=[]
            while True:
                w=stack.pop();active.remove(w);component.append(w)
                if w==v:break
            if len(component)>1 or v in edges.get(v,[]):cycles.append(component)
    for v in sorted(nodes):
        if v not in indices:visit(v)
    cycles=[[dict(symbol=n,label=nodes.get(n,n)) for n in c] for c in cycles]
    game=[c for c in cycles if any('stage1' in n['label'] for n in c)]
    result=dict(elf_sha256=hashlib.sha256((src/'HMM2.ELF').read_bytes()).hexdigest(),raw_elf_exact=True,
        raw_sha256=hashlib.sha256(raw).hexdigest(),nodes=len(nodes),edges=sum(map(len,edges.values())),
        cycle_count=len(cycles),game_cycles=game,cycles=cycles,
        limitation='Граф прямых вызовов LTO: виртуальные вызовы, указатели и внутренности отдельных runtime-архивов не раскрыты полностью.')
    ev=ROOT/'Docs/evidence'
    (ev/'stage1_fix2_callgraph.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(ev/'stage1_fix2_callgraph.zip','w',zipfile.ZIP_DEFLATED) as z:
        for f in sorted(OUT.glob('*.ci')):z.write(f,f.name)
        z.write(OUT/'link.log','link.log')
    print('Узлов:',len(nodes),'рёбер:',result['edges'],'циклов:',len(cycles),'игровых:',len(game),'ELF совпал:',True)
    for c in game:
        print([n['label'].replace('\\n',' | ') for n in c])
if __name__=='__main__':main()
