# XPBD

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)
> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)
>
> 물리 관점(glossary)과 구현 관점(glossary-impl)을 합친 문서.

[PBD](pbd.md)의 개량. [강성](stiffness.md)을 **물리 단위로** 지정 가능. 반복 횟수에 안 휘둘림.
**실제 구현은 이걸 쓸 것.** (Macklin et al. 2016)

---

## 구현 관점 (UE5 / Niagara)

[컴플라이언스](compliance.md) `α`(강성의 역수)를 도입하고 라그랑주 승수를 `dt²`로 정규화해,
**강성을 물리 단위로 지정**하고 반복 횟수·프레임레이트와 무관하게 만든 버전.
수분 → 흡입력 → [점착력](cohesion.md)을 물리적으로 유도해 꽂으려면 이쪽이어야 한다.

---

**관련 문서** — [FEM 메시 (유한요소 메시)](fem.md) · [PBD (Position Based Dynamics)](pbd.md) · [PBD의 강성 문제와 compliance](pbd-stiffness-compliance.md) · [Simulation Stages (Niagara)](simulation-stages.md) · [★ MPM의 입자 ≠ 모래알](mpm-particle-vs-grain.md) · [★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](stiffness.md) · [구조 이방성 / 조직 (fabric anisotropy) ★](anisotropy-fabric.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [리턴 매핑 (return mapping)](return-mapping.md) · [순차 파단 (progressive failure)](progressive-failure.md) · [시간 적분 (time integration) — 적분기의 종류](time-integration.md) · [우리 프로젝트에서 강성은 거의 안 중요하다](stiffness-in-our-project.md) · [점착력 (cohesion, c)](cohesion.md) · [컴플라이언스 (compliance)](compliance.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md) · [크리프 (creep)](creep.md)
