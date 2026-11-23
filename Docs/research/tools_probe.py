"""T1 맨 위 침식 진단 — swash 칸 시계열 (-probe) 분석 (trend-check "T1 맨 위 침식", papers D-M7-27).

입력: Saved/MeshRuns/waves_<tag>_probe.bin · _probe.txt (가운데 줄, 처음 바닥 −0.3 ~ +1.05 m 칸, 0.1 s, 120 s 뒤)
       한 기록 = [t, (W, HU, S = hC, B) × 칸]. x+ = 바다쪽 (HU > 0 = 내림).
C_eq: SandGrid.usf SusCS 와 같은 식 (Soulsby–van Rijn, k_b = 0 — -borekb 아닌 실행만 정확), 상한 0.5·C_max.
칸마다 (0.1 s 표본 적분, 침식 = 원천 (hC_eq − hC)/T_s 의 + 부분):
  - 오름 / 내림 별 부유 연행 E+ · 침강 E− (m, morfac 전), h 구간별
  - 상한 (C_eq = 0.5 C_max) 이 걸린 시간 비율
  - 이류 플럭스 ∫hu·C dt 오름 / 내림
사용: python tools_probe.py <tag> [tsfac] [tsmin] [sushmin]
"""
import sys
import numpy as np

D = 'C:/Users/onebe/Bay/BeachSimulator/Saved/MeshRuns/'
G, S_, D50, D90, CMAX, NU = 9.81, 2.65, 2.5e-4, 3e-4, 0.1, 1e-6
Dg = (S_ - 1) * G * D50
Ds = (G * (S_ - 1) / NU ** 2) ** (1 / 3) * D50
ASS = 0.012 * D50 * Ds ** -0.6 / Dg ** 1.2
WS = NU / D50 * (np.sqrt(10.36 ** 2 + 1.049 * Ds ** 3) - 10.36)
HDRY = 1e-4
DT = 0.1


def ceq(h, u, hmin=0.0):
    ucr = 0.19 * D50 ** 0.1 * np.log10(np.maximum(4 * h / D90, 1.0))
    ex = np.maximum(np.abs(u) - ucr, 0.0)
    raw = ASS / np.maximum(h, HDRY) * ex ** 2.4
    c = np.where(h < hmin, 0.0, np.minimum(raw, 0.5 * CMAX))
    return c, (raw >= 0.5 * CMAX) & (h >= hmin)


def main():
    tag = sys.argv[1]
    tsfac = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1
    tsmin = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    hmin = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0   # -sushmin
    meta = dict(l.split() for l in open(D + f'waves_{tag}_probe.txt'))
    i0, i1 = int(meta['i0']), int(meta['i1'])
    n = i1 - i0 + 1
    a = np.fromfile(D + f'waves_{tag}_probe.bin', dtype=np.float32).reshape(-1, 1 + 4 * n)
    t = a[:, 0]
    W, HU, S, B = (a[:, 1 + q::4] for q in range(4))
    h = W - B
    wet = h > HDRY
    hs = np.where(wet, h, np.nan)
    u = np.where(wet, HU / np.maximum(h, HDRY), 0.0)
    C = np.where(wet, S / np.maximum(h, HDRY), 0.0)
    Ce, cap = ceq(h, u, hmin)
    Ts = np.maximum(tsfac * h / WS, tsmin)
    src = np.where(wet, h * (Ce - C) / Ts, 0.0)   # m/s, + = 바닥에서 떠오름 (침식)
    up = wet & (u < 0)
    bw = wet & (u > 0)
    db = B[-1] - B[0]
    b0 = B[0]
    print(f'# {tag}  t {t[0]:.0f}-{t[-1]:.0f} s  cells {i0}-{i1}  ws {WS:.4f} m/s  Ass {ASS:.3e}  tsfac {tsfac} tsmin {tsmin}')
    print('# E+/E-: 오름(u) · 내림(b) 별 ∫원천 dt (mm, morfac 전) · cap: 젖은 시간 중 상한 걸린 비율 · F: ∫huC dt (m², +바다쪽)')
    print('   b0     db_mm   Eu+    Eu-    Eb+    Eb-   net  | cap_u cap_b | Fu       Fb       | h_u   h_b   C_u    C_b    Ceq_u  Ceq_b')
    for k in range(0, n, 3):
        m_u, m_b = up[:, k], bw[:, k]
        def I(x, m):
            return 1000 * DT * x[m].sum()
        sp, sn = np.maximum(src[:, k], 0), np.minimum(src[:, k], 0)
        F = HU[:, k] * C[:, k] * DT
        nw = max(m_u.sum(), 1), max(m_b.sum(), 1)
        print('%6.3f %7.1f %6.1f %6.1f %6.1f %6.1f %6.1f | %4.2f  %4.2f | %+.2e %+.2e | %.3f %.3f %.4f %.4f %.4f %.4f' % (
            b0[k], 1000 * db[k], I(sp, m_u), I(sn, m_u), I(sp, m_b), I(sn, m_b), 1000 * DT * src[:, k].sum(),
            cap[m_u, k].sum() / nw[0], cap[m_b, k].sum() / nw[1], F[m_u].sum(), F[m_b].sum(),
            np.nanmean(hs[m_u, k]) if m_u.any() else 0, np.nanmean(hs[m_b, k]) if m_b.any() else 0,
            C[m_u, k].mean() if m_u.any() else 0, C[m_b, k].mean() if m_b.any() else 0,
            Ce[m_u, k].mean() if m_u.any() else 0, Ce[m_b, k].mean() if m_b.any() else 0))
    # 최대 침식 칸: 내림 연행을 수심 구간별로
    k = int(np.argmin(db))
    print(f'\n# 최대 침식 칸 b0 {b0[k]:+.3f} (db {1000 * db[k]:.0f} mm): 내림 · 오름 원천을 h 구간별 (mm) · 시간 (s)')
    edges = [HDRY, 0.005, 0.01, 0.02, 0.05, 10]
    for lo, hi in zip(edges[:-1], edges[1:]):
        mh = (h[:, k] >= lo) & (h[:, k] < hi)
        print('  h %5.3f-%5.3f  오름 %+6.1f mm (%5.0f s)  내림 %+6.1f mm (%5.0f s)  내림 cap %.2f' % (
            lo, hi, 1000 * DT * src[mh & up[:, k], k].sum(), DT * (mh & up[:, k]).sum(),
            1000 * DT * src[mh & bw[:, k], k].sum(), DT * (mh & bw[:, k]).sum(),
            cap[mh & bw[:, k], k].mean() if (mh & bw[:, k]).any() else 0))


if __name__ == '__main__':
    main()
