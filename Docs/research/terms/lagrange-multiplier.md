# 라그랑주 승수 (Lagrange multiplier, λ)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

**"[제약](constraint.md)을 지키게 하려면 얼마나 세게 밀어야 하는가" — 그 세기.**
미는 *방향*은 제약 함수의 기울기 `∇C`가 알려주므로, `λ`는 *크기*만 담당한다.

```
∇f = λ · ∇C        제약 있는 최적화의 해에서 성립하는 조건
```

구슬을 철사에 꿴 그림으로 읽으면 된다 — `∇C`는 철사가 미는 방향(제약면 법선),
**`λ`는 철사가 미는 힘의 크기**다. 수학적 장치처럼 보이지만 **재면 나오는 물리량**이다.

## 거리 제약에서의 모습

```
C(x) = |x₁ − x₂| − d          위반량. 0이면 만족
∇₁C  = n,  ∇₂C = −n           n = 두 입자를 잇는 단위벡터
w    = 1/m                    역질량
```

**[PBD](pbd.md)** — `λ`를 들고 있지 않고 매 반복 새로 계산한다.
```csharp
float lambda = -C / (w1 + w2);
p1 += w1 * lambda * n;   p2 -= w2 * lambda * n;
```

**[XPBD](xpbd.md)** — [컴플라이언스](compliance.md) `α̃ = α/dt²`를 넣고 **`λ`를 누적한다.**
```csharp
float dLambda = (-C - alphaT * lambda) / (w1 + w2 + alphaT);
lambda += dLambda;                        // ★ 반복을 넘어 살아남는다
p1 += w1 * dLambda * n;   p2 -= w2 * dLambda * n;
```

`α = 0` 이고 누적을 빼면 정확히 PBD로 되돌아간다. **차이는 저 누적 한 줄뿐이다.**

## 왜 누적이 XPBD의 핵심인가

PBD는 "지금 얼마나 어겼나"만 보고 민다. 그래서 반복을 20회 돌면 10회일 때보다
**제약이 더 뻣뻣해진다** — [강성](stiffness.md)이 `iterations`와 `dt`에 끌려다닌다
([상세](pbd-stiffness-compliance.md)).

`λ`를 누적하면 **"이 제약에 지금까지 총 얼마나 밀어넣었는가"**가 기록되고,
분모의 `α̃`가 그 총량에 상한을 건다. `-C - α̃λ`가 0으로 수렴하면서 스스로 멈추므로
**강성이 반복 횟수가 아니라 `α` 하나로 결정된다.** XPBD가 강성을 물리 단위로
받을 수 있는 이유가 이것이다.

## ★ 이 프로젝트가 λ를 필요로 하는 진짜 이유

`λ`는 제약력이므로 **읽어서 판정에 쓸 수 있다.** [M0](../../design/m0-spike.md)의 차단 판정 둘이 여기 걸려 있다.

```
제약력 = λ · ∇C / dt²        (λ 자체의 차원은 force·dt²)
```

| 쓰임 | 식 | 없으면 |
|---|---|---|
| **마찰** | `\|λ_접선\| ≤ μ · \|λ_법선\|` | 쿨롱 마찰이 정의상 수직항력에 비례하므로 `λ_법선` 없이는 쓸 수 없다. `μ`로 [안식각](angle-of-repose.md) 34°를 교정하는 실험 자체가 성립하지 않는다 |
| **점착 파단** | `if (λ > breakThreshold(W)) 끊음` | 지금 이 결합에 얼마가 걸려 있는지 몰라 "덩어리로 떨어지는가"를 물리로 판정할 수 없다 |

수분 `W` → 흡입력 → [점착 강도](cohesion.md)를 물리적으로 유도해 꽂겠다는 설계 전체가
`λ`를 전제로 한다.

## λ를 들고 있으면 따라오는 것들

`λ`가 상태가 되는 순간 **프레임을 건너 재사용**할 수 있게 된다 → [워밍 스타트](warm-starting.md).
반대로 유효 질량을 건드리면 `λ` 값 자체가 달라진다 → [질량 스케일링](mass-scaling.md)이
교정 실험을 오염시키는 경로가 이것이다.

---

**관련 문서** — [PBD (Position Based Dynamics)](pbd.md) · [PBD의 강성 문제와 compliance](pbd-stiffness-compliance.md) · [XPBD](xpbd.md) · [★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](stiffness.md) · [안식각 (angle of repose)](angle-of-repose.md) · [워밍 스타트 (warm starting)](warm-starting.md) · [점착력 (cohesion, c)](cohesion.md) · [제약 (constraint)](constraint.md) · [질량 스케일링 (mass scaling)](mass-scaling.md) · [컴플라이언스 (compliance)](compliance.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md)
