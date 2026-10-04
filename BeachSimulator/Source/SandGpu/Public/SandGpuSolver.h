// M0-b 3-1 ② — 솔버 반복 루프의 GPU 판 (m0b-spike.md §2-2-a ⑦⑧⑨).
//
// 이 단계는 **반복 루프만** GPU 로 옮긴다. 이웃 탐색·워밍·예측·파단은 CPU(FSandSpike)가 그대로 하고,
// 서브스텝마다 상태를 올렸다가 전량 내려받는다. 물리 동등성(①-b 와 같은 답인가)을 먼저 닫기 위한 절단선이고,
// 정렬·쌍 생성·λ 해시의 GPU 이식과 성능(3-4)은 그 다음이다.

#pragma once

#include "CoreMinimal.h"

/** 서브스텝 하나의 반복 루프 입출력. 배열은 호출 측(FSandSpike)이 채우고, ★ 표시는 결과로 덮어쓴다. */
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
	TArray<uint32>    BlockRange;     // 2b = 시작, 2b+1 = 개수
	TArray<uint32>    BoundaryAdjOffset;   // 입자 수 + 1
	TArray<uint32>    BoundaryAdj;         // (경계 접촉 순번 << 1) | (입자가 j 면 1)

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

	/** 반복 루프를 GPU 에서 돌리고 끝날 때까지 기다린다. 게임 스레드에서 부른다 */
	SANDGPU_API void Solve(FSandGpuSolveData& Data);
}
