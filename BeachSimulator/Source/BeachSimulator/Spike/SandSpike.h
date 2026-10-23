// M0 스파이크 — 모래성이 서는가. 상세는 Docs/design/m0-spike.md
//
// 마찰은 Müller 2020(XPBD 강체) §3.5-3.6 형태다 — papers.md C2.
//   접촉마다 λn·λt 를 서브스텝마다 0에서 시작
//   정지마찰: λt < μs·λn 이면 접선 변위를 통째로 지운다 (위치 레벨)
//   동마찰:   Δv = −v_t/|v_t| · min(h·μd·f_n, |v_t|),  f_n = λn/h²  (속도 레벨)
//
// 입자는 회전이 없어 논문의 r1·r2 와 관성텐서가 사라지고, 질량이 같아 w_i = w_j = 1 이다.

#pragma once

#include "CoreMinimal.h"
#include "SandGpuSolver.h"

/**
 * 솔버 상수.
 *
 * Substeps / Iterations / Friction 은 상수가 아니라 3-0 안식각 스윕의 출력이다.
 * 기본값은 Müller 2020 표1(substep 10~40 × 반복 1)을 따랐다.
 */
struct FSandParams
{
	float  Radius     = 2.5f;    // cm. 지름 5cm → 터널 30cm = 6입자 폭 (확정값)
	float  Gravity    = -980.f;  // cm/s²
	// Frâncu 2014 §8.1 설정 — 서브스텝 1 · 반복 15.
	// Müller 2020 의 "서브스텝 20 × 반복 1" 과 정반대이고, 둘은 λ 누적 범위가 다르다.
	int32  Substeps   = 1;       // ★ 스윕 대상
	int32  Iterations = 15;      // ★ 스윕 대상
	float  Friction   = 0.7f;    // ★ 스윕 대상. μs = μd 로 쓴다
	float  Relaxation = 1.0f;    // Jacobi 평균에 곱하는 ω
	bool   bGaussSeidel = false; // true면 즉시 적용(순차). 진단용 A/B — GPU에선 못 쓴다
	bool   bPositionFriction = true;  // 위치 레벨 정지마찰. 끄면 동마찰 속도 패스만 남는다 (진단용)

	// --- Sequential Positions (Frâncu 2014 §6 Algorithm 1) ---
	//
	// PBD는 위치를 밀고 나중에 v = (p−x)/h 로 속도를 역산한다. 그 역산 때문에
	// 위치 보정이 곧 속도가 되고, 마찰 보정이 다음 접촉의 입력이 되는 고리가 생긴다.
	// SP는 임펄스 하나에서 v 와 x 를 함께 낸다(x 는 정확히 h 배) — 역산이 없다.
	bool   bSequentialPositions = true;
	float  Kv    = 0.1f;    // 잔차의 속도 성분. 0이면 순수 PBD, 0.1이 논문 권장 출발점
	float  Kc    = 1.0f;    // 잔차의 위치 성분
	float  SorOmega = 0.5f; // (36) 의 ω
	/**
	 * M0-b 갈래 A (m0b-spike.md §3-1) — gather 적용. 접촉·결합을 종류별로 "전부 계산 → 한 번 적용".
	 * 수식·클램프·모멘텀은 그대로고 적용 시점만 미룬다 (Frâncu 2014 §7 의 두 패스). 종류 사이(접촉→결합→지면→벽)는 갱신된 v·x 를 본다.
	 * false = 지금까지의 즉시 적용(순차, GS). GPU 는 true 형태만 가능하다.
	 */
	bool   bGatherApply = false;
	/**
	 * A′ 질량 분할 (Tonge 2012, papers C5). gather 에서만 의미. 접촉 j 가 보는 입자 i 의 역질량을 w_i/n_i 가 아니라
	 * n_i·w_i 로 — 입자를 n_i 개 가상 부분체로 쪼개 각 제약이 제 부분체에 대해 정확히 풀고, 적용은 전체 w_i 로 합친다.
	 * n_i = 이 반복에서 입자 i 에 붙은 활성 접촉 수 (+결합 수). 순수 Jacobi 의 n배 과보정을 원리적으로 없앤다.
	 */
	bool   bMassSplit = false;
	/** gather 를 접촉에만 걸고 결합은 순차(즉시 적용)로 둘 때 false. "결합만 GS 급 수렴" (클러스터 컬러링) 이 살릴 수 있는 상한을 재는 진단 스위치 */
	bool   bGatherBonds = true;
	/** 거울 진단: 접촉은 순차(즉시 적용), 결합만 gather. 3단계·큐3 이 결합 순차로도 무너져 접촉 gather 가 범인인지 가른다 */
	bool   bGatherContacts = true;
	/**
	 * ①-b 블록 GS 모사 (m0b-spike §3-1). 0 = 끔. > 0 이면 접촉을 공간 블록(한 변 cm)으로 나눠
	 * **블록 안 접촉은 순차(즉시 적용)**, **블록 경계를 넘는 접촉은 gather**. GPU 워크그룹 = 블록의 CPU 판.
	 * 블록끼리는 서로의 갱신을 못 보므로 단일 스레드 순차 처리가 병렬 블록 처리와 같은 답을 낸다.
	 */
	float  BlockSize = 0.f;
	/** 블록 격자를 홀수 반복마다 반 칸 민다 — 한 격자에서 경계였던 접촉이 다른 격자에선 블록 안이 되어, 모든 접촉이 절반의 반복에서 순차로 풀린다 (#37 마른 더미 크리프) */
	bool   bBlockShift = false;
	/**
	 * M0-b 3-1 ② — 반복 루프(접촉·결합·지면·벽)를 GPU 에서 돈다 (SandGpu 모듈). 이웃 탐색·워밍·예측·파단은 CPU 그대로.
	 * 형태는 ①-b 와 같다: 블록 안 접촉 순차 · 경계 접촉 gather(질량 분할은 bMassSplit) · 결합 gather. BlockSize 0 이면 30cm.
	 * RHI 가 없으면(커맨드릿에 -AllowCommandletRendering 누락) CPU 로 떨어지지 않고 멈춘다 — 조용히 대조군을 재면 안 된다.
	 */
	bool   bGpuSolve = false;
	/**
	 * M0-b 3-1 ② 2단계 — 스텝 전체를 GPU 에서 (FSandGpuSim). 상태가 GPU 에 상주하고 CPU 배열은 읽을 때만 내려받는다.
	 * 이웃 탐색·쌍 생성·워밍·예측·파단·통계가 전부 GPU 패스. 3-4 성능은 이 경로로 잰다. Substeps 1 만 지원.
	 * 입자 추가/제거·FormBonds·RestoreState 뒤에는 전량 다시 올린다 (접촉 λ 는 버린다 — 한 프레임 차가운 시작).
	 */
	bool   bGpuResident = false;
	/** gather 결합의 완화 계수. XPBD Δλ 에 곱한다 (1 = 완화 없음). 접촉의 SorOmega 와 같은 자리 — 결합 9개가 한 입자를 동시에 밀 때의 과보정 억제 */
	float  BondOmega = 1.f;
	/** 질량 분할을 결합에도 거는가. 컴플라이언스 제약은 WSum 을 키우면 α̃ 대비 물러지므로(#36 가설) 기본 false 로 두고 시험한다 */
	bool   bMassSplitBonds = false;
	/**
	 * D′ — APGD (Heyn 2013 §4.4.4, Mazhar 2015 · papers C5). 전부 gather(접촉·결합) 위에 Nesterov 모멘텀을 얹는다.
	 * γ = 투영된 해, y = γ + β(γ − γ_prev) 가 상태(v·x)에 적용된 승수. 적응 재시작(Σ g·Δγ > 0 → θ=1, y←γ) · 끝에 y→γ 정리.
	 * 백트래킹·fall-back 은 없다 — 스텝은 gather 의 ω/W(질량 분할이면 Tonge 대각)가 대신한다. gather 일 때만 의미, 블록 모드와 배타.
	 */
	bool   bApgd = false;
	/** APGD 외삽을 결합에도 거는가. #39: 결합 외삽이 그물을 파단 거리 너머로 당기는 의심 — false 면 결합은 평범한 gather */
	bool   bApgdBonds = true;
	/**
	 * A안 단계 0 (m0b-plan-a.md) — 8색 블록 GS 의 CPU 모사. 순차(GS) 그대로 두고 접촉 순서만 (블록 색, 블록, 블록 안 배치) 로 바꾼다.
	 * 블록 = BlockSize (0 이면 30cm), 소유 = min(i,j) 입자의 블록, 배치 = 블록 안 greedy 컬러링(같은 입자를 두 번 안 만진다). gather 와 함께 쓰지 않는다.
	 */
	bool   bColor8 = false;
	/** 색 순서를 프레임마다 한 칸 돌린다 (R5 — 고정 순서의 방향 편향 확인) */
	bool   bColor8Rotate = false;

	// (38) 모멘텀 항. β_l = min(a·(l/l_max)^b, 1). 접촉(단방향)에서는 a=2 를 시도하라고 논문이 밝힘
	float  MomentumA = 0.f;   // 0이면 모멘텀 없음 (기본은 꺼둔다 — 단계적으로 켠다)
	float  MomentumB = 0.6f;
	/**
	 * 워밍 스타트 (Catto 2005 · terms/warm-starting.md). 0 = 매 서브스텝 λ=0 에서 시작.
	 * 직전 서브스텝(프레임 경계에선 같은 입자쌍)의 λ 에 이 배율을 곱해 **속도 임펄스로 먼저 적용**하고 거기서 반복한다.
	 * 정지 더미의 답은 스텝이 바뀌어도 거의 같으므로, 미수렴 잔차(= 크리프)가 매 스텝 처음부터 쌓이는 것을 막는다 (#23).
	 */
	float  WarmStart = 0.f;
	/** 접선 λ 도 워밍하는가. 현재 접평면에 재투영하고 μ·λn 으로 다시 자른 뒤 적용한다 */
	bool   bWarmTangent = true;
	/** 워밍을 유지하는 최대 gap (반경 배수). 정지 접촉의 gap 요동(±0.01cm)은 넘기고, 떨어진 쌍의 버팀대는 끊는다 */
	float  WarmGapTol = 0.f;
	/**
	 * 정지 판정 임계 — |v| < ε·√(g·d) (m0-spike.md §2-2).
	 *
	 * 이전에는 0.01·(r/dt) 였다. 반경으로는 정규화됐지만 dt 가 식에 남아 있어서,
	 * 시간 조각을 바꾸면 같은 모래의 판정선이 움직였다 (dt 절반 → 임계 두 배).
	 * √(g·d) = 입자가 제 지름만큼 자유낙하했을 때의 속도 = 70 cm/s. 계산과 무관한 물리 규모다.
	 *
	 * ε = 0.002 의 근거: 4초 관측 창에서 표류가 지름의 10%(0.5cm)를 넘지 않아야 한다
	 *   → ε < 0.5 / (70 × 4) ≈ 0.0018.
	 * 에너지로 환산하면 E_k/(m·g·d) ≈ 2×10⁻⁶ 으로 DEM 통상 범위(10⁻⁶~10⁻⁸)와 일치한다.
	 * 이전 값 0.021 은 2.3×10⁻⁴ 로 100배 느슨했고, "정지했는데 계속 기어간다"의 원인이었다.
	 */
	float  SettleEpsilon = 0.002f;
	float  WGlobal    = 0.f;     // 0 = 마른 모래. 점착 비활성
	/** 상자·형틀 벽의 마찰. 0 = 마찰 없는 비계(기본). 무작위 충전을 마찰 없는 상자에 넣으면 벽 층이 미끄러지며 끓는다 (#29 진단) */
	float  WallFriction = 0.f;

	// --- 점착 결합 (m0-spike.md §2-4 · §2-6) ---
	/** 파단 순간의 결합력 / 자기 무게. Rumpf 역산 2.5N ≈ 1.5배 (papers B-S2). 상한이다 — 3-3-B 가 교정 */
	float  BondStrength    = 1.5f;
	/** 끊어지는 거리 / d_rest. ⚠ 1.3 은 V*≈0.2 (매우 젖음) 값 — W_opt 면 1.11 (Lian 1993, B2). 3-2 에서 W 의존으로 */
	float  BreakStretch    = 1.3f;
	/** 붙는 거리 / d_rest — V*≈0.001 의 파단 거리 (B2) */
	float  BondFormStretch = 1.05f;
	int32  BondSlots       = 12;
	/**
	 * 힘이 최대에 닿는 변형률 (d_rest 배수). 결합 힘은 여기까지 선형으로 오르고 그 뒤 **고원**(λ 클램프)이다.
	 * 모세관 다리는 닿자마자 거의 최대 힘을 내고 파단까지 유지한다(Willett 2000) — 파단 거리까지 선형으로 오르는
	 * 스프링(#27 첫 구현)은 블록을 젤리로 만들었다: 절토면이 5~13cm 부풀고 H_c 가 강도에 안 반응했다.
	 */
	float  BondStiffStrain = 0.02f;
	/** cohesion(W) 제어점 4개 — 구간 선형 (§2-4). B1 절벽 · Scheel 2008 고원 */
	float  WDry = 0.05f, WOptStart = 0.15f, WOptEnd = 0.70f, WSat = 0.90f;
	uint32 Seed       = 1337;    // 초기 배치 jitter 전용
};

/**
 * 경사판 배치 — Al-Hashemi 2018 이 정리한 안식각 측정법 넷 중 tilting box.
 *
 * 상자에 채우고 서서히 기울여 **사태가 시작되는 각**을 잰다.
 * 퇴적물 모양을 해석할 필요가 없다는 게 요점 — 모양이 아니라 시점만 본다.
 * 바닥 한 층은 고정해 거칠게 만든다. 매끄러우면 내부 파괴 대신 바닥에서 미끄러진다.
 */
struct FTiltSetup
{
	int32 TargetCount = 3000;
	int32 Layers      = 6;      // 두께. 얇으면 바닥 영향이, 두꺼우면 벽 영향이 커진다
	int32 FixedLayers = 1;      // 바닥 거칠기
	/** 바닥·자유 입자 격자 흔들림 (반경 배수). 0.05 격자는 결정 템플릿이 된다 (#25) */
	float BaseJitter  = 0.f;
	float FreeJitter  = 0.05f;

	/**
	 * 사면 방향(X)을 열어둘 것인가.
	 *
	 * 네 벽으로 가두면 내리막 벽이 덩어리를 통째로 받쳐서, 재는 값이 내부 파괴가 아니라
	 * 벽의 한계가 된다 (실측: atan(μ) 보다 +20~31°). 열어두면 문헌의 경사면 시험과 같아진다.
	 */
	bool bOpenDownhill = true;
	/** +X 면만 연다 — 절토면 (m0-spike §3-3-B). 나머지 세 벽은 그대로 */
	bool bOpenXPlus = false;
};

/**
 * 원통 배치 (m0-spike.md §3-0-D 기각 · §3-0-C).
 *
 * AspectRatio 로 두 실험을 겸한다.
 *   2.5  원통 들어올리기 — 원뿔이 나온다. θr 측정
 *   1.0  기둥 붕괴 — 안쪽 심이 안 무너져 평평한 꼭대기가 남는다. 3-2 관찰용
 */
struct FColumnSetup
{
	int32 TargetCount = 5000;
	float AspectRatio = 2.5f;   // H/R
	int32 FixedLayers = 1;      // 거친 바닥 (Goujon 2003). 0이면 매끄러운 지면

	/**
	 * 거친 바닥을 기둥 반경의 몇 배까지 깔 것인가.
	 *
	 * 기둥 밑에만 깔면 퍼진 모래의 바깥쪽이 매끄러운 지면 위에 놓여 **바닥 마찰이 반경에 따라 달라진다.**
	 * 자락만 완만해져 기울기가 둘인 퇴적물이 되고, 두 측정 방식이 서로 다른 답을 낸다(실측 차이 6~8°).
	 * 문헌의 rough plane 은 더미보다 넓다.
	 */
	float BaseRadiusScale = 2.5f;
	float Radius      = 0.f;    // 0이면 TargetCount·AspectRatio 에서 역산
	float Height      = 0.f;
};

/**
 * 부어 쌓기 배치 (m0-spike.md §3-0-A) — fixed funnel.
 *
 * 원통 들어올리기는 **높은 기둥이라는 불안정 상태를 먼저 만들고** 지지를 뺀다.
 * 밑동이 풀리는 순간 위 전체가 지지를 잃어 한 번에 무너진다(실측: 최대속력 650 cm/s,
 * 이동 입자 322 → 1599, 중심이 꺼진 둔덕). 계단을 잘게 나눠도 임계 노출량에서 똑같이 간다.
 *
 * 부어 쌓기는 더미를 한 알씩 키운다 — **불안정 상태가 존재한 적이 없다.**
 * 입자 하나의 추가는 언제나 작은 교란이라 준정적이 저절로 성립하고,
 * 계단·유지 시간·상한 같은 임의 상수가 통째로 사라진다.
 */
struct FPourSetup
{
	int32 TargetCount = 4000;
	int32 BaseLayers  = 1;      // 거친 바닥 (Goujon 2003)
	float BaseRadius  = 0.f;    // 0이면 예상 퇴적 반경 × 1.2 로 역산
	/** 바닥 격자 흔들림 (반경 배수). 0 이면 간격 D 정사각 격자 — 단분산 구가 그 홈을 템플릿으로 FCC 결정을 키운다 (#23: CN 10, 충전 0.70) */
	float BaseJitter  = 0.f;
	/** > 0 이면 닫힌 상자(반폭 cm) 안에 붓는다 — 절토·터널 블록. 격자 채움은 21층부터 준정적이 안 된다 (#29) */
	float BoxHalf     = 0.f;
	/** Y 반폭. 0 이면 BoxHalf 와 같다 (정사각). 터널 블록은 90×60 이라 직사각 */
	float BoxHalfY    = 0.f;

	/** 현재 더미 꼭대기 위 몇 지름에서 놓는가. 낮을수록 충격이 작다 */
	float DropHeightDiameters = 3.f;
	/** 놓는 지점의 산포 반경. 좁으면 자리가 없어 공급이 막힌다 */
	float SpawnRadiusDiameters = 2.5f;

	int32 PerDrop        = 2;   // 한 번에 몇 개
	int32 IntervalFrames = 2;   // 몇 프레임마다

	// --- 진단 (#22 최대속력 사건) ---
	/** 0 이면 꺼짐. 붓는 중 최대속력이 충돌 속도 √(2gh) 의 이 배수를 넘는 프레임을 찍는다 */
	float EventSpeedFactor = 0.f;
	/** 사건을 이만큼 찍으면 붓기를 멈춘다 (0 = 끝까지) */
	int32 EventStopCount   = 0;
	/** 이 프레임·입자의 반복별 상태를 찍는다 (-1 = 꺼짐) */
	int32 DebugTraceFrame  = -1;
	int32 DebugTraceIndex  = -1;
	/** 정지 판정 후 이만큼 더 돌리고 다시 잰다 — 크리프 검사 (0 = 안 함) */
	int32 HoldFrames       = 0;
};

/** 스냅샷 — 같은 다진 상태에서 여러 각도를 시험하기 위해. */
struct FSandState
{
	TArray<FVector3f> Pos;
	TArray<FVector3f> Vel;
	float TiltDeg = 0.f;
};

/**
 * 잠재 접촉쌍. 브로드페이즈가 프레임당 한 번 만들고, 좁은 판정은 서브스텝마다 다시 한다.
 * λ 는 서브스텝마다 0으로 초기화된다 (논문 §3.5).
 */
struct FSandContact
{
	int32 I = 0;
	int32 J = 0;
	float LambdaN = 0.f;
	float LambdaT = 0.f;

	// SP 경로 — 접선은 2차원(원뿔 투영), 모멘텀은 직전 Δλ 를 든다
	FVector3f LambdaTan  = FVector3f::ZeroVector;   // 월드 좌표의 접선 임펄스
	float     PrevDLambdaN = 0.f;
	FVector3f PrevDLambdaTan = FVector3f::ZeroVector;

	// 이번 반복에 "적용하려 한" 양. Jacobi 평균으로 실제 적용은 1/접촉수 로 줄어들므로,
	// λ 에 더할 때 같은 비율로 깎아야 장부가 맞는다.
	// 이걸 안 맞추면 접촉이 많은 입자일수록 게이트가 일찍 닫혀 마찰이 사라진다.
	float PendingN = 0.f;
	float PendingT = 0.f;

	// APGD (bApgd) — 투영된 해 γ. Lambda* 는 외삽된 y
	float     GammaN = 0.f;
	FVector3f GammaTan = FVector3f::ZeroVector;
};

/**
 * 점착 결합 — 접촉과 별개의 영구 목록. 도구 조작(FormBonds)에만 생기고, 늘어나면 끊어지며, 스스로 되붙지 않는다.
 * 인장만 받는다 (모세관 다리는 당기기만 한다 — Lian 1993). λ 는 스텝을 건너 살아 워밍 스타트의 초기값이 된다.
 */
struct FSandBond
{
	int32 I = 0;
	int32 J = 0;
	float RestLen = 0.f;
	float Lambda  = 0.f;   // XPBD 승수 (길이·질량). 인장이면 음수
	float Gamma   = 0.f;   // APGD 의 투영된 해 (bApgd). Lambda 는 외삽된 y
};

/**
 * 입자 시뮬 코어. 순수 C++ — 틱도 렌더링도 모른다.
 *
 * 실시간 뷰(ASpikeActor)와 배치 스윕(FSpikeBatch)이 같은 코어를 쓰기 위한 분리다.
 * GPU 이식(M0-b)에서 대조군이 되는 것도 이 코어라, 결정성이 요구사항이다.
 */
class FSandSpike
{
public:
	void Init(const FSandParams& InParams, const FColumnSetup& InColumn);
	void InitTiltBox(const FSandParams& InParams, const FTiltSetup& InTilt);

	/** 거친 바닥만 깔고 시작한다. 입자는 AddParticles 로 쌓아 올린다. */
	void InitPour(const FSandParams& InParams, const FPourSetup& InPour);

	/** 실행 중 입자를 더한다. 버퍼가 따라서 늘어난다. */
	void AddParticles(const TArray<FVector3f>& NewPos);

	/** 임의 배치 — 위치와 질량 역수(0 = 고정)를 직접 준다. 단위 검사용 */
	void InitCustom(const FSandParams& InParams, const TArray<FVector3f>& InPos, const TArray<float>& InInvMass);

	/**
	 * 점착 결합 생성 — "도구 조작" 시점. 거리 < d·BondFormStretch 이고 W ≥ WDry 인 쌍을 가까운 순으로 슬롯까지.
	 * 매 프레임 부르면 자발적 재결합이 되어 §4 의 규칙을 깬다. 생성된 개수를 돌려준다.
	 */
	int32 FormBonds();

	/**
	 * 자유 입자 제거 — 터널 뚫기(3-1). 조건에 맞는 자유 입자를 빼고 배열을 압축한다. 결합은 인덱스를 다시 매기고
	 * 빠진 입자에 걸린 결합은 삭제. 접촉 목록·워밍 표는 초기화(한 프레임 차가운 시작). 제거된 개수를 돌려준다
	 */
	int32 RemoveParticles(TFunctionRef<bool(int32)> ShouldRemove);
	void  SetWGlobal(float W) { Params.WGlobal = W; }
	/** cohesion(W) 0~1 — §2-4 구간 선형 */
	float CohesionOfW(float W) const;
	/** 결합 컴플라이언스 α [cm/힘]. k = BondStrength·g/((BreakStretch−1)·d) 에 cohesion(W) 를 곱한 것의 역수. 점착 0 이면 0 (비활성) */
	float BondCompliance() const;

	/** 중력을 기울인다. 상자를 돌리는 대신 상자 좌표계에서 중력을 돌린다 (같은 것). */
	void SetTiltDegrees(float Deg);
	float GetTiltDegrees() const { return TiltDeg; }

	void SaveState(FSandState& Out) const;
	void RestoreState(const FSandState& In);

	/** 프레임 하나. 브로드페이즈 1회 + Substeps 회의 서브스텝. */
	void Step(float Dt);

	/** 가상 벽 제거 = 붕괴 시작. 터널 실험의 "입자 지우기"와 같은 조작이다. */
	void ReleaseWalls() { bWallsActive = false; SettledFrames = 0; }
	/** 절토 — +X 벽만 제거. 뒤는 모래가 받치므로 박벽과 달리 좌굴이 없다 (§3-3-B) */
	void OpenFaceXPlus() { Tilt.bOpenXPlus = true; SettledFrames = 0; }
	/** 다지기 — 중력 배율. 상자에 부은 성긴 충전(0.5)은 스스로 안 조밀해지고 국소 붕괴를 반복한다 (#29). 과잉 하중으로 누른 뒤 1 로 되돌린다 */
	void SetGravityScale(float S) { GravityScale = S; SettledFrames = 0; }
	/**
	 * 준비 단계 전용 속도 감쇠 (DEM 의 local damping, PFC 관행). 프레임 끝에 자유 입자 v *= (1 − D).
	 * μ 0.13 의 무작위 성긴 충전은 스스로 준정적이 안 된다 — 창마다 35~50% 가 재배치되고 감쇠하지 않는다 (#29).
	 * 측정 구간에서는 반드시 0 으로 되돌리고, 준비 창(감쇠 0)으로 진짜 정지인지 확인한다.
	 */
	void SetVelocityDamping(float D) { VelocityDamping = D; SettledFrames = 0; }

	/**
	 * 원통을 여기까지 들어올린다 — 벽은 z > Z 인 입자에만 작용한다.
	 *
	 * 한순간에 없애면 관성이 섞여 기둥 붕괴가 되고, 그건 종횡비에 의존하는 다른 실험이다.
	 * 계단식으로 올리고 매 단계 정지시키면 준정적이 보장된다고 봤으나 틀렸다 — 밑동이 풀리면 전체가 간다 (§3-0-D, #20 기각. 대조용으로 남긴다).
	 */
	void SetWallLift(float Z) { WallLiftZ = Z; SettledFrames = 0; }
	float GetWallLift() const { return WallLiftZ; }

	// --- 관측 ---
	const TArray<FVector3f>& GetPositions() const { SyncFromGpu(); return Pos; }
	int32   Num()          const { return Pos.Num(); }
	const TArray<FVector3f>& GetVelocities() const { SyncFromGpu(); return Vel; }
	const TArray<FSandContact>& GetContacts() const { SyncFromGpu(); return Contacts; }
	const TArray<float>&     GetInvMass()    const { return InvMass; }
	FVector3f GetBoxHalf() const { return BoxHalf; }
	int32   NumPairs()     const { return Contacts.Num(); }
	const TArray<FSandBond>& GetBonds() const { SyncFromGpu(); return Bonds; }
	int32   BondsBroken()  const { return BondsBrokenTotal; }
	int32   BondOverflow() const { return BondSlotOverflow; }
	bool    WallsActive()  const { return bWallsActive; }
	float   AvgSpeed()     const { return LastAvgSpeed; }
	float   MaxSpeed()     const { return LastMaxSpeed; }
	/** 불평형력 비 (A-R7 · §2-1-a) — 스텝 끝 자유 입자 Σ m|v| ÷ 접촉 Σ|λn|. 정지 더미면 0 이 정답, 잔차가 곧 미수렴 */
	float   UnbalancedRatio() const { return LastUnbalanced; }
	int32   FrameIndex()   const { return Frame; }
	float   SimTime()      const { return Time; }
	float   LastStepMs()   const { return StepMs; }

	/** 정지 판정 (m0-spike.md §2-2) — 넷이 공유하는 단일 정의. */
	bool    IsSettled()    const { return SettledFrames >= SettleFramesRequired; }

	/** 진단 — 이 프레임에서 이 입자의 반복별 상태를 로그로 찍는다 (-1 = 꺼짐) */
	int32 DebugTraceFrame = -1;
	int32 DebugTraceIndex = -1;
	int32 ContactCountOf(int32 i) const;
	FString DescribeParticle(int32 i) const;

	const FSandParams&  GetParams() const { return Params; }
	const FColumnSetup& GetColumn() const { return Column; }

private:
	void SpawnColumn();
	void SpawnTiltBox();
	void SpawnPourBase();
	void ResizeBuffers();   // 입자 수가 바뀔 때마다
	void ResetRunState();

	/** ★ 프레임당 1회. 반경에 여유를 둬서 서브스텝 동안 새 접촉이 생겨도 놓치지 않는다. */
	void BuildContacts(float Dt);

	void BuildHash(float CellSizeIn);
	int32 CellKey(const FVector3f& P) const;

	void PredictPositions(float SubDt);
	void SolveContacts();       // Jacobi 누적. 법선 + 정지마찰 게이트

	/** Frâncu 2014 §6 — 임펄스 하나로 v 와 x 를 함께 갱신. 역산이 없다. */
	void SolveContactsSP(float StepDt, int32 Iter, int32 MaxIter);
	/** 결합 — XPBD 컴플라이언스, 인장만. 임펄스 하나로 v·x 함께 (SP 규약) */
	void SolveBondsSP(float StepDt);
	/** 거리 > RestLen·BreakStretch 또는 cohesion(W)=0 이면 삭제. 재결합 없음 */
	void BreakBonds();
	void SolveGroundSP(float StepDt);

	/**
	 * 형틀·상자 벽도 SP 규약을 지켜야 한다 — 임펄스 하나로 v 와 x 를 함께.
	 *
	 * 위치만 자르면 속도가 낡은 채로 남아 다음 스텝이 다시 벽을 파고들고,
	 * 그 보정이 v 로 환원되지 않아 에너지가 쌓인다. SP 에서 없앤 고리를 벽만 되살리는 꼴.
	 * 실측: 18층 원통이 78cm → 164cm 로 부풀었고 반복·서브스텝 어느 쪽으로도 안 잡혔다.
	 */
	void SolveWallsSP(float StepDt);
	void ApplyDeltas();
	void ApplyGatheredVelocityDeltas(float StepDt);   // gather 패스 2 (SP 경로)
	void ApgdSnapToGamma(float StepDt);               // y → γ (재시작·반복 끝). 차이를 임펄스로 적용
	float ApgdBeta = 0.f;       // 이번 반복의 외삽 계수
	double ApgdRestartAcc = 0.0; // Σ g·(γ_new − γ_old) — 양수면 모멘텀이 오르막
	int32 ApgdRestarts = 0;
public:
	int32 LastApgdRestarts() const { return ApgdRestarts; }
private:
	void CountConstraintsPerParticle(bool bBoundaryOnly = false);   // 질량 분할용 n_i (DeltaCount 재사용). 블록 모드면 경계 접촉만
	void OrderContactsColor8();                                     // A안 단계 0: 접촉을 (색, 블록, 배치) 순으로
	int32 LastMaxBatch = 0; float LastAvgBatches = 0.f;
	void AssignBlocks(float Offset = 0.f);                                          // BlockSize 격자로 BlockId 부여
	void SolveIterationsGpu(float StepDt);                           // 1단계: 반복 루프 전체를 GPU 로 (bGpuSolve)
	void StepGpuResident(float Dt);                                  // 2단계: 스텝 전체 (bGpuResident)
	void SyncToGpu();                                                // CPU → GPU 전량 (bGpuDirty 일 때)
	void SyncFromGpu() const;                                        // GPU → CPU 전량 (bGpuStale 일 때). 읽기 접근자가 부른다
	void BuildBondUpload(TArray<uint32>& IJ, TArray<FVector4f>& Data, TArray<uint32>& AdjOffset, TArray<uint32>& Adj) const;
	void BuildWallParams(int32& Mode, uint32& Mask, float& LX, float& LY, float& Limit, float& LiftZ) const;
	void ApplyFrameStats(double SumSpeed, float MaxSpeed, int32 Free, double SumLambda);
public:
	/** GPU 경로의 마지막 프레임 벽시계 (올리기 + 반복 + 내려받기) · CPU 쪽 포장(블록 분류·인접) 시간 */
	float LastGpuMs() const { return GpuMs; }
	float LastGpuPrepMs() const { return GpuPrepMs; }
	/** 마지막 접촉 패스에서 블록 경계를 넘은 활성 접촉 비율 (블록 모드 계측) */
	float BoundaryContactFrac() const { return LastBoundaryFrac; }
private:
	void CommitLambdas();       // 실제 적용 비율로 λ 를 갱신
	void SolveWalls();          // 형틀(원통) 또는 상자. 마찰 없는 단순 투영
	void SolveGround();
	void UpdateVelocities(float SubDt);
	void ApplyDynamicFriction(float SubDt);
	void UpdateSettleState(float Dt);

	FSandParams  Params;
	FColumnSetup Column;
	FTiltSetup   Tilt;
	FPourSetup   Pour;

	TArray<FVector3f> Pos;      // 현재 위치
	TArray<FVector3f> Vel;
	TArray<FVector3f> PrevPos;  // 스텝 시작 위치. 마찰의 위치 오차가 이 차이를 본다
	TArray<float>     InvMass;  // 1 = 자유, 0 = 고정(거친 바닥). 접촉이 이 비율로 나뉜다

	// Jacobi 누적 버퍼. 훑는 동안 Pos 는 읽기 전용이라 모든 접촉이 같은 스냅샷을 본다
	TArray<FVector3f> Delta;
	TArray<int64>     BlockId;          // ①-b 블록 모사용. 셀 좌표를 21비트씩 묶은 정확한 키
	float LastBoundaryFrac = 0.f;
	float GpuMs = 0.f;
	float GpuPrepMs = 0.f;
	TUniquePtr<FSandGpuSim> GpuSim;   // 2단계. bGpuResident 일 때만
	TArray<FSandBond> GpuBondsUploaded;   // GPU 결합 번호 = 이 배열의 순서. 내려받을 때 산 것만 Bonds 로
	bool bGpuDirty = true;            // CPU 가 상태를 바꿨다 → 다음 Step 전에 올린다
	mutable bool bGpuStale = false;   // GPU 가 스텝을 돌았다 → CPU 배열이 낡았다
	bool bGpuPairsValid = false;      // GPU 의 쌍 목록(접촉 λ)이 현재 입자 번호와 맞는다 — 덧붙임·결합 변경은 유지, 제거·초기화·복원은 무효
	TArray<int32>     DeltaCount;
	TArray<float>     DeltaScale;   // 입자별 ω/접촉수. 장부를 맞추는 데 쓴다

	TArray<FSandContact> Contacts;      // 브로드페이즈 산출. 프레임 동안 유지
	TMap<uint64, TPair<float, FVector3f>> WarmLambda;   // 프레임 경계용 — 입자쌍 키 → (λn, λtan)
	TArray<float> GroundLambdaN;        // 지면도 접촉이다. 무한 질량이라 w = 0
	TArray<float> GroundLambdaT;
	TArray<FVector4f> WallLambda;       // 벽별 λn: [0] 법선 +x(−X면) · [1] −x(+X면) · [2] +y · [3] −y. 원통은 [0]. ★ 워밍 대상 (#29)

	TArray<FSandBond> Bonds;            // 영구 목록. FormBonds 가 만들고 BreakBonds 가 지운다
	int32 BondsBrokenTotal = 0;
	int32 BondSlotOverflow = 0;

	// 정렬 기반 공간 해시 — 균일 격자는 빈 공간이 크고, Niagara Neighbor Grid3D는
	// 셀당 상한 초과를 조용히 자른다 (decision-log.md). 여기선 계수 정렬로 오버플로가 없다.
	TArray<int32> CellStart;
	TArray<int32> SortedIdx;
	int32 TableSize = 0;
	float CellSize  = 0.f;

	bool  bWallsActive = true;
	float WallRadius   = 0.f;      // 원통 형틀 (기둥 붕괴)
	float WallLiftZ    = 0.f;      // 이 높이 아래는 벽이 없다 (원통 들어올리기)
	FVector3f BoxHalf  = FVector3f::ZeroVector;   // 상자 반크기 (경사판). 0이면 원통
	bool  bBoxScene    = false;

	FVector3f GravityDir = FVector3f(0.f, 0.f, -1.f);
	float TiltDeg = 0.f;

	int32 Frame  = 0;
	float Time   = 0.f;
	float StepMs = 0.f;

	float LastAvgSpeed = 0.f;
	float LastMaxSpeed = 0.f;
	float LastUnbalanced = 0.f;
	float GravityScale = 1.f;
	float VelocityDamping = 0.f;
	int32 SettledFrames = 0;
	static constexpr int32 SettleFramesRequired = 60;
};
