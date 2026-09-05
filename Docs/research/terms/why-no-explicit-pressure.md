# ⚠ 압력을 명시적으로 모델링하지 않는 이유 (설계 결정)

> [← 용어집](../glossary.md) › [5. 해변](../glossary.md#5-해변)

정수압 관계에서 `p = ρg·(wt − z)` 이므로 **압력과 높이 차이는 같은 정보다.**
우리 코드는 이미 압력 부호를 높이 비교로 대체하고 있다.
```csharp
if (cellHeight < wt)               W = 1.0f;         // p > 0  포화, 흐름
else if (cellHeight < wt + fringe) W = max(W, ...);  // p < 0  모세관이 붙잡음
```
압력 필드를 따로 만들면 같은 걸 두 번 계산하는 셈.

**압력이 필요해지는 계산들과 우리의 우회**

| 압력이 필요한 것 | 우리는 어떻게 |
|---|---|
| 흡입력 → [점착력](cohesion.md) 유도 (van Genuchten) | **W → cohesion 커브로 직접 매핑** |
| 동수경사 → [침투력](seepage-force.md)(유효 중량 ②) | **[exit point](exit-point.md) 위/아래 이분법 + 계수** |
| 액상화 판정 | 안 함 |
| 다상 결합의 압력 항 (Tampubolon) | **[MPM](mpm.md) 자체를 안 씀** |

의도한 우회다. **물리적으로 유도 가능한 것도 커브로 대체한다**는 이 프로젝트의 일관된 결정.
→ 나중에 침식이 이상해도 **압력 필드 추가는 답이 아니다.** 커브를 먼저 의심할 것.

---

**관련 문서** — [MPM (Material Point Method)](mpm.md) · [exit point](exit-point.md) · [점착력 (cohesion, c)](cohesion.md) · [침투력 / 유효 중량 (seepage force / effective weight)](seepage-force.md)
