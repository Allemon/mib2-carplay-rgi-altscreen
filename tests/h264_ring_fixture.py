import struct,sys
data=open(sys.argv[1],'rb').read(); mode=sys.argv[3]
# split Annex-B into NALs
nals=[]; i=0; starts=[]
j=0
while True:
    k=data.find(b'\x00\x00\x01',j)
    if k<0: break
    starts.append(k+3); j=k+3
for n,s in enumerate(starts):
    e=starts[n+1]-3 if n+1<len(starts) else len(data)
    if n+1<len(starts) and data[e-1]==0: e-=1
    nals.append(data[s:e])
avcc=lambda ns: b''.join(struct.pack('>I',len(x))+x for x in ns)
pkts=[]; cur=[]
for x in nals:
    t=x[0]&0x1f
    if t in (1,5):
        if mode=='per_slice': pkts.append(avcc(cur+[x])); cur=[]
        else:
            b=x[1]; first_mb_zero = (b & 0x80)!=0
            if first_mb_zero and any((y[0]&0x1f) in (1,5) for y in cur): pkts.append(avcc(cur)); cur=[]
            cur.append(x)
    else: cur.append(x)
if cur: pkts.append(avcc(cur))
ring=bytearray(0x44+0x400000); off=0
for seq,p in enumerate(pkts,1):
    rec=struct.pack('<IIII',0x48323634,seq,len(p),0)+p
    ring[0x44+off:0x44+off+len(rec)]=rec; off+=len(rec)
struct.pack_into('<IIIIII',ring,0x18,off,len(pkts),sum(map(len,pkts)),len(pkts),0,0)
open(sys.argv[2],'wb').write(ring); print(sys.argv[2], len(pkts),'packets')
