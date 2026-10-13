import json, numpy as np, match, voices
def get(f, v, i=0):
    d=json.load(open(f'fits/{f}.json')); refs=d.get('refs') or ['_'.join(f.split('_')[1:])]
    knobs=voices.KNOBS[v] if len(refs)>1 else ()
    q={**voices.defaults(v),**{k:x for k,x in d['p'].items() if '@' not in k}}; q.update({k:d['p'][f'{k}@{i}'] for k in knobs})
    r=match.load(f'../refs/{refs[i]}.wav'); r=r[match.onset(r):]; r/=np.abs(r).max()
    x=voices.render(v,n=1.0,**q); x=x[match.onset(x):]; x/=np.abs(x).max()
    return q,r,x
def seg_db(s,a,b): s=s[int(a*48000):int(b*48000)]; return 10*np.log10(np.mean(s**2)+1e-12)
def spec(s,a,b,edges=(30,60,120,250,500,1000,2000,4000,8000,16000)):
    s=s[int(a*48000):int(b*48000)]; m=np.abs(np.fft.rfft(s*np.hanning(len(s)),1<<16))**2; f=np.fft.rfftfreq(1<<16,1/48000)
    e=np.array([10*np.log10(m[(f>=lo)&(f<hi)].sum()+1e-12) for lo,hi in zip(edges[:-1],edges[1:])]); return e
def centroid(s,a,b):
    s=s[int(a*48000):int(b*48000)]; m=np.abs(np.fft.rfft(s*np.hanning(len(s)),1<<15))**2; f=np.fft.rfftfreq(1<<15,1/48000)
    return float((f*m).sum()/m.sum())
