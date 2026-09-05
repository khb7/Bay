# 항력 (drag)

> [← 용어집](../glossary.md) › [1. 역학 기초](../glossary.md#1-역학-기초)

속도에 비례해 반대 방향으로 작용하는 저항력. Unity `Rigidbody.drag`와 같은 개념.
`F = −k·v`

논문에서는 **절대속도가 아니라 상대속도**를 쓴다:
```
F ∝ (v_water − v_sand)
```
물이 모래보다 빠르면 물이 느려지고 그만큼 모래가 밀린다. 크기 같고 방향 반대 →
**운동량 보존**. 그래서 **운동량 교환(momentum exchange)** 이라고도 부른다.

물리적 실체는 물이 알갱이 사이 좁은 틈을 지나며 받는 마찰을 거시적으로 뭉갠 것.

---

**관련 문서** — [Darcy의 법칙](darcy-law.md) · [침투력 / 유효 중량 (seepage force / effective weight)](seepage-force.md)
