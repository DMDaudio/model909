import sys, voices, match, numpy as np
def env_table(x, r, ts=(0,1,2,3,5,8,12,20,30,45,60,80,100,130,160,200,250,300,400)):
    x, r = match.align(x, r)
    ex, er = match.env_db(x), match.env_db(r)
    print("ms    " + " ".join(f"{t:5d}" for t in ts))
    print("model " + " ".join(f"{ex[t]:5.0f}" if t < len(ex) else "    -" for t in ts))
    print("ref   " + " ".join(f"{er[t]:5.0f}" if t < len(er) else "    -" for t in ts))
def pitch(x, r):
    x, r = match.align(x, r)
    px, pr = match.pitch_track(x), match.pitch_track(r)
    for i in (0,1,2,4,6,10,16,24,40,60):
        if i < min(len(px), len(pr)): print(f"  {5*i:4d} ms  model {px[i]:6.1f}  ref {pr[i]:6.1f}")
def bands(x, r, t0=0, t1=0.05):
    x, r = match.align(x, r)
    edges = [63,125,250,500,1000,2000,4000,8000,16000]
    out=[]
    for s in (x, r):
        seg = s[int(t0*48000):int(t1*48000)]
        S = np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f = np.fft.rfftfreq(len(seg),1/48000)
        e = [10*np.log10(S[(f>=a)&(f<b)].sum()+1e-12) for a,b in zip(edges[:-1],edges[1:])]
        out.append(np.array(e)-max(e))
    print("band  " + " ".join(f"{a:5d}" for a in edges[:-1]))
    print("model " + " ".join(f"{v:5.0f}" for v in out[0]))
    print("ref   " + " ".join(f"{v:5.0f}" for v in out[1]))
