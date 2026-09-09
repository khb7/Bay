# 캘리브레이션 (calibration) — 물성이 아니라 실험으로 정하는 값

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

**입자 파라미터를 물성표에서 가져오는 대신, 덩어리 거동을 실험과 맞춰 역산하는 것.**

[m0-spike.md](../../design/m0-spike.md) §2-1 *"솔버 상수 — 미리 박지 않는다"* 가 이 관행이다.
자체 발명이 아니라 **DEM 쪽에 이름과 리뷰 논문과 표준 절차가 다 있는 분야**다
([A-R5](../papers.md) 의 "자체 발명이라 생각한 절차가 이미 문헌 방식이었다"와 같은 일이 한 번 더).

---

## 왜 물성표에서 못 가져오나

| | |
|---|---|
| **모양** | 진짜 모래알은 각졌는데 시뮬은 구다. 구는 구른다 — 모래알이 안 하는 짓이다 |
| **크기** | 실물 입경으로는 못 돌린다. 키운 입자는 같은 `μ` 여도 다르게 거동한다 ([B-S6](../papers.md)) |
| **못 재는 것** | 감쇠계수처럼 애초에 측정 프로토콜이 마땅치 않은 것이 있다 |

앞의 둘이 우리에게 그대로 걸린다. 우리 입자는 **회전 자유도가 없는 반경 2.5cm 구**다 —
실물 모래(0.2~0.5mm)의 **50~100배**이고, 모양은 아예 다른 것으로 대체돼 있다.

## 두 접근

| | 하는 일 | 걸리는 것 |
|---|---|---|
| 직접 측정 (direct measurement) | 실제 입자 물성을 재서 그대로 넣는다 | 위 세 가지 |
| **벌크 캘리브레이션 (bulk calibration)** | 파라미터를 미지수로 두고 **덩어리 거동**(안식각·배출량)을 실험과 맞춰 역산 | 아래 세 결론 |

**Coetzee (2017)** 리뷰가 문헌 전체를 이 두 축으로 정리하고 실질적으로 후자를 지지한다.
권하는 조합은 **호퍼 배출 + 안식각**. 우리 §2-1 은 이미 후자다.

---

## 이 분야가 확립한 세 가지

### ① 실험 하나로는 유일한 답이 안 나온다

가장 중요하다. 논문 제목이 아예 그것이다 —
**Roessler, Richter, Katterfeld, Will (2019), "…part I: Solving the problem of
ambiguous parameter combinations"**.

안식각 34° 를 내는 (미끄럼 `μ`, 구름 `μ`) 조합은 **점이 아니라 곡선**이다.
높은 미끄럼 + 낮은 구름도 34°, 그 반대도 34°. 그런데 **호퍼에 넣으면 다르게 흐른다.**

해법은 미지수를 줄이는 게 아니라 **출력을 늘리는 것**. 드로다운 시험 하나에서
**안식각 · 벽 마찰각 · 배출 질량** 셋을 뽑아 방정식 수를 미지수 수에 맞춘다.

> ⚠ **우리 3-0 은 미지수가 셋이다** — `substep` · 반복 수 · `μ`.
> 안식각 하나로 셋을 확정하면 34° 는 맞는데 벽·터널·붕괴에서 어긋나는 조합을 고른다.
> → 아래 [⒜](#-스윕을-어떻게-설계하나--3-0-에-직접) · [⒞](#-스윕을-어떻게-설계하나--3-0-에-직접)

### ② 캘리브레이션 값은 물성이 아니다

그래서 문헌이 *material property* 가 아니라 **calibrated parameter** 라고 부른다.
그 모델·그 입경·그 접촉식에 딸린 값이라 **솔버나 반경을 바꾸면 다시 해야 한다. 이식이 안 된다.**

> `μ = 0.15` 가 모래 물성(0.5~0.7)과 어긋나 보여도 이상하지 않다.
> 그 0.15 는 모래의 [마찰계수](coulomb-friction.md)가 아니라
> **"이 [XPBD](xpbd.md), 이 반복 수, 이 배치에서 34° 를 내는 숫자"** 다.
> `μ` 를 물성표가 아니라 "솔버 상수" 표에 넣은 것이 문헌 관행 그대로다.

**반경을 1.25cm 로 바꾸는 진단([B-S8](../papers.md) ③)은 캘리브레이션을 다시 해야 한다는 뜻이기도 하다.**
같은 `μ` 를 그대로 들고 가면 그 실험은 해상도가 아니라 미교정을 잰다.

### ③ 마찰을 올려도 어느 지점부터 안 듣는다

블라스트 퍼니스 원료를 잰 [Materials 15(3), 903](https://doi.org/10.3390/ma15030903) 기준으로,
**미끄럼 마찰 0.35 / 구름 마찰 0.4** 를 넘으면 안식각 증가율이 꺾이고
구름 0.4→0.6 에서는 거의 평평하거나 되레 내려간다. 재료마다 값은 다르지만 **포화 자체는 일반적**이다.

→ `μ` 스윕은 **포화대 아래**를 봐야 정보가 나온다.

---

## ★ 회전 없음 = 무한 구름 마찰

[A-R3](../papers.md) 이 *"구름이 불가능하니 이 논의 밖. 문헌으로 답이 안 나온다"* 로 남겨둔 칸에
**부분적인 답이 있다.**

**Wensrich & Katterfeld (2012), "Rolling friction as a technique for modelling
particle shape in DEM"** — Powder Technology.
구름 마찰은 **모양의 대역(proxy)** 이다. 각진 알갱이가 안 구르는 것을 구 + 구름저항으로 흉내낸다.
클럼프(실제 모양)와 구+구름마찰을 안식각 시험에서 맞대보니
**같은 효과를 내는 데 구름 마찰이 형상의 약 절반**이면 됐다.

이 축에서 보면 우리 입자는 **구름 마찰 = ∞** 다. 눈금의 바깥 끝.

```
자유 회전 + 구름저항 0      구는 굴러내린다      안식각 낮음 (구형 DEM 의 통상 한계)
구름저항 유한               모양을 흉내냄        Wensrich 의 영역
회전 자유도 없음  ← 우리    절대 안 구른다       눈금 바깥
```

**→ 낮은 `μ` 에서 높은 각이 나오는 것이 정상이다.** μ 0.15 에서 34° 는
이상현상이 아니라 **이 축의 끝에 서 있다는 신호**로 읽힌다.

> ⚠ 좋기만 한 게 아니다. 눈금 바깥이라는 건 **`μ` 로 조절할 여지가 그만큼 줄었다**는 뜻이다.
> 각도의 대부분이 회전 잠금에서 오면 `μ` 를 흔들어도 각이 잘 안 움직인다 — ③의 포화와 같은 증상이
> 다른 이유로 나타난다. **스윕이 밋밋하면 두 원인을 구분해야 한다.**

> ⚠ [구조 이방성](anisotropy-fabric.md) 문서는 **같은 자유도가 없어서 강도를 과소평가**한다고 적혀 있다.
> 여기서는 그 부재가 각을 **과대**하게 만든다. **방향이 반대이므로 상쇄를 기대하면 안 된다** —
> 무엇을 재느냐에 따라 부호가 바뀌는 계통 편향이다.

---

## 스윕을 어떻게 설계하나 — 3-0 에 직접

### ⒜ 두 번째 실험을 붙인다 — [전단 상자](shear-box.md)

**Roessler & Katterfeld (2018)** 이 업스케일 입자로 여러 시험의 **크기 불변성**을 비교했다.

| 시험 | 결과 |
|---|---|
| 원통 인상 안식각 | **느린 인상 속도에서만** 입경 불변 |
| **[전단 상자](shear-box.md)** | **불변이면서 시험 시간이 더 짧다** |
| — | 고동적 흐름과 준정적 흐름의 캘리브레이션이 **같은 파라미터 셋**으로 수렴 |

첫 줄이 [3-0-D 기각](../../design/m0-spike.md)을 문헌 쪽에서 뒷받침한다 —
인상 속도가 결과를 정한다는 것이고, 우리가 계단식으로도 못 피한 그 문제다.

**둘째 줄이 새 카드다.** [3-0-A 부어 쌓기](../../design/m0-spike.md)(`θr`)가 출력 하나,
전단 상자가 둘. 미지수 셋에 출력 둘이면 ①의 축퇴가 크게 준다.
전단 상자는 [마찰각](friction-angle.md)·[임계상태](critical-state.md)를 직접 주므로
[Drucker-Prager](drucker-prager.md) 쪽 파라미터와도 **같은 자로** 이어진다.
**장치·출력·크기 요건·구현은 [전단 상자](shear-box.md) 에 따로 적었다.**

### ⒝ `μ` 스윕 범위를 아래로 옮긴다

시작값 0.7 은 ③의 포화대 안쪽이라 흔들어도 각이 안 움직인다. 답이 0.15 에서 나온 것과도 맞다.
→ **0.05~0.4 촘촘히, 그 위는 성기게.**

### ⒞ 3-0 의 출력을 "값" 이 아니라 "후보" 로 남긴다

34° 를 만족하는 `(substep, 반복, μ)` 를 **2~3 조합** 남기고 3-1~3-4 가 하나로 좁힌다.
§2-1 의 *"그 결과가 §4의 값을 갱신한다"* 를 **3-4 까지 끝나야 갱신한다** 로 미루는 것.
문헌의 처방("복수 실험")이 정확히 이 형태이고, **우리는 이미 복수 실험을 갖고 있다** — 순서만 바꾸면 된다.

---

## 젖은 모래는 아직 성숙하지 않은 영역이다

**Coetzee & Scheffler (2023), "Review: The Calibration of DEM Parameters for the
Bulk Modelling of Cohesive Materials"** — Processes 11(1), 5 의 결론:

> 무점착 재료의 캘리브레이션은 성숙 단계에 이르렀지만, **점착 재료는 연구가 더 필요하다.**

즉 우리 프로젝트의 본체([점착](cohesion.md) · `cohesion(W)`)는 **표준 절차가 아직 없는 쪽**이다.
[B-S3](../papers.md) 의 *"점착에는 자가 없다"* 와 같은 말을 캘리브레이션 쪽에서 확인해 주는 셈.

가장 가까운 선례가 **Roessler & Katterfeld (2019), "DEM parameter calibration of cohesive
bulk materials using a simple angle of repose test"** — Particuology.
**젖은 모래**를 원통 인상으로 캘리브레이션하며 원통 크기와 인상 속도를 축으로 실험한다.

> 우리는 원통 인상을 기각했지만 사유는 **준정적 보장 실패**였지 재료가 젖었을 때의 절차가 아니었다.
> [B-S8](../papers.md) ①(벽 검증 → `c[Pa]` 역산)을 설계할 때 이 논문이 가장 가까운 참고다.

---

## 문헌

| 문헌 | 무엇 |
|---|---|
| **Coetzee (2017)**, "Review: Calibration of the discrete element method", Powder Technology 310, 104-142 | ★ 이 분야의 리뷰. 직접 측정 vs 벌크 캘리브레이션. [B-S6](../papers.md) 에도 있다 |
| **Roessler, Richter, Katterfeld, Will (2019)**, "…part I: Solving the problem of ambiguous parameter combinations", Powder Technology 343, 803-812 | ★★ ①의 원전. 드로다운 하나에서 출력 셋 |
| **Roessler & Katterfeld (2018)**, "Scaling of the angle of repose test…using upscaled particles", Powder Technology 330, 58-66 | ★★ ⒜의 근거. 전단 상자가 크기 불변 + 빠름 |
| **Wensrich & Katterfeld (2012)**, "Rolling friction as a technique for modelling particle shape in DEM", Powder Technology | ★★ 구름 마찰 = 모양의 대역. 회전 없음의 위치를 준다 |
| **Coetzee & Scheffler (2023)**, "Review: The Calibration of DEM Parameters for the Bulk Modelling of Cohesive Materials", Processes 11(1), 5 | 점착 캘리브레이션은 미성숙 |
| Roessler & Katterfeld (2019), "DEM parameter calibration of cohesive bulk materials using a simple angle of repose test", Particuology | **젖은 모래** 캘리브레이션 절차 |
| Feng & Owen (2014), Computational Particle Mechanics 1, 159 | 업스케일 정확 스케일 법칙 → [B-S6](../papers.md) |

> ⚠ 페이지·권 번호는 검색 결과 기반이다. Coetzee (2017) 본문은 페이월이라 **초록과 인용 논문으로 재구성**했다.

---

**관련 문서** — [XPBD](xpbd.md) · [전단 상자 (direct shear box)](shear-box.md) · [구조 이방성 / 조직 (fabric anisotropy) ★](anisotropy-fabric.md) · [마찰각 (friction angle)](friction-angle.md) · [반복 (iteration) / 완화 (relaxation)](iteration-relaxation.md) · [안식각 (angle of repose)](angle-of-repose.md) · [임계상태 (critical state)](critical-state.md) · [점착력 (cohesion)](cohesion.md) · [질량 스케일링 (mass scaling)](mass-scaling.md) · [쿨롱 마찰 (Coulomb friction)](coulomb-friction.md) · [특성 길이 (characteristic length)](characteristic-length.md)
