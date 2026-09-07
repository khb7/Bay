# PBD (Position Based Dynamics)

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)
> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)
>
> 물리 관점(glossary)과 구현 관점(glossary-impl)을 합친 문서.

힘을 계산하지 않고 **위치를 직접 옮겨** [제약](constraint.md)을 만족시킨다.
```csharp
v += gravity * dt;
p = x + v * dt;
for (int i = 0; i < iterations; i++)
    foreach (var c in constraints) c.Project(ref p);
v = (p - x) / dt;   // 속도는 위치 변화에서 역산
x = p;
```
큰 수를 곱하는 곳이 없어 **절대 폭발하지 않음**. 대가는 물리적 정확성 —
[강성](stiffness.md)이 반복 횟수와 dt에 의존.

---

## 구현 관점 (UE5 / Niagara)

힘을 계산하지 않고 **위치를 직접 밀어** 제약을 만족시키는 방식. 속도는 위치 변화에서 역산.
```csharp
v += gravity * dt;
p = x + v * dt;                                   // 1. 제약 무시하고 예측
for (int i = 0; i < iterations; i++)
    foreach (var c in constraints) c.Project(ref p);   // 2. 위치 보정
v = (p - x) / dt;                                 // 3. 속도 역산
x = p;
```
**절대 폭발하지 않는다.** 대가는 강성이 `iterations`와 `dt`에 의존한다는 것.

---

**관련 문서** — [FEM 메시 (유한요소 메시)](fem.md) · [MPM 계보](mpm-lineage.md) · [PBD의 강성 문제와 compliance](pbd-stiffness-compliance.md) · [XPBD](xpbd.md) · [shape matching](shape-matching.md) · [★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](stiffness.md) · [⚠ 점착력 vs 접착력 — 용어 정리](cohesion-vs-adhesion.md) · [극분해 (polar decomposition)](polar-decomposition.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [시간 적분 (time integration) — 적분기의 종류](time-integration.md) · [제약 (constraint)](constraint.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md) · [하이트필드 (heightfield) ★](heightfield.md)
