// M1 GPU 메시화 (m1-plan-gpu.md) — 입자 → 메시를 컴퓨트 셰이더로. CPU 판 FSandSurfaceBuilder(BeachSimulator/Mesh) 와 단계마다 1:1.
// 지금은 G1-2 (이웃 · 치우침 · x̄ · 공분산 · 커널 · k_s · 등위값) 까지. 결과를 내려받아 CPU 와 대조한다.

#pragma once

#include "CoreMinimal.h"

struct FSandGpuMeshParams
{
	float NeighborR = 10.f;   // r_i
	float Lambda    = 0.9f;   // 중심 다듬기
	// 커널 (CPU FSandSurfaceParams 와 같은 뜻 · 같은 기본값). 장은 이방성만
	float Radius = 2.5f, H = 5.f, Kr = 4.f, Kn = 0.5f, Ks = 0.f, MinSigma = 0.f, MaxStretch = 2.f, IsoFrac = 0.5f;
	int32 NEps = 25, KnRamp = 0;
};

/** 대조용 내려받기 — 입자 순서는 입력과 같다 */
struct FSandGpuMeshResult
{
	TArray<uint32>    NeighborCount;   // 자기 제외
	TArray<float>     Deviation;
	TArray<FVector4f> Smoothed;        // xyz = x̄
	TArray<FVector4f> CovA;            // xx xy xz yy
	TArray<FVector4f> CovB;            // yz zz
	TArray<FVector4f> Kernel;          // 입자당 4 개: (G0 G1 G2 DetG) (G3 G4 G5 0) (G6 G7 G8 0) (Ext 0)
	float Ks = 0.f, InteriorPhi = 0.f, Iso = 0.f;
	int32 NumInteriorKs = 0, NumIsoSamples = 0, MaxNeighborCount = 0;
	double WallMs = 0.0;               // 올리기 + 패스 + 내려받기 (벽시계)
};

namespace SandGpuMesh
{
	/** 게임 스레드에서 부르고 끝날 때까지 기다린다 */
	SANDGPU_API void Build(const TArray<FVector3f>& Positions, const FSandGpuMeshParams& Params, FSandGpuMeshResult& Out);
}
