# M1 — 입자별 젖음 W (2026-10-27)

> 근거: [papers.md](../research/papers.md) C16 (Shi & McCarthy 2008 액체다리 부피) · L-3 ① (젖음 재질 값) · B1 (cohesion(W) 곡선).
> milestones M1 "W는 정점 컬러에 실어 우회", M2 "쌓기의 W 상속" 의 바탕.

## 지금
- 시뮬 W 는 전역값 하나 (`FSandSpikeParams::WGlobal`). 점착 상한 `BondLambdaMax`, 결합 끊기 `bBondsDry` 가 전부 이것 하나에서 나온다.
- 재질 `M_Sand` 의 `Wet` 은 MID 스칼라 하나.

## 단계

| 단계 | 내용 | 검증 |
|---|---|---|
| **W1** 시뮬 W 버퍼 | 입자마다 float W (CPU 배열 + GPU 버퍼). 장면 초기화에서 채운다 (`-sandwetmode=height|half`). 입자 순서가 바뀌는 곳이 있으면 같이 옮긴다. 아직 바뀌지 않는다 (침투 · 증발은 M5) | W 가 프레임 지나도 입자를 따라가는가 (재정렬 후 대조) |
| **W2** 정점 W → 정점 색 | 메셔: 정점 W = 주변 입자 W 의 셰퍼드 보간 (wnn 과 같은 가중 · 반경). 평활화 후 위치 기준. `FLocalVertexFactory` 색 버퍼 R 에 싣는다. 재질 `Wet` = VertexColor.R × 스칼라 | CPU 판과 정점 W 일치 (최대 오차 < 1e-4) · 반 젖은 장면 화면 |
| **W3** 점착을 쌍의 W 로 | 결합 하나의 W = 두 입자 W 의 평균 (C16: 같은 반지름이면 다리 부피 = 각자 절반씩 → 평균). 상한 = BondStrength·g·cohesion(W_pair)·dt², cohesion 0 이면 그 결합만 끊는다 | 전역 W 를 모두에 준 것과 비트 일치 (회귀) · 반 젖은 기둥에서 마른 쪽만 무너지는가 |

## 하지 않는 것
- W 의 시간 변화 (침투 · 배수 · 증발 · 입자 사이 이동) — M5.
- 결합 끊길 때 액체 재분배 (Schmelzle & Nirschl) — W 가 변하지 않으니 의미 없다. M5 에서.
- SSS · 반짝임 세부 — M8.

## 순서
W1 → W2 (눈으로 확인) → W3. 각 단계 m1-log 에 기록 후 커밋.
