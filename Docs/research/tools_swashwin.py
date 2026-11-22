"""C4·C5 추세 점검 — 처오름 정상성 (창별 통계) · 내림 최저점 (trend-check.md).
CSV: -gridwaves 출력 (t, R0..R3 처오름 높이 m). 창 길이 W s 마다 최댓값 · 2% · 평균 · 최저점.
문헌 기준: 2–3 분 창 CV 입사 ~0.2 · IG 지배 ~0.65–0.8 (합성 계산, papers D-M7-13), 20 분 이상 권장.
내림 기대 ≈ η − S/2 (S = 4σ 전체 swash).
사용: python tools_swashwin.py <tag> [W=120]"""
import sys, numpy as np
D='C:/Users/onebe/Bay/BeachSimulator/Saved/MeshRuns/'
tag=sys.argv[1]; W=float(sys.argv[2]) if len(sys.argv)>2 else 120.
path=D+f'waves_{tag}.csv'
with open(path,encoding='utf-8') as f: cols=next(l for l in f if l.startswith('t,')).strip().split(',')
raw=np.loadtxt(path,delimiter=',',comments=['#','t,'],encoding='utf-8')
d={c:raw[:,i] for i,c in enumerate(cols)}; t=d['t']
R=np.stack([d['R%d'%k] for k in range(4)])
def maxima(x):
    m=np.median(x); a=x>m; out=[]; i=0
    while i<len(x):
        if a[i]:
            j=i
            while j<len(x) and a[j]: j+=1
            out.append(x[i:j].max()); i=j
        else: i+=1
    return np.array(out)
print(f'# {tag} 창 {W:.0f} s, 4 줄 평균')
print(' t0    max    R2%   mean   min   S=4σ  η-S/2  n_max')
rows=[]
for t0 in np.arange(0,t[-1]-W+1,W):
    m=(t>=t0)&(t<t0+W); v=[]
    for k in range(4):
        x=R[k,m]; mx=maxima(x)
        r2=np.quantile(mx,0.98) if len(mx)>3 else np.nan
        S=4*x.std(); v.append((x.max(),r2,x.mean(),x.min(),S,x.mean()-S/2,len(mx)))
    v=np.array(v).mean(0); rows.append(v)
    print('%4.0f '%t0+' '.join('%6.2f'%q for q in v[:6])+' %4.0f'%v[6])
rows=np.array(rows[1:])  # 0–W 창 (시작 과도) 제외한 CV
print('CV (첫 창 제외): max %.2f  R2 %.2f  mean %.2f  min %.2f'%tuple(rows[:,i].std()/abs(rows[:,i].mean()) for i in (0,1,2,3)))
m=t>120
for k in [0]:
    x=R[:,m].mean(0)
full=[(R[k,m].mean(),4*R[k,m].std(),R[k,m].min(),np.quantile(R[k,m],0.02)) for k in range(4)]
f=np.array(full).mean(0)
print('120 s 이후 전체: η %.2f  S %.2f  η-S/2 %.2f  최저 %.2f  2%% 하위 %.2f'%(f[0],f[1],f[0]-f[1]/2,f[2],f[3]))
