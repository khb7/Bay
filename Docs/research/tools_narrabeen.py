"""Narrabeen 단면 분석 — 기준 해변 값 (papers D-M7-35/36).

자료: Turner et al. (2016) Sci. Data 3:160024 (Dryad doi:10.5061/dryad.28g01 · 갱신판 2019 까지) — Docs/research/csv/ (커밋 안 함).
고도 = m AHD (≈ 평균 해수면). Chainage = 육지 기준점에서 바다 쪽 거리 (m).

출력 (단면 PF1 · PF2 · PF4 · PF6 · PF8 각각):
  1. 전면 (foreshore) 경사 — 월별 측량마다 z ∈ [ZLO, ZHI] 직선 맞춤 → 중앙값 · 사분위
     (Stockdon 2006 β_f = 평균 수위 ± 2σ swash 구간 경사; 여기선 대조 저조~고조 ±0.65 m 근처로 근사)
  2. 해저 지형 측량 11 회 (2011–2015) 와 가장 가까운 날짜의 측량 단면을 이어 물가 (z = 0) 기준 거리 x' 로
     Dean h = A x'^(2/3) 맞춤 (h 0.5–10 m) · 수심 3.3 / 5.5 / 10 m 까지 거리 · 5.5→10 m 평균 경사 · 사주 (맞춤보다 얕은 혹)
사용: python tools_narrabeen.py
"""
import csv
import collections
import datetime as dt
import os
import numpy as np

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'csv')
ZLO, ZHI = -0.5, 1.0


def load_profiles():
    P = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in csv.DictReader(open(os.path.join(D, 'Narrabeen_Profiles.csv'), encoding='utf-8-sig')):
        P[r['Profile ID']][dt.date.fromisoformat(r['Date'])].append((float(r['Chainage']), float(r['Elevation'])))
    return P


def load_bathy():
    B = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in csv.DictReader(open(os.path.join(D, 'Narrabeen_Bathymetry.csv'), encoding='utf-8-sig')):
        d, m, y = map(int, r['Date'].split('/'))
        B[r['Profile ID']][dt.date(y, m, d)].append((float(r['Chainage (m)']), float(r['Elevation (m AHD)'])))
    return B


def arr(pts):
    a = np.array(sorted(pts))
    return a[:, 0], a[:, 1]


def crossing(x, z, z0):
    """바다 쪽으로 가며 z 가 z0 아래로 처음 내려가는 거리 (선형 보간)."""
    for i in range(len(x) - 1):
        if z[i] >= z0 > z[i + 1]:
            return x[i] + (z[i] - z0) / (z[i] - z[i + 1]) * (x[i + 1] - x[i])
    return np.nan


def fore_slope(x, z):
    m = (z >= ZLO) & (z <= ZHI)
    if m.sum() < 3 or z.min() > ZLO + 0.2 or z.max() < ZHI - 0.2:
        return np.nan
    return -np.polyfit(x[m], z[m], 1)[0]


def main():
    P = load_profiles()
    B = load_bathy()
    print(f'# 전면 경사 z ∈ [{ZLO}, {ZHI}] m AHD, 월별 측량')
    print('PF    n   tanβ 중앙  (25–75 %)    1/중앙   z 최저 중앙')
    for pf in sorted(P):
        s, zmin = [], []
        for d, pts in P[pf].items():
            x, z = arr(pts)
            zmin.append(z.min())
            b = fore_slope(x, z)
            if np.isfinite(b) and b > 0:
                s.append(b)
        s = np.array(s)
        q1, q2, q3 = np.percentile(s, [25, 50, 75])
        print(f'{pf}  {len(s):4d}  {q2:.3f}  ({q1:.3f}–{q3:.3f})   1:{1 / q2:4.1f}   {np.median(zmin):+.2f}')

    print('\n# 해저 지형 + 가장 가까운 측량 단면 → Dean 맞춤 (h 0.5–10 m, x\' = 물가 z 0 에서)')
    print('PF   날짜         A     맞춤 rms   x(3.3) x(5.5) x(10)   5.5→10 경사   사주 (x\', 높이)')
    summ = collections.defaultdict(list)
    for pf in sorted(B):
        for d in sorted(B[pf]):
            pd = min(P[pf], key=lambda k: abs((k - d).days))
            xp, zp = arr(P[pf][pd])
            xb, zb = arr(B[pf][d])
            x = np.concatenate([xp[xp < xb.min()], xb])
            z = np.concatenate([zp[xp < xb.min()], zb])
            x0 = crossing(x, z, 0.0)
            if not np.isfinite(x0):
                continue
            xs, h = x - x0, -z
            m = (xs > 0) & (h >= 0.5) & (h <= 10.0)
            X = xs[m] ** (2 / 3)
            A = float((X * h[m]).sum() / (X * X).sum())
            rms = float(np.sqrt(((h[m] - A * X) ** 2).mean()))
            xd = [crossing(xs, -h, -dd) for dd in (3.3, 5.5, 10.0)]
            sl = (10.0 - 5.5) / (xd[2] - xd[1]) if np.all(np.isfinite(xd[1:])) else np.nan
            # 사주: 맞춤보다 얕은 (h < A x^(2/3)) 구간의 가장 큰 혹 (h 1–6 m)
            r = A * xs ** (2 / 3) - h
            mb = (xs > 0) & (h > 1.0) & (h < 6.0)
            bar = (float(xs[mb][np.argmax(r[mb])]), float(r[mb].max())) if mb.any() else (np.nan, np.nan)
            summ[pf].append((A, rms, *xd, sl, *bar))
            print(f'{pf}  {d}  {A:.3f}  {rms:5.2f}   {xd[0]:6.0f} {xd[1]:6.0f} {xd[2]:6.0f}    1:{1 / sl:5.0f}      ({bar[0]:4.0f} m, {bar[1]:+.2f} m)')
    print('\n# 단면별 중앙값')
    allA = []
    for pf in sorted(summ):
        a = np.array(summ[pf])
        med = np.nanmedian(a, axis=0)
        allA += list(a[:, 0])
        print(f'{pf}  A {med[0]:.3f} (범위 {a[:, 0].min():.3f}–{a[:, 0].max():.3f})  rms {med[1]:.2f}  x(3.3/5.5/10) {med[2]:.0f}/{med[3]:.0f}/{med[4]:.0f} m  5.5→10 1:{1 / med[5]:.0f}  사주 {med[6]:.0f} m +{med[7]:.2f} m')
    print(f'전체 A 중앙 {np.median(allA):.3f}')


if __name__ == '__main__':
    main()
