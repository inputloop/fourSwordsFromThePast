import sys
def load(p):
    d={}
    for l in open(p, errors='replace'):
        if l.startswith('DUMP '):
            f=l.split(' ',3); d[int(f[1])]=(f[2], l.strip())
    return d
h=load(sys.argv[1]); c=load(sys.argv[2])
common=sorted(set(h)&set(c))
print("host frames %d-%d, client frames %d-%d, common %d" % (min(h),max(h),min(c),max(c),len(common)))
bad=[f for f in common if h[f][0]!=c[f][0]]
print("hash mismatches: %d" % len(bad))
if bad:
    f=bad[0]; print("first diverging confirmed frame", f); print("H:", h[f][1][:400]); print("C:", c[f][1][:400])
    if f-1 in common: print("prev H:", h[f-1][1][:400]); print("prev C:", c[f-1][1][:400])
spr=[f for f in common if h[f][1].split(' ',3)[3:]!=c[f][1].split(' ',3)[3:]]
print("sprite/player table mismatches: %d" % len(spr))
if spr: f=spr[0]; print("first", f); print("H:", h[f][1][:300]); print("C:", c[f][1][:300])
