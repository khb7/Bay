# 이웃 탐색 (neighbor search)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

모든 입자 쌍을 검사하면 O(N²) — 20만 개면 400억 쌍이라 불가능하다.
공간을 셀로 쪼개고 각 입자를 자기 셀에 등록해두면, **자기 셀 + 인접 26칸**만 보면 된다.
셀 크기를 상호작용 반경과 같게 잡으면 그보다 먼 입자는 애초에 관심 밖이므로 O(N)이 된다.

---

**관련 문서** — [FEM 메시 (유한요소 메시)](fem.md) · [MPM (Material Point Method)](mpm.md) · [Neighbor Grid3D (Niagara) — 균일 격자](neighbor-grid3d.md) · [라그랑주 vs 오일러 (MPM의 전제)](lagrangian-vs-eulerian.md)
