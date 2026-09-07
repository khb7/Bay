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

## ⚠ 우리 입자는 material point가 아니다

이 문서는 **MPM 논문을 읽을 때의 주의사항**이지 우리 설계 서술이 아니다.
이 프로젝트는 MPM을 구현하지 않는다 ([papers.md](../papers.md) §A — 근거와 파라미터의 출처로만 사용).

| | 입자 하나의 정체 | 크기가 물리에 들어가나 | 거시 강도는 |
|---|---|---|---|
| DEM | 알갱이 1개 | 들어간다 | 접촉에서 창발 |
| **MPM** | 연속체 표본점 | **안 들어간다** — 순전히 해상도 | [구성 모델](constitutive-model.md)이 Pa로 직접 준다 |
| **우리 ([XPBD](xpbd.md) 업스케일 결합 입자)** | 둘 다 아님 | 들어간다 | 접촉·결합에서 창발 |

우리는 DEM 쪽 기계(접촉 [제약](constraint.md)·[마찰](coulomb-friction.md) 클램프·결합 슬롯)를 쓰되
입자 크기를 물리가 아니라 **예산이** 정한다. Feng & Owen (2014)의 *업스케일 입자*가 이 제3범주다.

> **MPM이었다면 [papers.md](../papers.md) §B-S2가 통째로 불필요하다.** 크기가 물리에 안 들어가니
> [점착](cohesion.md)을 Pa로 꽂으면 끝이다. 우리는 접촉으로 강도를 만들기 때문에
> `σ_t ∝ 1/d` 가 그대로 물리고, 그래서 실제 모세관력의 약 220배를 넣는 상황이 생긴다.

---

**관련 문서** — [SVD (특이값 분해) ★](svd.md) · [XPBD](xpbd.md) · [경화 (hardening)](hardening.md) · [구성 모델 (constitutive model)](constitutive-model.md) · [변형 그래디언트 F](deformation-gradient.md) · [연속체 (continuum)](continuum.md) · [점착력 (cohesion, c)](cohesion.md) · [제약 (constraint)](constraint.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md) · [포화도 (saturation)](saturation.md) · [항복면 (yield surface) ★](yield-surface.md)
