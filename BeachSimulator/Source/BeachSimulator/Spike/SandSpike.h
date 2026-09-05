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
	int32  Substeps   = 20;      // ★ 스윕 대상
	int32  Iterations = 1;       // ★ 스윕 대상. 논문은 1 고정
	float  Friction   = 0.7f;    // ★ 스윕 대상. μs = μd 로 쓴다
	float  Relaxation = 1.0f;    // Jacobi 평균에 곱하는 ω
	float  WGlobal    = 0.f;     // 0 = 마른 모래. 점착 비활성
	uint32 Seed       = 1337;    // 초기 배치 jitter 전용
};

/** 기둥 붕괴 배치 (m0-spike.md §3-0). H/R ≈ 1 — 높으면 관성이 섞여 각이 낮게 깔린다. */
struct FColumnSetup
{
	int32 TargetCount = 5000;
	float Radius      = 0.f;  // 0이면 TargetCount와 H/R=1에서 역산
	float Height      = 0.f;
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

	/** 프레임 하나. 브로드페이즈 1회 + Substeps 회의 서브스텝. */
	void Step(float Dt);

	/** 가상 벽 제거 = 붕괴 시작. 터널 실험의 "입자 지우기"와 같은 조작이다. */
	void ReleaseWalls() { bWallsActive = false; SettledFrames = 0; }

	// --- 관측 ---
	const TArray<FVector3f>& GetPositions() const { return Pos; }
	int32   Num()          const { return Pos.Num(); }
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

	/** ★ 프레임당 1회. 반경에 여유를 둬서 서브스텝 동안 새 접촉이 생겨도 놓치지 않는다. */
	void BuildContacts(float Dt);

	void BuildHash(float CellSizeIn);
	int32 CellKey(const FVector3f& P) const;

	void PredictPositions(float SubDt);
	void SolveContacts();       // Jacobi 누적. 법선 + 정지마찰 게이트
	void ApplyDeltas();
	void SolveWalls();          // 형틀. 마찰 없는 단순 투영
	void SolveGround();
	void UpdateVelocities(float SubDt);
	void ApplyDynamicFriction(float SubDt);
	void UpdateSettleState(float Dt);

	FSandParams  Params;
	FColumnSetup Column;

	TArray<FVector3f> Pos;      // 현재 위치
	TArray<FVector3f> Vel;
	TArray<FVector3f> PrevPos;  // 서브스텝 시작 위치. 정지마찰이 이 차이를 본다

	// Jacobi 누적 버퍼. 훑는 동안 Pos 는 읽기 전용이라 모든 접촉이 같은 스냅샷을 본다
	TArray<FVector3f> Delta;
	TArray<int32>     DeltaCount;

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
	float WallRadius   = 0.f;

	int32 Frame  = 0;
	float Time   = 0.f;
	float StepMs = 0.f;

	float LastAvgSpeed = 0.f;
	float LastMaxSpeed = 0.f;
	int32 SettledFrames = 0;
	static constexpr int32 SettleFramesRequired = 60;
};
