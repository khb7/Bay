"""M3 T3b — 처오름 · 장주기파 분석 (m3-plan T3b, papers D-M3-7/8).

입력: `-run=SpikeSweep -gridwaves` 가 쓴 CSV (t, R0..R3 처오름 높이, eta_b, u_b 경계 근처, A2 포락선²).
측정 방법 = Stockdon & Holman, USGS Data Series 602:
  유의 swash = 4·sqrt(Σ PSD·df), 입사 > 0.05 Hz, IG < 0.05 Hz · R2% = 개별 처오름 최댓값의 2% 초과 · setup = 평균
비교 = Stockdon et al. 2006: setup 0.35β√(H0L0), S_inc 0.75β√(H0L0), S_IG 0.06√(H0L0), R2 = 1.1(η̄ + ½√(S_inc²+S_IG²))
기준 6: 경계 근처 η± = ½(η ± √(h/g)·u) — 나가는 IG 대비 다시 들어오는 IG
기준 7: IG 처오름과 포락선 A² 의 교차상관 지연

사용: python tools_waves.py <csv> [<csv> ...]   (앞 2000 초 중 처음 SPIN 초는 버린다)
"""
import sys, math
import numpy as np
from scipy import signal

G = 9.81
SPIN = 120.0 if len(sys.argv) < 2 or 'smoke' not in sys.argv[1] else 10.0   # 첫 2 분 (초기 충격 · 수위 자리잡기) 버림


def header(path):
    meta = {}
    with open(path, encoding='utf-8', errors='ignore') as f:
        for line in f:
            if not line.startswith('#'):
                break
            if not line.startswith('# beta'):
                continue
            toks = line[1:].split()
            for i in range(0, len(toks) - 1):
                if toks[i] in ('beta', 'deepH0', 'Tp', 'boundary_h0'):
                    meta[toks[i]] = float(toks[i + 1])
    return meta


def band_swash(x, fs, lo, hi):
    f, p = signal.welch(x - x.mean(), fs=fs, nperseg=min(len(x), int(fs * 512)), detrend='linear')
    m = (f >= lo) & (f < hi)
    df = f[1] - f[0]
    return 4.0 * math.sqrt(p[m].sum() * df), f, p


def r2_from_maxima(x):
    # 개별 처오름 = 국소 최댓값 (평균 위로 올라간 구간마다 하나)
    mx = []
    above = x > np.median(x)
    i = 0
    n = len(x)
    while i < n:
        if above[i]:
            j = i
            while j < n and above[j]:
                j += 1
            mx.append(x[i:j].max())
            i = j
        else:
            i += 1
    mx = np.sort(np.array(mx))
    return float(np.quantile(mx, 0.98)) if len(mx) > 10 else float('nan'), len(mx)


def analyze(path):
    meta = header(path)
    raw = np.loadtxt(path, delimiter=',', skiprows=3, encoding='utf-8')   # 줄 1·2 = '#' 머리, 줄 3 = 열 이름
    cols = ['t', 'R0', 'R1', 'R2', 'R3', 'eta_b', 'u_b', 'A2']
    d = {c: raw[:, i] for i, c in enumerate(cols)}
    t = d['t']
    keep = t > SPIN
    t = t[keep]
    fs = 1.0 / np.median(np.diff(t))
    beta, H0, Tp, h0 = meta['beta'], meta['deepH0'], meta['Tp'], meta['boundary_h0']
    L0 = G * Tp * Tp / (2 * math.pi)
    sq = math.sqrt(H0 * L0)
    st = dict(setup=0.35 * beta * sq, Sinc=0.75 * beta * sq, Sig=0.06 * sq)
    st['R2'] = 1.1 * (st['setup'] + 0.5 * math.hypot(st['Sinc'], st['Sig']))
    res = []
    for r in range(4):
        x = d['R%d' % r][keep]
        Sinc, f, p = band_swash(x, fs, 0.05, fs / 2)
        Sig, _, _ = band_swash(x, fs, 0.004, 0.05)
        R2, nmax = r2_from_maxima(x)
        res.append((x.mean(), Sinc, Sig, R2, nmax))
    res = np.array([r[:4] for r in res])
    m = res.mean(axis=0)
    print(f'== {path}')
    print(f'   β 1:{1/beta:.0f} · H0 {H0:.2f} · Tp {Tp:.1f} · L0 {L0:.0f} m · Iribarren ξ0 {beta/math.sqrt(H0/L0):.2f} · 기록 {t[-1]-t[0]:.0f} s')
    print(f'   계산 (4 줄 평균 ± 줄 편차)        Stockdon 2006')
    names = ['setup', 'S_inc', 'S_IG ', 'R2%  ']
    keys = ['setup', 'Sinc', 'Sig', 'R2']
    for k in range(4):
        print(f'   {names[k]} {m[k]:7.3f} ± {res[:, k].std():.3f} m   {st[keys[k]]:7.3f} m   (비 {m[k]/st[keys[k]]:.2f})')
    # 기준 6 — 경계 근처 들어오는 / 나가는 IG
    eta = d['eta_b'][keep]
    u = d['u_b'][keep]
    hb = np.maximum(h0 + eta, 1e-3)
    ein = 0.5 * (eta + np.sqrt(hb / G) * (-u))   # 해안 쪽 = −x
    eout = 0.5 * (eta - np.sqrt(hb / G) * (-u))
    b, a = signal.butter(3, [0.004, 0.05], btype='band', fs=fs)
    gin = signal.filtfilt(b, a, ein)
    gout = signal.filtfilt(b, a, eout)
    print(f'   기준 6 경계 IG: 들어옴 Hm0 {4*gin.std():.3f} m · 나감 {4*gout.std():.3f} m (반사 계수 {gout.std()/max(gin.std(),1e-9):.2f})')
    # 기준 7 — 포락선 A² (IG 대역) 와 처오름 (IG 대역) 교차상관
    A2 = signal.filtfilt(b, a, d['A2'][keep] - d['A2'][keep].mean())
    best = []
    for r in range(4):
        x = signal.filtfilt(b, a, d['R%d' % r][keep] - d['R%d' % r][keep].mean())
        c = signal.correlate(x, A2, mode='full')
        lags = signal.correlation_lags(len(x), len(A2), mode='full') / fs
        win = (lags > -60) & (lags < 120)
        i = np.argmax(np.abs(c[win]))
        norm = math.sqrt((x * x).sum() * (A2 * A2).sum())
        best.append((lags[win][i], c[win][i] / norm))
    print('   기준 7 처오름(IG) ↔ 포락선²(IG) 최대 상관: ' + ' · '.join(f'{l:+.1f}s ({cc:+.2f})' for l, cc in best))
    return m, st


if __name__ == '__main__':
    for p in sys.argv[1:]:
        analyze(p)
