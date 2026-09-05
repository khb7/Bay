# 표면 재구성 (surface reconstruction)

> [← 구현 용어집](../glossary-impl.md) › [5. 표현과 렌더링](../glossary-impl.md#5-표현과-렌더링)

입자 덩어리를 **연속된 표면**으로 그리는 것. 입자를 그대로 그리면 자갈 더미로 보인다.
- **화면공간 방식**: 깊이 렌더 → 블러 → 노멀 복원. 싸고 동적
- **메시화 방식**: [SDF](sdf.md)로 모아 [Marching Cubes](marching-cubes.md) / Surface Nets. 정적 캐시에 적합

---

**관련 문서** — [Marching Cubes / Surface Nets](marching-cubes.md) · [SDF (Signed Distance Field)](sdf.md)
