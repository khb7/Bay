// M1 GPU 메시화 (m1-plan-gpu.md) — 입자 → 메시를 컴퓨트 셰이더로. CPU 판 FSandSurfaceBuilder(BeachSimulator/Mesh) 와 단계마다 1:1.
// 지금은 G1-2 (이웃 · 치우침 · x̄ · 공분산 · 커널 · k_s · 등위값) 까지. 결과를 내려받아 CPU 와 대조한다.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

struct FSandGpuMeshParams
{
	float NeighborR = 10.f;   // r_i
	float Lambda    = 0.9f;   // 중심 다듬기
	// 커널 (CPU FSandSurfaceParams 와 같은 뜻 · 같은 기본값). 장은 이방성만
	float Radius = 2.5f, H = 5.f, Kr = 4.f, Kn = 0.5f, Ks = 0.f, MinSigma = 0.f, MaxStretch = 2.f, IsoFrac = 0.5f;
	int32 NEps = 25, KnRamp = 0;
	// 띠 · 장 · 메시 (G1-3 · G1-4). 격자 원점 · 크기는 CPU 가 정해 넘긴다 (CPU 와 같은 격자 — 대조용. G3 에선 시뮬 영역에서)
	int32 BandNEps = 25;
	float SurfDev = 0.15f, BandR = 7.5f, Cell = 1.25f;
	FVector3f GridOrigin = FVector3f::ZeroVector;
	FIntVector GridDims = FIntVector(0);
	// 가중 라플라시안 평활화 (CPU FSandSurfaceParams 와 같다)
	int32 SmoothIters = 0;
	float SmoothNorm = 13.f;
	float InteriorPhiFixed = 0.6f;  // > 0 이면 내부 φ 고정 (CPU 와 같다, #19)
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
	// G1-3
	int32 NumBandPoints = 0, NumActiveBlocks = 0, NumBlocks = 0, NumKPairs = 0, KListOverflowBlocks = 0, KPairOverflow = 0;
	double FieldSum = 0.0; int32 FieldPositive = 0;
	TArray<uint32> Band;               // 점별 (-meshdiff 대조용)
	TArray<float>  Field;              // φ − Iso
	// G1-4
	TArray<FVector3f> Vertices, Normals;
	TArray<int32>     Indices;
	int32 NumVertsTotal = 0, NumIndicesTotal = 0;   // 용량과 무관한 실제 수
	// G1-5 GPU 타임스탬프 (ms): [0] 이웃 [1] 커널 · 등위값 [2] 띠 · 장 [3] 메시
	float StageMs[4] = {};
	float KernelSubMs[4] = {};   // 커널 단계 안: 고유분해·커널 · 커널 격자·정렬 · 등위값 표본 · 등위값 마무리
	double WallMs = 0.0;               // 올리기 + 패스 + 내려받기 (벽시계)
};

/**
 * G2 — 화면용 출력 버퍼 (렌더 스레드 소유). FLocalVertexFactory 가 그대로 읽는 형식:
 * 위치 float3 · 접선 2 × FPackedNormal · 32 비트 색인 · 간접 그리기 인자 {색인 수, 1, 0, 0, 0}.
 */
struct FSandGpuMeshRenderBuffers
{
	TRefCountPtr<FRDGPooledBuffer> Position, Tangent, TexCoord, Index, DrawArgs;
	int32 MaxVerts = 0, MaxIndices = 0;
	bool IsValid() const { return Position.IsValid(); }
};

class FSandGpuSim;

namespace SandGpuMesh
{
	/** 렌더 스레드. 용량을 잡는다 (이미 충분하면 그대로) */
	SANDGPU_API void AllocateRenderBuffers(int32 MaxVerts, FSandGpuMeshRenderBuffers& Out);

	/** 렌더 스레드. 입자 → 메시를 화면용 버퍼에 직접 쓴다 (내려받기 없음). OutStats 는 nullptr 가능 */
	/** G3 — 시뮬 위치 버퍼를 그대로 읽는다 (내려받기 · 올리기 없음, 같은 프레임). 게임 스레드 · 결과 내려받기 (시험용) */
	SANDGPU_API void BuildFromSim(const FSandGpuSim& Sim, const FSandGpuMeshParams& Params, FSandGpuMeshResult& Out);
	/** G3 — 렌더 스레드, 화면용 버퍼로 */
	SANDGPU_API void BuildFromSimToRenderBuffers(FRHICommandListImmediate& RHICmdList, const FSandGpuSim& Sim, const FSandGpuMeshParams& Params, FSandGpuMeshRenderBuffers& Buffers);

	SANDGPU_API void BuildToRenderBuffers(FRHICommandListImmediate& RHICmdList, const TArray<FVector4f>& Positions, const FSandGpuMeshParams& Params, FSandGpuMeshRenderBuffers& Buffers);

	/** 게임 스레드에서 부르고 끝날 때까지 기다린다 */
	SANDGPU_API void Build(const TArray<FVector3f>& Positions, const FSandGpuMeshParams& Params, FSandGpuMeshResult& Out);
}
