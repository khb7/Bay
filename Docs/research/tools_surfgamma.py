"""C1 추세 점검 — 쇄파대 γs = Hs/h 단면 (trend-check.md C1).

입력: Saved/MeshRuns/waves_<tag>_row.bin (안쪽 5 cm 격자, 4 칸마다) · _rowo.bin (바깥 25 cm). 둘 다 0.1 s, 120 s 이후.
정의 (문헌 확인 후 고정, papers D-M7-12):
  h  = 평균 수위 − 처음 바닥 (setup 포함)
  Hs = 4·σ (sea-swell 대역 [flo, fhi] Hz, FFT 대역 통과)
  β  = 국소 바닥 경사 |dh/dx| (Dean h = A·(x − Xs)^(2/3) 의 도함수)
  kh = 선형 분산, 주기 = 대역 평균 주기 (m0/m1)
사용: python -I tools_surfgamma.py <tag> [flo fhi] [t0]
"""
import sys
import numpy as np

D = 'C:/Users/onebe/Bay/BeachSimulator/Saved/MeshRuns/'
A, Xs, Sl = 0.1, 19.52, 1 / 15
DT = 0.1
G = 9.81


def bed(x):
    return np.where(x >= Xs, -A * np.maximum(x - Xs, 0) ** (2 / 3), (Xs - x) * Sl)


def slope(x):
    return np.where(x > Xs, (2 / 3) * A * np.maximum(x - Xs, 1e-6) ** (-1 / 3), Sl)


def kh_of(T, h):
    w = 2 * np.pi / T
    k = w / np.sqrt(G * h)
    for _ in range(50):
        k = w * w / (G * np.tanh(k * h))
    return k * h


def band(e, flo, fhi):
    n = len(e)
    F = np.fft.rfft(e - e.mean())
    f = np.fft.rfftfreq(n, DT)
    m = (f >= flo) & (f <= fhi)
    S = np.abs(F) ** 2
    m0 = S[m].sum()
    m1 = (S[m] * f[m]).sum()
    Fb = np.where(m, F, 0)
    eb = np.fft.irfft(Fb, n)
    Tm = m0 / m1 if m1 > 0 else np.nan
    return eb, Tm


def stats(ts, x, flo, fhi):
    b = bed(np.array(x))
    h = ts.mean() - b
    if h < 0.05:
        return None
    eb, Tm = band(ts.astype(float), flo, fhi)
    Hs = 4 * eb.std()
    return dict(x=x, h=float(h), Hs=Hs, g=Hs / h, beta=float(slope(np.array(x))), kh=kh_of(Tm, h), Tm=Tm)


def main():
    tag = sys.argv[1]
    flo = float(sys.argv[2]) if len(sys.argv) > 2 else 0.05
    fhi = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    t0 = float(sys.argv[4]) if len(sys.argv) > 4 else 120.0
    ri = np.fromfile(D + f'waves_{tag}_row.bin', dtype=np.float32).reshape(-1, 168)
    ro = np.fromfile(D + f'waves_{tag}_rowo.bin', dtype=np.float32)
    ro = ro.reshape(ri.shape[0], ro.size // ri.shape[0])   # 바깥 칸 수는 경계 수심 따라 (3.3 m 837 · 11 m 2138)
    # 두 기록 모두 120 s 부터 (900 s 실행 → 7801 줄)
    ni = ri.shape[0]
    so = max(0, int(round((t0 - 120) / DT)))
    ri = ri[so:]
    ro = ro[so:]
    rows = []
    for i in range(0, 168, 4):
        r = stats(ri[:, i], 4 * i * 0.05, flo, fhi)
        if r:
            r['grid'] = 'in'
            rows.append(r)
    for i in range(130, min(ro.shape[1], 837), 20):   # x ≤ 209 m (Dean 구간)
        r = stats(ro[:, i], i * 0.25 + 0.125, flo, fhi)
        if r:
            r['grid'] = 'out'
            rows.append(r)
    print(f'# {tag} band {flo}-{fhi} Hz, t0 {t0} s, inner rows {ni}')
    print('grid  x(m)   h(m)  Hs(m)  Hs/h   beta    kh    b/kh   Tm')
    for r in rows:
        print('%-4s %6.1f %6.2f %6.2f %6.2f %6.3f %6.3f %6.3f %5.1f' % (
            r['grid'], r['x'], r['h'], r['Hs'], r['g'], r['beta'], r['kh'], r['beta'] / r['kh'], r['Tm']))


if __name__ == '__main__':
    main()
