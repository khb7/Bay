# 연속체 (continuum)

> [← 용어집](../glossary.md) › [1. 역학 기초](../glossary.md#1-역학-기초)

알갱이를 하나씩 세지 않고 공간에 퍼진 밀도·속도 **장(field)** 으로 취급하는 관점.
```
알갱이 관점 (DEM)          연속체 관점
 ○ ○ ○ ○ ○               이 지점: 밀도 1500, 속도 (2, -1)
○ ○ ○ ○ ○ ○      →       저 지점: 밀도 1480, 속도 (2.1, -0.9)
 ○ ○ ○ ○ ○               그 사이는 매끄럽게 이어짐
```
> ⚠️ **"한 덩어리로 뭉쳐 움직인다"가 아니다.** 각 지점이 제각기 다른 속도를 가질 수 있고,
> 그 값들이 공간에 매끄럽게 퍼져 있다고 보는 것.
> 물을 H₂O 분자 대신 밀도·속도 격자로 푸는 것과 같은 발상.

- **DEM (개별요소법)**: 알갱이를 하나씩 강체로. 정확하지만 수억 개면 불가능. **연속체가 아님**
- **species (종)**: 같은 공간에 연속체를 여러 개 겹쳐 놓는 것. 서로 항력으로만 대화

---

**관련 문서** — [★ MPM의 입자 ≠ 모래알](mpm-particle-vs-grain.md) · [리턴 매핑 (return mapping)](return-mapping.md) · [소성 = 클램프 ★](plasticity-as-clamp.md) · [열침식 (thermal erosion)](thermal-erosion.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md)
