"""T3 · C25 바깥 분산 격자의 비쇄파 에너지 손실 — Hs(x) vs 선형 천수 (trend-check C25, fix-list T3, papers D-M7-31).

입력: waves_<tag>_rowo.bin (바깥 격자 가운데 줄 모든 칸, 0.1 s, 120 s 뒤) · _row.bin (줄 수 확인용) · _nuo.csv (있으면 ν > 0 비율)
정의:
  h  = 평균 수위 − 처음 바닥 (Dean A 0.1, Xs 19.52, 앞빈 1:15 — 실행 단면과 같음)
  Hs = 4σ (0.05 Hz 넘는 대역, IG 제외)
  선형 천수 기대 = 가장 깊은 측정점 Hs × Ks(h)/Ks(h0), Ks = sqrt(c_g0/c_g) (대역 평균 주기)
  잔존 = Hs / 기대 (1 = 손실 없음)
사용: python tools_shoal.py <tag> [<tag> ...]
"""
import os
import sys
import numpy as np

D = 'C:/Users/onebe/Bay/BeachSimulator/Saved/MeshRuns/'
A, Xs, Sl, DT, G = 0.1, 19.52, 1 / 15, 0.1, 9.81
DXO = 0.25


def bed(x):
    return np.where(x >= Xs, -A * np.maximum(x - Xs, 0) ** (2 / 3), (Xs - x) * Sl)


def cg(T, h):
    w = 2 * np.pi / T
    k = w / np.sqrt(G * h)
    for _ in range(60):
        k = w * w / (G * np.tanh(k * h))
    n = 0.5 * (1 + 2 * k * h / np.sinh(2 * k * h))
    return n * w / k


def one(tag):
    ri = os.path.getsize(D + f'waves_{tag}_row.bin') // 4 // 168
    ro = np.fromfile(D + f'waves_{tag}_rowo.bin', dtype=np.float32)
    nxo = ro.size // ri
    ro = ro.reshape(ri, nxo)
    dxo = DXO
    nu = None
    if os.path.exists(D + f'waves_{tag}_nuo.csv'):
        nu = np.genfromtxt(D + f'waves_{tag}_nuo.csv', delimiter=',', names=True)['nu_frac']
    f = np.fft.rfftfreq(ri, DT)
    m = f > 0.05
    rows = []
    for i in range(nxo - 4, 0, -max(1, nxo // 24)):
        x = (i + 0.5) * dxo
        e = ro[:, i].astype(float)
        h = e.mean() - float(bed(np.array(x)))
        if h < 1.0:
            continue
        F = np.fft.rfft(e - e.mean())
        S = np.abs(F[m]) ** 2
        Hs = 4 * np.sqrt(2 * S.sum()) / ri
        Tm = S.sum() / (S * f[m]).sum()
        hi = (S * (f[m] > 0.5)).sum() / S.sum()
        rows.append((x, h, Hs, Tm, hi, nu[i] if nu is not None and i < len(nu) else np.nan))
    x0, h0, H0, T0 = rows[0][:4]
    print(f'# {tag}  nxo {nxo}  rows {ri}  기준 x {x0:.0f} m h {h0:.2f} Hs {H0:.2f} Tm {T0:.1f}')
    print('   x      h     Hs   기대   잔존   Hs/h  >0.5Hz  nu>0')
    for x, h, Hs, Tm, hi, n in rows:
        Ex = H0 * np.sqrt(cg(T0, h0) / cg(T0, h))
        print('%6.0f %5.2f %5.2f %5.2f %5.2f %5.2f %5.2f %6.3f' % (x, h, Hs, Ex, Hs / Ex, Hs / h, hi, n))


if __name__ == '__main__':
    for t in sys.argv[1:]:
        one(t)
