# 논문 조사

파도치는 모래사장 + 모래성 시뮬레이션 관련 문헌 정리.
용어는 [glossary.md](glossary.md), 구현 용어는 [glossary-impl.md](glossary-impl.md) 참조.
용어 하나당 개별 문서가 [terms/](terms/) 아래에 있다.

> ⚠️ 서지사항은 상당수 기억 기반이다. 실제로 찾아볼 때 연도·학회가 어긋날 수 있으니
> 저자명 + 제목 키워드로 검색하는 것을 권한다. 특히 **F절(해변 [지하수면](terms/water-table.md))** 은 검증이 덜 됐다.

---

## 읽는 순서

```
A2 Klár 2016        모래의 물리 (기초)
 ↓
A1 Tampubolon 2017  + 물 = 젖은 모래
 ↓
B1 Pakpour 2012     실제 젖은 모래의 강도 곡선 (파라미터 근거)
 ↓
C1 Macklin 2014     실시간 근사 (PBD)
 ↓
D1 Mei 2007         하이트필드 GPU 침식  ← 가장 먼저 구현할 것
 ↓
E1 Or 2013          증발/건조 (논문 공백 메우기)
 ↓
F1 Horn 2002        해변 지하수면
```

---

## A. 모래의 물리 — MPM 연속체 계열

> 정확하지만 **오프라인**. 직접 구현하지 않고 **근거와 파라미터의 출처**로 사용한다.

### A2. Klár, Gast, Pradhana, Fu, Schroeder, Jiang, Teran (2016)
**"[Drucker-Prager](terms/drucker-prager.md) Elastoplasticity for Sand Animation"** — SIGGRAPH 2016, UCLA

모래를 [연속체](terms/continuum.md)로 보고 MPM으로 푼다. 핵심은 **소성 = 클램프**라는 구조.

```
탄성 변형을 계속 쌓다가, 허용 영역(항복면)을 벗어나면
경계로 끌어당기고(리턴 매핑) 잘라낸 만큼을 영구 변형으로 기록한다.
```

- 허용 영역이 **원뿔**(Drucker-Prager). 누르는 힘이 셀수록 더 잘 버팀 = 마찰 재료
- [SVD](terms/svd.md)로 회전을 버리고 스케일 3개만 남긴 뒤 로그([Hencky 변형](terms/hencky-strain.md))를 취하면
  원뿔 투영이 **닫힌 형태**로 풀림 → 반복 없이 `if/else` 3분기. GPU 친화적
  - 원뿔 안 → 탄성, 그대로
  - 꼭짓점 바깥(팽창) → 탄성 변형 전부 0으로. 알갱이가 서로 놓아버림
  - 옆면 바깥 → 수직 투영. 초과분만 소성으로
- **[경화](terms/hardening.md)(hardening)**: 부푼 정도를 누적해 [마찰각](terms/friction-angle.md)을 조절. 한 번 흩어진 모래는 다시 다져질 때까지 약함
- 검증: **[안식각](terms/angle-of-repose.md)(34°)이 파라미터가 아니라 결과로 나옴**

**한계 → A1로 이어짐**: 원뿔 꼭짓점이 원점에 붙어 있어 **인장을 0도 못 버팀**.
수직 벽·오버행 불가 = **모래성을 못 쌓는다**.

**가져올 것**: 안식각 34°가 마찰각 파라미터의 튜닝 기준점.

---

### A1. Tampubolon, Gast, Klár, Fu, Teran, Jiang, Museth (2017)
**"Multi-species Simulation of Porous Sand and Water Mixtures"** — SIGGRAPH 2017 (TOG 36-4), UCLA + DreamWorks

**이 주제의 정답지.** 모래와 물을 각각 연속체로 두고 같은 공간에 겹쳐 시뮬레이션한다.

| | 모래 상 | 물 상 |
|---|---|---|
| [구성 모델](terms/constitutive-model.md) | Drucker-Prager (A2) + **[점착력](terms/cohesion.md)** | [약압축성](terms/weakly-compressible.md) 유체 (상태방정식) |
| 격자 속도장 | v_s | v_w |
| 추가 상태 | [공극률 φ](terms/porosity.md), [포화도](terms/saturation.md) | [자유표면](terms/free-surface.md) 여부 |

**핵심 4가지**

1. **[Darcy](terms/darcy-law.md) 항력 결합** — 두 상은 충돌하지 않고 관통한다. 대신 상대속도에 비례하는 [항력](terms/drag.md):
   `f ∝ (1/k(φ)) · (v_w − v_s)`. [투과율 k](terms/permeability.md)가 낮으면 항력이 크다 = 물이 잘 못 지나간다.
   이 항이 매우 stiff해서 **두 속도장을 묶은 [대칭 선형계](terms/symmetric-linear-system.md)를 암시적으로 한 번에 푼다** (수치해석적 기여)
2. **포화도 → 점착력** — A2의 원뿔 꼭짓점을 인장 쪽으로 밀어낸다.
   `‖ε_dev‖ + α·tr(ε) ≤ c(saturation)`. c > 0이면 당김을 버팀 = **수직 벽 = 모래성**
3. **자유수 / 공극수 구분** — 모래 안이면 포화도에 기여, 밖이면 그냥 물.
   파도가 적시고 빠질 때 일부는 남고 일부는 배어나옴(exfiltration)
4. **공극률 추적** — 다져지면 물이 덜 스미고, 부풀면 잘 먹음

**결과 영상**: 모래성이 물에 녹아내림, 모래 더미에 물 붓기, 물속으로 모래 쏟기, 이류

**한계**
- 오프라인 전용 (프레임당 수십 초~분)
- 점착력 모델이 **[현상론적](terms/phenomenological.md)**. [모세관 다리](terms/capillary-bridge.md)를 물리적으로 모사하지 않음
  → 실제 젖은 모래의 비단조 강도 곡선(1% 최적)은 자동으로 안 나옴. **B1의 곡선을 직접 넣어야 함**
- **증발이 없다.** 물은 흘러서만 사라진다

**가져올 것**
- saturation을 1급 상태량으로 두는 설계
- 확산은 Darcy로: 이웃 간 이동량 ∝ 투과율 × 포화도 차
- saturation → (강도, 알베도, 러프니스, 붕괴 임계값) 커브 4개

---

### A. 보충 (필요시)

| 논문 | 내용 |
|---|---|
| Zhu & Bridson (2005), "Animating Sand as a Fluid", SIGGRAPH | 출발점. FLIP + 마찰 |
| Narain, Golas, Lin (2010), "Free-Flowing Granular Materials with Two-Way Solid Coupling", SIGGRAPH Asia | 단방향 비압축성 |
| Daviet & Bertails-Descoubes (2016), "A Semi-Implicit [MPM](terms/mpm.md) for the Continuum Simulation of Granular Materials", SIGGRAPH | 마찰을 정확히 푸는 쪽 |
| Yue et al. (2018), "Hybrid Grains", SIGGRAPH Asia | 연속체↔개별 입자 자동 전환. 덩어리가 낱알로 부서지는 표현 |
| **Gao et al. (2018), "Animating Fluid Sediment Mixture in Particle-Laden Flows", SIGGRAPH** | A1의 후속. 빽빽한 바닥~묽은 부유의 농도 전 범위. **운반 공식 없이 연행·퇴적이 물리에서 나옴** → **빌릴 것 3가지는 [D1-b](#d1-b--gao-2018에서-빌린-개선-3가지)** |
| **Fei, Guo, Wu, Huang, Gao (2021), "Revisiting Integration in the Material Point Method: A Scheme for Easier Separation and Less Dissipation", SIGGRAPH** | **MPM의 두 고질병을 고치는 논문** (아래 상세) |

#### Fei 2021 — MPM은 왜 안 떨어지는가 (우리가 PBD를 고른 근거)

**두 고질병**
```
① 인공 접착   떨어져야 할 재료가 엿가락처럼 늘어짐
② 수치 소산   에너지가 새어 모든 게 댐핑 걸린 듯 굼뜸
```

**원인이 같다 — 격자 커널이 재료보다 훨씬 넓다**
```
┌───┬───┬───┐
│   │   │   │   입자 하나가 주변 3×3 격자점에 기여
├───┼───┼───┤   (2차 B-스플라인 3칸, 3차 4칸)
│   │ ● │   │
├───┼───┼───┤   → 두 덩어리가 격자 두어 칸 안으로 오면
│   │   │   │      같은 격자점을 공유 → 운동량이 섞임 → 붙어버림
└───┴───┴───┘
```
사이에 빈 공간이 있는데 시뮬레이션은 붙어 있다고 본다.
그리고 P2G/G2P를 왕복할 때마다 커널로 뭉개지며 에너지가 샌다(②).

**논문의 각도 — 적분(구적) 문제로 본다**
표준 MPM은 **입자를 [응력](terms/stress.md) 적분의 표본점으로** 쓴다. `∫(응력)dV ≈ Σ(입자 응력 × 부피)`.
이게 나쁜 구적 규칙이다 — 입자가 뭉치거나 성겨져 표본 분포가 나빠지고,
실효 지지 영역이 커널 폭이라 **실제 재료 경계와 안 맞는다**(특히 자유표면).
적분 방식을 다시 설계해 재료가 실제 있는 곳을 반영하면 두 병이 같이 완화된다는 논지.
> ⚠ 구체적 기법은 원문 확인 필요. 문제 정의는 MPM에서 잘 알려진 것.

**왜 모래에서 치명적인가** — **모래의 정체성이 "분리"이기 때문.**
손에서 흘러내리며 흩어지는 것, 벽이 무너지며 덩어리가 툭 떨어지는 것,
파도에 튀어오르는 것 — 전부 떨어져 나가는 장면이다.
인공 접착이 있으면 모래가 아니라 **젖은 점토**처럼 보인다.

**★ 우리에게 의미 — 뜻밖에 우리 선택을 뒷받침한다**

| 계층 | 인공 접착 |
|---|---|
| MPM | **있음.** 격자를 공유하므로. Fei 2021이 고치려는 것 |
| **[PBD](terms/pbd.md) 입자 (우리 [B])** | **없음. 격자를 공유하지 않는다** |
| [하이트필드](terms/heightfield.md) (우리 [A]) | 애초에 분리가 **불가능** (오버행 없음) |

```
MPM:  입자 → 격자 → 입자    격자가 중개 → 안 닿아도 섞임
PBD:  입자 ↔ 입자           직접 접촉 → 안 닿으면 무관
```
**"모래 덩어리가 툭 떨어져 나가는 것"이 우리 핵심 비주얼인데,
PBD를 고른 것만으로 이미 해결돼 있다.**

> 단, ②(수치 소산)는 PBD도 자유롭지 않다. 위치를 강제로 옮기고 속도를 역산하므로
> 에너지가 샌다. 모래가 튀어야 할 때 축 처지면 → [XPBD](terms/xpbd.md)로 가고, 반복 횟수를 필요한
> 만큼만 쓰고, 필요하면 속도 보정 항을 명시적으로 넣는다.
| Lenaerts, Adams, Dutré (2008), "Porous Flow in Particle-Based Fluid Simulations", SIGGRAPH | MPM 이전 세대. SPH 기반 다공성 흡수 |
| Lenaerts & Dutré (2009), "Mixing Fluids and Granular Materials", Eurographics | 위의 확장 |
| Rungjiratananon et al. (2008), "Real-time Animation of Sand-Water Interaction", Pacific Graphics | DEM(모래) + SPH(물). 실시간 지향 |

---

## A-R. 안식각을 **어떻게 재는가** — 측정 프로토콜

M0에서 부딪혀 뒤늦게 찾은 절. **안식각은 값보다 측정법이 먼저다.**

### A-R1. Beakawi Al-Hashemi & Baghabra Al-Amoudi (2018)
**"A review on the angle of repose of granular materials"** — Powder Technology 330, 397-417

> *"Depending upon the method of measurement, the angle of repose value varies appreciably."*

| 방식 | 하는 일 |
|---|---|
| **tilting box** | 용기를 서서히 기울여 **사태가 시작되는 각**을 잰다 |
| fixed funnel | 깔때기로 부어 쌓은 더미의 옆면 각 |
| hollow cylinder | 원통을 들어올려 무너뜨린 뒤 옆면 각 |
| revolving cylinder | 회전 드럼에서 흐르는 표면의 각 |

정적 안식각(막 미끄러지려는 상태)과 동적 안식각(흐르는 중)도 구분한다.
**값을 적을 땐 측정법을 함께 적어야 하는 종류의 물리량이다.**

### A-R2. 기둥 붕괴는 안식각 측정법이 아니다

Lube 2004 / Lajeunesse 2004 계열은 **런아웃 거리와 최종 높이를 종횡비의 함수로** 특성화하는 실험이다.
후속 연구는 *"the angle at the base of the deposit"* 가 종횡비뿐 아니라
**기둥 반지름 / 입자 지름 비**에도 의존한다고 보고한다.

→ **퇴적물 경사 ≠ 재료의 안식각.** M0가 결정성 때문에 기둥 붕괴를 골랐는데(그 근거는 유효하다),
그걸로 μ를 교정하려던 것이 무리였다. ([m0-log.md](../design/m0-log.md) 실행 #6)

### A-R3. Zhou, Xu, Yu, Zulli (2002)
**"An experimental and numerical study of the angle of repose of coarse spheres"** — Powder Technology

DEM에서 안식각은 미끄럼 마찰·**구름 마찰**·형상·크기·밀도·반발계수에 함께 의존한다.
미끄럼 마찰이 지배적이고 구름 마찰이 그다음. **구형 입자는 미끄럼 마찰만으로는 한계가 있다.**

> 우리 입자는 **회전 자유도가 아예 없다.** 구름이 불가능하니 이 논의의 밖에 있고,
> 그게 유리한지 불리한지는 문헌으로 답이 안 나온다 — 실측이 필요한 지점.

### A-R4. ★★ 경사판이 재는 것은 안식각이 아니다

**Jaeger, Liu, Nagel (1989), "Relaxation at the Angle of Repose"** — PRL 62, 40

```
θm  최대안정각 (maximum angle of stability)   정지한 더미가 무너지기 시작하는 각
θr  안식각 (angle of repose)                  사태가 끝난 뒤 남은 표면의 각
                                              θm > θr,  통상 2~5° 의 이력(hysteresis)
```

**경사판은 정의상 `θm`을 잰다** — 정지 상태에서 기울여 개시를 보므로.
fixed funnel · hollow cylinder 는 쌓인 뒤의 표면이라 `θr`이다.

> ⚠ [A-R1](#a-r1-beakawi-al-hashemi--baghabra-al-amoudi-2018)의 **정적/동적 구분과 겹치지만 같지 않다.**
> 정적/동적은 "흐르는 중이냐"이고, `θm`/`θr`은 **같은 이력 곡선의 양 끝**이다.

**→ `params.md`의 `talusDry` 34°가 어느 쪽인지 확정해야 한다.**
`θr`이라면 경사판 측정은 **34°보다 높게** 나와야 정상이고,
그러면 관측된 음의 절편은 실제로 더 큰 오차라는 뜻이 된다.

**젖은 쪽으로 갈 때** — Albert et al. (1997), *"Maximum angle of stability in wet and dry
spherical granular media"*, PRE 56, R6271. 구형 입자의 `θm`을 **응집력의 함수로 계산**하고
젖은 입상체에 적용해 액체 함량 의존성을 도출, 실험과 맞춘다.
§B의 Hornbaker 1997(Nature)과 같은 그룹.
**경사판이 재는 바로 그 양을 `W`의 함수로 준다** — Pakpour 2012이 *기둥 높이* 축의 검증이라면
이건 *각도* 축의 검증이고, 우리는 이미 그 각도를 재는 계기를 갖고 있다.

### A-R5. ★★ 경사판에는 표준 프로토콜이 있다 — 두께 의존성

**Pouliquen (1999), "Scaling laws in granular flows down rough inclined planes"**
— Physics of Fluids 11(3), 542

```
θ_start(h)   두께 h 인 정지층이 흐르기 시작하는 각
θ_stop(h)    흐르던 층이 멈추는 각
             두 곡선은 모양이 같고 약 1° 떨어져 있다
```

**핵심은 둘 다 층 두께 `h`의 함수라는 것.** 서로 다른 표면 거칠기의 비드계도
`h/d`로 스케일하면 한 곡선으로 무너진다(`h_stop(θ)` 스케일링).

`θ_stop`은 `h`가 커질수록 낮아진다 → **더미 두께가 계통 오차의 후보 1번.**
두께를 축으로 스윕하면 절편이 상수인지 두께 함수인지 한 번에 갈린다.

> 부수 소득: 우리의 "각도마다 붙잡고 정지 여부를 묻는" 방식은 사실상 **`θ_start` 측정**이다.
> 자체 발명이라 생각한 절차가 이미 문헌 방식과 같은 것이었다.

**바닥 거칠기** — Goujon, Thomas, Dalloz-Dubrujeaud (2003),
*"Monodisperse dry granular flows on inclined planes: role of roughness"*, EPJ E 11, 147.
바닥 거칠기가 각에 **직접** 영향을 준다. Pouliquen의 rough plane도 같은 전제라 두 논문이 짝이다.
→ "바닥 한 층 고정"이 임의 선택이 아니라 **문헌이 요구하는 조건**이고,
동시에 **거칠기 정도가 결과에 들어간다**는 뜻이기도 하다.

### A-R6. 기울기 속도는 관성수로 정당화한다

**GDR MiDi (2004), "On dense granular flows"** — EPJ E 14, 341-365

```
I = γ̇·d / √(P/ρ)        관성수 (inertial number)
                          입자 재배치 시간 / 거시 변형 시간
준정적 regime            I ≲ 10⁻³
```

기울기 속도 1°/s 와 4°/s 가 11° 차이를 낸 것은 **이상현상이 아니라 준정적 조건 위반**이다.
`I`로 환산하면 "얼마나 천천히 기울여야 하는가"에 숫자로 답할 수 있고,
**각도 고정 방식의 정당화도 여기서 나온다** — 고정하면 `γ̇ = 0`이라 `I = 0`.

`μ(I)` 유동학은 [Drucker-Prager](terms/drucker-prager.md)·[임계상태](terms/critical-state.md)와도 이어진다.

### A-R7. 정지 판정 — 속도 임계에는 표준값이 없다

**절대 속도(cm/s)로 된 표준값은 문헌에 없고, 있을 수도 없다.**
"1.5 cm/s 이하면 정지"는 입자 크기·중력·더미 깊이가 바뀌면 의미가 달라진다 —
5cm 자갈에겐 거의 멈춘 것이지만 0.1mm 밀가루에겐 격렬한 흐름이다.

**문헌은 전부 무차원 비율로 쓴다.**

| 기준 | 표준값 | 정의 |
|---|---|---|
| **불평형력 비** (unbalanced force ratio) | `< 10⁻³` | 입자에 남은 합력 ÷ 평균 접촉력. **지반공학 DEM 표준** |
| 입자당 운동에너지 | `< 10⁻¹³ · m·g·R` | 남은 운동에너지를 `mgR`로 정규화 |
| **관성수 `I`** ([A-R6](#a-r6-기울기-속도는-관성수로-정당화한다)) | `< 10⁻³` | 알갱이 재배치 시간 ÷ 거시 변형 시간 |

[Pouliquen 1999](#a-r5--경사판에는-표준-프로토콜이-있다--두께-의존성)는 아예 수치 임계를 안 쓴다 —
흘려보낸 뒤 **남은 퇴적층 두께**로 정의한다.

#### §2-2 를 관성수로 환산하면

```
가정   입자 지름 5cm · 더미 깊이 27.5cm(6층) · 벌크밀도 1600 · 석영밀도 2650

바닥 압력   P = 1600 × 9.81 × 0.275 ≈ 4,300 Pa
√(P/ρ)                              ≈ 1.28 m/s

전단이 전체에 퍼지면   I ≈ 2 × 10⁻³
전단이 한 층에 몰리면   I ≈ 1.2 × 10⁻²
```

**준정적 기준 `10⁻³`보다 2~12배 위다.** 즉 지금 "멈췄다"고 판정하는 상태가
문헌 기준으로는 **아직 아주 느리게 흐르는 중**일 수 있다.
맞추려면 `0.01 × (r/dt)` → `0.001 × (r/dt)`, 1.5 cm/s → 0.15 cm/s.

> ⚠ 밀도는 가정값이고 전단 위치에 따라 6배가 갈린다. **자릿수만 유효하다.**
> [params.md](params.md)의 실제 값으로 다시 계산할 것.

#### XPBD 와의 대응

**불평형력 비가 우리 구조에 가장 잘 맞는다.** XPBD 는 이미 매 반복 **제약 잔차**를
계산하고 있고 그것이 곧 "아직 안 풀린 힘"이다. 새로 계산할 것이 거의 없다.

```
현재      평균 속도 < 1.5 cm/s            절대 속도. 근거 없음
문헌 방식  제약 잔차 / 접촉력 < 10⁻³        비율. 지반공학 DEM 표준
```

덤으로 **붕괴 판정("뒤 절반이 앞 절반보다 큰가")과 같은 양을 쓰게 된다** —
잔차가 죽어가면 버티는 것, 유지되면 무너지는 것. 두 판정이 하나로 통합된다.

> ⚠ **바꾸기 전에 계기부터.** §2-2 는 검증 4개(안식각·벽·터널·붕괴)가 **공유하는** 정의라
> 바꾸면 넷이 다 바뀌고 [m0-log.md](../design/m0-log.md)의 기존 실행과 비교가 끊긴다.
> 먼저 **현재 판정이 통과한 시점의 `I` 또는 잔차 비를 로그에 찍어** 실제로 얼마인지 볼 것.
> 그러면 "느슨하다"가 가정이 아니라 측정값이 된다.

> ⚠ 임계를 조이면 부작용 둘: ① 다지기 프레임이 늘어난다 ② **아예 도달 못 할 수 있다**
> (반복이 모자라면 스텝당 잔류 변위가 그 아래로 안 내려간다).
> **②가 나오면 그게 곧 크리프 가설의 직접 증거다.**

### A-R. 절차 세부 → 문헌 대응표

| 절차 | 근거 |
|---|---|
| 측정법으로 tilting box 선택 | A-R1 (Al-Hashemi 2018) |
| 기둥 붕괴 기각 | A-R2 (Lube/Lajeunesse 계열) |
| **재는 양이 `θm`이라는 것** | **A-R4 (Jaeger 1989)** |
| **각도 고정 · 기울기 속도** | **A-R6 (GDR MiDi 2004, 관성수)** |
| **판정 = 흐름 개시** | **A-R5 (Pouliquen 1999, `θ_start`)** |
| **바닥 한 층 고정** | **A-R5 (Goujon 2003)** |
| 용기 벽 · 크기 영향 | A-R3 (Zhou 2002) · Janssen 벽 마찰 |
| 회전 자유도 없음 | A-R3. **문헌 밖. 실측 필요** |
| **정지 판정 임계** | **A-R7. 속도로는 표준 없음. 무차원 비율로 바꾸면 있다** |
| 이분 탐색 · 변위 임계값 · 유지 시간 | **없음. 우리 설계** ⚠ 아래 |

> ⚠ **이분 탐색은 이력에 오염될 수 있다.** `θ_start`와 `θ_stop` 사이에 이력이 있으므로
> 각도를 위아래로 오가는 탐색은 **직전에 어느 방향에서 왔는가**에 결과가 달라진다.
> 매 시행마다 더미를 새로 초기화하지 않으면 계통 오차가 들어간다.

> ⚠ **"N초 붙잡고 변위"는 사태가 아니라 크리프를 잴 수 있다.** 반복 수가 유한한 XPBD는
> 임계 이하에서도 스텝당 미세 잔류 변위가 남고, 유지 시간이 길면 그것이 누적되어 임계를 넘는다.
> **유지 시간을 축으로 스윕(1s / 4s / 16s)해서 각도가 내려가면 크리프**이지 사태가 아니다.

---

## B. 젖은 모래의 실제 물리 — 강도 곡선의 근거

> 그래픽스가 아니라 **물리/재료 논문**. 파라미터 튜닝의 근거로 쓴다.

### B1. Pakpour, Habibi, Møller, Bonn (2012)
**"How to construct the perfect sandcastle"** — Scientific Reports

수분 함량과 모래 기둥 최대 높이의 관계를 실측. **약 1% 부근에서 최적**, 최대 높이가 반지름과
스케일 법칙으로 묶임.

```
 강도
  ↑      ╱▔▔▔▔▔▔▔▔▔╲
  │     ╱             ╲___
  │    ╱                   ╲
  └─┴──┴──────────────┴──────┴──→ 수분량
    0  ~1%           중간     과포화
    ↑   ↑              ↑        ↑
  즉시 최적강도    거의 안 떨어짐  골격이 뜸
  붕괴 (모래성)     (넓은 고원)   → 슬러리
```

**게임에 중요한 두 가지**
1. **왼쪽 절벽이 가파르다.** 모세관 다리가 끊어지는 순간이라 점진적이지 않음
   → 마르는 모래성은 **버티다 갑자기 무너진다.** 연출로 훨씬 좋음
2. **중간이 넓은 고원이다** (Scheel 2008). 튜닝은 양 끝만 신경 쓰면 됨

### B2. ★ Lian, Thornton, Adams (1993)
**"A theoretical study of the liquid bridge forces between two rigid spherical bodies"**
— J. Colloid Interface Sci. 161(1), 138

**물다리 하나가 얼마나 세게 당기고, 언제 끊어지는가.** `breakStretch` 를 물리에서 뽑는 유일한 경로.

다리 모양이 미지수라는 게 문제다 — 표면장력이 잘록하게 만들고, 잘록해지면 [Laplace](terms/capillary-bridge.md)
압력이 내려가고, 그럼 더 당기고, 그럼 모양이 또 바뀐다. 이 순환의 평형이 Young-Laplace 방정식이다.

**결과 ① 토로이드 근사가 충분히 좋다.** 다리 옆면을 원호로 치고 푼 닫힌 식이 수치 정확해와
힘에서 몇 % 안에 든다. **이후 모든 DEM 이 닫힌 식을 쓸 수 있게 된 근거.**

**결과 ② 힘의 크기 — 물의 양이 안 들어간다.**
```
F₀ = 2π R γ cos θ            닿아 있을 때. 다리를 키워도 거의 그대로
```
→ Scheel 2008 "넓은 고원"의 미시적 이유. 다리 하나의 힘이 안 변하니
**다리 개수**가 포화되면 강도가 평평해진다.

```
              모세관력 F       입자 무게 W      F / W
 d = 0.3mm     69 μN           0.37 μN         186 배      ← 뭉친다
 d = 5cm       11.4 mN         1.70 N          0.007 배    ← 안 뭉친다
```
`F ∝ d` 인데 `W ∝ d³` 이라 **끈끈함은 `1/d²` 로 죽는다** → B-S2.

**결과 ③ ★ 파단 거리**
```
 S_rupt / R  =  (1 + θ/2) · V*^(1/3)      V* = 다리 부피 / R³,  θ 접촉각(rad)
```
**세제곱근**이 핵심이다 — 물을 8배 부어도 파단 거리는 2배밖에 안 는다.

#### → `breakStretch` 로 번역

중심 거리 기준이므로 `breakStretch = 1 + (1+θ/2)·V*^(1/3) / 2`.

| `V*` | 상태 | `breakStretch` |
|---|---|---|
| 0.001 | 거의 안 젖음 | **1.05** |
| 0.01 | pendular 전형 | **1.11** |
| 0.05 | 꽤 젖음 | **1.18** |
| 0.2 | funicular 진입 | **1.29** |

> `V*` 는 대략 고체 부피 대비 액체 비율(입자당 다리 4개 기준).

**★ 지금 쓰는 `breakStretch = 1.3` 은 `V* ≈ 0.2`, 즉 물이 꽤 많은 상태다.**
상수로 두면 **모든 `W` 에서 "매우 젖음" 으로 취급**하게 되고, 틀리는 방향이 나쁘다.

```
마를수록 결합이 물리보다 오래 버틴다
  → 과대 안정.  §B-S5(전단대)의 과대 안정과 같은 방향으로 겹친다
  → ★ M0 3-2 "마른 성이 갑자기 무너지는가" 가 안 나올 위험
```

**지금 구조는 강도만 `W`를 따라가고 결합의 수명은 `W`와 무관한데, 물리는 둘 다 `W`가 정한다고 말한다.**
```cpp
// 지금
breakStretch = 1.3;                                            // 상수
// Lian 1993 대로면
breakStretch = 1 + 0.5 * (1 + theta/2) * pow(V(W), 1.0/3.0);
```

#### 한계

| 가정 | 우리와의 거리 |
|---|---|
| **강체 구** | 우리도 강체 구 → **그대로 맞는다** |
| 축대칭 다리 **하나** | 다리가 합쳐지는 funicular 영역은 범위 밖 → `W_optEnd` 위쪽 |
| 준정적 | 빠른 하중엔 점성 항이 더 붙는다 — **더 세게 버티는 쪽**. M4 파도 충격에서 볼 것. → **Pitois, Moucheront, Chateau (2000), "Liquid bridge between two moving spheres: an experimental study of viscosity effects", JCIS 231, 26** |
| 부피 보존 | 증발·재분배 없음 → `V(W)` 매핑은 우리가 정해야 한다 |

**짝 논문: Willett, Adams, Johnson, Seville (2000), Langmuir 16** — 힘을 간극·부피의 닫힌 식으로 준다.
**Lian이 "언제 끊어지나", Willett이 "그때까지 얼마로 당기나"** 다.
둘을 합치면 `cohesion(W)` 와 `breakStretch(W)` 가 **같은 `V*(W)` 하나에서** 나온다 — 서로 어긋나지 않는다.

### B3. ★★ Radjaï & Richefeu (2009)
**"Bond anisotropy and cohesion of wet granular materials"** — Phil. Trans. R. Soc. A 367, 5123

**Rumpf 가 못 주는 것을 준다 — 인장 `σ_t` 가 아니라 전단 점착력 `c`.**

```
Rumpf 가 주는 것    σ_t   덩어리를 똑바로 잡아 뜯는 힘 [인장]
우리가 쓰는 것       c     Drucker-Prager 꼭짓점 이동량 [전단]
```

**모래성은 잡아 뜯겨서 무너지지 않고 미끄러져서 무너진다.** Rumpf 는 전단에 대해 아무 말도 안 한다.

#### 기계 — SFF 를 젖은 계로 확장한다

응력-힘-조직 관계 자체는 [구조 이방성 / 조직](terms/anisotropy-fabric.md)에 ①~⑤로 정리했다.
**이 논문의 기여는 거기에 두 줄을 고친 것**이다.

```
① 결합의 정의를 넓힌다
     마른 계   결합 = 닿은 쌍                 f > 0 만
     젖은 계   결합 = 파단 거리 안의 쌍        f < 0 도 포함  ← 안 닿아도 결합

② f₀ 가 음수 기여를 갖는다
     p = p_외부 + p_모세관
                   ↑ 외부에서 아무것도 안 눌러도 남는 자기응력
```

그리고 `p_외부 = 0`(아무도 안 누름)에서 `q` 를 구하면 **그게 곧 점착력**이다 — 버틸 수 있는 최대 전단이니까.

```
        ⎧ 얼마나 촘촘한가 ⎫   ⎧ 결합 하나가 세나 ⎫   ⎧ 얼마나 쏠렸나 ⎫
 c  ∝   ⎩   φ · z        ⎭ · ⎩     f₀ / d²      ⎭ · ⎩  (a_c+a_n+a_t) ⎭
```

**앞 두 묶음은 Rumpf 와 같다. 세 번째가 새로 붙은 것이다.**

| | Rumpf (1962) | Radjaï & Richefeu (2009) |
|---|---|---|
| 주는 양 | `σ_t` (인장) | **`c` (전단)** ← 우리가 쓰는 것 |
| 구조 | `(1-ε)/ε · F/d²` | `φ·z·f₀/d²` **× 이방성** |
| 결합의 정의 | 닿은 것만 | **떨어진 다리도 포함** (파단 거리까지) |
| 검증 | 이론 | **DEM + 젖은 유리구슬 실험** |

> ★★ **`σ_t ∝ 1/d` 는 살아남는다.** `params.md` 의 `cohesionMax ∝ 1/d`, B-S2 의 "5cm 로 키우면
> 강도가 죽는다"는 이 논문에서도 그대로다. **바뀌는 건 앞의 상수뿐.**

#### 왜 이방성이 점착을 만드는가

```
              마른 모래              젖은 모래

  누르는 축    접촉 정렬 → 밀어냄     접촉 정렬 → 밀어냄        ← 둘 다
  벌어지는 축  접촉 끊김 → 없음       ★ 물다리가 남음 → 붙잡음   ← 젖은 것만
```

마른 모래는 **미는 것만** 한다. 젖은 모래는 반대쪽에서 **당겨서도** 버틴다.
Rumpf 는 결합이 등방으로 흩어져 있다고 보므로 **이 두 번째 정렬이 통째로 사라진다** (B-S2 한계 ③).

#### 우리 프로젝트에 주는 것 넷

**① `breakStretch` 가 강도를 정한다** ← 가장 실용적
결합은 닿은 쌍이 아니라 **파단 거리 안의 쌍**이므로, 늘리면 `z`(배위수)가 커지고 `c` 가 직접 오른다.
```
breakStretch ↑  →  결합으로 세는 쌍이 늘어남  →  z ↑  →  c ↑
```
**M0 실패 분기의 `1.3 → 1.5` 는 "파단 조건 완화"가 아니라 "재료를 강하게 만드는 것"이다.**

**② 다지기가 강해지는 두 번째 경로** — 공극률만이 아니라 **결합 방향의 정렬**도 강도를 올린다.
→ M6 형틀 도구의 물리적 근거.

**③ 고원의 설명** — `f₀` 는 액체 부피에 둔감하므로(B2), `z` 가 포화되면 `c` 가 평평해진다.
Scheel 2008 의 넓은 고원, `cohesion(W)` 의 `W_optEnd = 0.70` 구간.

**④ ⚠ 우리에게 `a_t` 가 없다** — 접선력 이방성은 알갱이가 회전해야 쌓이는데
**우리 입자는 회전 자유도가 없다** (A-R3). 셋 중 하나가 빠진 채 도는 셈이라 **강도를 과소평가**한다.
```
a_t 없음        → 과소평가
B-S4 기하 아칭   → 과대평가
B-S5 전단대     → 과대평가
상쇄되는지는 문헌으로 답이 안 나온다. c[Pa] 실측(3-3-B)이 유일한 답
```

#### ⚠ 한계 — 재결합 전제가 우리와 다르다

| 가정 | 우리와의 거리 |
|---|---|
| 준정적 (아주 느린 전단) | 붕괴 순간은 동적 → 그 순간엔 안 맞는다 |
| 구형·단분산 | 우리도 구형 → **오히려 잘 맞는다** |
| pendular 영역 | `W_sat` 근처 슬러리는 범위 밖 |
| **결합 재생성 허용** | **우리는 [자발적 재결합 없음](../design/system-design.md)** |

마지막이 설계 차이라 중요하다.
```
논문   끊김률 = 생성률   →  정상상태에서 c 가 유지된다
우리   생성률 = 0        →  만질수록 z ↓ → c ↓ (되돌리는 건 도구뿐)
```
**논문의 `c` 를 그대로 가져오면 과대 추정이고, 우리 쪽이 더 취성이다.**
후자는 게임적으로 원하는 방향이라 그대로 두되,
**`c[Pa]` 실측은 결합이 한 번도 안 끊긴 새 벽에서 재야 한다** — 만지작거린 뒤엔 이미 약해진 재료다.

#### 읽는 법

```
§2 모형          건너뛰어도 된다 (contact dynamics 세부)
§3 SFF 관계      ★ 본론
§4 이방성의 진화  전단 중 a 가 어떻게 변하는가 — 다지기 근거(②)
§5 c 의 표현식    ★ 가져다 쓸 식
```

**선행 논문 Richefeu, El Youssoufi, Radjaï (2006), PRE 73, 051304 을 먼저 볼 것** —
실험 데이터와 `c(W)` 실측이 거기 있고, 2009 는 **왜 그런지 설명하는** 후속이다.

### B. 보충

| 논문 | 내용 |
|---|---|
| Hornbaker, Albert, Albert, Barabási, Schiffer (1997), "What keeps sandcastles standing?", Nature 387 | **모세관 다리(capillary bridge)** 가 모래성이 서는 진짜 이유 |
| **Albert, Albert, Hornbaker, Schiffer, Barabási (1997), "Maximum angle of stability in wet and dry spherical granular media", PRE 56, R6271** | **θm 을 응집력의 함수로.** 경사판이 재는 양을 그대로 `W`의 함수로 준다 → [A-R4](#a-r4--경사판이-재는-것은-안식각이-아니다) |
| Scheel et al. (2008), "Morphological clues to wet granular pile stability", Nature Materials | 물 형태가 pendular→funicular→capillary로 바뀌어도 **강도는 거의 일정**. 고원의 근거 |
| Mitarai & Nori (2006), "Wet granular materials", Advances in Physics | 젖은 입상체 종합 리뷰 |

> ★ **이 값들은 전부 0.3mm 모래의 것이다.** 지름 5cm 입자로 옮기는 문제는
> [B-S](#b-s-점착을-5cm-입자로-옮기는-법--입자-크기--해상도) — 강도가 `1/d` 로 죽으므로 그대로 쓰면 안 된다.

---

## B-S. 점착을 5cm 입자로 옮기는 법 — 입자 크기 = 해상도

> A-R이 **"안식각을 어떻게 재는가"** 라면 여기는 **"점착을 어떻게 옮기는가"** 다.
> B절이 실제 모래(0.3mm)의 강도를 주면, 이 절은 그걸 **지름 5cm 입자**로 옮길 때
> 무엇이 보존되고 무엇이 깨지는지를 다룬다.
>
> ⚠ 서지 확신도는 B-S2·B-S4·B-S5가 높고, B-S7의 연도는 확인이 덜 됐다.

### B-S1. ★★ "5cm 입자로 30cm 터널이 서는가"는 두 질문이다

[milestones.md](../design/milestones.md)의 불확실 항목 1번은 사실 둘이 붙어 있고,
**위험은 두 번째에만 있다.**

| | 질문 | 상태 |
|---|---|---|
| Q1 | **실물** 젖은 모래에서 30cm 터널이 서는가 | 문헌 + 손계산으로 **코드 없이 답 나옴** |
| Q2 | **6입자 폭**으로 그 결과를 재현하는가 | ★ 진짜 미검증 |

**Q1은 지금 답한다.** 천장을 양단 고정 보로 보면

```
σ = γL² / 2t        γ = 1600 × 9.8 = 15,700 N/m³,  L = t = 0.3 m
  ≈ 2.4 kPa
```

젖은 모래의 인장강도는 문헌상 **0.5 ~ 3 kPa**(Pierrat & Caram 1997, Richefeu 2006, Scheel 2008).
→ **사각 단면은 아슬아슬, 원형 아치는 압축이라 여유.**
[m0-spike.md](../design/m0-spike.md) 3-1의 "아치는 서고 사각만 무너짐" 분기가
**물리적으로 예상되는 정답**이라는 뜻이다. 시뮬이 사각을 여유롭게 통과시키면 오히려 의심할 것.

### B-S2. ★★ σ_t ∝ 1/d — 모세관 물리를 그대로 키우면 안 된다

**Rumpf (1962), "The strength of granules and agglomerates"**, in *Agglomeration* (Knepper 편), Interscience

[모세관 다리](terms/capillary-bridge.md)로 붙은 덩어리의 거시 인장강도를 **입자 하나의 결합력에서** 유도한다.

```
σ_t = C · (1-ε)/ε · F / d²        ε 공극률,  F 결합력 하나,  d 입자 지름
                                   C ≈ 1 (배위수 ≈ π/ε 가정에서 나옴)
```

**모세관력은 F ∝ γ_lv · d 이므로 이걸 대입하면**

```
σ_t ∝ 1/d           ← ★ 입자를 키우면 강도가 반비례로 죽는다
```

지름을 0.3mm → 5cm 로 키우면 **강도가 약 150배 사라진다.**
실물 5cm 자갈은 물을 부어도 아무것도 안 선다 — 우리 입자를 "큰 모래알"로 생각하면 안 된다는 뜻.

> **결론: 점착 파라미터는 물리에서 유도하지 말고 거시 목표값에서 역산한다.**
> ```
> σ_t 1.5 kPa 목표,  d = 5cm,  ε = 0.4  →  (1-ε)/ε = 1.5
>   F = σ_t · d² · ε/(1-ε)  =  1500 × 0.0025 / 1.5  ≈  2.5 N
>   입자 하나 무게 (석영, 지름 5cm)                    ≈  1.7 N
>   →  결합 하나가 자기 무게의 약 1.5배를 버텨야 한다
> ```
> 이 **"1.5배"** 가 [컴플라이언스](terms/compliance.md)·`breakStretch` 의 출발점이자 검산 지표다.
> ([결합 슬롯](../design/system-design.md) 12개면 입자 하나가 최대 18배까지 매달릴 수 있다.)

> **★ 무게 대비로 보면 더 극적이다** — 실제 모세관력은 `F = 2πRγ` (Lian 1993).
> ```
>              모세관력 F        입자 무게 W       F / W
>   d = 0.3mm   69 μN            0.37 μN          186 배     ← 뭉친다
>   d = 5cm     11.4 mN          1.70 N           0.007 배   ← 안 뭉친다
> ```
> `F ∝ d` 인데 `W ∝ d³` 이라 **끈끈함은 `1/d²` 로 죽는다.** 167배 키우면 28,000배 차이.
> 필요한 2.5 N 은 실제 11.4 mN 의 **약 220배** — 그만큼 부풀린 값을 넣는 것이고,
> 그게 "5cm 입자는 큰 모래알이 아니다"의 정확한 정도다.

#### ★ Rumpf 식의 한계와 대응 문헌

이 식은 **1962년의 1차 근사**다. 어디가 왜 틀리는지가 이후 60년의 문헌이다.

| # | 가정 | 실제 | 대응 문헌 |
|---|---|---|---|
| ① | 파단면의 결합이 **동시에** 끊어진다 | [순차 파단](terms/progressive-failure.md) — 균열이 지퍼처럼 번진다. **σ_t 2~5배 과대** | **Kendall (1988), "Agglomerate strength", Powder Metallurgy 31** — Rumpf 정면 반론. 강도는 결합력의 합이 아니라 **결함 크기**가 정한다 (`σ_f ∝ K_IC/√c`) · Thornton, Ciomocos, Adams (2004), Powder Technology 140 — DEM 압축시험으로 확인 |
| ② | 모든 결합이 하중을 **고르게** 나눠 짊어진다 | **힘사슬** — 접촉의 40% 정도가 대부분을 짊어지고 나머지는 놀고 있다 | **Radjaï, Wolf, Jean, Moreau (1998), "Bimodal character of stress transmission in granular packings", PRL 80, 61** · Mueth, Jaeger, Nagel (1998), "Force distribution in a granular medium", PRE 57 — 힘 분포가 지수꼬리 |
| ③ | 결합 방향이 **등방 랜덤** | 다지면 이방성이 생긴다. 그리고 점착은 인장이 아니라 **전단**으로 진다 | **Radjaï & Richefeu (2009), "Bond anisotropy and cohesion of wet granular materials", Phil. Trans. R. Soc. A 367, 5123** — Rumpf 를 **조직 이방성 항으로 일반화**해 전단 `c` 로 준다 |
| ④ | 결합력 `F ∝ γ_lv·d` | 실제 다리 힘은 액체량·접촉각·**입자 간 거리**의 함수 | **Willett, Adams, Johnson, Seville (2000), "Capillary bridges between two spherical bodies", Langmuir 16** — 닫힌 해 · **Lian, Thornton, Adams (1993), J. Colloid Interface Sci. 161** — **파단 거리** `d_rupt ≈ (1+θ/2)·V^(1/3)` |
| ⑤ | 액체량과 무관 (pendular 영역만) | funicular·capillary 로 넘어가면 식이 달라진다 | **Schubert (1975), "Tensile strength of agglomerates", Powder Technology 11, 107** — 포화 영역별 확장. Scheel 2008 의 "고원"이 여기서 설명된다 |
| ⑥ | 크기 효과 없음 (강도가 시편 크기와 무관) | 클수록 취성 | **Bažant (1984), "Size effect in blunt fracture", J. Eng. Mech. 110** — J-7 제곱-세제곱과 **별개의 두 번째 크기 효과** |

> **우리에게 실무적으로 가장 값진 둘**
> ```
> Lian 1993 (④)          파단 거리 → breakStretch 를 물리에서 뽑는 유일한 경로.
>                        지금은 "1.3" 이 근거 없는 초기값이다
> Radjaï & Richefeu 2009 (③)  인장 σ_t → 전단 c 로 옮겨주는 보정.
>                        우리가 실제로 필요한 건 c 인데 Rumpf 는 σ_t 만 준다
> ```

①은 **고칠 게 아니라 살려야 하는 것**이라는 점에서 특이하다 — 순차 파단이 곧 ★덩어리 붕괴다
(→ [terms/progressive-failure.md](terms/progressive-failure.md)).
그래서 이 절의 결론은 "Rumpf 를 정확한 식으로 대체하자"가 아니라 **"Rumpf 는 상한으로만 쓰고
실제 값은 `H_c` 역산으로 교정한다"** 이다.

### B-S3. 점착에는 자가 없다 — `cohesion(W)` 를 Pa 로

마찰에는 34°라는 자가 있는데(A-R), **점착에는 없다.** `α = 1/cohesion(W)` 는 솔버 내부 숫자일 뿐이라
터널 결과를 문헌과 대조할 수 없다. 환산 경로는 이미 M0 안에 있다 — **수직 벽 검증이 그 자다.**

```
H_c = 4c/γ · tan(45° + φ/2)         Terzaghi 임계 절토고 (→ J-5)
→ 벽이 무너지기 시작한 높이에서 c[Pa] 를 역산
→ B-S1 의 2.4 kPa · B-S2 의 1.5 kPa 목표와 직접 비교
```

**정보성이던 벽 검증이 이걸로 점착의 교정 실험이 된다** — 3-0(경사판)이 마찰에 대해 하는 일과 같다.

| 문헌 | 내용 |
|---|---|
| **Richefeu, El Youssoufi, Radjaï (2006), "Shear strength properties of wet granular materials", PRE 73, 051304** | ★ **가장 직접적.** DEM + 실험으로 `c(W, d)` 를 준다. Rumpf 스케일을 입상체에서 검증 |
| Pierrat & Caram (1997), "Tensile strength of wet granular materials", Powder Technology 91 | σ_t 실측값. B-S1 의 2.4 kPa 대조용 |
| **Potyondy & Cundall (2004), "A bonded-particle model for rock", IJRMMS 41** | 결합 입자 모델의 거시강도 교정 표준. **결합 슬롯·`breakStretch` 설계에 직결** |
| Soulié et al. (2006), "Influence of liquid bridges on the mechanical behaviour of polydisperse granular materials" | 다분산일 때의 결합력 분포 |

### B-S4. ★★ D/d = 6 은 하필 **막힘이 시작되는** 값이다

터널 30cm ÷ 입자 5cm = **6**. 이 숫자가 사일로 아칭의 임계 부근이다.

| 문헌 | 내용 |
|---|---|
| **Zuriguel, Garcimartín, Maza, Pugnaloni, Pastor (2005), "Jamming during the discharge of granular matter from a silo", PRE 71, 051303** | 배출구/입자 비가 **5 부근에서 막힘 확률이 발산** |
| **To, Lai, Pak (2001), "Jamming of granular flow in a two-dimensional hopper", PRL 86, 71** | 같은 현상의 2D 원전. 아치가 저절로 생긴다 |

```
점착이 0이어도 6입자 폭 구멍은 기하학적으로 막힌다.
```

→ **무점착 대조군 없이 통과한 터널은 맞는 답을 틀린 이유로 낸 것이다.**
`W = 0` 으로 같은 터널을 뚫었을 때 **무너져야** 3-1의 통과가 의미를 갖는다.

### B-S5. ★★ 전단대가 터널보다 크다 — 틀리는 방향은 **과대 안정**

**Mühlhaus & Vardoulakis (1987), "The thickness of shear bands in granular materials"** — Géotechnique 37(3), 271

```
전단대 두께 ≈ 8 ~ 16 · d50
d = 5cm  →  40 ~ 80 cm  >  터널 지름 30cm
```

**붕괴 메커니즘 자체가 격자에 안 들어간다.** 그리고 이 오차는 방향이 정해져 있다 —
파괴면을 만들 자리가 없으니 **안 무너지는 쪽으로** 틀린다.

> ★ **그래서 M0의 진짜 차단 위험은 3-1(터널)이 아니라 3-2(덩어리 붕괴)다.**
> 과대 안정한 시뮬은 터널을 쉽게 통과시키고, 그 다음 **마른 성이 안 무너져서** 게임의 정체성을 죽인다.
> [milestones.md](../design/milestones.md)의 "실패하면 성 15채가 1.9채로"는 위험을 **거꾸로** 적은 것.

### B-S6. 업스케일 입자 — 무엇을 스케일해야 등가인가

| 문헌 | 내용 |
|---|---|
| **Feng & Owen (2014), "Discrete element modelling of large scale particle systems—I: exact scaling laws", Computational Particle Mechanics 1, 159** | 입자를 키울 때 **정확히 등가가 되는 스케일 법칙**. 우리가 하려는 일 그 자체 |
| **Coetzee (2017), "Review: Calibration of the discrete element method", Powder Technology 310, 104** | DEM 교정 종합 리뷰. **형상/입경 비의 수렴 조건**(대체로 10~20 이상 요구) |
| Roessler & Katterfeld (2018), "Scaling of the angle of repose test and its influence on the calibration of DEM parameters using upscaled particles", Powder Technology 330 | 업스케일 입자로 **안식각**을 맞출 때 생기는 왜곡 → 3-0 과 직결 |
| Ding et al. (2014), "Effect of model scale and particle size distribution on PFC3D simulation results", Rock Mech. Rock Eng. 47 | 입경이 거시 강도를 바꾼다는 정량 결과 |

**공통 결론: 특성 길이 / 입경 비가 10 미만이면 결과는 물성이 아니라 해상도가 정한다.**
우리는 **6**이다. 그래서 이진 판정으로는 원인을 못 가른다 → B-S8.

### B-S7. 터널 안정 그 자체 (지반공학)

| 문헌 | 내용 |
|---|---|
| **Davis, Gunn, Mair, Seneviratne (1980), "The stability of shallow tunnels and underground openings in cohesive material", Géotechnique 30(4), 397** | ★ **문자 그대로 이 질문의 논문.** 안정수 `N = γ(C + D/2)/c` 와 피복비 `C/D` 로 무지보 터널 성립 조건 |
| Iglesia, Einstein, Whitman (2014), "Investigation of soil arching with centrifuge tests", JGGE 140(2) | 트랩도어 아칭 — 폭 대비 언제 아치가 서는가. Terzaghi (1943) 의 현대 실험판 |
| Heyman (1966), "The stone skeleton" | K-4의 `t/R ≈ 0.106`. **해석해가 있는 유일한 눈금자** |

### B-S8. → M0 에 무엇을 바꾸는가

```
① 벽 검증을 정보성 → 점착 교정으로 승격.  H_c 에서 c[Pa] 역산      B-S3
② 무점착 대조군 추가.  W=0 터널은 무너져야 한다                    B-S4
③ 이진 판정 → D_crit(d) 스윕.  무너지는 지름을 잰다                B-S6
④ 아치 t/R = 0.106 재현으로 기하 해상도를 검증                     B-S7
⑤ breakStretch 를 상수 → W 의 함수로.  1.3 은 "매우 젖음" 값이다   B2
```

### B-S9. 용어 문서에만 있는 문헌 — 역참조

개념을 읽을 때 필요한 것이라 **내용은 `terms/` 에 두고 여기서는 소재만** 가리킨다.

| 문헌 | 무엇 | 어디 |
|---|---|---|
| **Skempton (1964)**, "Long-term stability of clay slopes", Géotechnique 14 | 지반공학 **순차 파단의 원전**. 첨두가 아니라 잔류강도로 무너진다 | [terms/progressive-failure.md](terms/progressive-failure.md) |
| Bishop (1967), "Progressive failure — with special reference to the mechanism causing it" | 메커니즘 정리. **J-11 의 Bishop 1955(절편법)와 다른 논문** | 〃 |
| Griffith (1921), Phil. Trans. R. Soc. A 221 | 균열이 번지느냐 멈추느냐는 힘이 아니라 **에너지**가 정한다 | 〃 |
| Satake (1982) | 조직 텐서(fabric tensor) 개념 | [terms/anisotropy-fabric.md](terms/anisotropy-fabric.md) |
| **Rothenburg & Bathurst (1989)**, Géotechnique 39 | **SFF 관계의 원전** (2D) | 〃 |
| Ouadfel & Rothenburg (2001), Mechanics of Materials 33 | 3D 확장. `q/p = (2/5)Σa` 의 `2/5` | 〃 |
| **Roessler, Richter, Katterfeld, Will (2019)**, "…part I: Solving the problem of ambiguous parameter combinations", Powder Technology 343 | **교정 모호성의 원전.** 실험 하나로는 파라미터 조합이 안 정해진다 | [terms/dem-calibration.md](terms/dem-calibration.md) |
| **Roessler & Katterfeld (2018)**, Powder Technology 330 | 업스케일 입자에서 **전단 상자가 크기 불변 + 빠르다.** B-S6 에도 있다 | 〃 |
| **Wensrich & Katterfeld (2012)**, "Rolling friction as a technique for modelling particle shape in DEM", Powder Technology | 구름 마찰 = 모양의 대역. **A-R3 의 "회전 없음은 문헌 밖"에 부분적 답** | 〃 |
| Coetzee & Scheffler (2023), Processes 11(1), 5 | 점착 재료 교정은 아직 미성숙 — B-S3 을 문헌 쪽에서 확인 | 〃 |
| Roessler & Katterfeld (2019), Particuology | **젖은 모래** 교정 절차. 원통 인상 + 인상 속도 축 | 〃 |
| **Richter et al. (2022)**, "Shear lid motion in DEM shear calibration…", Powder Technology | 전단 뚜껑의 수직 자유도가 내부마찰각을 바꾼다 | [terms/shear-box.md](terms/shear-box.md) |
| ASTM D3080 / D6528 | 전단 상자-입경 비 표준 (`W/d ≥ 10`). DEM REV 는 `> 12.5` + 3,000 입자 | 〃 |

**③이 판정의 핵심이다.**

```
D_crit 이 cm 로 수렴 (2.5cm · 1.25cm 두 해상도에서)  → 물리를 풀고 있다. 5cm 채택
D_crit ∝ d (항상 6입자 근처)                        → 해상도가 답을 정한 것. 30cm는 의미 없음
```

기존 실패 분기의 "1.25cm 진단 1회"가 이미 이 구조의 절반이다.
**한 점이 아니라 두 곡선으로 만들면 판정이 훨씬 강해지고 비용은 거의 같다** — 무너지는 큰 구멍은 빨리 끝난다.

---

## C. 실시간 근사 — PBD 입자

### C1. Macklin, Müller, Chentanez, Kim (2014)
**"Unified Particle Physics for Real-Time Applications"** — SIGGRAPH 2014, NVIDIA
→ 이후 **NVIDIA Flex**가 됨

**모든 물체를 같은 반지름의 입자로 만들면 상호작용이 공짜가 된다.**

**전제: PBD (Position Based Dynamics)**
```csharp
v += gravity * dt;
Vector3 p = x + v * dt;                        // 1. 제약 무시하고 예측

for (int i = 0; i < iterations; i++)
    foreach (var c in constraints) c.Project(ref p);   // 2. 위치를 직접 밀어 제약 만족

v = (p - x) / dt;                              // 3. 속도는 위치 변화에서 역산
x = p;
```
힘을 계산하지 않고 위치를 옮긴다 → **절대 폭발하지 않음(무조건 안정)**.
대가는 물리적 정확성 — [강성](terms/stiffness.md)이 반복 횟수와 dt에 의존.
→ **실제 구현은 XPBD (Macklin et al. 2016)를 쓸 것.** 강성을 물리 단위로 지정 가능

**통일 표현**

| 재료 | [제약](terms/constraint.md) |
|---|---|
| 강체 | [shape matching](terms/shape-matching.md) |
| 변형체 | 느슨한 shape matching |
| 천 / 밧줄 | 거리 + 굽힘 제약 |
| 유체 | 밀도 제약 (= Position Based Fluids) |
| **입상체(모래)** | **접촉 제약 + 마찰** |

→ 충돌 판정이 전부 구-구 하나로 통일. **파도와 모래의 양방향 상호작용이 거저 나옴**

**GPU를 위한 장치**: Gauss-Seidel(순차) 대신 Jacobi(병렬) + **제약 평균화** + **SOR**.
한 입자에 제약 N개가 동시에 밀어 과보정하는 걸 개수로 나눠 막고, 1보다 큰 계수로 수렴 회복.

**마찰** (모래가 되는 지점): 힘이 아니라 위치 보정으로.
```
법선 보정량 Δn 계산 → 접선 상대이동 Δt를 되돌림, 단 μ·|Δn| 로 클램프
```
완전히 되돌림 = 정지 마찰 / 클램프됨 = 운동 마찰. **A2의 [리턴 매핑](terms/return-mapping.md)과 같은 구조.**
이 마찰만 제대로 넣으면 안식각이 나온다.

> ⚠ **이 클램프는 반복마다 예산이 새로 주어진다.** 반복이 많으면 접선 변위를 결국 전부 지워
> **반복 횟수가 마찰 계수 역할을 한다** — μ는 도달점이 아니라 도달 시점만 바꾼다.
> M0에서 실측으로 확인했다(각 22.7→55.8°, 반복 16↑에서 마른 기둥이 수직으로 섬).
> → 해결은 **C2**. ([m0-log.md](../design/m0-log.md) 실행 #4)

**cohesion / adhesion 파라미터** — 여기가 우리 지렛대.
```csharp
particle.cohesion = cohesionCurve.Evaluate(saturation);
particle.friction = frictionCurve.Evaluate(saturation);
```
A1의 결론을 실시간 가격에 사는 방법. 포화도가 계산되어 나오는 게 아니라 우리가 칠해 넣는다.

**언리얼 현실**
- Flex의 UE4 브랜치는 있었으나 **UE5에는 없음**. NVIDIA가 GameWorks UE 브랜치를 접었고 UE5는 Chaos를 씀
- **(A) Niagara로 직접 구현 ← 가장 현실적**
  - `Simulation Stages` → PBD 반복 루프
  - `Neighbor Grid3D` → 입자 [이웃 탐색](terms/neighbor-search.md) (논문의 균일 격자 구조 그대로)
  - Niagara Fluids 예제가 이 구조로 돼 있어 뼈대를 재사용 가능
- (B) PhysX 5 (오픈소스, PBD 파티클 내장) 직접 연동 — 통합 비용 큼

### C2. Müller, Macklin, Chentanez, Jeschke, Kim (2020)
**"Detailed Rigid Body Simulation with Extended Position Based Dynamics"** — SCA 2020 / CGF 39(8)
→ **C1의 마찰을 XPBD 승수로 다시 쓴 판.** M0에서 원문 대조함 (§3.5–3.6)

**접촉마다 승수 두 개를 0으로 초기화한다** — `λn`(법선) · `λt`(접선). 서브스텝마다 새로.

```
법선      침투 d = (p1−p2)·n,  d ≤ 0 이면 건너뜀
          Δx = d·n 을 α = 0 과 λn 으로 적용            ← λn 누적

정지마찰  Δp   = (p1−p̄1) − (p2−p̄2)                   p̄ = 서브스텝 적분 전 위치
          Δp_t = Δp − (Δp·n)n
          Δx = Δp_t,  단 λt < μs·λn 일 때만            ← ★ 게이트. 전부 아니면 전무
```

**정지마찰은 위치, 동마찰은 속도.** 위치 풀이가 끝난 뒤 속도 패스를 따로 돈다.

```
Δv ← −(v_t/|v_t|) · min(h·μd·|f_n|, |v_t|)        f_n = λn / h²   ← 실제 법선력
```

`min`이 "보정이 속도 자체를 못 넘는다"를 보장 → **무조건 안정**이라고 명시.
같은 패스에서 반발도 처리하는데, **떨림 방지로 `|v_n| ≤ 2|g|h` 면 `e = 0`**.

| 우리에게 중요한 것 | |
|---|---|
| `f_n = λn/h²` | 하중이 마찰에 정직하게 들어온다. C1의 침투량 근사가 못 하던 것 |
| **반복은 1회** | 표 1이 전부 `iters/substep = 1`. substep 10~40 × 반복 1 |
| 브로드페이즈 | **프레임당 1회** (AABB를 `k·Δt·v`로 부풀림, `k=2`). 좁은 판정만 서브스텝마다 |
| 입자에 적용 시 | 회전이 없어 `r1·r2`·관성텐서가 사라짐. `p̄ = PrevPos`, 질량 동일이라 `Δλ = d/2` |

### C3. Guendelman, Bridson, Fedkiw (2003)
**"Nonconvex Rigid Bodies with Stacking"** — SIGGRAPH 2003 / TOG 22(3), 871–878
→ **깊은 더미에서 하중이 바닥까지 안 가는 문제의 정본.** 처방보다 **진단과 경고**가 값지다.

**진단이 우리와 같다.** 접촉을 전파 모델로 풀면 스택에서 반복이 많이 필요하고,
모자라면 큐브가 서로 파고들기 시작한다(논문 Figure 6 중앙).
= Jacobi 한 스윕에 접촉 한 칸씩만 정보가 가는 문제.

**§8.1 접촉 그래프**
- 물체를 **하나씩 따로** 중력에 떨어뜨려(나머지 고정) `Δt`(스텝 정도) 동안 간섭을 기록 →
  **떨어진 쪽으로 향하는 유향 간선**
- DFS 두 번으로 강결합 성분을 접어 **DAG**
- 큐브 스택 → 바닥에서 위로 한 칸씩. **원형으로 늘어선 도미노 → 전부 한 레벨**
- 레벨 `i` 처리 = 레벨 내부 접촉 + **하위 레벨과의 접촉**을 리스트로 모아 여러 번 반복
- 전체 레벨을 **여러 번** 훑는다

**§8.2 shock propagation** — **마지막 스윕에서만**
```
레벨 0 처리 → 레벨 0 전체를 무한 질량 (K 행렬 = 0)
레벨 1 처리 → 레벨 1도 무한 질량
...                                  ← 전 레벨을 딱 한 번만 훑는다
```
무한 질량이 된 물체는 위 레벨의 임펄스에 안 밀리므로 **위쪽이 비켜나야 한다.**
같은 레벨끼리는 둘 다 아직 유한 질량이라 평범한 접촉 처리와 같다 →
**느린 수렴이 강결합 성분이라는 작은 그룹 안으로 국소화된다.** 이게 실제 이득이다.

**★ 논문이 직접 경고한 것 — shock은 반복의 대체가 아니다**

시소 위에 무거운 블록과 가벼운 블록을 올리는 실험(Figure 7):

> shock propagation만 돌리면(접촉 전파 단계를 빼면) 시소가 **가만히 있거나 아주 천천히 기운다
> — 무거운 블록의 무게를 느끼지 못하기 때문이다.**

```
접촉 전파 여러 스윕   ← 무게를 느끼게 하는 단계 (반복이 많이 필요한 바로 그것)
      ↓
shock 스윕 1회        ← 수렴을 강제로 끝내는 마무리
```
실사용값: **충돌 5회 / 접촉 10회 / shock 1회.** 물리를 만드는 건 접촉 반복 10회다.

> ⚠ **`λn`이 하중을 못 받는 상태에서 shock을 걸면 그 상태를 그대로 굳힌다.**
> 더미는 안 가라앉지만 여전히 무게를 모르고 `μ·λn` 예산은 비어 있다.
> 증상만 사라지고 **μ 무반응은 그대로 남는다.**

**우리에게 옮길 때의 벽 3개**

| 벽 | 내용 |
|---|---|
| 접촉 그래프 비용 | "하나씩 떨어뜨려보기 + DFS 2회"는 링 500개엔 되지만 **20만 입자엔 불가.** 근사는 지형 위 높이 — 하이트필드 조회가 O(1)이고 [정렬 기반 공간 해시](terms/spatial-hash.md)에 높이를 키로 넣으면 밴드가 공짜 |
| **★ 근사가 깨지는 곳이 하필 터널·아치** | 원형 도미노가 한 레벨로 묶이는 경우가 **정확히 아치**다. 키스톤은 같은 높이의 양옆이 받치고, 터널 천장 아래는 빈 공간이다. **타협 불가 요구사항이 근사의 실패 지점에 있다** |
| 무한 질량이 모래엔 과함 | 강체는 관통만 막으면 되지만 모래는 아래층도 재배치·다져짐이 일어나야 한다. 얼리면 더미가 인위적으로 단단해져 **[안식각](terms/angle-of-repose.md)이 높게 측정된다** — 솔버 교정 실험이 오염된다. → 무한 질량 대신 **질량 스케일링**(Tonge et al. 2012) |

**결론 — 우리 순서**
```
1. λn(깊이) 프로파일을 ρgh 와 대조          ← 진단
2. 접촉 반복을 늘려 수렴 지점을 찾는다        ← 논문도 10회. 이게 물리를 만든다
3. 워밍 스타트로 그 반복 수를 되돌려 받는다    ← 정지 더미는 λn이 이미 맞은 상태로 시작
4. shock 은 마지막 1회 마무리로만            ← 아치 예외를 다룰 수 있게 된 뒤에
```
**3번이 우리에겐 shock보다 잘 맞는다.** 워밍 스타트는 "무게를 느끼는 상태"를 프레임 간에
**보존**하는 것이라 논문이 걱정한 부작용이 없다. shock은 그걸 **버리고 굳히는** 쪽이다.

원문: [PDF](https://www.cs.ubc.ca/~rbridson/docs/rigid_bodies.pdf) · [ACM DL](https://dl.acm.org/doi/10.1145/882262.882358)

### C4. Frâncu & Moldoveanu (2014) ★★
**"An Improved Jacobi Solver for Particle Simulation"** — VRIPHYS 2014, 125–134
→ **적층 하중 전달 문제에 가장 직접적인 논문.** 입자 + Jacobi + 마찰이 정확히 우리 설정이다.

**핵심은 항 하나**
```
평범한 Jacobi   λ^{l+1} = proj( λ^l − (ω/dᵢ)·r^l )
개선된 Jacobi   λ^{l+1} = proj( λ^l − (ω/dᵢ)·r^l − β_l·Δλ^l )      (38)
                                              ^^^^^^^^^^^^^ 직전 반복의 보정량
```
Conjugate Residuals의 하강 방향에서 유도했지만 결과는 **모멘텀(heavy-ball) 항**이다.
`β`는 전역 내적이 필요한 진짜 CR 공식 대신 스케줄로 근사한다.
```
β_l = min( a·(l/l_max)^b , 1 )        권장 a=1, b=0.6, ω=0.5      (36)
```
> ⚠ **CG/CR을 그대로 쓰는 게 아니다.** 전역 내적이 빠졌으므로 **정보는 여전히 반복당 한 칸씩** 간다.
> 효과는 다른 경로 — 깊은 스택은 조건수가 나쁜 계이고, heavy-ball 가속이 정확히 그런 계의
> 수렴 상수를 개선한다. 결과적으로 **GS급 수렴**(양방향 제약에선 종종 더 나음).
> 단방향(접촉)에서는 "항상 GS보다 낫지는 않았고 `a=2`로 조정"이 필요했다고 명시.

비용은 **제약당 float 하나(직전 `Δλ`) + 곱셈-덧셈 한 번.** XPBD가 이미 `λ`를 들고 있으니 필드 하나.
측정: 입자 시나리오 GS 5.2ms vs IJ 6.5ms (단일 스레드). 반복당 25% 비싸지만 **병렬화된다.**

**★ 우리 실험을 이미 했다** — §8.1 / Figure 3
입자 3000개를 떨어뜨려 만든 더미, `μ` = 0.1 / 0.2 / 0.3 / 0.4, **반복 15 · 서브스텝 1 · 60Hz · γ=0.5**.
> "마찰 계수 값이 커질수록 **안식각이 커지는 것에 주목하라.**"

**★★ §6 Sequential Positions (Algorithm 1) — 위치 마찰의 에너지 주입을 끊는 구조**

논문이 PBD 마찰의 병을 두 개로 진단한다.
1. 계산된 임펄스가 대개 **과도**하다 (높은 충돌 허용오차 없이는)
2. 마찰력이 **솔버가 끝난 뒤에 매우 거칠게 근사**된다

그리고 처방이 이것이다 — **"마찰을 PBD 솔버 안에 넣으려면 매 반복마다 속도를 명시적으로 갱신해야 한다."**
(Müller 2007은 끝에서 유한차분으로 근사하고, Jakobsen 2001은 Verlet에 맡긴다.)

```
for l = 0 .. lmax-1:
    β(l) ← (36)
    for each constraint Cᵢ:
        δ = Cᵢ(x)          위치 오차
        Jᵢ = ∇Cᵢ(x)        제약 방향
        ν  = Jᵢ v          속도 오차
        α  = ω·mₑ,   mₑ = 1/dᵢ
        r  = kv·ν + (kc/h)·δ                   ← ★ 속도·위치 오차를 섞는다
        Δλᵢ ← (38) 로 계산
        λᵢ  ← proj(λᵢ + Δλᵢ);  Δλᵢ = λᵢ − λ̄ᵢ    ← 누적 후 클램프 (Catto 2005)
    v = v +   W Jᵀ Δλ
    x = x + h·W Jᵀ Δλ                          ← ★★ 같은 임펄스로 v 와 x 를 함께 갱신
```

| 설정 | 결과 |
|---|---|
| `kv=0, kc=1` | 순수 PBD |
| `kv=1, kc=γ` | 일종의 비선형 VTS |
| **`kv=0.1`** | **PBD의 충격을 부드럽게** — 권장 출발점 |

> **★ 마지막 두 줄이 요점이다.** PBD는 위치를 밀고 나중에 `v = (p−x)/h`로 **속도를 역산**한다 —
> 위치 보정이 곧 속도가 되므로 GS 스윕 안에서 마찰 보정이 다음 접촉의 입력이 되는 **고리**가 생긴다.
> SP는 반대로 **임펄스 하나에서 `v`와 `x`를 함께** 낸다(`x`는 정확히 `h`배). 역산이 없으므로
> 마찰이 에너지를 주입할 경로가 원천 차단된다.

**§5 마찰** — Catto의 단순화(법선 푸시 상수)를 **일부러 안 썼다**. 정확한 결합을 위해.
사각뿔 모델(접촉당 접선 제약 2개) 또는 CCP 원뿔 투영(Tonge 2012).
> 마찰 최적화의 최후 수단은 **마지막에 한 번의 반복으로 후처리**하는 것인데(Müller 2007식),
> 이 방식은 **대개 약하고 비현실적인 마찰력을 만든다.**

**§3.4.1** — "임펄스를 그 물체에 붙은 제약 수 `n_bi`로 나누고 `ω`를 아예 빼도 된다."
= 우리가 겪은 **Jacobi 평균 모드**를 논문이 직접 언급한다. §4.3이 그 처방.

**§7 워밍 스타트** — "도움은 되지만 **GS에서만큼 극적이지는 않다.**" Jacobi 계열에선 기대치를 낮출 것.

**§7 병렬 구현** — 두 패스: ① 제약을 돌며 임펄스 계산 ② 입자를 돌며 인접 리스트로 모아 적용.
**입자가 자기 것만 쓰는 gather 구조**라 [Niagara의 쓰기 제약](terms/simulation-stages.md)에도 안 걸린다.
논문 자신의 제안: **Tonge 2012 블록 솔버와 결합** — 워크그룹 안은 로컬 메모리로 GS,
워크그룹 사이는 개선된 Jacobi. **전파 깊이를 블록 크기만큼 한 번에 건너뛴다.**

**§9 결과** — Figure 7에서 GS / SOR(ω=1.2) / IJ를 비교하고 **IJ가 이긴다** → 단순 SOR 튜닝은 열등.
GS의 단점을 "제약 순서 편향 + 병렬화를 위한 배칭(그래프 컬러링)"으로 명시하고,
**IJ는 그 배칭을 불필요하게 만드는 것이 목적**이라고 위치 짓는다.
반복 수가 아주 낮으면 `ω`와 `b`를 더 낮출 것. 속도가 빠르고 물체가 작으면
**반복을 늘려도 안 되고 dt를 줄여야(서브스텝) 한다** — 서브스텝과 반복은 서로 다른 병을 고친다.

**★ 구조적 분기 — 이 논문을 채택하면 반복으로 돌아가야 한다**

| | Frâncu 2014 | Müller 2020 (C2) |
|---|---|---|
| 서브스텝 | **1** | 10~40 |
| 반복 | **15** | **1** |
| 마찰 | 접선 제약 2개 / CCP 원뿔 | λt 게이트 |

`β_l`이 반복 인덱스 `l`의 함수라 **반복 1회에서는 모멘텀이 생기지 않는다.**
그리고 SP는 `λn`·`λt`를 **같은 범위에서 누적하고 직접 클램프**한다(Catto 2005) —
누적 범위가 어긋나면 반복이 예산만 부풀리는 현상이 생긴다.

원문: [EG diglib](https://diglib.eg.org/handle/10.2312/vriphys.20141231.125-134)

### C. 보충

| 논문 | 내용 |
|---|---|
| Macklin & Müller (2013), "Position Based Fluids", SIGGRAPH | 유체의 밀도 제약. C1의 전제 |
| Müller et al. (2007), "Position Based Dynamics" | PBD 원조 |
| **Macklin et al. (2016), "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"** | **실제로 쓸 버전.** 강성이 반복 횟수에 안 휘둘림. 단 **탄성 제약만** — 마찰은 다루지 않는다 |
| **Macklin et al. (2019), "Small Steps in Physics Simulation"** | 같은 예산이면 반복보다 서브스텝. **"반복 1회" 관행의 출처** — C1 마찰의 반복 의존이 안 보이게 된 이유이기도 하다 |
| Gao et al. (2018), "GPU Optimization of Material Point Methods", SIGGRAPH Asia | MPM을 GPU로 |
| Wang et al. (2020), "A Massively Parallel and Scalable Multi-GPU Material Point Method", SIGGRAPH | 위의 확장 |

---

## D. 하이트필드 GPU 침식 ← 가장 먼저 구현할 것

### D1. Mei, Decaudin, Hu (2007)
**"Fast Hydraulic Erosion Simulation and Visualization on GPU"** — Pacific Graphics 2007

지형을 높이 텍스처로 두고 그 위에 물을 얹는다. 이웃 4칸만 보므로 완전 병렬.

**상태 (텍스처 채널)**

| 기호 | 의미 |
|---|---|
| `b` | 지형 높이 |
| `d` | 물 깊이 (`b+d` = 수면 높이) |
| `s` | 물에 뜬 모래 양 (탁도) |
| `f` | 4방향 유출량 |
| `v` | 속도 (u, v) |

**[가상 파이프](terms/virtual-pipes.md) (virtual pipes)** — 인접 두 칸이 파이프로 연결됐다고 본다.
```csharp
float dh = (b + d) - (b_n + d_n);            // 수면 높이 차 = 압력
f_n = max(0, f_n + dt * A * g * dh / l);     // ← "=" 가 아니라 "+=" : 흐름에 관성이 있다
```
**관성이 있어서 물이 확산되는 게 아니라 넘실거린다. 즉 파도가 이 모델로 표현된다.**

```csharp
// 있는 물보다 많이 못 뺀다 — 또 클램프
float K = min(1, (d * cellArea) / ((f_L+f_R+f_T+f_B) * dt));
f_L *= K; f_R *= K; f_T *= K; f_B *= K;
```

**[패스](terms/pass.md) 5개**
1. 물 추가 (비 / **파도 주입**)
2. 가상 파이프 흐름 → `d` 갱신 → `v` 산출
3. **침식/퇴적**
   ```csharp
   float C = Kc * sin(경사각) * length(v);      // 운반 능력
   if (C > s) { float a = Ks*(C-s); b -= a; s += a; }   // 깎아서 태움
   else       { float a = Kd*(s-C); b += a; s -= a; }   // 내려놓음
   ```
4. 모래 이류 — `s_new(x) = s_old(x - v*dt)` ([반라그랑주](terms/semi-lagrangian-advection.md), 텍스처 샘플 1회)
5. 증발 — `d *= (1 - Ke*dt)` ← **자유수만. 스민 수분이 아님. 이걸로 건조를 표현할 수 없다**

**한계**
- 하이트필드 → **오버행/터널 불가.** 모래성 터널 못 뚫음
- 덩어리째 무너지는 걸 못 함. 위에서부터 스르륵 녹음
- 운반 능력 공식이 경험식. 파라미터 4개(Kc, Ks, Kd, Ke) 수동 튜닝
- 아주 얕은 물(= 파도 끝자락)에서 수치가 지저분함

**해변용 수정 (필수)**

| 문제 | 수정 |
|---|---|
| 평평한 해변은 `sin α ≈ 0` → 침식이 아예 안 일어남 | `max(sin α, minSlope)`, minSlope ≈ 0.05~0.15 |
| 파도가 없음 | 바다쪽 경계 셀의 수면 높이를 파형 함수로 강제. 파이프 관성이 안쪽으로 전파 → uprush/backwash 자동 |
| **안식각/모래성 없음** | **[열침식](terms/thermal-erosion.md) 추가** (아래) |
| 스민 수분 개념 없음 | `W` 채널 추가 (침투/배수/증발/모세관) |

**열침식(thermal erosion) — 모래성을 서게 하는 지점** (Jákó & Tóth 2011)
```csharp
if ((b - b_neighbor) / cellSize > tan(talusAngle))
    아래쪽으로_모래를_조금_옮긴다();

float talusAngle = lerp(34f, 90f, wetness);   // ★ 젖으면 더 가파른 각도를 버틴다
```
**A1의 "포화도 → 점착력 → [항복면](terms/yield-surface.md) 이동"의 하이트필드 버전. 비용은 1/1000.**

> ### ⚠ 90°는 물리 한계가 아니라 하이트필드의 표현 한계다
>
> **실제 젖은 모래는 90°를 훌쩍 넘는다.** 오버행·아치·터널이 다 된다.
> [점착력](terms/cohesion.md)이 생기면 **"각도 한계"라는 개념 자체가 사라지기** 때문이다.
>
> | | 마른 모래 | 젖은 모래 |
> |---|---|---|
> | 버티는 방식 | 마찰만 | 마찰 + 점착력 |
> | **한계의 종류** | **각도 한계 (34°)** | **크기(응력) 한계** |
> | 90° 초과 | 절대 불가 | **가능** |
>
> 마른 모래는 크기와 무관하게 34°를 못 넘는다(각도 문제).
> 젖은 모래는 각도 제한이 없고 **자기 무게를 못 견디면** 무너진다(크기 문제).
> ```
> 수직 절토 한계:  H_c ≈ (4c/γ)·tan(45° + φ/2)    ← 각도가 아니라 높이가 한계
> 젖은 해변 모래(c ~ 수 kPa) → 대략 1m 안팎
> ```
> **B1(Pakpour 2012)의 `h ∝ R^(2/3)` 가 정확히 이 얘기다** — 각도가 아니라 크기의 문제.
>
> 하이트필드는 `(x,z)` 하나에 높이 하나라 **물리가 120°를 버텨도 그릴 방법이 없다.**
>
> #### ★ 그래서 [A]/[B] 전환 기준이 여기서 나온다
> ```
> [A] 하이트필드   34° ~ 90°     평범한 둔덕, 벽, 계단
> [B] 입자         90° 초과      오버행, 아치, 터널
> ```
> ```csharp
> if (localSlope > 80f || 플레이어가_터널을_판다)
>     ConvertToParticles(region);
> ```
> **플레이어가 오버행·터널을 만들려는 순간이 곧 입자화 시점.**
> 임의 반경이 아니라 **표현이 필요해지는 지점**이 기준이 된다.
> 평범한 둔덕은 싸게, 공들인 구조물만 비싸게 시뮬레이션한다.
>
> #### [B]에서는 각도를 쓰지 말 것
> `lerp(34, 90, W)`는 **하이트필드 전용 대용품**이다. 입자 계층엔 점착력을 직접 넣는다.
> ```csharp
> // [A] 하이트필드 — 각도로 (표현 한계 안에서만 유효)
> float talusAngle = lerp(34f, 90f, W);
>
> // [B] 입자 — 각도가 아니라 점착력으로
> particle.cohesion = cohesionMax(grainSize) * cohesionCurve.Evaluate(W);
> ```
> 인장력 < 점착력이면 붙어 있고 크면 떨어진다 → **오버행이 저절로 되고,
> 크기 한계도 저절로 나오고, Pakpour의 스케일 법칙이 결과로 나온다.**
>
> #### 덤 — 아치가 오버행보다 훨씬 잘 버틴다
> ```
>  오버행/캔틸레버            아치
>  ████▄                  ▄███████▄
>  ████ ▔▔▔╲              ██       ██
>  ↑ 윗면이 당겨짐(인장)     ↑ 전체가 눌림(압축)
>  → 점착력이 꼭 필요        → 점착력이 거의 불필요
>  → 약함                   → 강함
> ```
> 돌 아치가 접착제 없이 서 있는 것과 같은 원리. **같은 폭이면 아치가 훨씬 안정적.**
> → **"구조를 이해하면 더 대담한 형상을 만들 수 있다"가 물리적으로 성립한다.**
> 그리고 **마르면 아치가 먼저 간다** — 얇고 표면적이 커서 건조 전선이 빨리 관통.

### D1-a. ★★ 왜 침식률 지수를 3으로 올리는가 (Mei 원논문은 1)

> **"물이 모래를 미는 걸 시뮬레이션하는데 왜 공식이 필요한가?"** 에 대한 답.
> 나중에 반드시 다시 묻게 될 결정이라 근거를 남긴다.

#### 우리 시뮬에는 알갱이가 없다
```
b, d, v, s  ←  전부 텍스처의 숫자. 알갱이도, 물이 알갱이를 미는 장면도 없다
```
**"물이 빠르면 모래가 더 많이 움직인다"를 시뮬이 알 방법이 없다.** 우리가 알려줘야 한다.

| 계층 | 무엇을 공식으로 넣나 | 비용 |
|---|---|---|
| DNS + DEM (알갱이 하나씩) | 거의 없음 (뉴턴 역학만) | **계산 불가능** |
| MPM 2상 (A1, [D1-b](#d1-b-★-gao-2018에서-빌린-개선-3가지)) | 구성 모델 | 프레임당 수십 초 |
| PBD 입자 (C1) | 접촉·마찰 제약 | 실시간(좁은 영역) |
| **하이트필드 (D1) ← 우리** | **운반 능력 공식** | 실시간(전체) |

**"공식 없이 저절로" 되는 계층은 맨 위뿐이고 계산이 불가능하다.**
우리는 **"싼 계층 + 공식"** 을 골랐다. 그게 하이브리드 결정의 실체다.

#### u³는 임의의 숫자가 아니다 — 유도된다 (Bagnold 에너지론)
```
바닥 전단응력        τ = ρ·Cf·u²
바닥에 전달되는 일률  P = τ·u = ρ·Cf·u³
운반량 ∝ 가용 일률    q ∝ u³
```
실측 확인: Bagnold(1941 바람 / 1966 물), Bailard(1981 해변).
Meyer-Peter–Müller의 소류사 공식 `q ∝ (τ−τc)^1.5` 도 `τ ∝ u²`를 넣으면 **u³로 수렴**한다.
**바람과 물에서 같은 지수가 나오는 게 우연이 아니다** — 같은 에너지론이다.

#### ★ 진짜 이유: 지수가 1이면 해변 회복이 수학적으로 불가능하다
[G-5b의 파랑 비대칭](#g-5b-★★-폭풍이-쓸어간-모래는-어떻게-돌아오나)을 지수별로 계산하면:
```
육지 방향: u=1.0, 지속 1   /   바다 방향: u=0.5, 지속 2
```
| 지수 | 육지 | 바다 | 순 이동 |
|---|---|---|---|
| **1 (Mei 원논문)** | 1.0 | 1.0 | **0** ← 정확히 상쇄 |
| 2 | 1.0 | 0.5 | +0.5 |
| **3** | **1.0** | **0.25** | **+0.75** |

**지수 1이면 순 이동이 정확히 0이다.** `운반 ∝ u × 시간`을 다 더하면 그게 바로
**물의 순 이동량**이고 질량 보존으로 0이다. **튜닝 문제가 아니라 수학적으로 0.**
```
지수 1 → 파랑 비대칭이 아무것도 못 만듦 → 해변이 영원히 회복 안 됨
지수 3 → 빠른 쪽이 이김                → 회복이 나옴
```
**폭풍 이벤트가 영구 파괴가 되느냐 순환이 되느냐가 이 지수 하나에 달렸다.**

#### 지수가 바꾸는 다른 것
```
지수 1 → 속도가 조금만 있어도 계속 조금씩 깎임 → 항상 뭉근하게 침식, 밋밋
지수 3 → 느린 곳은 거의 안 깎이고 빠른 곳만 확 → 문턱을 넘는 느낌

지수 1 → 어디나 비슷하게 깎임 → 무늬가 안 생김
지수 3 → 물이 모이는 곳이 압도적으로 깎임 → 물길이 파이고 깊어짐 → 물이 더 모임
        (양의 되먹임)  ← 침식 지형의 무늬는 전부 이 비선형에서 나온다
```

#### 임계값도 같이 넣어야 한다
```
Mei 원논문:  C = Kc·sinα·|v|     ← 임계가 없다. 속도가 있으면 무조건 깎임
필요한 것:   q ∝ (u − u_c)³      ← u_c 아래면 아무 일도 안 일어남 (Shields)
```
임계가 있어야 **안전 구역과 위험 구역이 선명하게 갈리고**,
잔잔할 때 **지형이 가만히 있는다.** 임계가 없으면 아무리 잔잔해도 계속 조금씩 깎여
결국 다 평평해진다 — **플레이어가 쌓아놓은 게 유지되어야 하므로 이게 중요하다.**

### D1-b. ★ Gao 2018에서 빌린 개선 3가지

Gao et al. (2018), "Animating Fluid Sediment Mixture in Particle-Laden Flows" (SIGGRAPH)는
A1의 후속으로, **빽빽한 바닥 ~ 묽은 부유**의 농도 전 범위를 하나로 잇는다.
농도가 유변학을 결정하고(마찰 지배 → 점성 지배로 연속 전이), 연행·퇴적이
**운반 공식 없이 물리에서 나온다.** ← 앞 질문의 "저절로 되는 계층"이 바로 이 논문이다.

우리는 구현하지 않고 **논리만 빌린다.**

**① `s`(뜬 모래)가 물에 영향을 주게 한다** — 지금 설계에서 `s`는 수동적이다
```csharp
float density   = 1000f + s * (2650f - 1000f);   // 탁한 물은 무겁다
float viscosity = baseViscosity * ViscosityFactor(s);
```
하이트필드에서는 밀도가 압력항에 들어가므로 **가상 파이프 유량에 그대로 반영**된다.
→ **탁류가 무겁게 바닥을 따라 깔림**(밀도류). backwash가 모래를 싣고 내려가는 실제 거동.

**② ★ 퇴적을 [침강속도](terms/settling-velocity.md) 기반으로 — 가장 값진 개선**
```csharp
// Mei 원래 (경험식):  if (C < s) { a = Kd*(s-C); b += a; s -= a; }

// 침강속도 기반: dt 동안 바닥에 도달하는 비율 = 침강거리 / 물 깊이
float ws = SettlingVelocity(grainSize[x][z]);      // ← 입도 필드에서 파생
float deposit = s * min(1f, ws * dt / waterDepth);
b += deposit;  s -= deposit;
```
**여기서 [입도 필드](#h-6--구역별-입도-cross-shore-sorting)와 만난다.**
```
굵은 모래 → 침강 빠름 (1mm ≈ 10cm/s) → 가까이 쌓임
고운 모래 → 침강 느림 (0.2mm ≈ 2cm/s) → 멀리 간다
```
→ **구역별 입도 분급(H-6의 메커니즘 ② 선택적 퇴적)이 이 한 줄에서 공짜로 나온다.**

**③ 연행과 퇴적을 분리한다 (평형 → 비평형 모델)**
```csharp
// 연행 — 임계를 넘는 만큼만
float excess = max(0f, shearStress - CriticalShear(grainSize[x][z]));
float pickup = Ke * pow(excess, 1.5f);

// 퇴적 — 침강속도가 결정 (②와 동일)
float deposit = s * min(1f, ws * dt / waterDepth);

b -= (pickup - deposit) * dt;
s += (pickup - deposit) * dt;
```
Mei의 운반능력 `C` 하나로 둘을 동시에 정하는 건 **평형 모델** — 모래가 즉시 평형에
도달한다고 가정한다. 실제로는 **떠올랐다가 한참 실려 가서 딴 데 가라앉는다.**
그 지연이 해변에서 중요하다:
```
backwash가 실어 내려가 아래쪽에 쌓음 / uprush가 실어 올라가 위쪽에 쌓음
평형 모델이면 그 자리에서 바로 쌓여버려 이 이동이 안 생긴다
```

**렌더링에 공짜로 딸려오는 것**

| 채널 | 용도 |
|---|---|
| `W` | 알베도 어둡게 + 러프니스 낮춤 (H1 Jensen 1999) |
| `s` | 파도 끝자락의 탁한 갈색 물 |
| `v` | **[플로우맵](terms/flow-map.md)** — 물 왜곡, 거품 줄무늬, 젖은 모래의 흐른 자국 |
| `d` | 물 투명도, 깊이별 색, [굴절](terms/refraction.md) |
| `\|v\|` 큰 곳 | 포말 생성 마스크 |
| `b` 변화량 | 파도가 지나간 자리의 ripple 노멀 |

**언리얼 구현 경로**
1. Niagara `Grid2D` 데이터 인터페이스로 채널 할당
2. `Simulation Stages`로 패스 5~7개 배치
3. Render Target으로 export
4. 테셀레이션 평면 + [WPO](terms/wpo.md), 또는 [Virtual Heightfield Mesh](terms/virtual-heightfield-mesh.md)로 `b` 변위
5. 같은 텍스처를 머티리얼에서 소비
6. **프로토타입은 256×256으로 시작**

### D. 보충

| 논문 | 내용 |
|---|---|
| **Jákó & Tóth (2011), "Fast Hydraulic and Thermal Erosion on GPU"** | **열침식 = 안식각. 반드시 필요** |
| Šťava et al. (2008), "Interactive Terrain Modeling Using Hydraulic Erosion" | 다층(regolith) 확장 |
| Musgrave, Kolb, Mace (1989), "The Synthesis and Rendering of Eroded Fractal Terrains", SIGGRAPH | [수력 침식](terms/hydraulic-erosion.md)의 원조. 운반 능력 공식의 출처 |
| Sumner, O'Brien, Hodgins (1999), "Animating Sand, Mud, and Snow", CGF | 하이트필드 발자국 [변형](terms/strain.md). 지금도 게임에서 쓰는 방식 |
| Krištof et al. (2009), "Hydraulic Erosion Using Smoothed Particle Hydrodynamics" | SPH 버전 |

---

## E. 증발 / 건조 — 토양물리학 (그래픽스 공백)

> **여기가 논문 공백.** 그래픽스에 증발/건조를 다룬 논문이 사실상 없다.
> 토양물리학/화학공학 건조 이론에서 직접 이식해야 한다.

### 물이 빠지는 두 경로 (구분 필수)

| | 배수 (drainage) | 증발 (evaporation) |
|---|---|---|
| 원인 | 중력 | 햇빛 / 바람 |
| 속도 | **빠름** (초~분) | **느림** (분~시간) |
| 멈추는 지점 | **[포장용수량](terms/field-capacity.md)(field capacity)** 에서 멈춤 | **0까지 감** |
| 출처 | A1의 Darcy | E절 |

> 파도가 닿는 자리의 모래가 항상 모래성 쌓기 좋은 이유:
> 적셔짐 → 빠르게 배수 → field capacity에서 멈춤 → 그게 마침 최적 강도.

### 딱 3개만 읽는다면

**E1. Or, Lehmann, Shahraeeni, Shokri (2013)**
**"Advances in Soil Evaporation Physics — A Review"** — Vadose Zone Journal 12(4)
→ **여기서 시작.** 현대 표준 리뷰. 1/2단계 전환, [건조 전선](terms/drying-front.md), 공극 구조, 대기 [경계층](terms/boundary-layer.md) 결합

**E2. Lehmann, Assouline, Or (2008)**
**"Characteristic lengths affecting evaporative drying of porous media"** — Physical Review E 77, 056309
→ **"1단계가 언제 끝나는가"의 정량적 답.** 모세관이 물을 표면까지 끌어올릴 수 있는
최대 깊이([특성 길이](terms/characteristic-length.md) `L_C`)를 공극 크기 분포에서 유도. 코드의 `crustScale`이 이것.
**모래는 L_C가 짧다 (굵은 모래 수 cm)** → 표면이 빨리 마르고 그 다음 전선이 천천히 내려감

**E3. van Genuchten (1980)**
**"A closed-form equation for predicting the hydraulic conductivity of unsaturated soils"** — SSSAJ 44
→ **실제로 구현할 곡선.** 수분량 ↔ 모세관 흡입력 ↔ 투수계수.
흡입력이 곧 알갱이를 당기는 힘이므로 **수분 → 흡입력 → 점착력**이 물리적으로 이어진다.
B1의 강도 곡선을 경험식이 아니라 여기서 유도할 수 있음.

### 2단계 모델

```
1단계 (등속 건조기, constant-rate)
  모세관이 안쪽 물을 표면으로 계속 공급 → 표면이 계속 젖어 있음
  → 속도를 결정하는 건 모래가 아니라 공기 (햇빛·바람·습도). 일정함

2단계 (감속 건조기, falling-rate)
  모세관 공급이 못 따라감 → 건조 전선이 안으로 후퇴
  → 수증기가 마른 껍질을 통과해 확산돼야 함 → rate ∝ 1/두께

 시작            1단계            2단계
┌────────┐    ┌────────┐    ┌────────┐
│▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│    │░░░░░░░░│ ← 마른 껍질
│▓▓▓▓▓▓▓▓│    │▓▓▓▓▓▓▓▓│    │░▓▓▓▓▓▓░│
└────────┘    └────────┘    └────────┘
```

### 구현 (3번의 `W` 채널 확장)

```csharp
const float FIELD_CAPACITY = 0.35f;   // 중력으로 못 빼는 잔류량
const float L_C            = 0.03f;   // 특성 길이. 굵은 모래 수 cm

void Step(float dt)
{
    // ① 침투 — 위에 자유수가 있으면 흡수 (A1 Darcy)
    W += infiltrationRate * saturate(waterDepth) * dt;

    // ② 배수 — field capacity 초과분만, 빠르게 아래로
    if (W > FIELD_CAPACITY)
        아래셀로_이동(drainRate * (W - FIELD_CAPACITY) * dt);

    // ③ 증발 — 표면 노출 셀만. 2단계 모델
    if (isSurfaceExposed)
    {
        float sun = saturate(dot(normal, sunDir)) * shadowMask;   // ★ 햇빛
        float rate = evapBase * sun * windFactor;
        if (dryFrontDepth >= L_C)
            rate /= (1 + dryFrontDepth / crustScale);             // 2단계
        W -= rate * dt;
    }

    // ④ 모세관 재분배 — 이게 있어야 1→2단계 전환이 저절로 일어난다
    W += capillaryDiffusion * (이웃평균W - W) * dt;

    W = saturate(W);

    // ⑤ 강도로 변환 (B1 곡선)
    strength   = strengthCurve.Evaluate(W);
    talusAngle = lerp(34f, 90f, strength);    // 하이트필드
    cohesion   = maxCohesion * strength;      // 입자
}
```

**van Genuchten / Brooks-Corey 곡선**
```csharp
float Se = (theta - theta_r) / (theta_s - theta_r);     // 유효 포화도
float suction = P_entry * pow(Se, -1f / lambda);        // 모세관 흡입력
float K = K_sat * pow(Se, 3f + 2f / lambda);            // 투수계수
```
**핵심: `K`가 `Se`의 3~5제곱.** 조금만 말라도 물 이동 능력이 폭락한다 —
이게 2단계로 넘어가는 물리적 이유이고, 지수 하나로 표현된다.

**수치 감각**: 잠재 증발량 여름 한낮 5~10 mm/day. 표면 1~2mm 층 기준 **수십 분**.
실제 모래성이 눈에 띄게 부슬거리기까지 더운 날 **30분~2시간**.
게임에서는 압축하되 **1단계는 빠르고 2단계는 느리다**는 비율을 유지할 것.

### 이 모델에서 저절로 나오는 것 (게임적으로 좋음)

- 양지쪽 벽이 먼저 무너짐 (증발률이 법선·그림자 의존)
- 첨탑이 기단보다 먼저 감 (표면적/부피 비)
- 그늘/북쪽 면이 오래 버팀 → 배치가 전략이 됨
- 물을 다시 부으면 강도는 회복되지만 **이미 무너진 형상은 안 돌아옴** → 유지보수 압박
- 마르면서 알베도↑ 러프니스↑ — 같은 `W` 하나로 구동

### E. 계보 / 보충

**고전 — 2단계 개념의 기원**

| 논문 | 내용 |
|---|---|
| Sherwood (1929), "The Drying of Solids", Ind. Eng. Chem. | 화학공학 원조. **등속/감속 건조기** 용어의 출처 |
| Philip & de Vries (1957), "Moisture movement in porous materials under temperature gradients", Trans. AGU | 액체+수증기+열 결합. 2단계의 이론적 토대 |
| Gardner (1958), "Some steady-state solutions of the unsaturated moisture flow equation...", Soil Science | 모세관 최대 공급률의 원조 |
| Gardner & Hillel (1962), "The relation of external evaporative conditions to the drying of soils", JGR | 1단계는 외부 조건이, 2단계는 토양이 속도를 결정 |
| Idso et al. (1974), "The three stages of drying of a field soil", SSSAJ | 3단계 버전. 3단계는 게임에서 무시 가능 |
| Hillel, *Introduction to Environmental Soil Physics* (교재) | 배경 부족하면 여기 먼저 |

**현대 — Dani Or 그룹 (ETH Zürich)**

| 논문 | 내용 |
|---|---|
| Shokri, Lehmann, Vontobel, Or (2008), "Drying front and water content dynamics during evaporation from sand delineated by neutron radiography", WRR | **중성자 촬영으로 모래 속 건조 전선을 직접 찍음.** 시각 레퍼런스로도 좋음 |
| Shokri & Or (2011), "What determines drying rates at the onset of diffusion controlled stage-2 evaporation in porous media?", WRR | 1→2단계 전환 시점과 속도 |
| Shahraeeni, Lehmann, Or (2012), "Coupling of evaporative fluxes from drying porous surfaces with air boundary layer", WRR | 공기 경계층 결합. `sun`, `windFactor` 항의 근거 |
| Lehmann & Or (2009), "Evaporation and capillary coupling across vertical textural contrasts in porous media", PRE | 입도가 다른 층이 겹칠 때 |

**지배 방정식**

| 논문 | 내용 |
|---|---|
| [Richards](terms/richards-equation.md) (1931), "Capillary conduction of liquids through porous mediums", Physics 1 | **Richards 방정식.** 불포화 흐름의 근본 PDE (Darcy의 불포화 확장) |
| Brooks & Corey (1964), "Hydraulic properties of porous media", CSU Hydrology Paper 3 | van Genuchten보다 단순한 멱함수. **게임에는 이쪽이 나을 수 있음** |
| Mualem (1976), "A new model for predicting the hydraulic conductivity of unsaturated porous media", WRR | van Genuchten과 짝 ("van Genuchten–Mualem") |

---

## F. 해변 지하수면 & swash 침투/유출

> 해변에서는 증발보다 **조석에 따른 지하수면 변동**이 모래 수분을 더 크게 지배할 때가 많다.
> 그리고 구현이 가장 쉽다 — 높이 하나를 시간에 따라 올렸다 내리면 된다.
> ⚠️ 이 절은 서지 검증이 가장 덜 됐다.

### 개념: exit point가 해변을 둘로 가른다

```
                       ┌── 상부: 불포화. 스며듦(influent)
                       │    백워시 약함 → 모래 쌓임 (accretion)
    ─ ─ ─ exit point ──┤
                       └── 하부: 포화. 배어나옴(effluent)
                            백워시 강함 → 모래 깎임 (erosion)
```

**게임 함의**: 조석에 따라 [exit point](terms/exit-point.md)가 이동 → 모래성 위치가 곧 전략.
낮은 곳 = 항상 최적 수분이나 파도에 깎임 / 높은 곳 = 안전하나 마름.
이게 실제 해안공학의 **[beach dewatering](terms/beach-dewatering.md) 공법**(지하수면을 낮춰 퇴적을 유도)의 원리이기도 하다.

### F1. Horn (2002)
**"Beach groundwater dynamics"** — Geomorphology 48

그래픽스가 아니라 **물리지리학 리뷰**. 알고리즘이 없고, 가져올 것은 **머릿속 모델과 닫힌 식 몇 개**.

> **해변은 바닷물이 드나드는 스펀지다. 그 스펀지 속 물 높이가 어디까지 차 있느냐가,
> 파도가 모래를 쌓을지 깎을지를 결정한다.**

#### 구조 — exit point가 해변을 가른다

```
   육지                                                  바다
 ┌──────────────────────────────────────────────────────────┐
 │░░░░░░░░░░░ 불포화 (마름) ░░░░░░░░░░░░░░                    │
 │~~~~~~~~~~~~~~~ 지하수면 ~~~~~~~~~~~~~~~◆                  │
 │▓▓▓▓▓▓▓▓▓ 포화 ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓ ╲ exit point       │
 │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓╲~~~~ 해수면 ~~~~  │
 └──────────────────────────────────────────────────────────┘
   ← influent 구역 ──┤◆├── effluent 구역 →
```

| 구역 | 위치 | 물의 방향 | 결과 |
|---|---|---|---|
| **influent** | exit point **위** | 파도 물이 모래로 **들어감** | 퇴적 |
| **effluent** | exit point **아래** | 지하수가 모래에서 **나옴** | 침식 |

그 사이의 물이 배어나오는 띠 = **[seepage face](terms/seepage-face.md)**. 실제 해변에서 물가 조금 위가
유난히 질척하고 반들거리는 구간.

#### 조석 응답 — 감쇠 + 위상 지연 (★ 닫힌 식)

조석이 안쪽으로 파고들며 **약해지고 늦어진다.** 선형화하면:

```
h(x, t) = h_mean + A · exp(−x/L) · sin(ωt − x/L)

L = √( 2·K·D / (n·ω) )      ← 감쇠 길이 (tidal decay distance)
```
- `K` 투수계수 (모래 10⁻⁴ ~ 10⁻³ m/s), `D` 대수층 두께, `n` 유효 공극률(~0.3)
- `ω` 조석 각진동수 (반일주조 12.42h → ~1.4×10⁻⁴ rad/s)

**실제 숫자**
```
굵은 모래 K=10⁻³, D=2m, n=0.3  →  L ≈ 10 m
고운 모래 K=10⁻⁴               →  L ≈ 3 m
```
→ **조석 영향 도달 거리 5~30 m.** 게임 스케일에 절묘하게 맞는다.
→ `x = L` 지점에서 지하수면 최고점이 만조보다 **약 2시간 늦게** 온다.
→ **시뮬레이션이 필요 없다. 위치와 시간만 넣으면 나온다. 비용 0.**

#### 비대칭과 초과높이

```
        ╱▔▔▔╲___
       ╱         ▔▔▔╲___          밀물: 해안선이 지나가며 직접 연결 → 순식간에 참
  ────╱───────────────────▔▔───   썰물: 연결 끊기고 seepage face로만 배출 → 느림
      평균 해수면
```
이 비대칭 때문에 시간 평균이 평균 해수면보다 위 = **[super-elevation](terms/super-elevation.md)**.

**주의: 위 선형 공식은 대칭이라 초과높이를 못 낸다.** 비선형성 + seepage face에서
나오는 현상이므로 게임에서는 **오프셋 항으로 더한다.**

#### ★ 침식/퇴적 — 메커니즘 3개가 서로 싸운다

**① 부피 손실** (가장 단순, 가장 강력)
```
uprush 100 올라옴 → 20이 스며듦 → backwash 80만 내려감
→ 내려가는 물이 약함 → 모래를 덜 끌고 감 → 퇴적
```

**② [유효 중량](terms/seepage-force.md) (seepage force)** — 물이 알갱이에 거는 항력. 방향이 결정적
- 하향 침투 → 알갱이를 눌러줌 → **무거워짐 → 안정화**
- 상향 유출 → 알갱이를 들어올림 → **가벼워짐 → 불안정화, 심하면 액상화**

**③ 경계층 두께** — **①②와 방향이 반대**
- 하향 침투 → 경계층 **압축** → 속도 기울기 가팔라짐 → 전단응력 ↑ → **불안정화**
- 상향 유출 → 경계층 **부풀음** → 전단응력 ↓ → **안정화**

|  | 침투 (물이 들어감) | 유출 (물이 나옴) |
|---|---|---|
| ② 유효 중량 | 눌러줌 → 안 움직임 | 들어줌 → 잘 움직임 |
| ③ 경계층 | 세게 긁음 | 살살 긁음 |
| **합** | **무겁지만 세게 긁힌다** | **가볍지만 살살 긁힌다** |

②와 ③이 정확히 반대라 **어느 쪽이 이기는지가 미결 문제.**
Butt et al. 2001의 제목이 *"onshore or offshore?"* 인 이유.

**입도가 결정한다 — 하지만 통설과 다른 이유로**

`K ∝ d²` ([Hazen](terms/hazen-relation.md)). 굵은 모래(1mm) → 고운 모래(0.15mm)면 K가 대략 **40~50배 하락**.

swash 한 주기(2~10초) 동안 실제로 스며드는 깊이 `≈ K·i·t` (i ~ O(1)):
```
굵은 모래  10⁻³ × 5s   = 5 mm     ← swash 수막 두께(10~50mm)의 유의미한 비율
고운 모래  2×10⁻⁵ × 5s = 0.1 mm   ← 사실상 0
```

→ **고운 모래에서는 ③이 이기는 게 아니라, ①②③이 전부 사라진다.**
   침투/유출 메커니즘 자체가 **굵은 해변의 현상**이다.
   ②vs③ 논쟁은 효과가 측정되는 중~조립 해변에서 벌어지는 것.

> ⚠️ **오해 주의 — "세립사는 물이 안 스민다"가 아니다.**
> 위 계산은 **swash 한 주기(초 단위)** 기준이다. 시간 스케일이 바뀌면 결론이 뒤집힌다.
>
> | | swash 5초 | 조석 반주기 6시간 |
> |---|---|---|
> | 조립사 K=10⁻³ | 5 mm | 수 m |
> | 세립사 K=5×10⁻⁵ | 0.25 mm | 1 m 이상 |
>
> 같은 모래인데 4,000배 시간 차이. **스펀지에 물을 끼얹느냐 담가두느냐의 차이.**
>
> **오히려 세립사 해변이 더 젖어 있다:**
> 1. 배수도 느리다 (`K`는 나가는 속도이기도 함) → 머금고 있음
> 2. [모세관 상승대](terms/capillary-fringe.md)가 `1/d`로 두껍다 — 조립 7cm / 중립 15cm / **세립 35cm** / 실트 1~3m
> 3. `L ∝ √K`라 지하수면이 조석을 못 따라감 → 높은 위치에 머물고 seepage face가 넓게 유지
>
> → 넓고 평평하고 젖은 모래가 거울처럼 단단한 해변(자전거가 다니는 그런 곳)이 세립사다.
>
> **세립사에서 죽는 것은 물이 아니라 동역학이다.** 지하수면도 있고 exit point도 있고
> 오히려 더 젖어 있지만, **파도 한 번이 아무것도 바꾸지 않고 조석에도 상태가 잘 안 변한다.**
> 게임에서 문제인 이유는 물리적 부정확성이 아니라 **플레이어에게 보여줄 변화가 없다는 것.**

→ 관측되는 지형과 일치: **자갈/굵은 모래 해변은 가파르고([reflective](terms/reflective-dissipative.md)),
   고운 모래 해변은 완만하다(dissipative).** (→ Masselink & Li 2001)

→ **게임 결정: 굵은/중간 모래로 설정하고 ①②만 구현. ③은 무시.** 물리적으로 방어 가능.

#### 논문이 지적하는 한계 — 우리에게는 면허

| 고전 해석해의 가정 | 실제 해변 |
|---|---|
| [Dupuit 가정](terms/dupuit-assumption.md) (수직 등전위선) | 해변면이 경사져서 깨짐 |
| 모세관 무시 | 모래 모세관 상승대가 수십 cm |
| seepage face 없음 | 존재하고, 배수를 지배 (초과높이의 원인) |
| 불포화대 무시 | 사실 여기가 대부분 |

또 **[swash zone](terms/swash-zone.md)에서 측정 자체가 어렵다**는 점을 강하게 지적. 데이터가 귀하다.
→ 정밀 수치해로 가봐야 얻을 게 적다 = **해석식 + 오프셋으로 충분하다는 면허.**

### F2. Horn (2006)
**"Measurements and modelling of beach groundwater flow in the swash-zone: a review"** — Continental Shelf Research 26

F1의 후속. swash zone에 초점.

### 고전 / 기초

| 논문 | 내용 |
|---|---|
| **Grant (1948), "Influence of the water table on beach aggradation and degradation", J. Marine Research** | **exit point 개념의 원조.** 지하수면이 해변의 퇴적/침식을 가른다 |
| **Nielsen (1990), "Tidal dynamics of the water table in beaches", WRR 26(9)** | **이 분야의 고전.** 지하수면 진동이 **비대칭** — 밀물에 빠르게 차고 썰물에 천천히 빠짐 → 평균보다 높아짐 |
| Turner (1993), "Water table outcropping on macro-tidal beaches: A simulation model", Marine Geology | 지하수면이 해변 표면과 만나는 지점의 모델 |
| Turner, Coates, Acworth (1997), "Tides, waves and the super-elevation of groundwater at the coast", J. Coastal Research 13(1) | **super-elevation** — 왜 해변 지하수면이 평균 해수면보다 높은가 |
| Raubenheimer, Guza, Elgar (1999), "Tidal water table fluctuations in a sandy ocean beach", WRR 35(8) | 실측 |
| Baird & Horn (1996), "Monitoring and modelling groundwater behaviour in sandy beaches", J. Coastal Research | 관측 + 모델 |

### 모세관을 포함한 것 (E절과 연결)

| 논문 | 내용 |
|---|---|
| **Li, Barry, Parlange, Pattiaratchi (1997), "Beach water table fluctuations due to wave run-up: Capillarity effects", WRR 33(5)** | **모세관 효과 포함.** E절의 불포화대 논의와 직접 연결 |
| Li & Barry (2000), "Wave-induced beach groundwater flow", Advances in Water Resources | 파랑 유도 지하수 흐름 |
| Nielsen & Perrochet (2000), "Watertable dynamics under capillary fringes: experiments and modelling", Advances in Water Resources | **모세관 상승대(capillary fringe).** 지하수면 위 수십 cm도 젖어 있는 이유 |
| Cartwright, Baldock, Nielsen 등 (2006), "Swash-aquifer interaction in the vicinity of the water table exit point on a sandy beach", JGR Oceans | exit point 주변의 swash-대수층 상호작용 |

### swash 침투/유출이 퇴적물에 미치는 영향 ★ 직접 관련

| 논문 | 내용 |
|---|---|
| **Packwood (1983), "The influence of beach porosity on wave uprush and backwash", Coastal Engineering** | 다공성이 uprush/backwash를 어떻게 바꾸는가. 원조 |
| **Turner & Masselink (1998), "Swash infiltration-exfiltration and sediment transport", JGR 103(C13)** | **②(유효 중량) vs ③(경계층)을 정량 비교하는 프레임워크.** 어느 게 언제 이기는지 |
| **Butt, Russell, Turner (2001), "The influence of swash infiltration–exfiltration on beach face sediment transport: onshore or offshore?", Coastal Engineering** | **정확히 필요한 메커니즘.** 스며들면 퇴적, 배어나오면 침식 |
| **Masselink & Li (2001), "The role of swash infiltration in determining the beachface gradient: a numerical study", Marine Geology** | **왜 굵은 모래 해변이 더 가파른가** — 침투 때문. 지형 생성의 근거 |
| Elfrink & Baldock (2002), "Hydrodynamics and sediment transport in the swash zone: a review and perspectives", Coastal Engineering | swash zone 종합 리뷰 |
| Masselink & Puleo (2006), "Swash-zone morphodynamics", Continental Shelf Research | 위의 후속 리뷰 |
| Turner & Leatherman (1997), "Beach dewatering as a 'soft' engineering solution to coastal erosion: A history and critical review", J. Coastal Research | 지하수면을 인위적으로 낮춰 퇴적 유도. 메커니즘의 실무 검증 |

### 패턴 형성 (선택)

| 논문 | 내용 |
|---|---|
| Werner & Fink (1993), "Beach cusps as self-organized patterns", Science | 물 빠질 때 생기는 규칙적 만곡 무늬 |
| Coco & Murray (2007), "Patterns in the sand: from forcing templates to self-organization", Geomorphology | 해변 자기조직화 패턴 리뷰 |

### 구현 (가장 쉬움)

```csharp
// ── 지하수면: 시뮬레이션 없이 해석식으로 (F1) ──────────────
float WaterTable(float distFromShore, float t)
{
    float L     = sqrt(2f * K * D / (n * omega));   // 감쇠 길이. 모래 기준 5~30 m
    float decay = exp(-distFromShore / L);
    float phase = TWO_PI * t / tidePeriod - distFromShore / L;   // 위상 지연

    return seaLevel
         + tideAmp * decay * sin(phase)             // 감쇠 + 지연 (선형해)
         + superElevation * decay;                  // 비대칭 초과높이 (경험적 오프셋)
}

// ── 수분 ─────────────────────────────────────────────
float wt = WaterTable(dist, time);

if (cellHeight < wt)
    W = 1.0f;                                              // 포화
else if (cellHeight < wt + capillaryFringe)
    W = max(W, 1.0f - (cellHeight - wt) / capillaryFringe); // 모세관 상승대

// ── exit point → 침식/퇴적 방향을 뒤집는다 ───────────────
bool isEffluent = (cellHeight < wt);

if (isEffluent)
{
    erosionRate *= effluentBoost;     // ② 유효 중량 감소 → 잘 깎임
}
else
{
    backwashVolume -= infiltrationRate * dt;   // ① 부피 손실 → 백워시 약화
    erosionRate    *= influentDamp;            // ② 유효 중량 증가 → 안정
}
// ③ 경계층 효과는 구현하지 않음 (굵은 모래 가정 — F1 참조)
```

---

## G. 파도

### G-0. "파도의 세기"는 네 가지가 섞인 말

| 양 | 의미 | 게임에서 |
|---|---|---|
| 파고 `H` | 파도 높이 | 시각적 크기 |
| **주기 `T`** | 마루 사이 시간 | **의외로 여기가 핵심** |
| **[처오름](terms/runup.md) `R`** | 물이 해변을 얼마나 올라오는가 | **모래성 생사를 가름** |
| [바닥 전단응력](terms/bed-shear-stress.md) `τ` | 알갱이를 실제로 끄는 힘 | 침식 속도 |

**파고가 크다고 잘 깎이는 게 아니다. 넷이 따로 논다.**

### G-1. 파도가 만들어지는 것 — 바람 × fetch × 지속시간

셋 중 가장 작은 것이 병목. 다 채워지면 **[완전발달해상](terms/fully-developed-sea.md)**이라 더 안 커진다.

```
JONSWAP 취송거리 제한 관계 (대략)
   파고   H_s ∝ U¹   · F^0.5
   주기   T_p ∝ U^⅓  · F^⅓
```
**주기는 파고보다 훨씬 느리게 자란다.** 국지 바람으로 [장주기파](terms/infragravity-wave.md)를 만들 수 없는 이유.

완전발달해상의 첨두 주기 (Pierson-Moskowitz):
```
T_p ≈ 0.75 ~ 0.8 × U        (U: m/s, T: s)

풍속 10 m/s → T_p ≈ 8 s
풍속 20 m/s → T_p ≈ 15 s     ← 큰 폭풍이라야 장주기가 나온다
```

### G-2. ★ 주기를 정하는 진짜 요인 = 폭풍까지의 거리

심해 [분산 관계](terms/dispersion-relation.md) `ω² = gk` 에서:
```
위상속도  C   = gT/2π ≈ 1.56 T   (m/s)
군속도    C_g = C/2   ≈ 0.78 T   (m/s)   ← 에너지가 실제로 이동하는 속도
```

**긴 파도가 더 빠르다.**
```
15초 파도 → C_g = 11.7 m/s = 42 km/h ≈ 1,010 km/일
 6초 파도 → C_g =  4.7 m/s = 17 km/h ≈   400 km/일
```

폭풍은 온갖 주기를 뒤섞어 만들지만, 수천 km를 이동하며 **스스로 정렬된다**([분산 정렬](terms/dispersive-sorting.md)).
긴 것이 먼저 도착하고 짧은 것이 뒤따른다. 짧은 파도는 감쇠도 빨라 도중에 사라진다.
→ **먼 폭풍의 너울은 깨끗하고 규칙적이고 주기가 길다.**

| 주기 | 정체 | 폭풍까지 거리 |
|---|---|---|
| 2 ~ 5 s | 잔물결 / 국지 [풍파](terms/wind-sea-vs-swell.md) | 바로 여기 |
| 5 ~ 8 s | 풍파 (wind sea) | 수십 ~ 수백 km |
| 9 ~ 12 s | 약한 너울 | 수백 ~ 1,500 km |
| 13 ~ 16 s | 너울 (swell) | 1,500 ~ 4,000 km (며칠 이동) |
| 17 ~ 22 s | 장주기 너울 | 5,000 km+ (일주일 이상) |

**해변에 서서 주기를 재면 폭풍이 얼마나 먼지 알 수 있다.**

> **주기는 해안으로 오면서 거의 안 변한다.** 천수·굴절이 파고와 방향과 파장을 바꿔도
> 주기는 보존된다. → **T는 먼 곳에서 오는 입력, H는 국지적으로 변형되는 값.**

**폭풍의 서명 (게임 메커니즘으로 직결)**
```
먼 폭풍   : 주기가 길게 시작 → 점점 짧아짐. 파고는 올랐다 내림. 하늘은 맑음
가까운 폭풍: 주기·파고가 같이 오름. 바람도 셈
```

### G-3. 해안으로 오면서 변형

- **[천수 변형](terms/shoaling.md)(shoaling)**: 수심이 얕아지면 느려지고 짧아지며 **높아진다**
- **굴절(refraction)**: 등수심선에 나란해지도록 휨 → **곶에 에너지 집중, 만에 분산**
- **★ [수심 제한 쇄파](terms/depth-limited-breaking.md)**: `γ = H_b/h_b ≈ 0.78`
  → **쇄파대 안에서는 파고를 심해 파고가 아니라 그 지점의 수심이 정한다.**
  ```
  H ≈ 0.78 × h        // 게임에서는 이 한 줄
  ```
  먼바다 3m 파도도 수심 50cm 지점에서는 약 40cm. **넓고 얕은 세립사 해변이 국소적으로 안전한 이유.**

**쇄파 유형 — [Iribarren](terms/iribarren-number.md) 수** `ξ = tanβ / √(H/L₀)`

| ξ | 유형 | 모습 | 해변 |
|---|---|---|---|
| < 0.5 | spilling | 거품 내며 서서히 무너짐 | 완만·세립사 |
| 0.5 ~ 3.3 | **plunging** | 말려서 통째로 꽂힘. **가장 격렬** | 중간 |
| > 3.3 | surging | 안 부서지고 그대로 밀고 올라옴 | 급경사·조립사 |

→ **우리 선택(중~조립사)은 plunging~surging 영역.** 해변면에서 직접 부서지는 격렬한 쪽.

### G-4. ★ 처오름 (runup) — 모래성 안전선

Hunt 공식:
```
수직 처오름   R = tanβ · √(H₀ · L₀)
수평 도달거리 X = R / tanβ = √(H₀ · L₀)      ← 경사가 상쇄된다

L₀ = gT²/2π ≈ 1.56 T²
∴  X ≈ 1.25 · √H₀ · T
```

**파고는 제곱근으로, 주기는 그대로 들어간다. 주기가 훨씬 세게 작용한다.**

| 심해 파고 | 주기 | 수평 도달거리 |
|---|---|---|
| 1 m | 6 s (풍파) | **7.5 m** |
| 1 m | 14 s (너울) | **17.5 m** |
| 2 m | 6 s | 10.6 m |

**2m 풍파보다 1m 너울이 더 멀리 온다.** 겉보기엔 잔잔한데 물이 훨씬 위까지 올라옴
→ 안전하다고 생각한 위치의 모래성을 먹는다. **게임 메커니즘으로 그대로 쓸 것.**

> Hunt 공식은 plunging/surging에 맞고 dissipative에서는 과대평가한다.
> 정확도가 필요하면 **Stockdon et al. (2006)** 의 경험식을 쓸 것.

**처오름 상승 ([wave setup](terms/wave-setup.md))**: 부서진 파도가 물을 밀어붙여 평균 수위 자체를 올린다.
```
η̄ ≈ 0.15 ~ 0.2 × H_b
```
**폭풍 + 만조 + 큰 너울**이 겹치면 전부 더해진다.

### G-5. 모래를 실제로 움직이는 것

```
τ = ½ ρ f_w u²
Shields:  θ = τ / ((ρs − ρ) g d)  >  θ_cr ≈ 0.05  이면 움직임
운반량 ∝ u³
```
**속도의 세제곱.** 유속 1.5배 → 운반량 3.4배.
게임 감각으로는 **임계점을 넘는 느낌**이 나야 맞다.

### G-5b. ★★ 폭풍이 쓸어간 모래는 어떻게 돌아오나

**쓸려가는 건 직관적인데 돌아오는 게 이상하다.** 물은 왔다 갔다 할 뿐인데
왜 모래가 한 방향으로 순 이동을 하는가.

#### 답: 파랑 비대칭 — 물은 제자리인데 모래만 간다
얕은 물에서 파도는 사인파가 아니다. 비선형이 되며 **마루는 좁고 높고 골은 넓고 얕아진다.**
```
    ╱╲            ╱╲          마루 밑: 빠르게 잠깐, 육지 방향
   ╱  ╲          ╱  ╲
──╱────╲────────╱────╲──
        ╲______╱                골 밑: 느리게 오래, 바다 방향
```
**물의 순 이동은 0이다**(질량 보존). 그런데 **운반량은 `u³`** 이라:
```
육지 방향:  u=1.0, 지속 1  →  1.0³ × 1 = 1.00
바다 방향:  u=0.5, 지속 2  →  0.5³ × 2 = 0.25

물의 순 이동   1.0×1 − 0.5×2 = 0       ← 제자리
모래의 순 이동  1.00 − 0.25   = +0.75   ← 육지 방향
```
**세제곱이라 빠른 쪽이 압도적으로 이긴다.** 이를 **속도 왜도(velocity skewness)** 라 하고,
이게 회복의 주 동력이다.
> 부서지기 직전엔 파형이 **톱니**가 되어(앞면 가파름) 가속도도 육지 방향으로 작용한다
> (**가속도 왜도**). 이차적이지만 실측된다.

#### 그럼 폭풍 때는 왜 반대인가 — undertow(저층 이안류)
파랑 비대칭이 사라지는 게 아니라 **더 강한 반대 힘이 생긴다.**
```
────→ ────→ ────→     표면: 육지 방향 (쇄파가 밀어줌)
~~~~~~~~~~~~~~~~~~
←──── ←──── ←────     바닥: 바다 방향 (되돌아가는 흐름)
▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓      ← 모래는 여기 있다
```
**모래는 바닥에 있어 바다 방향 흐름을 정통으로 맞는다.** 게다가 진동이 아니라
**한 방향으로 계속 흐르는 정상류**라 훨씬 세다. 큰 파도가 모래를 오래 띄워놓는 것도 겹친다.

| | 쇄파 강도 | undertow | 승자 | 결과 |
|---|---|---|---|---|
| **폭풍** | 강함 | **강함** | undertow | **침식.** 앞바다로 → [사주](terms/offshore-bar.md) 형성 |
| **잔잔** | 약함 | 약함 | **파랑 비대칭** | **회복.** 사주가 육지로 기어 올라와 해변에 붙음 |

여기에 [침투 효과](#f-해변-지하수면--swash-침투유출)(uprush가 스며들어 backwash 약화)가
해빈면에서 한 번 더 육지 방향으로 밀어준다.

#### [Dean 수](terms/dean-number.md)로 정리하면 = 계절 순환
```
Ω = H / (w_s·T)

Ω 작음 (작은 파고, 긴 주기, 굵은 모래)  →  육지 방향  →  퇴적
Ω 큼   (큰 파고, 짧은 주기, 고운 모래)  →  바다 방향  →  침식

여름 → 긴 너울, 작은 파고 → Ω 작음 → 회복.  넓고 범이 있는 "여름 해빈"
겨울 → 폭풍, 큰 파고      → Ω 큼   → 침식.  좁고 앞바다에 사주가 있는 "겨울 해빈"
```

#### ⚠ 시간이 비대칭이다
```
침식:  폭풍 한 번 — 수 시간 ~ 며칠
회복:  수 주 ~ 수 개월
```
**잃는 건 순식간, 되찾는 건 아주 오래.** 침식은 폭풍의 격렬한 에너지를 쓰고,
회복은 작은 파도의 **미약하지만 꾸준한 편향**에 의존한다. 일종의 래칫.
**게임적으로 좋은 성질 — 한 번의 재난이 오래 남는다.**

#### 바람과 사구 = 더 긴 주기의 저수지
```
평상시 → 바람이 마른 해변 모래를 사구로 옮김      (저축)
폭풍시 → 파도가 사구를 깎아 해변·앞바다로 되돌림   (인출)
```
사구를 없애면(개발) 해변이 폭풍에 훨씬 취약해지는 이유.

#### 안 돌아오기도 한다 — 한계수심(depth of closure)
보통 수심 5~10m. **그보다 깊이 나간 모래는 안 돌아온다**(파도가 바닥까지 못 미침).
폭풍이 회복할 틈 없이 연달아 오면 **순 손실**이 쌓이고, 그게 장기 해안 침식이다.

#### ⚠ 우리 시뮬에서 저절로 나올까 — 아마 아니다
경계 조건에 순수 사인파를 넣으면 비대칭이 안 생긴다. 가상 파이프가 얕은 물 비선형성을
어느 정도 만들지만 **실제 파랑 왜도를 재현할지는 검증이 필요하다.**

| 선택지 | 평가 |
|---|---|
| ① `u³`를 쓰고 기대한다 | 비대칭이 조금이라도 생기면 증폭됨. 프로토타입에서 확인할 가치 |
| ② 경계 조건을 비대칭 파형으로 | 물리적으로도 옳다(천수 변형 결과를 미리 넣는 것) |
| **③ ★ 명시적 편향 항** | **가장 안전. 회복은 게임 리듬을 만드는 요소라 확실히 통제되어야 한다** |

```csharp
// 파랑 왜도를 파라미터화 — 파도가 작을수록 육지 방향 편향이 강해진다
float onshoreBias = skewnessCoef * (1f - saturate(Hs / stormThreshold));
```
**창발에 맡겼다가 안 나오면 폭풍 이벤트가 그냥 영구 파괴가 된다.**

| 문헌 | |
|---|---|
| **Wright & Short (1984), Marine Geology 56** | 해빈 상태 모델 (G-7b에도) |
| **Hoefel & Elgar (2003), "Wave-induced sediment transport and sandbar migration", Science 299** | **가속도 왜도가 사주를 육지로 민다** |
| Gallagher, Elgar, Guza (1998), "Observations of sand bar evolution on a natural beach", JGR | undertow에 의한 바다 방향 이동 |
| Dean (1991), J. Coastal Research | 평형 프로파일, Dean 수 |
| Roelvink et al. (2009), "Modelling storm impacts on beaches, dunes and barrier islands", Coastal Engineering | **XBeach** — 폭풍 침식 모델 |

### G-6. 게임 구현 — 입력 3개, 공식 4줄

층 1·2를 시뮬레이션할 필요 없다.

```csharp
// ── 입력: "날씨" 파라미터 3개 ──────────────────────
float H0;      // 심해 파고 (m)
float T;       // 주기 (s)   ← 너울/풍파를 가르는 값. 먼 폭풍의 서사
float dir;     // 파향

// ── 파생 ─────────────────────────────────────────
float L0          = 1.56f * T * T;
float swashReach  = sqrt(H0 * L0);          // ★ 수평 도달거리 = 모래성 안전선 (UI에 선으로)
float runup       = tanBeta * swashReach;
float setup       = 0.18f * breakerHeight;
float seaLevelEff = seaLevel + tide + setup;

// ── 하이트필드 경계조건(D절)에 주입 ───────────────
float surface = seaLevelEff + 0.5f * H0 * sin(TWO_PI * time / T);
if (isOceanBoundary) d = max(0, surface - b);

// ── 쇄파대 안에서는 수심이 파고를 정한다 ──────────
waveHeight = min(shoaledHeight, 0.78f * depth);

// ── 침식 ────────────────────────────────────────
erosionRate ∝ pow(length(v), 3);
```

`swashReach` 하나가 **"물가에서 몇 미터 위에 지어야 안전한가"** 를 직접 알려준다.

### G-6b. ★ 파도의 불규칙성 — 랜덤을 만들 필요가 없다

실제 바다는 파도 하나가 아니라 **주파수가 다른 사인파 수십 개의 중첩**이다.
```
η(t) = Σ aᵢ·cos(ωᵢt + φᵢ)      aᵢ는 스펙트럼에서, φᵢ만 랜덤
```

**① 파고가 Rayleigh 분포를 따른다** — 자동으로
```
P(H > h) = exp(−2·(h/H_s)²)
```
| | |
|---|---|
| 평균 파고 | **0.63 × H_s** |
| 상위 1/10 평균 | 1.27 × H_s |
| H_s 초과 비율 | **13.5%** |
| 1.5·H_s 초과 | 1.1% |
| 2·H_s 초과 | 0.034% (**약 3000파도에 1번**) |
| N파도 중 최댓값 | ≈ H_s·√(ln N / 2) → 1000파도면 1.86 H_s |

**[유의파고](terms/significant-wave-height.md) 1m여도 가끔 2m가 온다.** 주기 8초면 3000파도 ≈ 7시간.
→ "가끔 세게 온다"는 **이벤트를 짤 필요가 없다.**

**② [파군](terms/wave-group.md)(set)** — 주파수가 살짝 다른 둘이 겹치면 맥놀이
```
cos(ω₁t)+cos(ω₂t) = 2·cos((ω₁−ω₂)t/2)·cos((ω₁+ω₂)t/2)
                        ↑ 느린 포락선 = 파군      ↑ 개별 파도
```
**세트가 뚜렷한 정도 = 스펙트럼 폭.** JONSWAP의 첨두 강조 계수 `γ`:
```
γ = 1     Pierson-Moskowitz. 넓음. 완전발달 풍파 → 어수선
γ = 3.3   표준
γ = 7+    매우 좁음. 먼 너울 → 세트가 극적
```
"너울은 규칙적, 풍파는 어지럽다"가 **γ 하나로 표현된다.**

#### ★ 왜 해변 모양이 그대로인데 도달 위치가 크게 달라지나
**파고 변동 때문이 아니다.** `X ∝ √H` 라서 파고 2배여도 도달거리는 1.41배뿐 —
제곱근이 변동을 오히려 눌러준다.

**진짜 이유: 파도가 올라타는 기준 수위 자체가 계속 움직인다.**
```
도달 위치 = [ 기준 수위 ] + [ 개별 파도의 처오름 ]
              ↑ 크고 느리게 변동    ↑ √H 라 변동이 눌림
```

| 기준 수위를 흔드는 것 | 내용 |
|---|---|
| **① wave setup** | `η̄ ≈ 0.18·H_b`. **세트일 때 파고가 크니 setup도 커짐** → 바닥이 통째로 상승 |
| **② 장주기파 (IG)** | 파군이 만드는 20~200초 파동. 물가 수위를 **천천히 크게** 올렸다 내림 |
| **③ swash 중첩** | 앞 파도 backwash와 정면 충돌하면 죽고, 이미 빠졌으면 멀리 감 |

**①②가 같은 타이밍에 겹친다.** 큰 세트가 올 때 IG 수위도 이미 올라가 있어서
큰 파도가 높아진 수면에서 출발 → 도달거리가 곱절로 늘어난다.

> ③은 **우리 하이트필드 시뮬이 이미 처리한다.** 가상 파이프에 관성이 있어
> backwash가 `d`와 `f`에 남아 있고 들어오는 물과 자연스럽게 충돌한다. 만들 게 없다.

#### 구현 — 랜덤은 초기 위상 N개뿐
```csharp
// ── 초기화 (한 번) ─────────────────────────
const int N = 32;                    // 게임엔 20~50이면 충분
for (int i = 0; i < N; i++)
{
    // ⚠ 대역 안에서 흔든다 (아래 함정)
    f[i]     = fMin + (fMax - fMin) * (i + Random(0f, 1f)) / N;
    a[i]     = sqrt(2f * JonswapSpectrum(f[i], Hs, Tp, gamma) * df);
    phase[i] = Random(0f, TWO_PI);   // ★ 랜덤은 여기 딱 한 번
}

// ── 매 프레임: 경계 셀 수면 ────────────────
float surface = seaLevelEff;
for (int i = 0; i < N; i++)
    surface += a[i] * sin(TWO_PI * f[i] * time + phase[i]);

if (isOceanBoundary) d = max(0f, surface - b);
```

자동으로 나오는 것: Rayleigh 분포 / 파군 / setup / 장주기파 / swash 중첩.

> **⚠ 함정 — 반복 주기.** 주파수를 정확히 등간격으로 잡으면 전체 패턴이
> `N/(fMax−fMin)` 주기로 **정확히 반복된다.**
> `fMin=0.05, fMax=0.25, N=32` → **160초마다 똑같은 파도.** 플레이어가 눈치챈다.
> → 각 대역 안에서 주파수를 **흔들면([jitter](terms/jitter.md))** 반복 주기가 사실상 무한. 표준 트릭.

#### 날씨 파라미터 4개
| 파라미터 | 의미 | 게임에서 |
|---|---|---|
| `Hs` 유의파고 | 전체 세기 | 위험도 |
| `Tp` 첨두 주기 | **폭풍까지의 거리** | 처오름 거리에 선형 |
| **`gamma` 스펙트럼 폭** | **너울 ↔ 풍파** | **세트가 뚜렷한가** |
| `dir` 파향 | 방향 | 굴절, 사면 이동 |

**`gamma`가 수확.** 같은 `Hs`, `Tp`라도:
```
γ 높음 → 잔잔하다 확 몰려옴 → 리듬을 읽을 수 있음 (긴장/이완)
γ 낮음 → 계속 어수선함      → 읽을 수 없음, 지속적 압박
```
**같은 위험도인데 체감이 완전히 다르다.** 스테이지 성격을 가르는 손잡이.

### G-6c. ⚠ γ는 지역 상수가 아니다 — 이봉 스펙트럼으로 갈 것

**γ의 정체**: JONSWAP은 **북해에서 자라고 있는 풍파**를 맞춘 스펙트럼이고,
γ는 "이 풍파가 얼마나 덜 자랐나"를 나타낸다.
```
γ 5~7  아직 자라는 중. 에너지가 한 주파수에 몰림
γ 3.3  북해 평균
γ 1    완전발달(Pierson-Moskowitz). 다 자라서 퍼짐
```
→ **"지역"이 아니라 "바람이 분 시간과 거리"의 함수.**

**더 큰 문제: 너울은 JONSWAP으로 표현이 안 된다.**
```
풍파:  ╱▔╲___          γ로 봉우리 조절 가능
너울:    ▏             γ를 10, 50으로 올려도 이만큼 못 좁힘
      ___▏▏___
```
수천 km 분산 정렬된 너울은 주파수 폭이 몇 % 수준. **애초에 다른 종류의 스펙트럼.**

#### 지역차를 만드는 것 = 풍파와 너울의 비율

| 지역 | 특징 | 너울 비율 | 체감 |
|---|---|---|---|
| **태평양 섬** (하와이) | 사방 수천 km 외양. **대륙붕이 없어 감쇠 없이 도달** | **90%+** | 세트가 극적 |
| 외양 노출 (캘리포니아·호주·포르투갈) | 외양 + 오후 국지 바람 | 60~80% | 세트 있되 섞임 |
| **갇힌 바다** (북해·지중해·발트해) | fetch 짧아 **긴 너울이 아예 안 생김** | **10% 이하** | 계속 어수선 |
| **동해** | 반쯤 갇힘. fetch 수백 km | 20~40% | 보통. 가끔 오는 너울이 예외적 |
| 서해 | fetch 매우 짧고 얕음 | ~0 | **조석이 파도보다 지배적** |
| 제주·남해 | 태풍 너울에 노출 | 40~60% | 계절 편차 큼 |
| 카리브·걸프 | 막혀 있음 | 낮음 | 평소 잔잔 |

> 동해안 **"너울성 파도" 경보**가 따로 있는 이유: 평소 풍파 지역이라 너울이 드물고,
> 그래서 왔을 때 못 읽는다. "겉보기엔 잔잔한데 물이 훨씬 위까지 올라옴"이 실제 사고로 이어짐.

**지역차의 원인 3가지**: ① [취송거리](terms/fetch.md)(갇힌 바다는 긴 주기 불가)
② 외양 노출도(먼 폭풍 너울이 올 열린 방향) ③ **대륙붕 폭**(넓고 얕으면 걸러짐).
하와이가 하와이인 건 ③ 때문 — 대륙붕이 없다.

#### ★ 권장: 이봉(bimodal) 스펙트럼
실제 해양 모델(WAVEWATCH III 등)과 공학 실무의 방식.

**스펙트럼이란**: 어느 주파수에 에너지가 얼마나 있는가. 오디오 스펙트럼 애널라이저와 같은 개념.
가로축 주파수, 세로축 에너지 밀도, **면적이 총 에너지**. `f = 1/T` 이므로
**긴 주기 = 낮은 주파수 = 왼쪽.**

```
   S(f)
    │       ▏
    │      ╱▏╲          ╱▔╲
    │     ╱ ▏ ╲        ╱    ╲
    │    ╱  ▏  ╲      ╱      ╲___
    └───┴─────────┴──┴────────────→ f (Hz)
         너울            풍파
       0.067 Hz        0.17 Hz
       (T = 15초)      (T = 6초)
       좁고 뾰족        넓고 완만
```

**왜 봉우리가 둘인가 — 출처가 다른 파도라서.**
너울은 3일 전 5000km 밖 폭풍이 보낸 것, 풍파는 지금 여기 부는 바람이 만드는 것.
**서로 아무 관계 없이 같은 자리에 동시에 도착해 겹쳐 있을 뿐.**
(실제 바다는 삼봉·사봉도 흔하다. 게임엔 둘이면 충분)

**★ 왜 중요한가 — 처오름이 주기에 선형이라서** (`X ≈ 1.25·√H·T`)
```
너울 성분의 파도 (T=15s) → 훨씬 멀리 처오름
풍파 성분의 파도 (T=6s)  → 짧게 그침
```
**같은 물가에서 어떤 파도는 두 배 멀리 온다.**
단봉이면 모든 파도의 주기가 고만고만해서 도달거리도 고만고만 — 밋밋해진다.

**구현 — S(f) 함수만 갈아끼우면 된다. 합성 코드는 그대로.**
```csharp
float Spectrum(float f)
{
    float wind  = Jonswap(f, TpWind,  gamma: 3.3f);
    float swell = Jonswap(f, TpSwell, gamma: 20f);   // 아주 좁게
    return (1f - swellFraction) * wind + swellFraction * swell;
}
// a[i] = sqrt(2f * Spectrum(f[i]) * df);   ← 이 줄은 안 바뀜
```

**유의파고는 제곱합으로 더해진다** (에너지가 더해지므로)
```
Hs_total = √(Hs_풍파² + Hs_너울²)      너울 1m + 풍파 1m = 1.41m (2m 아님)

swellFraction 은 에너지 비율:  Hs_너울 = Hs_total · √swellFraction
  0.9 → 너울 0.95·Hs, 풍파 0.32·Hs   (하와이)
  0.3 → 너울 0.55·Hs, 풍파 0.84·Hs   (동해)
```

**γ는 3.3 고정하고 손대지 말 것.** 지역 파라미터는 둘만:
```csharp
float swellFraction;   // 0~1. 하와이 0.9 / 북해 0.05 / 동해 0.3
float swellTp;         // 너울 주기(s). 하와이 16 / 동해 11
```
**파라미터 2개로 지역 성격이 다 나오고, 물리적으로도 정확하다.**

> **실제로 보는 법**: 서핑 예보 사이트(Surfline, Windy)가 보여주는 게 정확히 이것.
> `Primary swell 15s SW 1.2m / Secondary 9s NW 0.5m / Wind swell 5s N 0.4m`
> — 봉우리를 하나씩 따로 표시한 것. 하와이와 동해를 나란히 보면 감이 잡힌다.

**덤 — 방향 분산**: 너울은 한 방향(10~20°) → 파도가 나란한 선으로 깨끗하게 들어옴.
풍파는 사방(30~60°) → 어지럽게 교차. 같은 `swellFraction`으로 함께 구동하면
"깨끗한 파도 ↔ 지저분한 파도"가 시각적으로 갈린다.

### G-7. 파랑 스펙트럼 문헌 (γ의 계보)

**읽는 순서**
```
1. Horvath 2015        ← 그래픽스 관점. 바로 구현에 연결됨. 1번만 읽어도 구현 가능
2. Holthuijsen 2007    ← 필요한 장만. 스펙트럼 / 통계 / 천해 변형
3. Hasselmann 1973     ← γ의 원전. 공식 확인용
4. Longuet-Higgins 1952 + 1962   ← Rayleigh 분포 + surf beat
5. Ochi & Hubble 1976  ← 이봉으로 갈 때
```

#### ★ 우선순위 둘
**Horvath (2015), "Empirical Directional Wave Spectra for Computer Graphics", DigiPro 2015**
Pixar. **해양학의 실제 스펙트럼(PM, JONSWAP, TMA, 방향 분산)을 CG에서 제대로 쓰는 법.**
기존 CG 물이 Tessendorf의 단순화 스펙트럼을 관성적으로 쓰는 걸 지적하고 대안을 준다.
**해양학과 그래픽스 사이의 다리 역할을 하는 거의 유일한 논문.**

**Holthuijsen (2007), *Waves in Oceanic and Coastal Waters*, Cambridge Univ. Press**
논문이 아니라 교재. **이 분야의 현대 표준**이고 읽기 쉽다.
G절에서 다룬 내용의 90%가 여기 있다.

#### 원전 — Hasselmann et al. (1973)
**"Measurements of wind-wave growth and swell decay during the Joint North Sea Wave Project (JONSWAP)"**
Deutsche Hydrographische Zeitschrift, Ergänzungsheft Reihe A(8), Nr. 12

1968~69 북해 질트섬 앞바다 공동 관측(저자 16명).
**발견**: 취송거리 제한 스펙트럼은 완전발달보다 봉우리가 훨씬 뾰족하다 → 보정 인자 γ.

```
S(f) = (α g²/(2π)⁴) · f⁻⁵ · exp[−(5/4)(fp/f)⁴] · γ^r
       └───────── Pierson-Moskowitz 부분 ─────────┘  └보정┘

r = exp[ −(f − fp)² / (2σ²fp²) ]
σ = 0.07 (f ≤ fp),  0.09 (f > fp)
γ = 3.3 (평균), 실측 범위 1~7
```
**γ=1이면 정확히 Pierson-Moskowitz로 돌아간다.** γ가 "얼마나 덜 자랐나"의 척도인 이유.

> **구현 팁 — α는 무시할 것.** 취송거리 함수라 성가시다.
> 모양만 계산하고 목표 `Hs`에 맞춰 정규화하는 게 훨씬 편하다:
> ```csharp
> // α=1로 모양만 계산 → m0 = ∫S(f)df 수치적분 → 스케일
> float scale = (Hs/4f)*(Hs/4f) / m0;    // Hs = 4√m0
> ```

#### 앞뒤 맥락
| 논문 | 내용 |
|---|---|
| **Phillips (1958), "The equilibrium range in the spectrum of wind-generated waves", JFM 4** | JONSWAP 식의 **`f⁻⁵` 꼬리**의 출처. 평형 영역 이론 |
| **Pierson & Moskowitz (1964), "A proposed spectral form for fully developed wind seas...", JGR 69** | **γ=1 기준선.** 북대서양 기상관측선 데이터 |
| Kitaigorodskii (1962) | 위 둘의 바탕이 된 차원해석/상사이론 |
| Donelan, Hamilton, Hui (1985), "Directional spectra of wind-generated waves", Phil. Trans. R. Soc. A 315 | 꼬리가 `f⁻⁵`가 아니라 **`f⁻⁴`** 라는 반론 + 방향 분산 |

> 꼬리 지수 논쟁(f⁻⁵ vs f⁻⁴)은 실제로 미결. **게임에서는 차이가 안 보이니 JONSWAP 그대로.**

#### G절의 나머지 내용들의 출처
| 우리가 쓴 것 | 출처 |
|---|---|
| **파고 Rayleigh 분포** (13.5%, 2Hs 확률 등) | **Longuet-Higgins (1952), "On the statistical distribution of the heights of sea waves", J. Marine Research 11** |
| **wave setup** | **Longuet-Higgins & Stewart (1964), "Radiation stresses in water waves", Deep-Sea Research 11** — 복사응력의 원전 |
| **장주기파 / surf beat** | **Longuet-Higgins & Stewart (1962), "Radiation stress and mass transport in gravity waves, with application to 'surf beats'", JFM 13** ← **"왜 도달거리가 크게 달라지나"의 이론적 근거** |
| surf beat 최초 관측 | Munk (1949), "Surf beats", Trans. AGU 30 / Tucker (1950), Proc. R. Soc. A |
| 주기-진폭 결합분포 | Longuet-Higgins (1975), JGR 80 |

**Longuet-Higgins가 계속 나온다.** 이 분야를 사실상 혼자 세운 사람.

#### 이봉 스펙트럼 (`swellFraction` 방식의 근거)
| 논문 | 내용 |
|---|---|
| **Ochi & Hubble (1976), "Six-parameter wave spectra", ICCE** | **이봉 스펙트럼의 고전.** 풍파 + 너울 두 봉우리 |
| Torsethaugen & Haver (2004), "Simplified double peak spectral model for ocean waves", ISOPE | 노르웨이 해역 기반. 해양공학 표준 |
| Guedes Soares (1984), "Representation of double-peaked sea wave spectra", Ocean Engineering 11 | 이봉 표현법 |

#### 얕은 물 / 방향 분산
| 논문 | 내용 |
|---|---|
| Bouws, Günther, Rosenthal, Vincent (1985), "Similarity of the wind wave spectrum in finite depth water", JGR 90 | **TMA 스펙트럼.** JONSWAP의 유한수심 확장. 엄밀히는 해변에 이게 맞으나, 쇄파대 안은 `H=0.78h`로 수심이 지배하므로 **경계조건은 JONSWAP으로 충분** |
| **Mitsuyasu et al. (1975), "Observations of the directional spectrum of ocean waves using a cloverleaf buoy", JPO 5** | **`cos^2s` 방향 분산 함수**의 표준 출처 |

### G-7b. 해안공학 문헌 (그 외)

| 문헌 | 내용 |
|---|---|
| **Stockdon, Holman, Howd, Sallenger (2006), "Empirical parameterization of setup, swash, and runup", Coastal Engineering 53** | **처오름 현대 표준 경험식** |
| Hunt (1959), "Design of seawalls and breakwaters", ASCE J. Waterways & Harbors | 단순 처오름 공식 `R = ξH₀` |
| Battjes (1974), "Surf similarity", ICCE | Iribarren 수 / 쇄파 유형 분류 |
| **Snodgrass, Groves, Hasselmann, Miller, Munk, Powers (1966), "Propagation of ocean swell across the Pacific", Phil. Trans. R. Soc.** | **태평양을 가로지르는 너울 추적.** 분산 정렬의 실증 |
| Munk et al. (1963), "Directional recording of swell from distant storms", Phil. Trans. R. Soc. | 위의 선행 연구 |
| Bailard (1981), "An energetics total load sediment transport model for a plane sloping beach", JGR | 퇴적물 운반 (에너지론) |
| [Shields](terms/shields-parameter.md) (1936) | 이동 한계 |
| **Soulsby (1997), *Dynamics of Marine Sands*** | **실무 핸드북. 구현할 때 제일 유용** |
| **Coastal Engineering Manual (CEM)**, US Army Corps | **무료 공개.** Part II가 파랑역학. 공식 모음집 |
| Goda (2010), *Random Seas and Design of Maritime Structures*, 3rd ed. | 해안공학 실무 표준 |
| Young (1999), *Wind Generated Ocean Waves* | 파랑 생성 전문 |
| Komar, *Beach Processes and Sedimentation* | 표준 교재 |

### G-8. 그래픽스 — 파도 렌더링/시뮬레이션

| 논문 | 내용 |
|---|---|
| Tessendorf, "Simulating Ocean Water" | FFT 스펙트럼. UE5 Water 플러그인의 뿌리 |
| **Yuksel, House, Keyser (2007), "Wave Particles", SIGGRAPH** | 장애물(모래성) 주변 반사/회절을 싸게. **게임에 매우 적합** |
| Thürey, Müller-Fischer, Schirm, Gross (2007), "Real-time Breaking Waves for Shallow Water Simulations", Pacific Graphics | 얕은 물 + 부서지는 파도 |
| Jeschke & Wojtan (2017), "Water Wave Packets", SIGGRAPH | 현대적 파동 표현 |
| Jeschke et al. (2018), "Water Surface Wavelets", SIGGRAPH | 위의 후속 |
| Chentanez & Müller (2010), "Real-time Eulerian water simulation using a restricted tall cell grid", SIGGRAPH | 실시간 3D 물 |
| Canabal et al. (2016), "Dispersion Kernels for Water Wave Simulation", SIGGRAPH Asia | 분산 관계 |

---

## H. 풍성 모래 이동 (aeolian) & 구역별 입도

> 1940년대에 물리가 거의 정리된, 잘 닦인 분야.
> **핵심: 젖은 모래는 안 날린다 → 우리 `W` 필드가 그대로 게이트가 된다.**

### 딱 두 개
**Bagnold (1941), *The Physics of Blown Sand and Desert Dunes*, Methuen**
이 분야의 원전이자 아직도 현역. 1930년대 리비아 사막을 차로 돌던 영국 군인이 쓴 책.
80년이 지나도 모든 논문이 인용한다. 읽기도 좋다.

**Kok, Parteli, Michaels, Karam (2012), "The physics of wind-blown sand and dust", Reports on Progress in Physics 75**
현대 종합 리뷰. Bagnold 이후 밝혀진 것이 다 정리돼 있다.

### H-1. 핵심 개념 4개

**① 임계 마찰속도** `u*t = A·√((ρs−ρ_air)/ρ_air · g · d)`, A ≈ 0.1

| 입도 | u*t | 10m 풍속 |
|---|---|---|
| 0.25 mm | 0.23 m/s | 약 **8 m/s** |
| **0.5 mm (우리 값)** | **0.33 m/s** | 약 **11.5 m/s** |
| 1.0 mm | 0.47 m/s | 약 16 m/s |

**히스테리시스**: 유체 임계(시작) > 충격 임계(유지, 약 80%).
튀는 알갱이가 다른 알갱이를 때려서 **더 약한 바람에서도 계속된다.**
→ 시작은 어렵고 한번 터지면 잘 안 멈춤. 게임 긴장 곡선에 좋다.

**② ★ 모래는 뜨지 않는다. 튄다**

| 모드 | 대상 | 비중 |
|---|---|---|
| **도약 (saltation)** | 모래. 탄도 궤적 | **약 75%** |
| 포행 (creep) | 굵은 알갱이. 충격에 밀려 굴러감 | 약 20~25% |
| 부유 (suspension) | **실트·먼지(<0.07mm)만** | 모래는 해당 없음 |

```
   바람 →
        ↗‾‾‾‾‾╲            급하게(50~60°) 떠서
      ↗         ╲___        완만하게(10~15°) 착지
   ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓        높이 수 cm, 길이는 그 10~20배
```
**모래는 대부분 지면 5cm 이내. 강풍에도 1m를 잘 안 넘는다.**
→ 영화의 "모래 안개"는 실은 **먼지(실트)**. VFX를 지면에 붙은 얇은 층으로 만들 것.

**③ 이동량 `q ∝ u*³`** — 물의 침식과 똑같은 세제곱 법칙.
(Bagnold 이후 Kawamura 1951, Owen 1964, Lettau 1978, Sørensen 2004이 계수를 다듬음. 지수는 그대로)

**④ 최적 입도 — 또 U자 곡선**
```
 필요 풍속
   │╲                        ╱
   │ ╲ 점착력(실트·점토)   ╱  무게(조립사)
   │      ╲______╱
   └────┴──────┴──────┴──→ 입도
       0.05  0.08~0.1  0.5mm
             └ 가장 잘 날림 ┘
```
**[Hjulström 곡선](glossary.md)과 판박이.** 매질만 물에서 공기로 바뀐 것.

### H-2. ★ 수분 — 우리 `W`와 직결

모세관 다리가 알갱이를 붙잡아 임계 마찰속도를 올린다. **점착력과 같은 물리.**
```
수분 1~2%  → 임계가 눈에 띄게 상승
수분 5% 이상 → 사실상 정지
```
**표면 몇 알갱이 층만 관여한다.** 속은 젖어도 표면만 마르면 날린다.
→ **간헐적**. 표면이 부분부분 마르며 여기저기서 시작됐다 멈췄다 한다.

| 문헌 | 내용 |
|---|---|
| **Hotta, Kubota, Katori, Horikawa (1984), "Sand transport by wind on a wet sand surface", ICCE** | 젖은 표면 임계 마찰속도. 표준 참조 |
| **McKenna Neuman & Nickling (1989)**, Canadian J. Soil Science | 모세관수의 비산 억제 이론 + 풍동실험 |
| Belly (1964), "Sand movement by wind", US Army CERC | 고전 |
| Namikas & Sherman (1995) | 리뷰 |
| **Wiggs, Baird, Atherton (2004)** / **Darke & McKenna Neuman (2008)** | **F절에 이미 있는 것들** |

> **조사가 한 바퀴 돌아 만난 지점.** F절(지하수면)·E절(증발)에서 나온 해변 수분 연구자들이
> 사실 **바람에 모래가 날리는 걸 연구하던 사람들**이었다. 젖은 모래는 안 날리니까.

### H-3. 해변 특유 — 취송거리(fetch) 효과

사막과 다르다. **해변은 공급이 제한된 환경.**
```
   바다 →→→ 바람 →→→ 육지
   젖은 하부 해변      마른 상부 해변
   (공급 없음)        (여기부터 이동량이 쌓임)
```
- **직각으로 부는 바람** → 마른 구간을 짧게 지남 → 이동 적음
- **비스듬히 부는 바람** → 마른 구간을 길게 지남 → **이동 많음**

**바람 방향이 세기만큼 중요하다.**

| 문헌 | 내용 |
|---|---|
| **Bauer & Davidson-Arnott (2003), "A general framework for modeling sediment supply to coastal dunes including wind angle, beach geometry and fetch effects", Geomorphology** | **해변 fetch 효과의 표준 프레임워크** |
| **Sherman, Jackson, Namikas, Wang (1998), "Wind-blown sand on beaches: an evaluation of models", Geomorphology** | 사막용 공식을 해변에 쓰면 **과대예측**한다는 실측 검증 |
| Nickling & Davidson-Arnott (1990) | 해변 풍성작용 리뷰 |

### H-4. 리플과 사구
**풍성 리플** — 파장 5~15cm. 튀는 알갱이의 충격으로 생김.
→ **마른 곳에만 생긴다.** "여기가 말랐다"를 물리적으로 옳으면서 즉시 읽히게 보여주는 신호.

| 문헌 | 내용 |
|---|---|
| Bagnold (1941) | 최초 설명 |
| **Anderson (1987), "A theoretical model for aeolian impact ripples", Sedimentology** | 현대 이론 |
| Pye & Tsoar (2009), *Aeolian Sand and Sand Dunes* | 사구 현대 표준서 |
| Durán, Claudin, Andreotti (2011), Aeolian Research | 이론 리뷰 |

### H-5. 그래픽스
| 논문 | 내용 |
|---|---|
| **Paris, Peytavie, Guérin, Argudo, Galin (2019), "Desertscape Simulation", CGF (Pacific Graphics)** | **현대 GPU 사구 시뮬. 그래픽스 최신 표준** |
| Onoue & Nishita (2000/2003), "dunes with wind-ripples" / "Virtual sandbox" | 하이트필드 + 바람. 초기 연구 |
| Beneš & Roa (2004), "Simulating desert scenery", WSCG | |

### H-6. ★ 구역별 입도 (cross-shore sorting)

```
   후빈/사구      상부 해빈      해빈면        스텝        쇄파대 → 외해
  ┌──────────┬───────────┬───────────┬─────────┬──────────────┐
  │ 바람만    │ 최고조위   │ swash    │ 쇄파점   │ 에너지 감소   │
  │ 도달      │ swash 상단 │ 주 영역   │ 최강난류 │              │
  ├──────────┼───────────┼───────────┼─────────┼──────────────┤
  │ 가장 고움 │ 다소 굵음  │ 중간      │ 가장 굵음│ 외해로 곱아짐 │
  │ 잘 분급   │ (침투효과) │ (변동 큼) │ (lag)   │              │
  └──────────┴───────────┴───────────┴─────────┴──────────────┘
```

**확실한 것 셋**
1. **스텝이 가장 굵다** — 해빈면 아래끝, 파도가 꽂히는 지점. 조개껍질·자갈 lag 층.
   걸어 들어가다 갑자기 자갈 밟히는 그 지점
2. **후빈/사구가 가장 곱고 잘 분급됨** — **바람이 고른 것만 골라 올려서** (파도가 아니다)
3. **외해로 갈수록 곱아짐** — 수심이 깊어지며 에너지 감소

> ⚠ 해빈면 자체(2~4구역)는 해변·계절·파랑조건마다 크게 다르다. 일반화하기 어렵다.

**분급을 만드는 메커니즘 4가지**
| | 내용 |
|---|---|
| ① 선택적 이동 | 작은 것이 먼저 움직임 → 큰 것이 남음 (lag) |
| ② 선택적 퇴적 | 유속이 떨어지면 **침강속도가 큰 것부터** 가라앉음 |
| ③ **침투 효과** | uprush가 굵은 걸 올려놓고 → 물이 스며 backwash 약화 → 못 데려옴 → **상부에 굵은 것이 남음** (F절 메커니즘 ①이 입도 분급도 만든다) |
| ④ **바람의 선별(winnowing)** | 바람이 고운 것만 올림 → 후빈이 곱고, 남은 자리는 굵어짐 |

**③과 ④는 정반대 방향이지만 작용 구역이 다르다.** ③은 swash가 닿는 곳, ④는 그 위.

**입도 ↔ 경사** — Bascom (1951), "The relationship between sand size and beach face slope", Trans. AGU 32
```
굵을수록 → 투수성↑ → 침투 많음 → backwash 약함 → 퇴적 → 가파름
고울수록 → 투수성↓ → 침투 없음 → backwash 강함 → 침식 → 완만
```
→ **입도가 구역마다 다르면 경사도 구역마다 다르다.** 스텝 근처가 가파르고 위로 갈수록
완만해지는 실제 해변 단면이 여기서 나온다. (Masselink & Li 2001 — F절에 이미 있음)

| 문헌 | 내용 |
|---|---|
| **Bascom (1951)**, Trans. AGU 32 | **입도 ↔ 경사의 고전** |
| **Folk & Ward (1957), "Brazos River bar...", J. Sed. Petrology 27** | **입도 분포 통계의 표준** — 평균·분급도·왜도·첨도를 φ 단위로 |
| McLaren & Bowles (1985), J. Sed. Petrology | 이동이 분포를 어떻게 바꾸는가 |
| Short (1999), *Handbook of Beach and Shoreface Morphodynamics* | 종합 |

### H-7. 구현 — D절 기계를 거의 그대로 재사용

**수력 침식과 구조가 똑같다.**
```
        물 (D절)                        바람 (새로)
  ─────────────────────────────────────────────────
  속도장 v (가상 파이프)          →   바람 벡터 (그냥 입력)
  운반능력 C = Kc·sinα·|v|        →   q ∝ (u* − u*t(W))³
  깎음/쌓음 b ±= amount           →   동일
  이류 s(x − v·dt)                →   동일 (바람 방향으로)
  열침식 (안식각)                  →   동일 (사구 미끄럼면이 공짜)
```

```csharp
// 임계는 W가 게이트
float uStarT = uStarT_dry(d) * (1f + moistureGain * W);

if (uStar > uStarT)
{
    float q = Kwind * pow(uStar - uStarT, 3f);
    b    -= q * dt;      // 풍상측에서 깎고
    sAir += q * dt;      // 공중에 실어 → 이류·퇴적은 수력 침식 패스와 동일
}
```
**새 시스템이 아니라 D절 패스에 채널 하나 추가하는 수준.**

**구역별 입도 — 세 가지 길**

| | 방식 | 평가 |
|---|---|---|
| (a) 정적 프로파일 | 거리에 따른 입도 곡선 | 공짜, 통제 가능. **시뮬과 따로 놈** |
| (b) 동적 분급 | 입도 클래스 여러 개를 각각 운반 | 정확. **채널·계산 3배** |
| **(c) ★ 권장** | **단일 입도 필드 `d[x][z]` + winnowing 편향** | **세 줄로 대부분의 이득** |

```csharp
float d[x][z];      // 국소 입도. 하이트필드 채널 하나

// 지형이 옮겨지면 입도도 같이 이류 (기존 s 이류 코드에 채널 하나 얹기)

// ★ 바람은 고운 것만 가져간다
if (windErosion > 0) d += winnowRate * windErosion;   // 남은 것이 굵어짐
if (windDeposit > 0) d -= winnowRate * windDeposit;   // 쌓인 것은 곱음
```
이것만으로 실제 패턴이 재현된다: 마른 상부에서 고운 것이 날아가 그 자리가 굵어지고(lag),
날아간 것이 더 위에 쌓여 후빈이 곱아진다.

### H-8. ★ 아밀링(armoring) — 공짜로 나오는 자기 제한
```
바람이 붐 → 고운 것이 날아감 → 표면이 굵어짐
         → u*t ∝ √d 상승 → 같은 바람으로는 더 이상 안 날림 → 스스로 멈춤
```
**자기 제한 되먹임.** 실제 현상(surface armoring / lag deposit)이고,
`d`가 필드이기만 하면 별도 코드 없이 나온다.
게임적으로도 좋다 — 바람이 계속 불어도 무한정 깎이지 않고 **자연스럽게 소강**되며,
파도가 한번 훑고 지나가면 섞여서 리셋된다.

### H-9. 게임에서 나오는 것

| | |
|---|---|
| **마른 모래만 날린다** | `W`가 게이트 → **물을 뿌리는 게 방어 수단** |
| 모래성이 마르면 바람에도 깎임 | 마름 압박이 이중 작동 |
| **바람이 모래를 실어와 쌓기도 함** | 모래성 **매몰** — 깎이는 것과 다른 종류의 위협 |
| 리플이 마른 구역에 생김 | **시각적 건조 표시기** |
| 바람 방향이 세기만큼 중요 | 비스듬한 바람이 더 위험 (fetch) |
| 히스테리시스 | 한번 시작되면 잘 안 멈춤 |
| 아밀링 | 자연스러운 소강 → 파도가 리셋 |

**★ 재료와 물이 반대 방향에 있다**
```
고운 모래 (모래성에 좋음, σ ∝ 1/d)  →  마른 상부/후빈에 있음
물        (지하수면)                →  아래쪽 물가에 가까움
```
하나를 가지러 가면 다른 하나에서 멀어진다. **"우물 파기"와 자연스럽게 엮인다.**
그리고 입도가 경사도 정하므로 위쪽은 완만, 아래쪽은 가파름 — 공간감까지 달라진다.

### H-10. ⚠ 입도 트레이드오프에 항목이 하나 늘었다
```
              고움 ←──────────────────→ 굵음
 모래성 강도    강함                     약함
 풍성 이동      잘 날림                  거의 안 날림   ← 새 항목
 배수/건조      느림                     빠름
 침투 침식      없음                     강함
 해변 경사      완만                     가파름
```
**우리 0.5mm는 11.5 m/s 이상 강풍이라야 날린다.** 선택지 셋:
1. **0.4mm로 내린다** — 둘 다 살되 침투 효과가 약해짐
2. **0.5mm 유지 + 바람을 강하게** — 폭풍 이벤트에서만 발동. 희소성이 오히려 극적일 수 있음
3. **★ 구역별 입도 (H-6)** — 상부가 실제로 더 곱다. **물리적으로 가장 정직**하고,
   마침 상부가 마르는 곳이라 풍성 이동이 일어나는 곳과 일치한다

---

## I. 조석 (tides)

> **이미 완전히 풀린 문제.** 예보 정확도가 수 cm 수준이라 논문보다 교재 하나면 끝난다.
> F절(지하수면)을 구동하는 입력이자, G절(파도)과 독립인 두 번째 환경 리듬.

### I-1. 딱 두 개

**Pugh, D.T. (1987), *Tides, Surges and Mean Sea-Level*, Wiley**
→ **저자가 PDF를 무료 공개.** 해양 조석의 표준 입문서. 필요한 게 다 있다.
개정판은 **Pugh & Woodworth (2014), *Sea-Level Science***.
제목에 **Surges**가 들어 있다 — 폭풍해일(I-6)도 같은 책에서 해결된다.

**Masselink & Short (1993), "The effect of tide range on beach morphodynamics and
morphology: a conceptual beach model", J. Coastal Research**
→ **조차가 해변 형태를 어떻게 바꾸는가.** Wright & Short 1984(해빈 상태 모델)를 조석까지 확장.
```
RTR = 조차 / 쇄파 파고        (상대조차, relative tide range)

RTR < 3     파랑 지배 (wave-dominated)
3 ~ 15      조석 수정 (tide-modified)
RTR > 15    조석 지배 (tide-dominated) — 갯벌
```
**우리 지역 프리셋에 그대로 꽂힌다.** 동해(0.3m) → 파랑 지배, 서해(9m) → 조석 지배.

### I-2. 계보

| | |
|---|---|
| **Newton (1687), Principia** | 평형조석론. **그런데 실제 조석을 못 맞춘다** |
| **Laplace (1775~76)** | **동역학 이론.** 조석은 강제 진동하는 파동. 지금도 이게 기반 |
| **Kelvin (1867~)** | **조화분해** 도입 + 기계식 조석 예보기 |
| **Darwin, G.H. (1883, 1898)** | 분조 명명법(M2, S2, K1, O1…). **지금 쓰는 기호** |
| **Doodson (1921), Proc. Roy. Soc. A** | 조석 기전력의 완전한 조화 전개. 388개 분조 |

> Newton이 왜 못 맞췄나 — 평형조석론은 바다가 즉시 반응한다고 가정한다.
> 실제 바다는 **깊이 유한하고 대륙에 막혀** 조석력에 강제된 **공명 시스템**으로 반응한다.
> 그래서 지역마다 위상도 진폭도 제각각이다.

### I-3. ★ 조화분해 — 파랑 스펙트럼과 같은 구조

```
h(t) = Z₀ + Σ Aᵢ · cos(ωᵢ·t + φᵢ)
```
[G-6b의 스펙트럼 합성](#g-6b-★-파도의-불규칙성--랜덤을-만들-필요가-없다)과 **똑같다.**
다만 주파수가 랜덤이 아니라 **천체 운동에서 정해진 고정값**이다.

| 기호 | 주기 | 정체 |
|---|---|---|
| **M2** | **12.4206 h** | 주태음반일주조. **가장 큼** |
| **S2** | **12.0000 h** | 주태양반일주조 |
| N2 | 12.6583 h | 달 궤도 이심률 |
| **K1** | **23.9345 h** | 일월합성일주조 |
| **O1** | **25.8193 h** | 주태음일주조 |

### I-4. ★ 대조·소조가 맥놀이다 — 파군과 같은 수학

```
M2(12.4206h) 와 S2(12.0000h) 의 맥놀이
  = 1 / (1/12.0000 − 1/12.4206) = 354.4시간 = 14.77일   ← 대조·소조 주기
```
보름·그믐에 달과 태양이 나란해져 M2·S2가 같은 위상 → **대조**.
상현·하현에 어긋나 상쇄 → **소조**.

**[파군(wave set)](#g-6b-★-파도의-불규칙성--랜덤을-만들-필요가-없다)과 정확히 같은 맥놀이다.**
주파수가 살짝 다른 둘이 겹치면 느린 포락선이 생기는 것.

같은 식으로 **K1·O1 맥놀이 = 13.66일**이고, 이게 **일조부등**
(아침 만조와 저녁 만조 높이가 다른 것)의 주기다.

### I-5. 지역차 — 형태수와 공진

**조석형태수** `F = (K1 + O1) / (M2 + S2)` — 숫자 하나가 지역 성격을 정한다.
```
F < 0.25     반일주조     하루 두 번, 높이 비슷
0.25 ~ 1.5   혼합(반일주 우세)
1.5 ~ 3.0    혼합(일주 우세)
F > 3.0      일주조       하루 한 번
```

**조차가 지역마다 극단적으로 다른 이유 = 공진.**
만이나 해협의 고유 진동 주기가 조석 주기(12.42h)와 맞으면 증폭된다.

| | 조차(대조) | |
|---|---|---|
| 펀디만(캐나다) | **약 16 m** | 세계 최대. 완벽한 공진 |
| **인천(서해)** | **약 9 m** | 황해가 반폐쇄 분지라 공진 |
| 남해 | 2~3 m | |
| **동해** | **약 0.3 m** | 거의 없음 |
| 지중해 | ~0.3 m | 좁은 지브롤터로 막힘 |

**바로 옆인데 동해와 서해가 30배 차이난다.** 지역 프리셋으로 이만한 대비가 없다.

### I-6. ⚠ 폭풍해일 (storm surge) — 조석과 별개로 수위를 올린다

조석만 넣으면 놓치는 것. **폭풍은 파도만 세지는 게 아니라 평균 수위 자체를 올린다.**

```
① 역기압 효과   기압 1 hPa 하강 → 해수면 약 1 cm 상승
② wind setup   바람이 물을 해안으로 밀어붙임. 얕은 대륙붕에서 특히 큼 (①보다 크다)

태풍급이면 합쳐서 1~3 m
```

**최악의 조합**: 폭풍해일 + 대조 만조 + 큰 너울 + wave setup.
전부 더해지므로 평소 안전한 위치가 잠긴다.

```csharp
seaLevel = MSL + tide(t) + surge(storm) + waveSetup(Hb);
//               I절        I-6           G-4
```
**네 항 중 셋은 이미 있다.** surge 하나만 추가하면 된다.

| 문헌 | |
|---|---|
| **Pugh (1987)** | 제목 그대로 Surges를 다룬다. 위와 같은 책 |
| Resio & Westerink (2008), "Modeling the physics of storm surges", Physics Today | 접근하기 쉬운 개관 |
| Flather (2001), "Storm surges", Encyclopedia of Ocean Sciences | |

### I-7. 구현 — 4개 분조면 충분

```csharp
const float W_M2 = TWO_PI / 12.4206f;   // 시간 단위
const float W_S2 = TWO_PI / 12.0000f;
const float W_K1 = TWO_PI / 23.9345f;
const float W_O1 = TWO_PI / 25.8193f;

float TideLevel(float t)
{
    return Z0
         + A_M2 * cos(W_M2 * t + p_M2)    // 하루 두 번
         + A_S2 * cos(W_S2 * t + p_S2)    // M2와 맥놀이 → 대조·소조 14.77일
         + A_K1 * cos(W_K1 * t + p_K1)
         + A_O1 * cos(W_O1 * t + p_O1);   // K1과 맥놀이 → 일조부등 13.66일
}
```

**네 줄이 리듬 네 겹을 만든다**
```
12.4 시간   하루 두 번의 만조·간조         즉각적 압박
24.8 시간   일조부등, 아침·저녁이 다름      하루 계획
13.66 일    일조부등의 강약                부차적
14.77 일    대조·소조                     ★ 메타 리듬
```
```
소조 주간 → 물이 덜 오르내림 → 안전 → 크게 짓기 좋음
대조 주간 → 물이 깊이 들어옴 → 위험 → 방어·유지보수
```
**폭풍(파도) 리듬과 조석 리듬이 서로 독립**이라, 둘이 겹치는 날이 진짜 위기가 된다.

### I-8. 문헌 정리

| 문헌 | 내용 |
|---|---|
| **Pugh (1987), *Tides, Surges and Mean Sea-Level*** | ★ **무료 공개. 여기서 시작** |
| Pugh & Woodworth (2014), *Sea-Level Science* | 개정판 |
| **Masselink & Short (1993)**, J. Coastal Research | ★ **조차 → 해변 형태. RTR 지표** |
| Short (1991), "Macro-meso tidal beach morphodynamics", J. Coastal Research | 위의 배경 |
| Cartwright (1999), *Tides: A Scientific History* | 역사와 이론 |
| Pawlowicz, Beardsley, Lentz (2002), "T_TIDE", Computers & Geosciences 28 | 조화분해 표준 코드 |
| Egbert & Erofeeva (2002), TPXO | 전지구 조석 모델 |
| Doodson & Warburg (1941), *Admiralty Manual of Tides* | 실무 고전 |

> **읽는 순서**: Pugh 1987의 조화분해 장 → Masselink & Short 1993. 반나절이면 충분.

### I-9. 안 봐도 되는 것

| | 왜 |
|---|---|
| 조류(tidal current) | 우리 스케일(해변 수십 m)에서 미미. 연안류는 파랑 구동이라 별개 |
| 기준면(datum) 체계 | 실무 측량용. 게임엔 불필요 |
| 평균해수면 상승 | 장기 스케일. 게임 범위 밖 |
| 388개 분조 전체 | 4개면 충분. 수 cm 정확도가 필요한 게 아니다 |

---

## J. 구조 안정성 — 모래성이 어떻게 무너지는가

> **A~I절이 "재료와 환경"이라면 여기는 "플레이어가 만든 것"이다.**
> 지금까지 판 것은 전부 **국소적**("이 지점이 버티는가")이었다.
> 구조 안정은 **전역적**("이 형태가 버티는가") — 완전히 다른 문제다.
> ```
> 넓적한 둔덕   → 안 무너짐
> 가늘고 높은 탑 → 무너짐        재료는 똑같다
> ```

### J-1. 평형 두 개 — 가장 기초
```
ΣF = 0   힘의 합이 0    → 안 움직인다
ΣM = 0   모멘트 합이 0  → 안 돈다
```
**붕괴 = 둘 중 하나가 깨지는 것.**
힘 평형이 깨지면 **미끄러지고**(활동), 모멘트 평형이 깨지면 **넘어진다**(전도).

### J-2. 안전율 — 이 분야의 전부
```
      저항하는 것          FS > 1  안전
FS = ─────────────         FS = 1  한계
      무너뜨리는 것         FS < 1  붕괴
```
지반공학의 모든 안정 해석이 결국 이 비율이다. 나머지는 **"그 두 값을 어떻게 계산하느냐"** 의 문제이고, 파괴 모드마다 다르다.

### J-3. ★ 무한사면 해석 — 안식각이 여기서 유도된다

경사면 위 두께 `h` 인 층. 밑면 힘을 경사 기준으로 분해하면:
```
수직응력  σ = γ·h·cos²β
전단응력  τ = γ·h·sin β·cos β          ← 무너뜨리는 것
저항      τ_f = c + σ·tan φ            ← Mohr-Coulomb

       c + γ·h·cos²β·tan φ
FS = ──────────────────────
        γ·h·sin β·cos β
```

**① 마른 모래 (c = 0) → 안식각**
```
     cos²β·tan φ      tan φ
FS = ───────────  =  ───────      FS = 1  ⟺  β = φ
     sin β·cos β      tan β
```
**안식각 = 마찰각.** 우리가 34°라 써온 값이 세 줄에서 나온다.
그리고 **`h`가 약분되어 사라진다** — 두께와 무관 → **순수한 각도 문제**.

**② ★ 젖은 모래 (c > 0) → 각도 문제가 크기 문제로 바뀐다**
```
        c                tan φ
FS = ─────────────  +  ───────
     γ·h·sin β·cos β    tan β
     ↑ h가 분모에 있다 → 얇을수록 안전, h→0이면 FS→∞

한계 두께:   h_c = c / [ γ·cos β·(sin β − cos β·tan φ) ]

β ≤ φ  →  분모 ≤ 0  →  h_c = ∞     두께 무관하게 안정
β > φ  →  h_c 유한                 두께 한계가 있다
```
**[D절의 "90°는 표현 한계"](#-90는-물리-한계가-아니라-하이트필드의-표현-한계다) 주장의 증명이 이것.**
```
마른 모래 → β > 34°면 두께와 무관하게 무너짐      각도 한계
젖은 모래 → β = 90°도 얇으면 됨. 두꺼우면 무너짐   크기 한계
```
**모래성이 서는 이유가 이 한 식에 다 있다.**

### J-4. 유한사면 — 원호활동과 절편법
무한사면은 얕고 긴 층을 가정한다. 실제 사면은 **곡면(대략 원호)을 따라** 무너진다.
미끄러질 덩어리를 세로로 잘라(절편법) 조각마다 계산하고 모멘트를 합산한다.
```
      Σ(절편별 저항 모멘트)
FS = ───────────────────────
      Σ(절편별 활동 모멘트)
```

**★ 핵심 — "가장 위험한 면을 찾는 것"**
원호 하나를 계산하는 게 아니라 **중심과 반지름을 바꿔가며 수백 개를 시험하고 최소 FS를 찾는다.**
```
"이 점이 강한가"가 아니라 "어딘가 무너질 수 있는 면이 존재하는가"
```
**우리 국소 모델이 절대 못 잡는 지점.** 셀마다 경사를 보는 건 무한사면의 특수한 경우라
깊은 원호 파괴가 안 보인다.

| 방법 | |
|---|---|
| **Fellenius (1936)** | 절편법 원조. 단순, 보수적 |
| **Bishop (1955), Géotechnique 5** | **표준.** 절편 사이 힘을 일부 고려 |
| Janbu / Spencer / Morgenstern-Price | 더 엄밀 |

### J-5. 수직 절토 한계 — 모래성 벽
```
H_c = (4c/γ)·tan(45° + φ/2)
젖은 해변 모래(c ~ 수 kPa, γ ~ 19 kN/m³) → 약 1 m
```
벽 윗부분에 **인장 영역**이 생기고 그게 갈라지며 시작된다.
**해변에서 구멍을 팔 때 1m쯤에서 벽이 무너지는 게 이 값.**

### J-6. 전도 (toppling) — 파도 충격이 원인
자기 무게만으로는 수직 탑이 안 넘어진다. **옆에서 미는 힘**이 필요하고,
우리 게임에서는 **파도 충격**이다.
```
      W · (B/2)            B = 밑면 폭
FS = ─────────────         h_F = 파도가 때리는 높이
      F · h_F
```
직관적 기준 하나로도 충분: **무게중심의 연직선이 밑면 밖으로 나가면 넘어진다.**
→ 넓적한 게 안전하고 가는 게 위험. 파도가 **높은 곳을 때릴수록** 위험.

### J-7. ★ 크기 효과 — 제곱-세제곱 법칙
```
자기 무게  ∝ 부피  ∝ L³
버티는 능력 ∝ 단면적 ∝ L²
내부 응력 = 무게/단면적 ∝ L        ← 크기가 2배면 응력이 2배
```
개미가 몸무게 50배를 들고 코끼리 다리가 굵은 이유.
**작은 모래성은 아무 모양이나 되고, 큰 건 두껍고 낮아야 한다.**

**B1(Pakpour 2012)의 `h ∝ R^(2/3)` 이 여기서 읽힌다**
```
압축 파괴라면:  γh = σ_c  →  h_max = σ_c/γ      반지름과 무관
실측은 R^(2/3):  자기 무게 기둥의 좌굴 스케일 (Greenhill 1881)
```
→ **모래성은 뭉개져서 무너지는 게 아니라 휘청거려 넘어간다.**
→ **높이 자체가 아니라 세장비(높이/폭)가 진짜 지표.**

### J-8. 지지력과 액상화
Terzaghi (1943)의 지지력 공식이 표준이지만 **우리 케이스에선 거의 문제가 안 된다** —
모래성 밑면 압력이 지반 지지력에 비해 아주 작다.

**단 하나 예외: [액상화](terms/tamping-vs-stomping.md).** 지반이 액상화되면 지지력이 순식간에 0이 된다.
파도가 반복 충격을 주면 기초가 액상화되어 통째로 가라앉을 수 있다.
→ **무너지는 게 아니라 주저앉는다.** 연출로 좋다.

### J-9. ★★ 무엇이 진실이고 무엇이 지표인가 (설계 결정)

> **이 프로젝트의 목적은 "실제 해변에서 모래성 쌓기 자체의 구현"이다.**
> 그래서 이 구분이 흐려지면 안 된다. 나중에 반드시 흐려질 지점이다.

**안전율은 실제 붕괴 과정과 같지 않다.** 네 가지가 다르다.

| | FS 계산 | 실제 붕괴 |
|---|---|---|
| 시간 | **정적 스냅샷** | 변형 → 재분배 → 국소 파괴 → 연쇄 |
| 파괴면 | **우리가 후보를 가정** | 스스로 형성됨 (가장 약한 경로) |
| 응력 재분배 | **없음** | **있음. "버티다 갑자기"의 진짜 이유** |
| 동적 효과 | 없음 | 낙하 충격, 진동, 액상화 |

**그래서 지표는 보이는 것에 관여하지 않는다.**
```
✗ 위험:  FS < 1 → 붕괴를 직접 실행       지표가 물리를 대체 → 어긋나면 거짓말
✓ 정당:  FS는 UI 전용                    틀려도 보이는 것은 안 망가짐
```
> **지표 = 계기판. 엔진이 아니다.** 계기판이 틀려도 차는 정상 동작한다.
> 실제 해변엔 안전율 표시 같은 게 없다 — **안 보여주는 선택지도 있다.**

**예고는 지어내지 말고 실제 전조를 드러낸다** (전부 실제 현상이고 대부분 이미 시뮬에 있다)

| 전조 | 우리 시뮬에서 |
|---|---|
| 균열이 생기고 자람 | 입자 결합이 끊기기 시작 → 렌더링만 붙이면 됨 |
| 작은 흘러내림이 먼저 | 입자면 자동 |
| 표면이 뽀얗게 마름 | 이미 `W`로 구동 중 |
| 밟으면 느낌이 다름 | 다져짐/팽창 |

### J-10. 하이트필드에 하중 전파 — 지표가 아니라 물리

겉에 지표를 얹는 대신 **안을 채운다.** 채널 하나 + 패스 하나.
```csharp
float load[x][z];   // 이 셀이 지탱하는 위쪽 무게

load = 자기무게 + 이웃에서 넘어온 하중;

float capacity = strength(W, grainSize) * 유효단면;
if (load > capacity) {
    항복();
    이웃으로_하중을_넘긴다(load - capacity);   // ← 여기서 연쇄가 생긴다
}
```
| 나오는 것 | 왜 |
|---|---|
| 아래쪽이 먼저 항복 | 하중이 누적되므로 |
| **연쇄 붕괴** | 넘긴 하중이 이웃을 과부하 |
| **크기 효과** | 높이 쌓을수록 바닥 하중 증가 |
| **버티다가 갑자기** | 임계 도달 시 연쇄가 한 번에 퍼짐 |
| **파괴 영역이 스스로 정해짐** | 원호를 가정하지 않음 |

**J-9의 ①②③이 다 해결된다.** 셀룰러 오토마톤 형태의 응력 전파 —
정확한 응력 해석은 아니지만 **"국소 경사만 본다"에서 한 계단 올라간다.**

### J-11. 문헌

**교재 하나면 충분**: **Craig's Soil Mechanics** 또는 **Das, *Principles of Geotechnical Engineering***
— 사면 안정 장만 읽으면 위 내용이 다 있다.

| 문헌 | 내용 |
|---|---|
| **Terzaghi (1943), *Theoretical Soil Mechanics*** | **토질역학의 아버지.** 지지력, 토압 |
| Taylor (1948), *Fundamentals of Soil Mechanics* | 사면 안정 |
| **Fellenius (1936)** | 절편법 원조 |
| **Bishop (1955), "The use of the slip circle in the stability analysis of slopes", Géotechnique 5** | **표준 방법** |
| Duncan & Wright, *Soil Strength and Slope Stability* | 현대 표준서 |
| Greenhill (1881) | 자기 무게 기둥의 좌굴. `h ∝ R^(2/3)`의 출처 |

**그래픽스 — 압축 구조물 (아치와 직결)**

D절에서 "아치가 오버행보다 잘 버틴다"고 한 것의 이론.
**추력선(thrust line)** — 압축력이 지나가는 경로가 구조 안에 머물면 안전, 밖으로 나가면 붕괴.

| 문헌 | 내용 |
|---|---|
| **Whiting, Ochsendorf, Durand (2009), "Procedural modeling of structurally-sound masonry buildings", SIGGRAPH Asia** | 구조적으로 성립하는 조적조 생성 |
| Whiting et al. (2012), "Structural optimization of 3D masonry buildings" | 후속 |
| **Block & Ochsendorf (2007), "Thrust network analysis"** | **압축만으로 버티는 형태 찾기.** 아치 이론의 현대판 |
| Deuss et al. (2014), "Assembling self-supporting structures", SIGGRAPH Asia | 자립 구조물 조립 |

---

## K. 압축 구조 — 조적조 역학 (어떻게 하면 안 무너지게 만드는가)

> J절이 **"왜 무너지는가"** 라면 여기는 **"어떻게 세우는가"** 다. 짝이 되는 절.
>
> **핵심 인식: 모래는 역학적으로 조적조(모르타르 없는 석조)와 거의 같다.**
> ```
> 조적조 가정              모래
> ─────────────────────────────
> 압축은 매우 강함          ✓ 같음
> 인장은 0                 ✓ 거의 같음 (젖으면 아주 조금)
> 마찰로 미끄러짐 저항       ✓ 같음
> ```
> **수백 년간 쌓인 석조 건축 역학이 그대로 적용된다.**

> ⚠ **이건 시뮬레이션을 만들기 위한 지식이 아니다.** 입자 시뮬은 아치도 버트레스도
> 후프 균열도 알아서 만든다. 공부하는 이유는 셋:
> **① 시뮬 검증**(아치가 t/R≈0.106에서 무너지면 물리가 맞는 것)
> **② 게임 설계**(플레이어에게 무엇을 가르칠 것인가) ③ 피드백 설명(왜 무너졌는지)

### K-1. Heyman의 조적조 이론 + 안전 정리

**Heyman, J. (1966), "The stone skeleton", Int. J. Solids and Structures**
**Heyman, J. (1995), *The Stone Skeleton: Structural Engineering of Masonry Architecture***

> **★ 안전 정리: 구조 안에 완전히 들어가는 추력선이 하나라도 존재하면 그 구조는 안전하다.**

**실제 추력선을 찾을 필요가 없다.** 가능한 것 하나만 보이면 증명 끝 → 계산이 극적으로 쉬워진다.

### K-2. 추력선 (line of thrust)
압축력이 구조를 통과하는 경로.
```
 추력선이 두께 안        밖으로 나감
 ┌─────────┐          ┌─────────┐
 │  ╱‾‾‾╲  │          │╱‾‾‾╲    │
 │ ╱     ╲ │          ╱      ╲  │  ← 힌지 발생
 │╱       ╲│         ╱│       ╲ │
 └─────────┘          └─────────┘
    안전                 붕괴 시작
```
추력선이 표면 밖으로 나가면 **힌지(hinge)** 가 생기고, 힌지 4개면 아치가 메커니즘이 되어 무너진다.
→ 아치가 왜 되는지뿐 아니라 **왜 두꺼워야 하는지**도 설명된다.

### K-3. ★ 현수선 — 최적 아치 형태
Robert Hooke, 1675 (애너그램으로 발표):
> **"늘어진 사슬을 뒤집으면 서 있는 아치가 된다."**
```
 사슬을 늘어뜨림          뒤집음
    ╲     ╱              ╱‾‾‾╲
     ╲   ╱      →      ╱      ╲
      ╲_╱             ╱        ╲
   순수 인장            순수 압축
```
사슬은 인장만 받고 형태가 저절로 정해진다. 뒤집으면 **압축만 받는 완벽한 아치** = **현수선(catenary)**.
가우디가 사그라다 파밀리아 설계에 **거꾸로 매단 모형**을 쓴 원리.

**게임에 직결**: 반원 아치는 현수선보다 불리하고, 현수선에 가까울수록 잘 버틴다.
**물리에서 저절로 나오지만, 우리가 알아야 튜토리얼로 가르칠 수 있다.**

### K-4. 아치 최소 두께 — 검증용 숫자
**Couplet-Heyman 문제**의 고전적 답:
```
반원 아치가 자기 무게로 버티는 최소 두께:   t / R ≈ 0.106
```
스팬 1m 아치면 두께 최소 약 5.3cm (모래는 안전율을 더 봐야 하니 실제론 더 두껍게).

**→ 시뮬 검증에 쓸 것.** 입자 시뮬로 반원 아치를 만들어 두께를 줄여가며
무너지는 지점이 이 근처면 물리가 맞는 것.

### K-5. ★ 수평 추력과 버트레스 — 게임 깊이가 여기 있다
**아치는 아래로만 누르지 않는다. 옆으로도 민다.**
```
      ╱‾‾‾╲
    ╱       ╲
  ←╱         ╲→      ← 수평 추력
 ▓▓           ▓▓     ← 이걸 받아줄 게 없으면 발치가 벌어지며 무너진다
```
고딕 성당의 **버트레스(부벽)** 가 이걸 받는다.
**모래성도 똑같다** — 아치 옆에 두꺼운 벽이나 받침이 있으면 버티고, 홀로 서면 벌어진다.
**플레이어가 배울 수 있는 진짜 구조 지식이고, 물리에서 저절로 나온다.**

### K-6. 돔과 볼트 — 아치의 3D 확장
```
아치 → 2D           볼트 → 아치를 밀어낸 것        돔 → 아치를 회전시킨 것
```
**돔에는 아치에 없는 문제가 생긴다 — 후프 응력(hoop stress).**
```
   ╱‾‾‾╲      위쪽: 압축 후프 (안전)
  │     │
  ╰─────╯     아래쪽: 인장 후프 → 세로로 갈라진다
```
실제 석조 돔에 **쇠사슬·띠를 두르는** 이유(성 베드로 대성당 등).
**모래 돔은 인장이 0에 가까우니 아래쪽이 세로로 갈라진다.** 실제로 일어나는 현상.

### K-7. 형태 찾기 / 해석 방법 (선택)

| | 내용 |
|---|---|
| **도해정역학 (graphic statics)** | 힘을 벡터로 그려 다각형을 닫는 도식적 해법. Culmann(1866), Cremona, Maxwell. **직관적이라 게임 UI로 쓸 여지도** |
| **Thrust Network Analysis** | **Block & Ochsendorf (2007)** — 도해정역학의 3D 확장 |
| Force density method | Schek (1974) |

### K-8. 그래픽스 문헌

| 논문 | 내용 |
|---|---|
| **Whiting, Ochsendorf, Durand (2009), "Procedural modeling of structurally-sound masonry buildings", SIGGRAPH Asia** | 구조적으로 성립하는 조적조 생성 |
| Whiting et al. (2012), "Structural optimization of 3D masonry buildings" | 후속 |
| **Panozzo, Block, Sorkine-Hornung (2013), "Designing Unreinforced Masonry Models", SIGGRAPH** | 대화형 조적조 설계 |
| Vouga, Höbinger, Wallner, Pottmann (2012), "Design of self-supporting surfaces", SIGGRAPH | 자립 곡면 |
| de Goes, Alliez, Owhadi, Desbrun (2013), "On the equilibrium of simplicial masonry structures", SIGGRAPH | 평형 해석 |
| Deuss et al. (2014), "Assembling self-supporting structures", SIGGRAPH Asia | 조립 순서 |

### K-9. 실제 모래조각 기법 — 곧 게임의 동사

학술 문헌은 거의 없고 실무 가이드가 전부. 그런데 **가장 실용적이다** —
**기법이 곧 게임 동사가 되고, 전부 우리가 이미 모델링한 물리 위에서 돈다.**

| 실제 기법 | 대응하는 물리 |
|---|---|
| 물 붓기 / **우물 파기** | `W`, 지하수면(F절) |
| **다지기(두드리기)** | 다짐 → φ 하락 → 강도 상승 |
| **거푸집 쌓기** | 형틀 안에서 다져 블록 제작 |
| **팬케이크** (묽은 모래를 얹고 흔들어 안착) | 순간 액상화 → 재배치 → 배수 |
| **위에서 아래로 조각** | 아래를 건드리면 위가 무너지므로 |
| 터널·아치 뚫기 | 압축 구조 (이 절) |
| 물 뿌려 유지보수 | 증발 대응(E절) |

**게임 동사를 지어낼 필요가 없다.** 실제 기법을 그대로 넣으면 물리가 받아준다.

참고: Ted Siebert, *The Art of Sandcastling*. 나머지는 대회 가이드·영상.

### K-10. 읽는 순서 / 안 봐도 되는 것
```
1. Heyman 안전 정리 + 추력선          개념 둘. 반나절
2. 현수선 / 역현수 (Hooke, Gaudí)     30분. 튜토리얼 소재
3. t/R ≈ 0.106, 수평 추력·버트레스     검증용 숫자
4. 실제 모래조각 기법 조사             게임 동사 목록. 영상 몇 개
5. (선택) 도해정역학 / TNA / 그래픽스 논문
```
**1~4가 하루 정도.** 5는 직접 구조 해석을 할 게 아니면 불필요 — 입자 시뮬이 대신한다.

| 안 봐도 되는 것 | 왜 |
|---|---|
| 철근콘크리트 / 강구조 | 인장을 쓰는 재료. 모래와 무관 |
| 내진 설계 | 규모가 다름 |
| 유한요소 구조해석 | 입자 시뮬이 대신함 |
| 건축 의장·계획 | 게임 아트 영역 |

---

## L. 렌더링

> ⚠ **렌더링은 앞 절들과 성격이 다르다.**
> ```
> 물리   →  논문을 읽고 이해하면 됨
> 렌더링 →  읽어도 안 됨. 만들면서 눈으로 맞춰야 함
> ```
> **그래서 L-1~L-2(기초)만 먼저 보고, 나머지는 프로토타입 이후에 판다.**
> `W`·`d`·`s`·`v`를 실제로 화면에 뿌려본 다음에 뭐가 부족한지 보고 정하는 게 맞다.

### L-0. ★ 우리는 이미 렌더링 입력을 다 갖고 있다

렌더링 작업은 "무엇을 더 시뮬레이션하나"가 아니라 **"이미 있는 필드를 어떻게 쓰나"** 다.

| 필드 | 렌더링 용도 |
|---|---|
| **`W` 수분** | **알베도, 러프니스, 반짝임 밀도** — 프로젝트의 정체성 |
| `d` 물 깊이 | 투명도, 색(흡수), 굴절 세기, 가장자리 페이드 |
| `s` 뜬 모래 | **탁도** — 파도 끝자락의 갈색 물 |
| `v` 속도 | **플로우맵** — 물 왜곡, 거품이 흐르는 방향 |
| `\|v\|` | **포말 마스크** |
| `b` 지형 | 메시 변위, 노멀 |
| `grainSize` | 반짝임 밀도, 리플 파장, 중광물 줄무늬 |

### L-1. 기초 ① — 빛이 표면에서 하는 일

`W → albedo/roughness`가 왜 물리적으로 맞는지 설명하려면 이 언어가 필요하다.

**프레넬** — 모든 경계면에서 빛은 일부 반사되고 일부 굴절해 들어간다. 비율은 각도와
**굴절률 차이**로 정해진다.
```
공기(1.00) ↔ 물(1.33)    → 정면 반사율 약 2%
공기(1.00) ↔ 석영(1.54)  → 약 4.5%
물(1.33)  ↔ 석영(1.54)   → 약 0.5%     ← 젖으면 이렇게 된다
```
**젖은 모래가 어두워지는 이유** — 알갱이 표면에서 튕기는 빛이 1/9로 줄고,
대신 안으로 들어가 여러 번 흡수된다. [Jensen 1999](terms/wet-sand-darkening.md)를
프레넬 언어로 다시 쓴 것.

> 물가에서 발밑은 바닥이 보이는데 멀리는 하늘만 비치는 것도 프레넬(각도 의존)이다.

**미세면(microfacet)** — 러프니스의 물리적 의미
```
러프니스 = 표면을 덮은 아주 작은 거울들의 방향이 얼마나 흩어져 있나
낮음 → 정렬 → 선명한 반사    /    높음 → 제각각 → 뿌옇게 퍼짐
```
**젖으면 물막이 미세 요철을 메운다** → 거울들이 정렬 → 러프니스 하락 → 반들거림.
```
albedo    ↓   프레넬 대비 감소 + 다중 산란 흡수
roughness ↓   물막이 요철을 메움
```
**두 줄이 물리적으로 서로 다른 이유에서 나온다.** 이걸 알아야 튜닝이 된다.

| 문헌 | |
|---|---|
| Cook & Torrance (1982) | 미세면 모델의 고전 |
| Walter et al. (2007), "Microfacet models for refraction through rough surfaces", EGSR | GGX |
| **Karis (2013), "Real Shading in Unreal Engine 4"** | ★ **UE의 PBR이 정확히 이 문서. 실무는 이것 하나면 충분** |
| Burley (2012), "Physically-based shading at Disney" | Disney BRDF |

### L-2. 기초 ② — 빛이 매질 속에서 하는 일

**Beer-Lambert 흡수**: `I = I₀·exp(−σ·거리)`. **물은 빨강을 먼저 먹는다** → 깊을수록 청록.
```
수 cm  →  거의 무색. 바닥 색이 그대로
30 cm  →  살짝 청록
1 m    →  뚜렷한 청록
```
**해변 물의 색은 대부분 "바닥이 비쳐 보이는 것"이다.** 먼바다처럼 물 자체의 색이 아니다.
→ `d`와 `b`가 색을 지배하고, `s`를 섞으면 파도 끝자락의 **탁한 갈색 물**이 나온다.

### L-3. 우리 고유의 어려운 곳 셋

#### ① 젖은 모래 ↔ 마른 모래 — 프로젝트의 정체성
```csharp
albedo    = lerp(dryAlbedo, dryAlbedo * 0.5f, W);
roughness = lerp(0.9f, 0.2f, W);
sparkle   = lerp(1.0f, 0.0f, W);        // ★ 젖으면 반짝임이 사라진다
```
**세 번째가 자주 빠진다.** 마른 모래는 햇빛에 알갱이가 반짝이는데 젖으면 물이 틈을 메워
사라진다. 넣으면 젖음/마름 대비가 훨씬 강해진다.

| 문헌 | |
|---|---|
| **Jensen, Legakis, Dorsey (1999), "Rendering of Wet Materials", EGSR** | 기본 |
| Lekner & Dorf (1988) | 젖은 모래가 어두운 이유의 광학 |
| **Twomey, Bohren, Mergenthaler (1986), "Reflectance and albedo differences between wet and dry surfaces", Applied Optics** | 젖은 표면 광학의 고전 |
| **Nakamae, Kaneda, Okamoto, Nishita (1990), "A lighting model aiming at drive simulators", SIGGRAPH** | **젖은 노면 렌더링.** 그래픽스 고전 |

#### ② ★ 얕은 물과 젖음 경계 — 여기가 제일 어렵다
**해변을 해변처럼 보이게 하는 건 먼바다가 아니라 물가의 젖은 선과 거품이다.**
그리고 **표준 물 셰이더가 가장 못 하는 부분**이다 — "표면이 있는 두꺼운 물"을 가정하는데
swash 끝자락은 두께가 mm이고 표면이랄 것도 없다.
```
물 깊이:  30cm → 5cm → 5mm → 0.5mm → 0
표현:    보통 물 → 굴절 약함 → 표면이 사라짐 → 그냥 젖은 모래
         ↑ 이게 연속적으로 이어져야 한다
```
```
깊이 페이드      d가 얕으면 알파·굴절·색을 함께 죽임
젖음 잔상        물이 빠져도 W가 남아 젖은 자국이 서서히 마름
가장자리 거품선   d의 등고선을 따라 포말 띠
```
> **이 부분은 논문보다 실무 기법(GDC 발표, 셰이더 분해)이 유용하다.**
> 다만 **우리는 `d`와 `W`를 실제로 갖고 있어서** 대부분의 게임이 가짜로 하는 걸
> 진짜로 할 수 있다.

#### ③ 포말 — 필드가 이미 있다
```csharp
foamMask  = saturate((length(v) - foamThreshold) * k);   // 빠른 곳
foamMask += 쇄파 지점 (수심 제한 판정에서)
// 포말은 v를 따라 이류되고 시간에 따라 감쇠
```
**포말이 `v`를 따라 흐르는 게 중요하다.** 고정 텍스처면 즉시 가짜로 보인다.
| Ihmsen et al. (2012), "Unified Spray, Foam and Air Bubbles for Particle-Based Fluids", The Visual Computer | 포말/거품/물보라 |

### L-4. 그 다음 층

#### 알갱이 반짝임 (glint / sparkle)
마른 모래가 햇빛에 반짝이는 것. **일반 러프니스로는 안 나온다** — 픽셀 하나 안에
알갱이가 수백 개라 통계적으로 뭉개진다.
**`grainSize`가 밀도를, `W`가 강도를 구동한다.**

| 문헌 | |
|---|---|
| Jakob, Hašan, Yan et al. (2014), "Discrete stochastic microfacet models", SIGGRAPH | 이론 |
| Yan et al. (2014), "Rendering glints on high-resolution normal-mapped specular surfaces", SIGGRAPH | |
| **Zirr & Kaplanyan (2016), "Real-time rendering of procedural multiscale materials", i3D** | **실시간 기법** |

#### 코스틱 (caustics)
얕은 물 바닥의 일렁이는 밝은 그물 무늬. **해변 얕은 물의 인상을 결정한다.**
게임에서는 보통 애니메이션 텍스처로 치는데, **우리는 실제 수면 높이를 알고 있어 계산 가능.**
| **Wyman & Davis (2006), "Interactive image-space techniques for approximating caustics", i3D** | 출발점 |

#### 하늘·대기 — UE5가 해준다
해변은 화면의 절반이 하늘인데 **UE5 Sky Atmosphere가 Bruneton & Neyret 방식을 이미 구현**해뒀다.
깊이 공부할 필요는 낮다.
| Preetham, Shirley, Smits (1999) / Hosek & Wilkie (2012) / Bruneton & Neyret (2008) | 이론이 궁금하면 |

#### 균열
| Iben & O'Brien (2006), "Generating Surface Crack Patterns", SCA (확장판 TVCG) | 마른 진흙 균열. **순수 모래는 균열 없이 부슬거리므로** 갯벌/진흙, 또는 붕괴 예고 연출에 |

#### 먼바다 물 표면
| Bruneton, Neyret, Holzschuch (2010), "Real-time realistic ocean lighting using seamless transitions from geometry to BRDF", EG | 스케일 전환 |
| Tessendorf, "Simulating Ocean Water" | G-8에도. UE5 Water의 뿌리 |

### L-5. UE5가 주는 것 / 우리가 해야 하는 것

| UE5가 해줌 | 우리가 해야 함 |
|---|---|
| Sky Atmosphere, 볼류메트릭 구름 | **젖은 모래 머티리얼 (`W` 구동)** |
| Lumen (GI + 반사) | **얕은 물 / 젖음 경계** ← 가장 어렵고 가장 중요 |
| Water 플러그인 (Gerstner, 먼바다) | **포말 (`v`, `s` 구동)** |
| Nanite | **알갱이 반짝임** |
| **Substrate (레이어 머티리얼)** | 코스틱 |

> **Substrate가 우리 케이스에 잘 맞는다** — 마른 모래 위에 물막을 레이어로 얹는 구조가
> 물리와 그대로 대응한다.

### L-6. 권하는 순서
```
1. PBR 물리 기반 (프레넬 + 미세면)   ← Karis 2013 하나. 반나절
2. Beer-Lambert 흡수                ← 개념 하나. 30분
─────────── 여기까지 지금, 아래는 프로토타입 이후 ───────────
3. 젖은 모래 머티리얼                ← 프로젝트 정체성
4. 얕은 물 / 젖음 경계               ← 실질적인 작업 대부분
5. 포말                             ← 필드가 이미 있음
6. 반짝임 (glint)                   ← 마른 모래의 인상
7. 코스틱                           ← 얕은 물의 인상
8. 하늘                             ← UE5에 맡김
```

---

## M. 소리 — 절차적 오디오

> **방침 결정: 하이브리드. 분위기는 샘플, 반응은 절차적.**
> 순수 절차적으로 사실적인 파도 소리를 만드는 건 어렵다. 대신 **시뮬 상태를 따라가야 하는
> 소리만** 절차적으로 간다.
> ```
> 기반층(bed)  =  샘플 루프      먼 파도, 분위기        ← 사실성 담당
> 반응층       =  절차적         쇄파, 모래, 붕괴, 물   ← 시뮬 연동 담당
> ```
> **모래와 붕괴는 샘플로는 시뮬 상태를 못 따라간다.** 먼 파도 분위기는 샘플로 깔아도 된다.

### M-1. 왜 이 프로젝트에 절차적이 맞나

**소리에 필요한 상태를 이미 전부 계산하고 있다.**
```
파도:  Hs, Tp, 쇄파 유형(ξ), 처오름 속도, 물 깊이
모래:  W(수분), φ(공극률), 입도, 충돌 속도, 무너진 부피
물:    v(속도), d(깊이), s(탁도)
```
샘플 기반이면 이 **연속적인 값들을 샘플 서너 개로 뭉개야** 한다.
그리고 목적이 "실제 해변의 구현"이라면 —
**물리는 창발인데 소리만 미리 녹음한 걸 트리거하면 어긋난다.**

| | 샘플 | 절차적 |
|---|---|---|
| 사실성 | **높음** | 어려움 |
| 변주 | 반복이 티남("기관총 효과") | **무한** |
| 파라미터 반응 | 양자화 | **연속적** |
| 용량 / CPU | 큼 / 낮음 | 거의 0 / 높음 |

### M-2. 기초 — 자연음은 노이즈 + 필터 + 엔벨로프

**자연의 소리는 대부분 음정이 없다.** 파도·바람·모래·비 전부 무작위 신호다.
→ **절차적 오디오가 자연음에 특히 잘 맞는 이유.**

| 노이즈 | 스펙트럼 | 예 |
|---|---|---|
| 백색 (white) | 균등. 날카로운 "치익" | 물보라, 거품 |
| **분홍 (pink, 1/f)** | 저음이 강함. **자연음에 가장 가까움** | 바람, 폭포, 비 |
| 브라운 (brown, 1/f²) | 훨씬 저음 | 천둥, **먼 파도** |

| 필터 | 들리는 느낌 |
|---|---|
| **로우패스(LPF)** | **멀다 / 막혔다** ← 공기가 고음을 먼저 흡수하므로 |
| 하이패스(HPF) | 얇다 / 가볍다 |
| 레조넌스 | 통 울림 |

**엔벨로프(ADSR)**: 충돌음 = 빠른 attack + 짧은 decay / 파도 = 느린 attack + 긴 release.

**레시피 예**
```
바람     = 분홍 노이즈 → LPF(컷오프를 풍속으로) → 볼륨(풍속)
먼 파도  = 브라운 노이즈 → LPF → 아주 느린 엔벨로프
쇄파     = 백색 노이즈 → HPF → 빠른 attack, 긴 release
물방울   = 사인파 + 피치를 빠르게 위로 훑기 + 짧은 엔벨로프
```
> 물방울이 "똑" 하는 건 기포가 진동하며 **피치가 올라가기** 때문. 사인파 피치를 훑으면 된다.

### M-3. ★ Granular Synthesis — 이름이 우연히 딱 맞는다

아주 짧은 음향 조각(**grain**, 1~100ms)을 수없이 뿌려 소리를 만드는 기법.
```
grain 하나   →  거의 안 들리는 짧은 클릭
수천 개 동시  →  "쏴아" 하는 연속음으로 합쳐짐

모래 = 알갱이(grain)   ↔   소리 = grain
입자 충돌 하나         ↔   음향 grain 하나
```

**단, 수만 개 충돌을 전부 소리 내면 CPU가 못 버틴다 → 통계로 요약한다.**
```csharp
float collisionRate  = 초당 충돌 횟수;    // → grain 밀도
float avgImpactSpeed = 평균 충돌 세기;    // → 필터 컷오프, 볼륨
grainDensity = collisionRate * k;
filterCutoff = lerp(8000f, 2000f, W);    // ★ 젖으면 둔탁
```
**젖은 모래가 둔탁한 게 물리에서 나온다** — 알갱이가 붙어 고주파 스침이 줄어드니까. `W` 하나로 구동.

### M-4. 파라미터 매핑 — 전부 이미 계산 중인 값

| 소리 | 구동 파라미터 | 합성 |
|---|---|---|
| 먼 파도 | Hs, 거리 | 브라운 노이즈 + LPF *(또는 샘플 bed)* |
| **쇄파** | **쇄파 유형 ξ**, H_b | 아래 ★ |
| uprush / backwash | 처오름 속도, 물 깊이 | 노이즈 + 이동하는 필터 |
| 모래 밟기 | W, φ, 압력 | granular. W↑ → 둔탁 |
| 파기 | 도구 속도, W | granular burst |
| 다지기 | 충격 세기, W | 둔탁한 저역 + 물 소리 |
| **붕괴** | 무너진 부피, 낙하 속도 | grain 밀도 ∝ 부피, 지속 ∝ 규모 |
| 바람 | 풍속 | 분홍 노이즈 + LPF |

#### ★★ 쇄파 유형 ξ가 소리를 가른다 — 이걸 놓치기 쉽다

[G-3의 Iribarren 수](#g-3-해안으로-오면서-변형)로 이미 쇄파 유형을 계산하고 있다.
**실제로 이 셋은 소리가 완전히 다르다.**
```
spilling (ξ<0.5)     지속적인 "쉬이익"        거품이 계속 생김
plunging (0.5~3.3)   한 번의 "철썩" + 거품    말려서 꽂힘
surging  (ξ>3.3)     낮게 "쏴아"             안 부서지고 밀려옴
```
샘플이면 세 종류를 녹음해 전환해야 하고 경계가 어색하다.
**절차적이면 ξ를 따라 연속적으로 변한다.**

그리고 **ξ는 경사와 입도에서 나온다** → **입도를 바꾸면 파도 소리가 바뀐다.**
지역 프리셋이 소리까지 갈라놓는다.

### M-5. UE5 — MetaSounds

**정확히 이걸 위한 시스템.** 노드 그래프로 오실레이터·노이즈·필터·엔벨로프를 조립하고,
블루프린트/C++에서 파라미터를 실시간 전달한다. **샘플도 같이 쓸 수 있어 하이브리드에 맞다.**
(이전 세대 Sound Cue는 샘플 조합 중심이라 절차적 합성이 약했다.)

### M-6. 문헌

**Andy Farnell (2010), *Designing Sound*, MIT Press**
→ ★ **절차적 오디오의 표준 교재. 이거 하나면 된다.**
Pure Data로 자연음을 직접 만들어보는 구성이고, **바람·물·불·발소리가 전부 챕터로 있다.**

| 문헌 | 내용 |
|---|---|
| Curtis Roads (2001), *Microsound* | **granular synthesis 표준서** |
| Perry Cook (2002), *Real Sound Synthesis for Interactive Applications* | 물리 모델링 기반 합성 |
| van den Doel, Kry, Pai (2001), "FoleyAutomatic", SIGGRAPH | **물리 시뮬 → 소리**의 고전 |
| **Zheng & James (2009), "Harmonic fluids", SIGGRAPH** | **기포 진동으로 물소리 합성.** 물방울·거품의 정확한 물리 |
| **Moss, Yeh, Hong, Lin, Manocha (2010), "Sounding liquids: Automatic sound synthesis from fluid simulation", TOG 29(3)** | **유체 시뮬에서 소리를 자동 생성** |
| Zheng & James (2010), "Rigid-body fracture sound with precomputed soundbanks", SIGGRAPH | 파괴음 |

**Zheng & James, Moss et al.이 우리와 가장 가깝다** — 물리 시뮬 상태에서 소리를 뽑는 논문들.
