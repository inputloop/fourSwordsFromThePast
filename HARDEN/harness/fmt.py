import re,sys,glob
# find printf-family calls whose format argument is not a string literal
fam={'printf':0,'fprintf':1,'sprintf':1,'snprintf':2,'vsnprintf':2,'vsprintf':1,'vfprintf':1,'StrFmt':0,'Die':0,'SDL_Log':0,'SDL_ShowSimpleMessageBox':-1}
for fn in sorted(glob.glob('src/**/*.c',recursive=True)+glob.glob('snes/*.c')+glob.glob('src/*.h')):
    s=open(fn,errors='replace').read()
    for m in re.finditer(r'\b(printf|fprintf|sprintf|snprintf|vsnprintf|vsprintf|vfprintf|StrFmt|Die)\s*\(',s):
        name=m.group(1); idx=fam[name]
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
        if idx<len(args):
            a=args[idx]
            if not re.match(r'^(".*"\s*)+$',a,re.S) and not re.match(r'^[A-Z_]+\(?',a) :
                ln=s.count('\n',0,m.start())+1
                print(f'{fn}:{ln}: {name} fmt={a[:70]!r}')
