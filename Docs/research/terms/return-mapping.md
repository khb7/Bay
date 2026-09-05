# 리턴 매핑 (return mapping)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

**"허용 영역을 벗어나면 경계로 끌어당기고, 잘라낸 만큼을 영구 변형으로 기록"** 하는 소성 처리.
출처는 [Drucker-Prager](drucker-prager.md) 소성 (papers.md A2, Klár 2016).

## 이름의 뜻

[응력](stress.md)이 허용 영역 밖으로 나가면 **경계 위로 되돌린다(return)** — 즉 밖의 점을
경계 위의 점으로 **사상(map)** 하는 함수다. 수학적으로는 [항복면](yield-surface.md)에 대한 투영.

## 왜 필요한가

고무줄을 조금 늘리면 놓았을 때 돌아온다(**탄성**). 너무 늘리면 늘어난 채로 남는다(**소성**).
시뮬레이션은 이 둘을 이렇게 처리한다.

```
1. 일단 전부 탄성이라 가정하고 변형을 누적한다
2. 그 결과 응력이 허용 영역 안인가?
     안 → 그대로 둔다 (진짜 탄성이었음)
     밖 → 경계로 투영하고, 잘라낸 초과분을 소성 변형으로 영구 기록
```

2번의 투영이 리턴 매핑이다. **"매 스텝 일단 넘겨보고, 넘친 만큼만 되돌린다"** 는 구조.

## Drucker-Prager의 경우 — 분기 셋

허용 영역이 **원뿔**이다(누르는 힘이 셀수록 더 잘 버팀 = 마찰 재료).
[SVD](svd.md)로 회전을 버리고 [Hencky 변형](hencky-strain.md)의 로그 공간으로 가면
원뿔 투영이 **닫힌 형태**로 풀려 반복 없이 `if/else` 셋으로 끝난다. GPU 친화적.

| 위치 | 처리 | 물리적 의미 |
|---|---|---|
| 원뿔 **안** | 그대로 | 탄성 |
| **꼭짓점 바깥** (팽창) | 탄성 변형 전부 0 | 알갱이가 서로 놓아버림 |
| **옆면 바깥** | 수직 투영 | 초과분만 소성 |

> 원뿔 꼭짓점이 원점에 붙어 있으면 **인장을 0도 못 버틴다** = 모래성을 못 쌓는다.
> [점착력](cohesion.md)은 이 꼭짓점을 인장 쪽으로 밀어내는 것이고, 그래서 수직 벽이 선다.
> (papers.md A1, Tampubolon 2017)

## ★ 이 프로젝트에서 — 같은 구조가 세 계층에 나온다

리턴 매핑은 [MPM](mpm.md) 전용 개념이 아니라 **"허용 영역 + 투영"이라는 패턴**이다.
우리가 실제로 구현할 두 계층이 전부 이 패턴의 특수 사례다.

| 계층 | 허용 영역 | 벗어나면 |
|---|---|---|
| MPM (근거로만 참조) | [Drucker-Prager](drucker-prager.md) 원뿔 | 원뿔 표면으로 투영 |
| **[XPBD](xpbd.md) 마찰** | 마찰 원뿔 `μ·\|Δn\|` | 접선 이동을 클램프 |
| **[열침식](thermal-erosion.md)** | [안식각](angle-of-repose.md) `talus(W)` | 초과 경사만큼 모래를 아래로 |

### XPBD 마찰
```
법선 보정량 Δn 계산 → 접선 상대이동 Δt를 되돌림, 단 μ·|Δn| 로 클램프
```
- 완전히 되돌림 = **정지 마찰** = "허용 영역 안" = 탄성
- 클램프됨 = **운동 마찰** = "경계로 투영" = 소성

이 마찰만 제대로 넣으면 [안식각](angle-of-repose.md) 34°가 파라미터가 아니라 **결과로** 나온다.

### 열침식
```
경사 < talus  → 그대로            (허용 영역 안)
경사 > talus  → talus까지만 남기고 초과분을 아래 셀로   (경계로 투영)
```
**하이트필드판 리턴 매핑.** [연속체](continuum.md)의 "[포화도](saturation.md) → [항복면](yield-surface.md) 이동"과 같은 아이디어를
1/1000 비용으로 사는 지점이고, 모래성이 서는 곳이다.

```csharp
float talusAngle = lerp(34f, 90f, wetness);   // ★ 젖으면 더 가파른 각도를 버틴다
```

---

**관련 문서** — [Drucker-Prager](drucker-prager.md) · [Hencky 변형 (로그 변형)](hencky-strain.md) · [MPM (Material Point Method)](mpm.md) · [SVD (특이값 분해) ★](svd.md) · [XPBD](xpbd.md) · [소성 = 클램프 ★](plasticity-as-clamp.md) · [안식각 (angle of repose)](angle-of-repose.md) · [연속체 (continuum)](continuum.md) · [열침식 (thermal erosion)](thermal-erosion.md) · [응력 (stress, σ)](stress.md) · [점착력 (cohesion, c)](cohesion.md) · [팽창을 시뮬레이션이 읽어야 하는 이유](why-simulate-dilation.md) · [포화도 (saturation)](saturation.md) · [항복면 (yield surface) ★](yield-surface.md)
