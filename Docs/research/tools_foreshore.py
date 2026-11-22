"""T1 · C12 앞빈 지형 변화 (trend-check C12, fix-list T1).

입력: Saved/MeshRuns/waves_<tag>_bed.csv (x, b0 처음 바닥, b1 끝 바닥, db_mean, db_min, db_max — 줄 평균 단면)
측정 (C12 와 같은 정의):
  앞빈 경사 = 처음 높이 −0.2 ~ +0.6 m 구간 바닥 선형 맞춤 (처음 · 끝)
  해안선 이동 = b = 0 위치 변화 (+ = 바다쪽)
  윗 swash 부피 = 처음 높이 0 ~ +0.8 m 구간 Σ Δb·dx (m²/m) · 아래 swash · 내측 = −0.6 ~ 0 m
사용: python tools_foreshore.py <tag> [<tag> ...]
"""
import sys
import numpy as np

D = 'C:/Users/onebe/Bay/BeachSimulator/Saved/MeshRuns/'


def x_at_zero(x, b):
    i = np.where((b[:-1] >= 0) & (b[1:] < 0))[0][0]
    return x[i] + b[i] / (b[i] - b[i + 1]) * (x[i + 1] - x[i])


def one(tag):
    d = np.genfromtxt(D + f'waves_{tag}_bed.csv', delimiter=',', names=True)
    x, b0, b1 = d['x'], d['b0'], d['b1']
    db = b1 - b0
    dx = x[1] - x[0]
    m = (b0 >= -0.2) & (b0 <= 0.6)
    s0 = -np.polyfit(x[m], b0[m], 1)[0]
    s1 = -np.polyfit(x[m], b1[m], 1)[0]
    up = (b0 >= 0) & (b0 <= 0.8)
    lo = (b0 >= -0.6) & (b0 < 0)
    print('%-12s 1:%.1f -> 1:%.1f  shore %+.2f m  up %+.3f  low %+.3f m2/m  min %+.0f mm (b0 %+.2f)  max %+.0f mm (b0 %+.2f)' % (
        tag, 1 / s0, 1 / s1, x_at_zero(x, b1) - x_at_zero(x, b0), (db[up] * dx).sum(), (db[lo] * dx).sum(),
        1000 * db.min(), b0[db.argmin()], 1000 * db.max(), b0[db.argmax()]))


if __name__ == '__main__':
    for t in sys.argv[1:]:
        one(t)
