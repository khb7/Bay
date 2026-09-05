# Neighbor Grid3D (Niagara) — 균일 격자

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

Niagara가 제공하는 [이웃 탐색](neighbor-search.md) 구조. 3스테이지로 쓴다.
```
Reset      셀 카운트 0
Populate   입자마다: 셀 계산 → 원자적 증가로 슬롯 확보 → 인덱스 기록
Solve      입자마다: 27칸 순회 → 이웃 읽어 제약 처리
```
**한계 두 가지 — 모래에는 둘 다 걸린다.**
1. `MaxNeighborsPerCell`이 **고정**이다. 넘치면 조용히 잘리고, 이웃이 누락된 입자는 뚫고 지나간다.
   모래는 최밀 충전(≈0.64)에 가깝고 다지기까지 하므로 위험하다
2. **균일 격자라 빈 공간도 메모리를 먹는다.** 활성 영역 8×8×2m를 5cm 셀로 덮으면 33MB인데,
   모래성이 실제로 차지하는 건 5% 미만이다

---

**관련 문서** — [공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택](spatial-hash.md) · [이웃 탐색 (neighbor search)](neighbor-search.md)
