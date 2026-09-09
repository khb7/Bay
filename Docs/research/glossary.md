# 용어집

> **용어 하나에 문서 하나.** 항목을 누르면 [terms/](terms/) 아래 개별 문서로 간다.
> 구현·엔진 쪽 용어는 [glossary-impl.md](glossary-impl.md).
> [papers.md](papers.md)를 읽다 막히면 여기로.

## 목차

**[1. 역학 기초](#1-역학-기초)**  
[항력 (drag)](terms/drag.md) · [변형 (strain, ε)](terms/strain.md) · [응력 (stress, σ)](terms/stress.md) · [편차 / 체적 분해](terms/deviatoric-volumetric.md) ·
[Hencky 변형 (로그 변형)](terms/hencky-strain.md) · [연속체 (continuum)](terms/continuum.md) · [등방성 (isotropy) ★](terms/isotropy.md) ·
[구조 이방성 / 조직 (fabric anisotropy) ★](terms/anisotropy-fabric.md) ·
[구성 모델 (constitutive model)](terms/constitutive-model.md)

**[2. 소성과 항복](#2-소성과-항복)**  
[탄성 / 소성 / 탄소성](terms/elastic-plastic.md) · [소성 = 클램프 ★](terms/plasticity-as-clamp.md) · [항복면 (yield surface) ★](terms/yield-surface.md) ·
[Drucker-Prager](terms/drucker-prager.md) · [마찰각 (friction angle)](terms/friction-angle.md) · [안식각 (angle of repose)](terms/angle-of-repose.md) ·
[⚠ 점착력 vs 접착력 — 용어 정리](terms/cohesion-vs-adhesion.md) · [점착력 (cohesion, c)](terms/cohesion.md) · [경화 (hardening)](terms/hardening.md) ·
[★ 팽창 / 다져짐 (dilation / compaction)](terms/dilation-compaction.md) · [Reynolds 팽창 (Reynolds dilatancy)](terms/reynolds-dilatancy.md) ·
[팽창을 시뮬레이션이 읽어야 하는 이유](terms/why-simulate-dilation.md) · [★ 다지기 vs 밟기 — 방향이 반대다](terms/tamping-vs-stomping.md) · [임계상태 (critical state)](terms/critical-state.md) ·
[순차 파단 (progressive failure)](terms/progressive-failure.md) ·
[열침식 (thermal erosion)](terms/thermal-erosion.md)

**[3. 다공성 매질 / 물의 이동](#3-다공성-매질--물의-이동)**  
[다공성 매질 (porous medium)](terms/porous-medium.md) · [공극률 φ (porosity) ★](terms/porosity.md) · [포화도 (saturation)](terms/saturation.md) ·
[투과율 k (permeability)](terms/permeability.md) · [Darcy의 법칙](terms/darcy-law.md) · [Richards 방정식](terms/richards-equation.md) ·
[포장용수량 (field capacity)](terms/field-capacity.md) · [침투 / 유출 (infiltration / exfiltration)](terms/infiltration-exfiltration.md) ·
[모세관 다리 (capillary bridge)](terms/capillary-bridge.md) · [모세관 상승대 (capillary fringe)](terms/capillary-fringe.md) ·
[젖은 입상체의 3영역](terms/wet-granular-regimes.md) · [유변학 (rheology) ★](terms/rheology.md)

**[4. 증발 / 건조](#4-증발--건조)**  
[1단계 (등속 건조기, constant-rate period)](terms/drying-stage-1.md) · [2단계 (감속 건조기, falling-rate period)](terms/drying-stage-2.md) ·
[건조 전선 (drying front)](terms/drying-front.md) · [특성 길이 L_C (characteristic length)](terms/characteristic-length.md) ·
[유효 포화도 Se / 흡입력 / 투수계수](terms/effective-saturation.md)

**[5. 해변](#5-해변)**  
[swash zone](terms/swash-zone.md) · [지하수면 (water table)](terms/water-table.md) · [⚠ 압력을 명시적으로 모델링하지 않는 이유 (설계 결정)](terms/why-no-explicit-pressure.md) ·
[super-elevation](terms/super-elevation.md) · [exit point](terms/exit-point.md) · [seepage face](terms/seepage-face.md) ·
[경계층 (boundary layer)](terms/boundary-layer.md) · [바닥 전단응력 (bed shear stress, τ)](terms/bed-shear-stress.md) · [흡입 / 분출 (suction / blowing)](terms/suction-blowing.md) ·
[침투력 / 유효 중량 (seepage force / effective weight)](terms/seepage-force.md) · [②와 ③의 대립 ★](terms/mechanism-2-vs-3.md) ·
[Hazen 관계 / 투수계수와 입도](terms/hazen-relation.md) · [Dupuit 가정](terms/dupuit-assumption.md) · [Hjulström 곡선](terms/hjulstrom-curve.md) ·
[침강속도 (settling velocity, w_s)](terms/settling-velocity.md) · [무차원 침강속도 Ω (Dean number)](terms/dean-number.md) · [사주 (offshore bar)](terms/offshore-bar.md) ·
[엔진 vs 변속기 (침식 메커니즘 정리) ★](terms/engine-vs-transmission.md) · [reflective / dissipative 해변](terms/reflective-dissipative.md) ·
[beach dewatering](terms/beach-dewatering.md) · [beach cusp](terms/beach-cusp.md)

**[6. 수치해석 / 시뮬레이션 기법](#6-수치해석--시뮬레이션-기법)**  
[라그랑주 vs 오일러 (MPM의 전제)](terms/lagrangian-vs-eulerian.md) · [MPM (Material Point Method)](terms/mpm.md) · [★ MPM의 입자 ≠ 모래알](terms/mpm-particle-vs-grain.md) ·
[MPM 계보](terms/mpm-lineage.md) · [PIC / FLIP / APIC](terms/pic-flip-apic.md) · [SVD (특이값 분해) ★](terms/svd.md) · [극분해 (polar decomposition)](terms/polar-decomposition.md) ·
[SVD의 다른 용도](terms/svd-other-uses.md) · [변형 그래디언트 F](terms/deformation-gradient.md) · [PBD (Position Based Dynamics)](terms/pbd.md) ·
[XPBD](terms/xpbd.md) · [shape matching](terms/shape-matching.md) · [Gauss-Seidel / Jacobi / SOR ★](terms/gauss-seidel-jacobi-sor.md) ·
[★ 그래프 컬러링 — 현대적 답](terms/graph-coloring.md) · [★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](terms/stiffness.md) · [stiff (수치적으로 뻣뻣하다)](terms/stiff.md) · [크리프 (creep)](terms/creep.md) ·
[PBD의 강성 문제와 compliance](terms/pbd-stiffness-compliance.md) · [우리 프로젝트에서 강성은 거의 안 중요하다](terms/stiffness-in-our-project.md) ·
[명시적 / 암시적 (explicit / implicit)](terms/explicit-implicit.md) · [시간 적분 (time integration)](terms/time-integration.md) · [대칭 선형계](terms/symmetric-linear-system.md) · [약압축성 (weakly compressible) / 상태방정식](terms/weakly-compressible.md) ·
[하이트필드 (heightfield) ★](terms/heightfield.md) · [가상 파이프 (virtual pipes)](terms/virtual-pipes.md) · [반라그랑주 이류 (semi-Lagrangian advection)](terms/semi-lagrangian-advection.md) ·
[자유표면 (free surface)](terms/free-surface.md) · [현상론적 (phenomenological)](terms/phenomenological.md) · [슬러리 (slurry) / 이류 (mudflow)](terms/slurry-mudflow.md)

**[7. 파도](#7-파도)**  
[파고 H / 주기 T / 파장 L](terms/wave-height-period-length.md) · [유의파고 (significant wave height, H_s)](terms/significant-wave-height.md) ·
[분산 관계 (dispersion relation)](terms/dispersion-relation.md) · [취송거리 (fetch)](terms/fetch.md) · [완전발달해상 (fully developed sea)](terms/fully-developed-sea.md) ·
[풍파 (wind sea) vs 너울 (swell) ★](terms/wind-sea-vs-swell.md) · [분산 정렬 (dispersive sorting)](terms/dispersive-sorting.md) ·
[천수 변형 (shoaling)](terms/shoaling.md) · [굴절 (refraction)](terms/refraction.md) · [수심 제한 쇄파 ★](terms/depth-limited-breaking.md) ·
[Iribarren 수 (surf similarity parameter, ξ)](terms/iribarren-number.md) · [처오름 (runup, R) ★](terms/runup.md) · [처오름 상승 (wave setup)](terms/wave-setup.md) ·
[파군 (wave group / set)](terms/wave-group.md) · [스펙트럼 대역 (spectral band)](terms/spectral-band.md) · [장주기파 (infragravity wave)](terms/infragravity-wave.md) · [Shields 파라미터](terms/shields-parameter.md)

**[8. 모래알 자체](#8-모래알-자체)**  
[왜 석영인가](terms/why-quartz.md) · [구성 성분](terms/sand-composition.md) · [중광물 줄무늬 (placer)](terms/placer.md) · [시뮬레이션에 들어가는 물성 4가지](terms/sand-properties.md) ·
[★ 젖으면 왜 어두워지는가 (Jensen 1999의 물리적 근거)](terms/wet-sand-darkening.md)

---

## 1. 역학 기초

| 용어 | |
|---|---|
| **[항력 (drag)](terms/drag.md)** | 속도에 비례해 반대 방향으로 작용하는 저항력. |
| **[변형 (strain, ε)](terms/strain.md)** | 원래 모양에서 얼마나 찌그러졌나. |
| **[응력 (stress, σ)](terms/stress.md)** | 재료 내부의 단위면적당 힘 (Pa). |
| **[편차 / 체적 분해](terms/deviatoric-volumetric.md)** | 응력(또는 변형)을 두 성분으로 쪼갬. |
| **[Hencky 변형 (로그 변형)](terms/hencky-strain.md)** | `ε = ln(늘어난 비율)`. |
| **[연속체 (continuum)](terms/continuum.md)** | 알갱이를 하나씩 세지 않고 공간에 퍼진 밀도·속도 장(field) 으로 취급하는 관점. |
| **[등방성 (isotropy) ★](terms/isotropy.md)** | *방향에 따라 성질이 달라지지 않음. |
| **[구조 이방성 / 조직 (fabric anisotropy) ★](terms/anisotropy-fabric.md)** | 지금 이 순간 접촉이 어느 방향으로 쏠려 있는가. 물성이 아니라 상태. 전단강도가 여기서 나온다. |
| **[구성 모델 (constitutive model)](terms/constitutive-model.md)** | "이만큼 변형시키면 내부에 이만큼 응력이 생긴다"는 관계식. |

## 2. 소성과 항복

| 용어 | |
|---|---|
| **[탄성 / 소성 / 탄소성](terms/elastic-plastic.md)** | 탄성 (elastic): 힘을 빼면 원래대로. |
| **[소성 = 클램프 ★](terms/plasticity-as-clamp.md)** | 가장 중요한 직관. |
| **[항복면 (yield surface) ★](terms/yield-surface.md)** | *응력 공간(stress space) 이라는 추상 공간. |
| **[Drucker-Prager](terms/drucker-prager.md)** | 모래·흙 같은 마찰 재료용 항복면. |
| **[마찰각 (friction angle)](terms/friction-angle.md)** | 원뿔의 기울기를 정하는 값. |
| **[안식각 (angle of repose)](terms/angle-of-repose.md)** | 마른 모래를 부었을 때 생기는 원뿔의 경사각. |
| **[전단 상자 (direct shear box)](terms/shear-box.md)** | 덩어리를 강제로 잘라 얼마나 버티는지 잰다. 안식각이 각도 하나를 줄 때 이쪽은 `φ_peak`·`φ_cv`·`c`·`ψ` 넷을 준다. |
| **[⚠ 점착력 vs 접착력 — 용어 정리](terms/cohesion-vs-adhesion.md)** | *물컵 하나로 둘 다 보인다 |
| **[점착력 (cohesion, c)](terms/cohesion.md)** | *원뿔 꼭짓점을 인장 쪽으로 밀어낸 거리. |
| **[경화 (hardening)](terms/hardening.md)** | 소성 이력에 따라 재료 성질이 변하는 것. |
| **[★ 팽창 / 다져짐 (dilation / compaction)](terms/dilation-compaction.md)** | *모래 덩어리의 부피 = 알갱이 부피 + 빈틈(공극). |
| **[Reynolds 팽창 (Reynolds dilatancy)](terms/reynolds-dilatancy.md)** | *촘촘히 맞물린 모래를 옆으로 밀면 부피가 늘어난다. |
| **[팽창을 시뮬레이션이 읽어야 하는 이유](terms/why-simulate-dilation.md)** | *(a) 팽창하면 약해진다 — 알갱이가 제대로 안 닿아 힘을 전달 못 함. |
| **[★ 다지기 vs 밟기 — 방향이 반대다](terms/tamping-vs-stomping.md)** | *둘 다 힘을 주는데 왜 반대인가 → 시작 상태가 조밀한가 느슨한가. |
| **[순차 파단 (progressive failure)](terms/progressive-failure.md)** | 결합이 동시에 끊어지지 않고, 하나 끊어질 때마다 하중이 이웃으로 넘어가 차례로 끊어지는 것. 덩어리 붕괴의 정체. |
| **[임계상태 (critical state)](terms/critical-state.md)** | 촘촘한 모래를 계속 밀면 → 팽창해서 느슨해짐 느슨한 모래를 계속 밀면 → 수축해서 촘촘해짐 양쪽 다 같은 밀도로 수렴한다 = 임계상태 |
| **[열침식 (thermal erosion)](terms/thermal-erosion.md)** | 하이트필드에서의 안식각. |

## 3. 다공성 매질 / 물의 이동

| 용어 | |
|---|---|
| **[다공성 매질 (porous medium)](terms/porous-medium.md)** | 내부에 빈틈이 뚫려 있어 액체가 통과하는 재료. |
| **[공극률 φ (porosity) ★](terms/porosity.md)** | *모래 더미의 약 40%는 사실 공기다. |
| **[포화도 (saturation)](terms/saturation.md)** | 빈틈 중 물로 찬 비율. |
| **[투과율 k (permeability)](terms/permeability.md)** | 물이 얼마나 잘 통과하는가. |
| **[Darcy의 법칙](terms/darcy-law.md)** | 항력 계수가 `1/k`에 비례하는 이유. |
| **[Richards 방정식](terms/richards-equation.md)** | Darcy를 불포화 상태로 확장한 근본 PDE. |
| **[포장용수량 (field capacity)](terms/field-capacity.md)** | 중력이 못 빼는 잔류 수분량. |
| **[침투 / 유출 (infiltration / exfiltration)](terms/infiltration-exfiltration.md)** | 침투: 물이 모래로 스며듦. |
| **[모세관 다리 (capillary bridge)](terms/capillary-bridge.md)** | 젖은 알갱이 두 개 사이에 생기는 물 목걸이. |
| **[모세관 상승대 (capillary fringe)](terms/capillary-fringe.md)** | 지하수면 위쪽인데도 모세관이 물을 끌어올려 포화 상태인 층. |
| **[젖은 입상체의 3영역](terms/wet-granular-regimes.md)** | 물이 늘어나면서 형태가 바뀐다. |
| **[유변학 (rheology) ★](terms/rheology.md)** | 물질이 어떻게 흐르고 변형되는지. 항복응력이 있어야 모래성이 선다. |

## 4. 증발 / 건조

| 용어 | |
|---|---|
| **[1단계 (등속 건조기, constant-rate period)](terms/drying-stage-1.md)** | 모세관이 안쪽 물을 표면으로 계속 공급 → 표면이 계속 젖어 있음. |
| **[2단계 (감속 건조기, falling-rate period)](terms/drying-stage-2.md)** | 모세관 공급이 못 따라감 → 건조 전선(drying front) 이 안으로 후퇴. |
| **[건조 전선 (drying front)](terms/drying-front.md)** | 젖은 부분과 마른 부분의 경계. |
| **[특성 길이 L_C (characteristic length)](terms/characteristic-length.md)** | 모세관이 물을 표면까지 끌어올릴 수 있는 최대 깊이. |
| **[유효 포화도 Se / 흡입력 / 투수계수](terms/effective-saturation.md)** | *`K`가 `Se`의 3~5제곱 — 조금만 말라도 물 이동 능력이 폭락한다. |

## 5. 해변

| 용어 | |
|---|---|
| **[swash zone](terms/swash-zone.md)** | 파도가 밀려왔다 빠지는 물가 구역. |
| **[지하수면 (water table)](terms/water-table.md)** | *정확한 정의: 공극수압이 대기압과 같아지는 면. |
| **[⚠ 압력을 명시적으로 모델링하지 않는 이유 (설계 결정)](terms/why-no-explicit-pressure.md)** | 정수압 관계에서 `p = ρg·(wt − z)` 이므로 압력과 높이 차이는 같은 정보다. |
| **[super-elevation](terms/super-elevation.md)** | 해변 지하수면이 평균 해수면보다 높게 유지되는 현상. |
| **[exit point](terms/exit-point.md)** | 지하수면이 해변 표면과 만나는 지점. |
| **[seepage face](terms/seepage-face.md)** | exit point와 정지 수면 사이, 지하수가 해변 표면으로 배어나오는 띠. |
| **[경계층 (boundary layer)](terms/boundary-layer.md)** | 바닥의 유속 0에서 자유흐름 속도 U까지 바뀌는 얇은 전이층. |
| **[바닥 전단응력 (bed shear stress, τ)](terms/bed-shear-stress.md)** | 모래 알갱이를 실제로 끌고 가는 힘. |
| **[흡입 / 분출 (suction / blowing)](terms/suction-blowing.md)** | 다공성 벽을 통한 수직 흐름이 경계층을 바꾸는 고전 효과. |
| **[침투력 / 유효 중량 (seepage force / effective weight)](terms/seepage-force.md)** | 물이 모래를 통과하며 알갱이에 거는 항력. |
| **[②와 ③의 대립 ★](terms/mechanism-2-vs-3.md)** | 침투/유출이 퇴적물에 미치는 영향은 두 메커니즘이 정확히 반대 방향이다. |
| **[Hazen 관계 / 투수계수와 입도](terms/hazen-relation.md)** | `K ≈ C · d²` — 투수계수는 알갱이 지름의 제곱에 비례. |
| **[Dupuit 가정](terms/dupuit-assumption.md)** | 지하수 해석해가 깔고 있는 가정 — 등전위선이 수직이고 흐름이 수평이라고 봄. |
| **[Hjulström 곡선](terms/hjulstrom-curve.md)** | 입도별로 침식에 필요한 유속을 그린 곡선. |
| **[침강속도 (settling velocity, w_s)](terms/settling-velocity.md)** | 알갱이가 물속에서 가라앉는 속도. |
| **[무차원 침강속도 Ω (Dean number)](terms/dean-number.md)** | `Ω = H / (w_s · T)` — 해변 형태 분류의 표준 지표. |
| **[사주 (offshore bar)](terms/offshore-bar.md)** | 폭풍이 해변 모래를 바다로 실어내 수중에 만드는 모래 둔덕. |
| **[엔진 vs 변속기 (침식 메커니즘 정리) ★](terms/engine-vs-transmission.md)** | 엔진 = 파도 에너지 → 바닥 전단응력 → 알갱이가 들림. |
| **[reflective / dissipative 해변](terms/reflective-dissipative.md)** | reflective: 가파르고 굵은 입자. |
| **[beach dewatering](terms/beach-dewatering.md)** | 지하수면을 인위적으로 낮춰 퇴적을 유도하는 해안 공법. |
| **[beach cusp](terms/beach-cusp.md)** | 물 빠질 때 해변에 생기는 규칙적인 만곡 무늬. |

## 6. 수치해석 / 시뮬레이션 기법

| 용어 | |
|---|---|
| **[라그랑주 vs 오일러 (MPM의 전제)](terms/lagrangian-vs-eulerian.md)** | 재료를 따라다니느냐(라그랑주) 고정 격자에서 지켜보느냐(오일러). MPM은 둘을 섞은 것. |
| **[FEM 메시 (유한요소 메시)](terms/fem.md)** | 물체를 사면체로 쪼개 꼭짓점을 미지수로 푸는 것. 메시가 재료에 붙어 있어 크게 흐르면 뒤틀려 터진다 — MPM·PBD가 둘 다 "이게 아닌 것"으로 자기를 설명한다. |
| **[MPM (Material Point Method)](terms/mpm.md)** | *재료 정보는 입자가 들고 다니고, 계산은 격자에서 한다. |
| **[★ MPM의 입자 ≠ 모래알](terms/mpm-particle-vs-grain.md)** | 가장 헷갈리는 지점. "입자를 쓰는데 왜 연속체냐?" |
| **[MPM 계보](terms/mpm-lineage.md)** | *입문 교재: Jiang, Schroeder, Teran, Stomakhin, Selle (2016), "The Material Point Method… |
| **[PIC / FLIP / APIC](terms/pic-flip-apic.md)** | 격자 → 입자 전달 방식의 차이. |
| **[SVD (특이값 분해) ★](terms/svd.md)** | 어떤 행렬이든 "회전 → 축을 따라 늘리기 → 회전" 세 단계로 쪼갤 수 있다. |
| **[극분해 (polar decomposition)](terms/polar-decomposition.md)** | SVD의 사촌. 그래픽스에서는 이쪽이 더 흔하다. |
| **[SVD의 다른 용도](terms/svd-other-uses.md)** | PCA·이미지 압축·최소제곱·노이즈 제거 — 전부 같은 분해의 다른 사용법. |
| **[변형 그래디언트 F](terms/deformation-gradient.md)** | "이 입자 주변 공간이 원래 대비 어떻게 변형됐나"를 담은 3×3 행렬. |
| **[PBD (Position Based Dynamics)](terms/pbd.md)** | 힘을 계산하지 않고 위치를 직접 옮겨 제약을 만족시킨다. |
| **[XPBD](terms/xpbd.md)** | PBD의 개량. 강성을 물리 단위로 지정 가능. |
| **[shape matching](terms/shape-matching.md)** | 입자 덩어리를 원래 형태로 되돌리는 제약. |
| **[Gauss-Seidel / Jacobi / SOR ★](terms/gauss-seidel-jacobi-sor.md)** | 제약이 여러 개인데 서로 얽혀 있어 한 번에 다 만족시킬 수 없다 → 반복해서 오차를 줄인다. |
| **[★ 그래프 컬러링 — 현대적 답](terms/graph-coloring.md)** | *아이디어: 서로 영향을 안 주는 것끼리 묶으면 동시에 처리해도 된다. |
| **[워밍 스타트 (warm starting)](terms/warm-starting.md)** | 직전 프레임의 λ를 초기값으로 쓴다. 수렴을 빠르게 하는 게 아니라 수렴한 상태를 프레임 간에 보존하는 것. |
| **[질량 스케일링 (mass scaling)](terms/mass-scaling.md)** | 솔버 안에서만 유효 질량을 손대 나쁜 질량비를 완화. 교정 실험을 오염시키므로 순서가 중요하다. |
| **[★ 캘리브레이션 (calibration)](terms/dem-calibration.md)** | 물성을 표에서 가져오지 않고 실험으로 역산한다. `substep`·반복·μ 를 "솔버 상수"라 부르는 근거. 실험 하나로는 답이 유일하지 않다. |
| **[★ 강성 (stiffness) — 강도·경도·인성과 구분할 것](terms/stiffness.md)** | Unity Spring Joint의 `spring`, 천 시뮬의 `stiffness` 슬라이더. |
| **[stiff (수치적으로 뻣뻣하다)](terms/stiff.md)** | 힘이 너무 강해 타임스텝을 작게 안 하면 발산하는 상태. |
| **[크리프 (creep)](terms/creep.md)** | 부서지지 않는 하중인데도 시간이 지나며 변형이 쌓이는 것. 물리 크리프는 잦아들고 수치 크리프는 일정하다 — 그게 판별법이다. |
| **[PBD의 강성 문제와 compliance](terms/pbd-stiffness-compliance.md)** | PBD는 `k`를 아예 안 쓴다. |
| **[우리 프로젝트에서 강성은 거의 안 중요하다](terms/stiffness-in-our-project.md)** | 모래는 탄성 구간이 극히 좁다. |
| **[명시적 / 암시적 (explicit / implicit)](terms/explicit-implicit.md)** | 명시적: 현재 상태로 힘 계산 → 다음 상태. |
| **[시간 적분 (time integration) — 적분기의 종류](terms/time-integration.md)** | 가속도에서 위치까지 dt씩 끊어 메우는 방법. 전진/준암시적 오일러 · Verlet · RK4 · 후진 오일러. 게임 표준은 준암시적 오일러. |
| **[대칭 선형계](terms/symmetric-linear-system.md)** | `Ax = b`에서 A가 대칭인 경우. |
| **[약압축성 (weakly compressible) / 상태방정식](terms/weakly-compressible.md)** | 물이 아주 조금은 압축되게 허용. |
| **[하이트필드 (heightfield) ★](terms/heightfield.md)** | *2D 격자의 각 칸이 높이값 하나만 들고 있는 지형 표현. |
| **[가상 파이프 (virtual pipes)](terms/virtual-pipes.md)** | 인접한 두 셀이 파이프로 연결됐다고 보는 하이트필드 물 흐름 모델. |
| **[반라그랑주 이류 (semi-Lagrangian advection)](terms/semi-lagrangian-advection.md)** | `s_new(x) = s_old(x − v·dt)` — 속도를 거슬러 올라가 값을 가져옴. |
| **[자유표면 (free surface)](terms/free-surface.md)** | 공기와 맞닿은 액체 표면. |
| **[현상론적 (phenomenological)](terms/phenomenological.md)** | 미시 원리에서 유도한 게 아니라 관찰 결과가 맞도록 식을 맞춰 넣은 모델. |
| **[슬러리 (slurry) / 이류 (mudflow)](terms/slurry-mudflow.md)** | 고체가 잔뜩 섞인 걸쭉한 액체 / 진흙 흐름. |

## 7. 파도

| 용어 | |
|---|---|
| **[파고 H / 주기 T / 파장 L](terms/wave-height-period-length.md)** | 파고: 골에서 마루까지 높이 주기: 마루 두 개가 지나가는 시간 파장: 마루 사이 거리. |
| **[유의파고 (significant wave height, H_s)](terms/significant-wave-height.md)** | 큰 쪽 1/3 파도의 평균 높이. |
| **[분산 관계 (dispersion relation)](terms/dispersion-relation.md)** | 심해에서 `ω² = gk`. |
| **[취송거리 (fetch)](terms/fetch.md)** | 바람이 같은 방향으로 불어간 바다의 길이. |
| **[완전발달해상 (fully developed sea)](terms/fully-developed-sea.md)** | 바람이 충분히 오래·멀리 불어 더 이상 파도가 안 커지는 상태. |
| **[풍파 (wind sea) vs 너울 (swell) ★](terms/wind-sea-vs-swell.md)** | 지금 여기 바람이 만든 거친 파도 vs 먼바다 폭풍이 보낸 매끈하고 주기 긴 파도. 후자가 훨씬 강하다. |
| **[분산 정렬 (dispersive sorting)](terms/dispersive-sorting.md)** | 폭풍은 온갖 주기를 뒤섞어 만들지만, 수천 km를 이동하며 긴 것이 먼저 도착하고 짧은 것이 뒤따르며 스스로 정렬된다. |
| **[천수 변형 (shoaling)](terms/shoaling.md)** | 수심이 얕아지면 파도가 느려지고 파장이 짧아지며 높아진다. |
| **[굴절 (refraction)](terms/refraction.md)** | 파도가 등수심선에 나란해지도록 휘는 것. |
| **[수심 제한 쇄파 ★](terms/depth-limited-breaking.md)** | 파도는 수심 대비 너무 높아지면 부서진다. |
| **[Iribarren 수 (surf similarity parameter, ξ)](terms/iribarren-number.md)** | `ξ = tanβ / √(H/L₀)` — 쇄파 유형을 가른다. |
| **[처오름 (runup, R) ★](terms/runup.md)** | 파도가 부서진 뒤 물이 해변을 올라오는 높이. |
| **[처오름 상승 (wave setup)](terms/wave-setup.md)** | 부서진 파도가 물을 해안으로 밀어붙여 평균 수위 자체를 올리는 것. |
| **[파군 (wave group / set)](terms/wave-group.md)** | 주파수가 약간 다른 파도들이 겹쳐 생기는 맥놀이. |
| **[스펙트럼 대역 (spectral band)](terms/spectral-band.md)** | 연속 스펙트럼 S(f)를 사인파 N개로 쪼갠 조각. 대역 하나의 넓이 S·df가 사인파 하나의 에너지가 된다. |
| **[장주기파 (infragravity wave)](terms/infragravity-wave.md)** | 20~200초의 아주 긴 파동. |
| **[Shields 파라미터](terms/shields-parameter.md)** | 알갱이가 움직이기 시작하는 기준. |

## 8. 모래알 자체

> 시뮬레이션 상수들이 어디서 나왔는지의 출처. 프리셋 설계는 [params.md](params.md) 참조.

| 용어 | |
|---|---|
| **[왜 석영인가](terms/why-quartz.md)** | 암석이 풍화되면 온갖 광물이 나오지만 대부분 화학적으로 분해되거나 물러서 부서진다. |
| **[구성 성분](terms/sand-composition.md)** | 석영·장석·탄산염·현무암·중광물의 밀도·경도·분포 비교표. |
| **[중광물 줄무늬 (placer)](terms/placer.md)** | 파도가 빠진 자리에 남는 검붉은 줄무늬. |
| **[시뮬레이션에 들어가는 물성 4가지](terms/sand-properties.md)** | *① 밀도 — 유효 중량, Shields, 침강속도의 기준 |
| **[★ 젖으면 왜 어두워지는가 (Jensen 1999의 물리적 근거)](terms/wet-sand-darkening.md)** | *마른 모래: 공기↔석영 굴절률 차가 큼 → 표면 첫 알갱이에서 강하게 반사·산란 → 바로 눈으로 돌아옴 → 밝음 |
