# ★ MPM의 입자 ≠ 모래알

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

가장 헷갈리는 지점. **"입자를 쓰는데 왜 연속체냐?"**

```
DEM (연속체 아님)           MPM (연속체)
 입자 1개 = 모래알 1개       입자 1개 = 연속체의 한 조각 (material point)
 강체로 취급                 수천~수만 알갱이의 평균을 대표하는 표본점
 접촉을 하나씩 계산           "이 지점의 밀도·응력은 이렇다"
```
입자는 **[연속체](continuum.md) 장(field)을 저장하는 수단**일 뿐이다. 부피 1mm³ 안에 알갱이가
수천 개 있어도 입자 하나로 대표한다.

**입자가 들고 다니는 것**: 위치, 속도, 질량, 부피, **[변형 그래디언트](deformation-gradient.md) `F`**,
소성 이력 `F_p`, [경화](hardening.md) 상태, (2상이면) [포화도](saturation.md).
→ `F`가 핵심. [SVD](svd.md) → 로그 → [항복면](yield-surface.md) 클램프가 전부 이 `F`에 대해 일어난다.

---

**관련 문서** — [SVD (특이값 분해) ★](svd.md) · [경화 (hardening)](hardening.md) · [변형 그래디언트 F](deformation-gradient.md) · [연속체 (continuum)](continuum.md) · [포화도 (saturation)](saturation.md) · [항복면 (yield surface) ★](yield-surface.md)
