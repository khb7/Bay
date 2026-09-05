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

	// (38) 모멘텀 항. β_l = min(a·(l/l_max)^b, 1). 접촉(단방향)에서는 a=2 를 시도하라고 논문이 밝힘
	float  MomentumA = 0.f;   // 0이면 모멘텀 없음 (기본은 꺼둔다 — 단계적으로 켠다)
	float  MomentumB = 0.6f;
	float  WGlobal    = 0.f;     // 0 = 마른 모래. 점착 비활성
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

	/**
	 * 사면 방향(X)을 열어둘 것인가.
	 *
	 * 네 벽으로 가두면 내리막 벽이 덩어리를 통째로 받쳐서, 재는 값이 내부 파괴가 아니라
	 * 벽의 한계가 된다 (실측: atan(μ) 보다 +20~31°). 열어두면 문헌의 경사면 시험과 같아진다.
	 */
	bool bOpenDownhill = true;
};

/** 기둥 붕괴 배치 (m0-spike.md §3-0). H/R ≈ 1 — 높으면 관성이 섞여 각이 낮게 깔린다. */
struct FColumnSetup
{
	int32 TargetCount = 5000;
	float Radius      = 0.f;  // 0이면 TargetCount와 H/R=1에서 역산
	float Height      = 0.f;
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

	/** 중력을 기울인다. 상자를 돌리는 대신 상자 좌표계에서 중력을 돌린다 (같은 것). */
	void SetTiltDegrees(float Deg);
	float GetTiltDegrees() const { return TiltDeg; }

	void SaveState(FSandState& Out) const;
	void RestoreState(const FSandState& In);

	/** 프레임 하나. 브로드페이즈 1회 + Substeps 회의 서브스텝. */
	void Step(float Dt);

	/** 가상 벽 제거 = 붕괴 시작. 터널 실험의 "입자 지우기"와 같은 조작이다. */
	void ReleaseWalls() { bWallsActive = false; SettledFrames = 0; }

	// --- 관측 ---
	const TArray<FVector3f>& GetPositions() const { return Pos; }
	int32   Num()          const { return Pos.Num(); }
	const TArray<FVector3f>& GetVelocities() const { return Vel; }
	const TArray<FSandContact>& GetContacts() const { return Contacts; }
	const TArray<float>&     GetInvMass()    const { return InvMass; }
	FVector3f GetBoxHalf() const { return BoxHalf; }
	int32   NumPairs()     const { return Contacts.Num(); }
	bool    WallsActive()  const { return bWallsActive; }
	float   AvgSpeed()     const { return LastAvgSpeed; }
	float   MaxSpeed()     const { return LastMaxSpeed; }
	int32   FrameIndex()   const { return Frame; }
	float   SimTime()      const { return Time; }
	float   LastStepMs()   const { return StepMs; }

	/** 정지 판정 (m0-spike.md §2-2) — 넷이 공유하는 단일 정의. */
	bool    IsSettled()    const { return SettledFrames >= SettleFramesRequired; }

	const FSandParams&  GetParams() const { return Params; }
	const FColumnSetup& GetColumn() const { return Column; }

private:
	void SpawnColumn();
	void SpawnTiltBox();
	void AllocateBuffers();

	/** ★ 프레임당 1회. 반경에 여유를 둬서 서브스텝 동안 새 접촉이 생겨도 놓치지 않는다. */
	void BuildContacts(float Dt);

	void BuildHash(float CellSizeIn);
	int32 CellKey(const FVector3f& P) const;

	void PredictPositions(float SubDt);
	void SolveContacts();       // Jacobi 누적. 법선 + 정지마찰 게이트

	/** Frâncu 2014 §6 — 임펄스 하나로 v 와 x 를 함께 갱신. 역산이 없다. */
	void SolveContactsSP(float StepDt, int32 Iter, int32 MaxIter);
	void SolveGroundSP(float StepDt);
	void ApplyDeltas();
	void CommitLambdas();       // 실제 적용 비율로 λ 를 갱신
	void SolveWalls();          // 형틀(원통) 또는 상자. 마찰 없는 단순 투영
	void SolveGround();
	void UpdateVelocities(float SubDt);
	void ApplyDynamicFriction(float SubDt);
	void UpdateSettleState(float Dt);

	FSandParams  Params;
	FColumnSetup Column;
	FTiltSetup   Tilt;

	TArray<FVector3f> Pos;      // 현재 위치
	TArray<FVector3f> Vel;
	TArray<FVector3f> PrevPos;  // 스텝 시작 위치. 마찰의 위치 오차가 이 차이를 본다
	TArray<float>     InvMass;  // 1 = 자유, 0 = 고정(거친 바닥). 접촉이 이 비율로 나뉜다

	// Jacobi 누적 버퍼. 훑는 동안 Pos 는 읽기 전용이라 모든 접촉이 같은 스냅샷을 본다
	TArray<FVector3f> Delta;
	TArray<int32>     DeltaCount;
	TArray<float>     DeltaScale;   // 입자별 ω/접촉수. 장부를 맞추는 데 쓴다

	TArray<FSandContact> Contacts;      // 브로드페이즈 산출. 프레임 동안 유지
	TArray<float> GroundLambdaN;        // 지면도 접촉이다. 무한 질량이라 w = 0
	TArray<float> GroundLambdaT;

	// 정렬 기반 공간 해시 — 균일 격자는 빈 공간이 크고, Niagara Neighbor Grid3D는
	// 셀당 상한 초과를 조용히 자른다 (decision-log.md). 여기선 계수 정렬로 오버플로가 없다.
	TArray<int32> CellStart;
	TArray<int32> SortedIdx;
	int32 TableSize = 0;
	float CellSize  = 0.f;

	bool  bWallsActive = true;
	float WallRadius   = 0.f;      // 원통 형틀 (기둥 붕괴)
	FVector3f BoxHalf  = FVector3f::ZeroVector;   // 상자 반크기 (경사판). 0이면 원통
	bool  bBoxScene    = false;

	FVector3f GravityDir = FVector3f(0.f, 0.f, -1.f);
	float TiltDeg = 0.f;

	int32 Frame  = 0;
	float Time   = 0.f;
	float StepMs = 0.f;

	float LastAvgSpeed = 0.f;
	float LastMaxSpeed = 0.f;
	int32 SettledFrames = 0;
	static constexpr int32 SettleFramesRequired = 60;
};
