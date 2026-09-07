# ★ 강성 (stiffness) — 강도·경도·인성과 구분할 것

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

```
F = −k·x        k = 강성. 1만큼 변형시키는 데 필요한 힘
```
Unity Spring Joint의 `spring`, 천 시뮬의 `stiffness` 슬라이더.

| 용어 | 영어 | 묻는 것 |
|---|---|---|
| **강성** | stiffness | 얼마나 힘을 줘야 **변형되나** (딱딱함) |
| **강도** | strength | 얼마나 버티다 **부러지나** (항복·파괴) |
| **경도** | hardness | 표면이 **긁히나** (Mohs) |
| **인성** | toughness | 부서지기까지 **에너지를 얼마나 먹나** |

**서로 독립적이다. 딱딱한 것이 튼튼한 게 아니다.**
```
유리 → 강성 높음(안 휨) + 강도 낮음(툭 깨짐)
고무 → 강성 낮음(잘 휨) + 강도 높음(안 끊김)
```
> ⚠️ **이 문서에서 모래성 얘기할 때 쓴 "강도"는 전부 strength다.**
> [항복면](yield-surface.md)·[점착력](cohesion.md)·[안식각](angle-of-repose.md)은 "언제 무너지나"이지 "얼마나 딱딱한가"가 아니다.

**재료 강성의 정식 이름들**

| 기호 | 이름 | 무엇에 대한 저항 |
|---|---|---|
| `E` | 영률 | 잡아당기거나 누르는 것 |
| `G` (=`μ`) | 전단탄성계수 | 옆으로 미는 것 |
| `K` | 체적탄성계수 | 부피를 줄이는 것 |
| **`μ`, `λ`** | **라메 상수** | 위 셋을 [등방성](isotropy.md) 탄성체용으로 묶은 것 |

[MPM](mpm.md) 논문에 나오는 건 라메 상수. Klár 2016의 [변형](strain.md) 에너지:
```
ψ = μ‖ε‖² + (λ/2)·tr(ε)²
    ↑          ↑
  모양 변형    부피 변형     ← 또 편차/체적 분해
```

**숫자 감각 (영률 E)**
```
강철 200 GPa │ 유리 70 │ 콘크리트 30 │ 뼈 15 │ 나무 10 │ 나일론 3
고무 0.001~0.01 │ 모래(다발) 0.01~0.1 ← 의외로 물렁 │ 젤리 ~kPa
```
**모래가 물렁한 이유**: 알갱이(석영)는 딱딱한데 변형되는 건 **알갱이 배열**이다.
서로 미끄러지고 재배치되는 것이라 훨씬 무르다.

---

**관련 문서** — [MPM (Material Point Method)](mpm.md) · [PBD (Position Based Dynamics)](pbd.md) · [XPBD](xpbd.md) · [stiff (수치적으로 뻣뻣하다)](stiff.md) · [등방성 (isotropy) ★](isotropy.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [변형 (strain, ε)](strain.md) · [안식각 (angle of repose)](angle-of-repose.md) · [우리 프로젝트에서 강성은 거의 안 중요하다](stiffness-in-our-project.md) · [점착력 (cohesion, c)](cohesion.md) · [컴플라이언스 (compliance)](compliance.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md) · [항복면 (yield surface) ★](yield-surface.md)
