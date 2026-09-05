# 우리 프로젝트에서 강성은 거의 안 중요하다

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

모래는 **탄성 구간이 극히 좁다.** 조금만 힘을 줘도 즉시 항복해 소성으로 넘어간다.

| | |
|---|---|
| **중요한 것** | **[항복면](yield-surface.md)(강도)** = talusAngle, [cohesion](cohesion.md), [마찰각](friction-angle.md) |
| 안 중요한 것 | [강성](stiffness.md) = μ, λ, [compliance](compliance.md) |

Klár 2016도 μ, λ를 꽤 임의로 잡는다 — 어차피 바로 항복하니 그 전 구간은 결과에 별 영향이 없다.

| 계층 | 강성이 필요한가 |
|---|---|
| [A] [하이트필드](heightfield.md) | **아예 없음.** talusAngle과 침식률이 전부 |
| [B] [XPBD](xpbd.md) 입자 | compliance 하나. 거의 0(강체에 가깝게)으로 두고 **항복만 튜닝** |

---

**관련 문서** — [XPBD](xpbd.md) · [★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](stiffness.md) · [마찰각 (friction angle)](friction-angle.md) · [점착력 (cohesion, c)](cohesion.md) · [컴플라이언스 (compliance)](compliance.md) · [하이트필드 (heightfield) ★](heightfield.md) · [항복면 (yield surface) ★](yield-surface.md)
