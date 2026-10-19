import re,glob
wr={'AddText':1,'SetStatus':2,'Dbg':0,'BufPrintf':1,'SetErr':2,'SetError':1,'StrFmt':0,'Notice':1}
for fn in sorted(glob.glob('src/*.c')):
    s=open(fn,errors='replace').read()
    for m in re.finditer(r'\b('+'|'.join(wr)+r')\s*\(',s):
        name=m.group(1); idx=wr[name]
        # skip definitions
        pre=s[max(0,m.start()-12):m.start()]
        i=m.end(); depth=1; args=[]; cur=''; instr=None
        while i<len(s) and depth>0:
            c=s[i]
            if instr:
                cur+=c
                if c=='\\': cur+=s[i+1]; i+=1
                elif c==instr: instr=None
            else:
                if c in '"\'': instr=c; cur+=c
                elif c in '([{': depth+=1; cur+=c
                elif c in ')]}':
                    depth-=1
                    if depth>0: cur+=c
                elif c==',' and depth==1: args.append(cur.strip()); cur=''
                else: cur+=c
            i+=1
        args.append(cur.strip())
        if idx<len(args) and not args[idx].startswith('"'):
            ln=s.count('\n',0,m.start())+1
            print(f'{fn}:{ln}: {name}({", ".join(a[:30] for a in args)[:100]})')
