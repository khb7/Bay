# 쿨롱 마찰 (Coulomb friction)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

**마찰력의 한계가 수직항력에 비례한다.** 그게 전부다.

```
|F_마찰| ≤ μ · |F_수직항력|
```

## 세 가지 반직관

| | |
|---|---|
| **접촉 면적과 무관** | 벽돌을 눕히든 세우든 미는 힘이 같다. 면적이 늘면 단위면적당 압력이 그만큼 준다 |
| **속도와 무관** | 실제로는 정지마찰 `μs` > 운동마찰 `μk` 지만 시뮬에서는 보통 하나로 퉁친다 |
| **★ 등호가 아니라 부등호** | 마찰은 "정해진 힘을 내는 것"이 아니라 **"필요한 만큼 내되 상한이 있는 것"** |

세 번째가 구현을 결정한다. 안 움직이는 동안 마찰력은 밀리는 힘과 정확히 상쇄될 만큼만
생기고, 한계를 넘는 순간 `μ·N`으로 고정되며 나머지가 가속으로 간다.
**그래서 코드에서 힘이 아니라 클램프로 나타난다.**

## 마찰 원뿔 (friction cone)

3D에서는 접선 방향이 평면이라 위 식이 원뿔이 된다.

```
        ↑ n (법선)
       /|\
      / | \      ← 반각 φ = atan(μ)
     /  |  \
    ────┼────    접선 평면

  합력이 원뿔 안 → 정지        원뿔 밖 → 미끄러짐
```

원뿔의 반각이 곧 [마찰각](friction-angle.md) `φ = atan(μ)` 이고,
[항복면](yield-surface.md)·[Drucker-Prager](drucker-prager.md)에서 말하는 "원뿔의 기울기"와 같은 것이다.

## ★ 안식각이 결과로 나오는 이유

더미 표면의 알갱이에 걸리는 힘을 경사각 `θ`로 분해하면

```
미끄러뜨리는 성분  mg·sinθ  ≤  μ·mg·cosθ  수직항력
                    →  tanθ ≤ μ
```

**`m`과 `g`가 통째로 약분된다.** 경사각의 한계가 오직 `μ`로만 결정된다.

```
tan([안식각](angle-of-repose.md)) = μ        →   34° ↔ μ ≈ 0.67
```

> ⚠ **다만 시뮬에서 `μ`와 안식각은 1:1이 아니다.** 위 유도는 [연속체](continuum.md) 가정이고,
> 실제로는 입자 배열·회전 없음·[반복 횟수](iteration-relaxation.md)가 전부 끼어든다.
> 이론값으로 못 정하므로 [M0-a](../../design/m0-spike.md)에서 스윕으로 뽑는다.

## [XPBD](xpbd.md) 구현

```csharp
// 1. 접촉 제약을 풀어 법선 방향 λ_n 을 얻는다
// 2. 접선 보정을 원뿔 안으로 클램프
float maxTangent = mu * lambda_n;
if (dLambda_t > maxTangent) dLambda_t = maxTangent;   // 미끄러짐
// else 정지 마찰 — 접선 이동을 통째로 되돌린다
```

`maxTangent`를 만들려면 [라그랑주 승수](lagrange-multiplier.md) `λ_n`이 있어야 한다.
**쿨롱 마찰을 물리적으로 걸려면 [PBD](pbd.md)가 아니라 XPBD여야 한다**는 결론이 여기서 나온다.

> ⚠ **기호 충돌 주의.** 이 문서의 `μ`는 마찰계수지만,
> [강성](stiffness.md)·[MPM](mpm.md) 쪽 `μ`는 전단탄성계수(라메 상수)다.
> `λ`도 마찬가지로 여기선 [라그랑주 승수](lagrange-multiplier.md), 저기선 라메 상수다.

---

**관련 문서** — [Drucker-Prager](drucker-prager.md) · [MPM (Material Point Method)](mpm.md) · [PBD (Position Based Dynamics)](pbd.md) · [XPBD](xpbd.md) · [★ MPM의 입자 ≠ 모래알](mpm-particle-vs-grain.md) · [★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](stiffness.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [마찰각 (friction angle)](friction-angle.md) · [반복 (iteration) / 완화 (relaxation)](iteration-relaxation.md) · [안식각 (angle of repose)](angle-of-repose.md) · [연속체 (continuum)](continuum.md) · [크리프 (creep)](creep.md) · [항복면 (yield surface) ★](yield-surface.md)
