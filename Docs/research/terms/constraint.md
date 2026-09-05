# 제약 (constraint)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

입자들이 지켜야 할 조건. `C(p) = 0` 형태.
| 제약 | 표현하는 것 |
|---|---|
| 접촉(contact) | 겹치지 않음 |
| 마찰(friction) | 접선 이동을 `μ·|Δn|`로 클램프 → **[안식각](angle-of-repose.md)이 결과로 나옴** |
| 점착([cohesion](cohesion.md)) | 이웃을 서로 당김. **끊어지는 게 곧 덩어리 붕괴** |

---

**관련 문서** — [Gauss-Seidel / Jacobi / SOR ★](gauss-seidel-jacobi-sor.md) · [PBD (Position Based Dynamics)](pbd.md) · [shape matching](shape-matching.md) · [★ 그래프 컬러링 — 현대적 답](graph-coloring.md) · [공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택](spatial-hash.md) · [반복 (iteration) / 완화 (relaxation)](iteration-relaxation.md) · [안식각 (angle of repose)](angle-of-repose.md) · [점착력 (cohesion, c)](cohesion.md)
