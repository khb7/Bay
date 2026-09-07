# 제약 (constraint)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

입자들이 지켜야 할 조건. `C(p) = 0` 형태.
| 제약 | 표현하는 것 |
|---|---|
| 접촉(contact) | 겹치지 않음 |
| 마찰(friction) | 접선 이동을 `μ·|Δn|`로 클램프 → **[안식각](angle-of-repose.md)이 결과로 나옴** |
| 점착([cohesion](cohesion.md)) | 이웃을 서로 당김. **끊어지는 게 곧 덩어리 붕괴** |

---

**관련 문서** — [FEM 메시 (유한요소 메시)](fem.md) · [Gauss-Seidel / Jacobi / SOR ★](gauss-seidel-jacobi-sor.md) · [PBD (Position Based Dynamics)](pbd.md) · [Simulation Stages (Niagara)](simulation-stages.md) · [shape matching](shape-matching.md) · [★ MPM의 입자 ≠ 모래알](mpm-particle-vs-grain.md) · [★ 그래프 컬러링 — 현대적 답](graph-coloring.md) · [공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택](spatial-hash.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [반복 (iteration) / 완화 (relaxation)](iteration-relaxation.md) · [시간 적분 (time integration) — 적분기의 종류](time-integration.md) · [안식각 (angle of repose)](angle-of-repose.md) · [점착력 (cohesion, c)](cohesion.md)
