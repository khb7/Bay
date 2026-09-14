# 제작 도구 상세 — 입자 단위 모래를 실제로 돌리는 것들 (2026-09-14)

> [references.md](references.md) §2-b 의 상세판. 도구마다 **원문(공식 문서 · 논문 · 저장소)** 을 읽고, 우리 설계([system-design §4](../design/system-design.md) · [m0-spike §2-6](../design/m0-spike.md))의 어느 부품과 대응하는지, 무엇을 가져오고 무엇이 없는지를 적는다.
> 순서는 우리와 가까운 순. 숫자는 출처가 있는 것만.

```
1. Houdini POP Grains / Vellum Grains   PBD 알갱이 · 영구 결합 · 파단        ← 우리 설계가 통째로 있음 (오프라인)
2. NVIDIA Flex (2014)                    GPU PBD · 위치 마찰 · Sandcastle 73k  ← 우리 솔버의 조상, 실측표 있음
3. PhysX 5 PBD Particle System           Flex 의 후속, 엔진에 붙는 유일한 것
4. Unity Obi                              CPU PBD, 소규모
5. Blender Molecular+                     입자 링크(결합) 시스템, CPU
6. Taichi / taichi_elements               MPM 연구용, 오프라인 대규모
```

---

## 1. Houdini POP Grains / Vellum Grains (SideFX)

**무엇** — Houdini 의 알갱이 솔버. 영화 VFX 모래의 사실상 표준. 두 세대가 공존한다: **POP Grains**(H15~, 독립 PBD 솔버)와 **Vellum Grains**(H17~, XPBD 기반 Vellum 안의 알갱이 — 천·연체와 같은 계에서 상호작용).

**솔버** — "Position Based Dynamics 로 알갱이를 시뮬레이션한다. 위치 제약만으로 점의 움직임을 정하고, 침투를 유효한 위치로 투영해 푼다." 힘 기반의 오버슈트가 없고 "프레임 간 풀이 시간의 편차가 작다". 알갱이 = 하드 스피어.

**파라미터 → 우리 대응** (POP Grains 노드 문서 기준)

| Houdini | 뜻 (원문 요지) | 우리 |
|---|---|---|
| **Constraint Iterations** | "한 서브스텝에서 제약을 몇 번 강제하나. 입자가 반복마다 바로 옆 이웃만 보므로 **쌓이는 높이에 맞춰야 한다 — 대개 더미 지름의 두 배**" | 반복 120 (#35). **경로 길이 규칙을 매뉴얼이 명시** |
| Internal Collisions · Weight / Stiffness | 입자 간 반발. 강성이 높을수록 덜 튐 | 접촉 제약 |
| **Mass-Shock Scaling** · Shock Scaling Power | "위쪽 입자를 인위적으로 가볍게 해 스택을 안정화". 지수 1 이면 층당 ~15% 비 | A′ 질량 분할 · C3 충격 전파의 제작 도구판 |
| **Clumping (Attraction)** · Weight / Stiffness | "가까운 입자끼리 달라붙는 응집 강도 — 모래의 습기·입자 결합을 흉내" | `cohesion(W)`. 단 **거리 기반이라 자발적 재결합이 있다** |
| Friction · **Static Threshold** | "마찰은 입자 겹침에 비례 — 작은 입자는 겹침이 작아 마찰이 약하다". 정지 판정은 속도/침투 비 | 우리 μ·λn 클램프. "겹침 비례"는 우리 1/d 문제(B-S)와 같은 자리 |
| **Accurate Friction** | "마찰을 **매 제약 반복마다** 적용 — 더 강한 스택" | SP 의 매 반복 마찰 (Frâncu C4). 우리가 #11 에서 찾아간 것을 체크박스 하나로 |
| **Explicit Constraints** | 폴리라인으로 이은 입자쌍이 `restlength` 를 유지. `strain` 이 변형을 추적하고 **시간에 따라 감쇠**. **Break Constraints** = 강도 문턱을 넘으면 삭제. Remove Strands = 실처럼 풀리는 사슬 제거 | **우리 영구 결합 그대로** — 목록 · 휴지 길이 · 파단 · 재결합 없음. `strain` 감쇠 = 우리에게 없는 **연화 가지** (Matter DPSoft 자리) |
| Drift Threshold | 정지한 입자의 잔류 이동을 잘라내는 문턱. 가파른 사면에서 필요 | 우리 크리프(#24). **우리는 워밍 스타트로 근본을 고쳤고, 이쪽은 잘라낸다** |

**안정성 규칙 (문서 원문)** — "서브스텝이나 제약 반복이 모자라면 솔버는 제약을 덜 강제한 채 넘어간다." "최대 강성 ≈ 서브스텝 × 제약 반복." "높이 쌓인 입자는 튀지 않으려면 많은 반복이 필요하다."
Vellum Grains 는 **서브스텝 ≥ 5** 가 기본(1 → 5 로 올림, "안 올리면 터진다"). Vellum 쪽이 서브스텝, POP 쪽이 반복으로 예산을 사는 셈 — Müller 2020(C2) 대 Frâncu 2014(C4) 의 갈림이 한 제품 안에 둘 다 있다.

**젖은 모래 · 모래성** — 셸프 "Wet Sand" 는 "clumping 가중을 켜고 `wetness` 어트리뷰트를 만든다 — 젖은/마른 전이를 아티스트가 칠한다". 모래성은 **Explicit Constraints**(연결 인접 조각 → Vellum Constraints, 파단 문턱 예: 18)로 굳혀 두고 부순다. 즉 Houdini 도 젖은 모래를 두 방식으로 갖고 있다 — 끌림(자발적) 과 결합(영구). **우리는 후자를 골랐다.**

**알려진 함정 — 우리가 겪은 것과 같다**
- "격자 패턴을 피해 안정된 더미 만들기" 가 별도 문서다. 셸프 도구가 규칙 격자로 채우는데, 지터를 켜면 초기 겹침으로 터지니 Relax Iterations 로 풀라고 한다 → **#25 결정 템플릿 · #27 초기 겹침**과 같은 병.
- 포럼 "Vellum grains never settle down" — 600프레임 뒤에도 안 멈춤. 처방: 지터 끄기 · Sphere Packing · Dither Surface. **크리프(#24)의 사용자 버전**이고, 근본 처방(워밍)이 아니라 배치 처방이다.
- Wet Sand 문서: "**해상도를 바꾸면 결과가 바뀐다** — 알갱이 수가 주변 입자 수에 의존하므로". 우리 B-S(1/d) 그대로.

**규모** — 문서에 수치 없음. 튜토리얼 수준에서 "수백만 입자"를 다루며, 오프라인(프레임당 초~분).

**가져올 것 / 없는 것**
- 가져옴: "반복 = 높이" 규칙의 **업계 관행 인용** · `strain` 감쇠(연화) · Mass-Shock(A′) · "해상도 의존" 경고
- 없음: 실시간 · GPU · 결정성 · 아치·터널 검증

---

## 2. NVIDIA Flex — Macklin, Müller, Chentanez, Kim (2014) "Unified Particle Physics for Real-Time Applications" (papers C1)

**무엇** — 모든 것을 같은 반지름의 입자로 만든 GPU PBD 통합 솔버. CUDA. 우리 솔버의 조상.

**★ 실측표 (Table 1, GTX 680, 프레임 16 ms 고정)**

```
장면          입자    서브스텝/프레임   반복/서브스텝   ms/프레임
Bunny Pile    44k        2                2            3.8
Sandcastle    73k        2               12           10.2      ← 마른 더미 + 위치 마찰. 결합 없음
Water Balloons 29k       3               12           12.1
```

**Sandcastle 73k · 반복 24/프레임 · 10.2 ms** = 초당 **1.7억 입자·반복 (2012 GPU).** 우리 요구 20만 × 120 × 60 = 14.4억 → **GTX 680 의 8.4배.**
현세대 GPU 는 GTX 680 의 원시 처리량으로 10~20배 안팎이라, **반복 120 그대로도 20만 · 60fps 가 "안 될 이유가 없는" 범위**다 — Green(반복 1 DEM)으로 셈했던 125배와 크게 다른데, Flex 쪽이 우리와 같은 PBD·같은 반복 구조라 **이 추정이 더 믿을 만하다.** m0b-spike §2-2-a 에 반영.
단, Flex 의 Sandcastle 은 **결합 없는 마른 더미**다(Fig. 11: "위치 마찰이 가파른 더미를 300프레임 유지, 옛 PBD 마찰은 거의 무너짐"). 결합 그물의 비용은 여기 없다.

**우리와 겹치는 것**
- §6.1 위치 마찰: 침투 해소 뒤 스텝 시작 대비 접선 변위를 `μ_s·d` 안이면 전부 되돌리고(정지), 넘으면 `μ_k·d` 로 클램프(운동). **"마찰 강도가 반복 수에 다소 의존한다"** 는 한계를 논문이 명시 — 우리 #24 의 출발점.
- §4.4 완화: 제약 평균화(`Δx/n_i`)가 "너무 공격적이라 반복이 늘어" **SOR ω ∈ [1, 2]** 로 되찾음. 우리 #4 평균 모드 → Frâncu §3.4.1 → Tonge 분할과 같은 자리.
- §4.5 **입자 슬리핑**: "스텝 끝에 제약이 다 안 맞으면 위치 드리프트가 생긴다. 속도가 문턱 아래면 입자를 얼린다." → 크리프의 업계 처방 = **얼리기**. 우리는 교정이 오염되니 M0 에선 안 얼리고 워밍으로 고쳤다(#24); 출하는 타일 sleep/wake(M4).
- Fig. 11 자체가 우리 #24 그림이다 — "정지한 더미가 서서히 무너진다".

**없는 것** — 결합 그물 · 순차 파단 · 젖은 모래(점착은 유체 쪽 Akinci 모델만).

---

## 3. NVIDIA PhysX 5 — PBD Particle System (5.1~5.5)

**무엇** — Flex 의 후속으로 PhysX 5 에 들어간 GPU 전용 PBD 입자계. "CUDA 가능 GPU 필수." 유체 · **granular** · 천 · 팽창체 · 강체(shape matching)를 한 버퍼 체계로. **게임 엔진(UE·Unity·Omniverse)에 실제로 붙는 유일한 GPU PBD 알갱이.**

**구조** — `PxParticleBuffer`(유체·granular) / `PxParticleAndDiffuseBuffer` / `PxParticleClothBuffer` / `PxParticleRigidBuffer`. **Phase** 로 자기충돌·유체 여부·재료를 묶는다("같은 phase = 같은 재료, 다른 phase 끼리는 충돌"). 이웃 상한을 생성 시 고정(예: 96). `solidRestOffset` 이 granular 입자 간 거리, `fluidRestOffset` 이 유체 쪽.

**재료 `PxPBDMaterial`** — friction · damping · **adhesion**(+ adhesionRadiusScale: 휴지 오프셋 배수로 끊기는 거리) · **cohesion** · viscosity · vorticityConfinement · surfaceTension · lift · drag · CFL · **particleFrictionScale** · **particleAdhesionScale** · gravityScale.
→ 점착이 **스칼라 둘(adhesion · cohesion)** 이고 "끊기는 거리"가 휴지 오프셋 배수 하나다. 우리 `cohesion(W)` + `breakStretch` 와 같은 형태지만 **결합 목록이 없다** — 매 스텝 거리 기반.

**솔버** — 문서가 반복·서브스텝 수치를 안 준다(씬 설정). granular 전용 지침·샘플 없음(SnippetPBF · PBDCloth · PBDInflatable 만).

**가져올 것 / 없는 것**
- 가져옴: 3-4 의 "남들은 어디까지" 비교점 · phase/버퍼 설계(우리 타일 · 잠든 입자 제외와 대응) · adhesionRadiusScale = 파단 거리 파라미터화의 선례
- 없음: 결합 그물 · 파단 이력 · 순차 파단 · 결정성 언급

---

## 4. Unity Obi (Virtual Method Studio)

**무엇** — Unity 용 통합 입자 물리(천 · 밧줄 · 연체 · 유체). CPU(Burst/Oni 백엔드). **granular** 재료 = "강체 구처럼 행동하는 입자 — 모래 같은 효과(바위 · 파편)". 유체 밀도 제약을 뺀 것.

**솔버 설정 (ObiSolver)** — 제약 종류별 **반복**(기본 3, 거리 제약은 15+ 권장) · **Sequential(빠른 수렴 · 순서 의존) vs Parallel(안정 · 순서 무관)** 평가 모드 · **SOR 1~2**(1~1.5 권장) · **Sleep Threshold**(운동에너지 문턱 아래 동결, 비용 절감은 없음) · 감쇠(0.15 권장) · **Shock Propagation**("입자 질량을 인위적으로 늘려 적층 안정").

**규모** — 공식 벤치(4코어 Xeon, 2013급): 유체 2,000 입자 3.2 ms · 4,500 입자 5.0 ms. "타임스텝이 2~3 개 필요해지는 순간 비용이 뛴다." Unity DOTS/Burst 계열 통칭 2.5만~5만 입자에서 60fps 가 상한이라는 서베이(MDPI 2025).

**우리와 겹치는 것** — Sequential/Parallel 스위치가 **우리 GS/gather 스위치 그대로**이고, 문서가 "Parallel 은 순서 무관·안정, Sequential 은 수렴 빠름" 이라고 같은 트레이드를 적는다. Shock Propagation = A′·C3.

**없는 것** — GPU · 규모(우리 20만의 1/10 이하) · 결합.

---

## 5. Blender Molecular+ (molecular-plus, u3dreal 유지)

**무엇** — Blender 입자에 **자기충돌 + 입자 간 링크(결합)** 를 붙이는 애드온. 원조 Pyroevil 의 Molecular Script(2013~, "Really 1998 · Softimage Lagoa 에서 영감"), 현재 molecular-plus 로 Blender 4.2~5.1 유지(v1.21.9). Cython 코어 · 멀티스레드 · **공간 해시 격자**(KD-tree 대체, 이웃 질의 1.5~3배). "100만 입자" 데모.

**링크 시스템** — 입자 간 결합을 만들고(초기 이웃), 강성·감쇠, **인장/압축 파단 문턱**, **relink**(재결합) 옵션. 프리셋 "sand · water · iron".

**우리와 겹치는 것** — 구조가 우리 영구 결합 목록과 가장 닮은 **오픈소스 구현**이다(링크 = 결합, 파단 = 삭제). 다만 relink 를 켜면 자발적 재결합.

**없는 것** — GPU · 실시간 · 마찰 모델 문서화 부족 · 검증.

---

## 6. Taichi / taichi_elements (연구용)

**무엇** — Taichi 언어의 MLS-MPM 엔진. 물 · 탄성 · 눈 · **모래(Drucker-Prager)**. GPU. "V100 에서 1억 입자 8시간", 양자화로 입자당 40B → 10억 입자(80GB). Blender 애드온 실험적. "work in progress".

**우리와의 거리** — MPM 이라 우리 PBD 와 다른 계열(papers A). 프로토타입 검증(예: Matter 대신 짧은 실험)엔 쓸 수 있으나 엔진 밖·오프라인. GeoTaichi(지반) 도 같은 계열.

---

## 7. 한눈에

| | 솔버 | GPU | 결합(영구·파단) | 젖은 모래 | 실측 규모 | 우리와 |
|---|---|---|---|---|---|---|
| Houdini Grains | PBD / XPBD | ✗ | **○** Explicit Constraints · strain 감쇠 | 끌림 + 결합 둘 다 | 수백만(오프라인) | 설계 대응 가장 완전 |
| Flex 2014 | PBD Jacobi + SOR | ○ CUDA | ✗ | ✗ | **73k · 반복 24 · 10.2 ms (GTX 680)** | 성능 기준점 |
| PhysX 5 | PBD | ○ CUDA | ✗ (adhesion/cohesion 스칼라) | 스칼라 | 미공개 | 엔진 붙는 유일한 것 |
| Obi | PBD, GS/Jacobi 선택 | ✗ | ✗ | ✗ | 수천 | GS/gather 스위치 선례 |
| Molecular+ | 링크 입자 | ✗ | **○** 링크 · 파단 · relink | ✗ | 100만(오프라인) | 결합 목록 오픈 구현 |
| taichi_elements | MLS-MPM | ○ | 연속체 | ✗ | 1억+(오프라인) | 다른 계열 |

---

## 8. 우리 설계에 돌아오는 것

1. **3-4 사전 추정을 고친다.** Flex Sandcastle(73k · 반복 24 · 10.2 ms · GTX 680)로 셈하면 우리 20만 × 120 × 60fps 는 **GTX 680 의 8.4배** — 현세대 GPU 범위 안. Green(DEM 반복 1)으로 셈한 125배는 비교 대상이 틀렸다. 남는 미지수는 **결합 그물**의 비용뿐이고, 그건 어느 실물에도 없다.
2. **모두가 크리프를 "얼려서" 처리한다** — Flex 슬리핑, Houdini Drift/Static Threshold, Obi Sleep Threshold. 우리만 워밍으로 근본을 고쳤다(#24). 교정 단계에선 그게 맞고(얼리면 자가 오염, C3), 출하 단계엔 타일 sleep 이 같은 자리다.
3. **"반복 = 높이" 는 관행이다.** Houdini 매뉴얼이 "더미 지름의 두 배" 라고 적는다. #35 의 하한 120 은 이상값이 아니다.
4. **질량 스케일링은 표준 파라미터다** — Houdini Mass-Shock, Obi Shock Propagation. 3단계 실패(결합 아래서 gather 미수렴)에 대한 업계의 첫 답이 이것이라, A′ 의 **결합에도** 분할을 거는 변형을 봐야 한다.
5. **영구 결합 + 파단 + 재결합 없음**은 Houdini Explicit Constraints · PFC BPM · Molecular 링크가 공유하는 형태다. 우리가 뺀 것은 자발적 재결합(Clumping · relink)뿐이고, 그건 의도다.
6. Houdini 의 `strain` 감쇠는 **연화 가지의 제작 도구판** — 3-2 가 부슬부슬이면 Matter DPSoft 와 함께 참조.
7. "해상도를 바꾸면 결과가 바뀐다"(Houdini Wet Sand) — 우리 B-S 의 1/d 문제를 도구 문서가 경고문으로 갖고 있다.
