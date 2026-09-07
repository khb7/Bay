# 공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

자료구조를 만드는 게 아니라 **입자 배열을 셀 순서로 정렬**한다. 같은 셀 입자가 저절로 연속으로 모인다.

```hlsl
// ① 셀 키 — 해시로 접어서 테이블 크기를 입자 수에 비례하게
int3 c   = (int3)floor(pos / cellSize);
uint key = (c.x*73856093 ^ c.y*19349663 ^ c.z*83492791) & (TABLE_SIZE-1);
//                                    TABLE_SIZE = 2 × 입자 수 (2의 거듭제곱)

// ② (key, index) 쌍을 radix sort

// ③ 셀 범위 테이블 — 키가 바뀌는 지점을 찾는다. 한 패스, 완전 병렬
if (i == 0   || key != sortedKeys[i-1]) cellStart[key] = i;
if (i == N-1 || key != sortedKeys[i+1]) cellEnd[key]   = i + 1;

// ④ 이웃 순회 — 27칸의 [start, end) 구간
for (uint j = cellStart[key]; j < cellEnd[key]; j++) { ... }
```

해시 **충돌이 나도 정확성은 안 깨진다** — 이웃 후보가 몇 개 늘 뿐이고 거리 검사로 걸러진다.

| | [Neighbor Grid3D](neighbor-grid3d.md) | 공간 해시 |
|---|---|---|
| 메모리 | 공간 부피에 비례 (33MB) | **입자 수에 비례** (20만 개 ≈ 5MB) |
| 오버플로 | 조용히 잘림 | 없음 |
| 빈 공간 | 다 할당 | 0 |

**정렬은 프레임당 한 번**이면 된다. 한 프레임에 입자가 셀 하나보다 덜 움직이면
이웃 목록이 여전히 유효하므로, [제약](constraint.md) 반복 10~20회는 그 결과를 재사용한다.

**재배치(reorder)**: 정렬 후 입자 데이터를 정렬 순서대로 실제로 옮기면 이웃 접근이
연속 메모리 읽기가 된다. 복사 한 번 값으로 캐시 적중률이 크게 오른다.

> **타일과의 관계**: 4m 타일이 이 구조의 상위 계층이다(CPU가 sleep·[예산](budget.md) 판단).
> 잠든 타일의 입자는 정렬 입력에서 아예 빠지므로, 위 `N`은 "존재하는 입자"가 아니라
> **"지금 활성인 입자"** 다. 성 10채가 서 있어도 정렬 대상은 지금 만지는 한 채뿐.

---

**관련 문서** — [Neighbor Grid3D (Niagara) — 균일 격자](neighbor-grid3d.md) · [예산 (budget)](budget.md) · [워밍 스타트 (warm starting)](warm-starting.md) · [제약 (constraint)](constraint.md)
