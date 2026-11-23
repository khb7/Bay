"""T1 바닥 변화 분해 — 소류 · 부유 플럭스 발산 (trend-check "T1 맨 위 침식", papers D-M7-27).

입력: waves_<tag>_flux.csv (y 평균 ∫q dt, x+ 바다쪽, 칸 중심) · _bed.csv
Δb_sed = −morfac/(1−p) · ∂(∫q_bed dt)/∂x ,  Δb_sus = −morfac/(1−p) · ∂(∫q_sus dt)/∂x  (면 플럭스 ≈ 이웃 칸 평균)
나머지 (Δb − 두 합) = 사태 (avalanche) · 120 s 전 · 근사 오차.
구간: 처음 높이 b0 (윗 swash 0–0.8 · 아래 −0.6–0) 별 부피 (m²/m).
사용: python tools_budget.py <morfac> <tag> [<tag> ...]
"""
import sys
import numpy as np

D = 'C:/Users/onebe/Bay/BeachSimulator/Saved/MeshRuns/'
POR = 0.4


def one(tag, mf):
    f = np.genfromtxt(D + f'waves_{tag}_flux.csv', delimiter=',', names=True)
    b = np.genfromtxt(D + f'waves_{tag}_bed.csv', delimiter=',', names=True)
    dx = f['x'][1] - f['x'][0]
    qb = f['bed_off'] + f['bed_on']
    qs = f['sus_off'] + f['sus_on']
    def div(q):
        qf = np.concatenate([[0.0], 0.5 * (q[1:] + q[:-1]), [q[-1]]])   # 면 (육지 벽 0)
        return -mf / (1 - POR) * (qf[1:] - qf[:-1]) / dx
    zb, zs = div(qb), div(qs)
    # 꼭짓점 Δb 를 칸 중심으로
    db = 0.5 * ((b['b1'] - b['b0'])[1:] + (b['b1'] - b['b0'])[:-1])
    b0 = 0.5 * (b['b0'][1:] + b['b0'][:-1])
    out = [tag]
    for lo, hi in ((0.0, 0.8), (-0.6, 0.0)):
        m = (b0 >= lo) & (b0 < hi)
        out += [(db[m] * dx).sum(), (zb[m] * dx).sum(), (zs[m] * dx).sum()]
    print('%-12s 윗 Δb %+.3f = 소류 %+.3f + 부유 %+.3f   아래 Δb %+.3f = 소류 %+.3f + 부유 %+.3f  (m²/m)' % tuple(out))


if __name__ == '__main__':
    mf = float(sys.argv[1])
    for t in sys.argv[2:]:
        one(t, mf)
