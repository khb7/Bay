# 수력 침식 (hydraulic erosion)

> [← 구현 용어집](../glossary-impl.md) › [3. 격자 시뮬레이션](../glossary-impl.md#3-격자-시뮬레이션)

물의 운반 능력 `C`와 현재 실린 모래 `s`를 비교해 깎거나 쌓는 것.
```
C = Kc · max(sinα, minSlope) · |v|
C > s  → 깎아서 태움 (b↓ s↑)
C < s  → 내려놓음   (b↑ s↓)
```
