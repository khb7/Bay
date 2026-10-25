// M0-b 3-1 ② — GPU 솔버 (m0b-spike.md §2-2-a).
//
// 두 단계가 있다.
//   1단계 SandGpu::Solve     반복 루프만 GPU. 이웃 탐색·워밍·예측·파단은 CPU(FSandSpike), 서브스텝마다 전량 왕복. 동등성 검증용 절단선 (#37)
//   2단계 FSandGpuSim        스텝 전체가 GPU. 상태가 GPU 에 상주하고 CPU 는 매개변수를 보내고 통계만 받는다. 3-4 성능은 이것으로 잰다

#pragma once

#include "CoreMinimal.h"

/** 1단계 — 서브스텝 하나의 반복 루프 입출력. 배열은 호출 측(FSandSpike)이 채우고, ★ 표시는 결과로 덮어쓴다. */
struct FSandGpuSolveData
{
	int32 Iterations = 0;

	// 입자
	TArray<FVector4f> Pos;        // ★ xyz 위치 · w 역질량
	TArray<FVector4f> Vel;        // ★
	TArray<FVector4f> PrevPos;

	// 접촉 — 앞쪽 NumInternal 개는 블록 안(블록 순), 나머지는 블록 경계를 넘는 쌍
	TArray<uint32>    ContactIJ;      // 2k = i, 2k+1 = j
	TArray<FVector4f> ContactLambda;  // ★ xyz λtan · w λn
	int32 NumInternal = 0;
	TArray<uint32>    BlockRange;     // 블록 b 의 접촉 = [BlockRange[b], BlockRange[b+1]) — 블록 수 + 1
	TArray<uint32>    BoundaryAdjOffset;   // 입자 수 + 1
	TArray<uint32>    BoundaryAdj;         // (접촉 자리 << 1) | (입자가 j 면 1) — 자리는 NumInternal 이상

	// 결합
	TArray<uint32>    BondIJ;
	TArray<FVector4f> BondData;       // ★ x 휴지 길이 · y λ
	TArray<uint32>    BondAdjOffset;
	TArray<uint32>    BondAdj;

	TArray<float>     GroundLambda;   // ★
	TArray<FVector4f> WallLambda;     // ★

	float Radius = 0.f, StepDt = 0.f, Kv = 0.f, Kc = 0.f, SorOmega = 0.f, Friction = 0.f;
	bool  bMassSplit = false;
	float BondAlphaTilde = 0.f, BondLambdaMax = 0.f, BondOmega = 1.f;
	bool  bBondsActive = false;       // cohesion(W) > 0 이고 결합이 있을 때만
	int32 WallMode = 0;               // 0 없음 · 1 상자 · 2 원통
	uint32 WallActiveMask = 0;
	float WallLX = 0.f, WallLY = 0.f, WallLimit = 0.f, WallLiftZ = 0.f, WallFriction = 0.f;

	double GpuMs = 0.0;               // ★ 올리기 + 반복 + 내려받기 (벽시계)
};

namespace SandGpu
{
	/** RHI 가 살아 있는가. 커맨드릿은 -AllowCommandletRendering 없이는 NullRHI 다 */
	SANDGPU_API bool IsAvailable();

	/** 1단계 — 반복 루프를 GPU 에서 돌리고 끝날 때까지 기다린다. 게임 스레드에서 부른다 */
	SANDGPU_API void Solve(FSandGpuSolveData& Data);
}

// ------------------------------------------------------------------------------------------------
// 2단계

/** 전체 상태 올리기 — Init · AddParticles · RemoveParticles · FormBonds · RestoreState 뒤. 접촉 λ 는 버린다 (CPU 의 RemoveParticles 와 같이 한 프레임 차가운 시작) */
struct FSandGpuUpload
{
	TArray<FVector4f> Pos;            // xyz · w 역질량
	TArray<FVector4f> Vel;
	TArray<uint32>    BondIJ;
	TArray<FVector4f> BondData;       // x 휴지 길이 · y λ
	TArray<uint32>    BondAdjOffset;  // N + 1
	TArray<uint32>    BondAdj;        // (결합 번호 << 1) | j 쪽
	TArray<float>     GroundLambda;
	TArray<FVector4f> WallLambda;
};

/** 프레임 매개변수 — 매 Step 마다 전부 보낸다 (W · 기울기 · 벽 상태 · 감쇠가 프레임 사이에 바뀐다) */
struct FSandGpuFrameParams
{
	int32 Iterations = 120;
	float Radius = 2.5f, StepDt = 1.f / 60.f;
	float SearchRange = 0.f;          // 접촉 탐색 반경 = D + 여유. 셀 크기이기도 하다
	float BlockSize = 30.f;
	bool  bColor8 = false;            // A안 단계 1 — 8색 블록 GS (m0b-plan-a.md). 경계 Jacobi 없음
	int32 FrameIndex = 0;             // 색 회전용
	bool  bColor8Par = false;         // A안 단계 2 — 블록 안 배치 병렬 (bColor8 위에서만)
	bool  bSleep = false;              // C안 재우기 (m0b-plan-sleep.md) — -color8par 와 함께
	bool  bParticleMargin = false;     // #50 입자별 탐색 여유 (Müller 2020)
	float MarginMinR = 0.1f;           // #50 입자별 여유 하한 (반경 배수)
	bool  bWakeCold = false;           // #50 깬 첫 프레임은 접촉 워밍 없이
	float SleepVel = 0.25f;           // cm/s (C9 2.5 mm/s)
	int32 SleepFrames = 30;
	float WakeVel = 15.f;             // cm/s (C9 0.15 m/s)
	float WarmStart = 1.f, WarmGapTol = 0.f;
	bool  bWarmTangent = true;
	float Kv = 0.1f, Kc = 1.f, SorOmega = 1.f, Friction = 0.17f;
	float MaxDepenVel = 0.f;          // #47 접촉 분리 속도 상한 (cm/s). 0 = 끔
	bool  bMassSplit = true;
	FVector3f Gravity = FVector3f(0.f, 0.f, -980.f);   // 방향 × 크기 × 배율
	float BondAlphaTilde = 0.f, BondLambdaMax = 0.f, BondOmega = 1.f, BreakStretch = 1.3f;
	bool  bBondsActive = false;       // α > 0 이고 결합이 있다
	bool  bBondsDry = false;          // cohesion(W) = 0 → 전부 끊는다
	float VelocityDamping = 0.f;
	int32 WallMode = 0;
	uint32 WallActiveMask = 0;
	float WallLX = 0.f, WallLY = 0.f, WallLimit = 0.f, WallLiftZ = 0.f, WallFriction = 0.f;
};

/** 프레임 통계 — UpdateSettleState 가 쓰는 것 전부 */
struct FSandGpuStats
{
	double SumSpeed = 0.0;            // 자유 입자 Σ|v|
	float  MaxSpeed = 0.f;
	int32  FreeCount = 0;
	double SumLambda = 0.0;           // Σ|λn| (접촉 + 지면)
	int32  BondsBroken = 0;           // 이 프레임
	int32  NumPairs = 0, NumInternal = 0, NumBoundary = 0;
	int32  PairOverflow = 0;          // > 0 이면 MaxPairs 초과 — 결과 무효
	int32  MaxBatch = 0, SumBatch = 0, NumBatchBlocks = 0, BatchOverflow = 0;   // 단계 2 블록 안 배치 (접촉 있는 블록 기준)
	int32  NumAwakeBlocks = 0, NumAsleep = 0;   // C안 — 풀이를 돈 블록 · 잠든 입자
	double GpuMs = 0.0;               // 스텝 벽시계 (통계 리드백 포함)
};

/** 전량 내려받기 — 계측·도구 조작 전 */
struct FSandGpuDownload
{
	TArray<FVector4f> Pos, Vel;
	TArray<uint32>    PairIJ;         // 풀이 순서. 2k, 2k+1
	TArray<FVector4f> PairLambda;     // xyz tan · w n
	TArray<FVector4f> BondData;       // x < 0 = 끊긴 결합
	TArray<float>     GroundLambda;
	TArray<FVector4f> WallLambda;
};

/**
 * 2단계 — GPU 상주 시뮬. 게임 스레드에서 부르고 각 호출은 끝날 때까지 기다린다 (렌더 명령 플러시).
 * 쌍 목록은 두 벌을 번갈아 써서 다음 프레임의 워밍 조회(쌍 ID → λ)를 지난 프레임 목록의 선형 탐색으로 한다 (§2-2-a ⑤ 의 해시 대신).
 */
class SANDGPU_API FSandGpuSim
{
public:
	FSandGpuSim();
	~FSandGpuSim();

	/** bKeepPairs: 입자를 덧붙이기만 했거나 결합만 바뀌었다 → 쌍 목록(접촉 λ 워밍)을 유지한다. 제거·재배치면 false (한 프레임 차가운 시작) */
	void Upload(const FSandGpuUpload& In, bool bKeepPairs);
	void Step(const FSandGpuFrameParams& P, FSandGpuStats& OutStats);
	void Download(FSandGpuDownload& Out) const;

	int32 NumParticles() const { return N; }
	int32 NumBonds() const { return NumB; }
	int32 MaxPairs() const { return MaxPairCount; }

private:
	struct FState;
	FState* State = nullptr;   // 렌더 스레드 소유 버퍼
	int32 N = 0;
	int32 NumB = 0;
	int32 MaxPairCount = 0;
	int32 LastNumPairs = 0;
};
