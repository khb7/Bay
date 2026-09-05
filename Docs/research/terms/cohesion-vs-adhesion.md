# ⚠ 점착력 vs 접착력 — 용어 정리

> [← 용어집](../glossary.md) › [2. 소성과 항복](../glossary.md#2-소성과-항복)

```
점착력 (cohesion)  =  같은 것끼리 붙는 힘
접착력 (adhesion)  =  다른 것끼리 붙는 힘
```
**물컵 하나로 둘 다 보인다**
```
┃~~~╲___╱~~~┃   물이 벽을 타고 올라감 = 접착력 (물↔유리)
┃    물     ┃   물끼리 서로 당김     = 점착력 (물↔물)
┗━━━━━━━━━━━┛
```
접착력이 이기면 오목한 메니스커스(모세관 상승), [점착력](cohesion.md)이 이기면 수은처럼 볼록.
**모세관 현상은 둘의 경쟁 결과** — [모세관 다리](capillary-bridge.md)와
[모세관 상승대](capillary-fringe.md)가 전부 여기서 나온다.

**★ 진성 점착력 vs 겉보기 점착력**

| | 진성 (true) | 겉보기 (apparent) |
|---|---|---|
| 원인 | 전기화학적·시멘트질 결합 | **모세관 흡입력** |
| 재료 | **점토** | **모래** |
| 마르면 | 남음 (오히려 단단해짐) | **사라짐** |
| 물에 잠기면 | 남음 | **사라짐** |

**모래성은 겉보기 점착력만으로 서 있다** → 마르면 무너지고, 잠겨도 무너진다.
**양쪽 끝에서 다 무너지는 것**이 [강도 곡선](cohesion.md)의 봉우리 모양의 정체.
(마른 진흙이 벽돌처럼 단단하고 물속에서도 뭉쳐 있는 것과 대비)

**문서 안의 번역어 혼란**

| 영어 | 분야 | 한국어 | 이 문서에서 |
|---|---|---|---|
| **cohesion** | 토질역학 (Mohr-Coulomb의 `c`) | **점착력** | A절, B절, [항복면](yield-surface.md) |
| **cohesion** | 물리/유체 | 응집력 | C절 (Macklin [PBD](pbd.md) 파라미터) |
| **adhesion** | 전 분야 | **접착력/부착력** | C절 |

같은 영어인데 분야 관행이 달라 번역어가 다르다.
papers.md A1의 "[Drucker-Prager](drucker-prager.md) + 점착력"은 **항복면 꼭짓점을 미는 `c` 항**을 뜻한다.

> **우리 프로젝트에서 접착력은 명시적으로 안 다룬다.** 물이 모래알 표면에 붙어야
> 메니스커스가 생기고 → 흡입력 → 겉보기 점착력, 이 사슬 전체를
> `W → cohesion` 커브 하나로 대체했으므로 중간 단계가 안 나타난다.
> (물이 표면을 타고 번지는 표현을 넣게 되면 그때 필요해질 수 있음)

---

**관련 문서** — [Drucker-Prager](drucker-prager.md) · [PBD (Position Based Dynamics)](pbd.md) · [모세관 다리 (capillary bridge)](capillary-bridge.md) · [모세관 상승대 (capillary fringe)](capillary-fringe.md) · [점착력 (cohesion, c)](cohesion.md) · [항복면 (yield surface) ★](yield-surface.md)
