// M0 스파이크 — 모래성이 서는가. 상세는 Docs/design/m0-spike.md

#pragma once

#include "CoreMinimal.h"

/**
 * 솔버 상수.
 *
 * Substeps / Iterations / Friction 은 상수가 아니라 3-0 안식각 스윕의 출력이다.
 * 여기 값은 시작점일 뿐 확정값이 아니다 (m0-spike.md §2-1).
 */
struct FSandParams
{
	float  Radius     = 2.5f;    // cm. 지름 5cm → 터널 30cm = 6입자 폭 (확정값)
	float  Gravity    = -980.f;  // cm/s²
	int32  Substeps   = 10;      // ★ 스윕 대상
	int32  Iterations = 2;       // ★ 스윕 대상. substep당 투영 횟수
	float  Friction   = 0.7f;    // ★ 스윕 대상
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
 * 입자 시뮬 코어. 순수 C++ — 틱도 렌더링도 모른다.
 *
 * 실시간 뷰(ASpikeActor)와 배치 스윕(FSpikeBatch)이 같은 코어를 쓰기 위한 분리다.
 * GPU 이식(M0-b)에서 대조군이 되는 것도 이 코어라, 결정성이 요구사항이다.
 */
class FSandSpike
{
public:
	void Init(const FSandParams& InParams, const FColumnSetup& InColumn);

	/** 프레임 하나. 내부에서 Params.Substeps 회 나눠 푼다. */
	void Step(float Dt);

	/** 가상 벽 제거 = 붕괴 시작. 터널 실험의 "입자 지우기"와 같은 조작이다. */
	void ReleaseWalls() { bWallsActive = false; SettledFrames = 0; }

	// --- 관측 ---
	const TArray<FVector3f>& GetPositions() const { return Pos; }
	int32   Num()          const { return Pos.Num(); }
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

	void BuildHash();
	int32 CellKey(const FVector3f& P) const;

	void PredictPositions(float SubDt);
	void SolveContacts();
	void SolveWalls();
	void SolveGround();
	void UpdateVelocities(float SubDt);
	void UpdateSettleState(float Dt);

	FSandParams  Params;
	FColumnSetup Column;

	TArray<FVector3f> Pos;      // 현재 위치
	TArray<FVector3f> Vel;
	TArray<FVector3f> PrevPos;  // 서브스텝 시작 위치. 마찰이 이 차이를 본다

	// 정렬 기반 공간 해시 — 균일 격자는 빈 공간이 크고, Niagara Neighbor Grid3D는
	// 셀당 상한 초과를 조용히 자른다 (decision-log.md). 여기선 계수 정렬로 오버플로가 없다.
	TArray<int32> CellStart;    // 크기 TableSize + 1
	TArray<int32> SortedIdx;    // 셀 순으로 정렬된 입자 인덱스
	int32 TableSize  = 0;
	float CellSize   = 0.f;

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
