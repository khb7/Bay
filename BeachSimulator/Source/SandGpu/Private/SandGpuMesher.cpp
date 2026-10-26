#include "SandGpuMesher.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"

// 모든 메시 패스가 한 파라미터 구조를 나눠 쓴다 (SandGpuSolver.cpp 와 같은 방식 — 안 쓰는 쪽은 AddPass 가 비운다)
BEGIN_SHADER_PARAMETER_STRUCT(FSandMeshParams, )
	SHADER_PARAMETER(uint32, NumParticles)
	SHADER_PARAMETER(uint32, TableSize)
	SHADER_PARAMETER(uint32, ScanCount)
	SHADER_PARAMETER(float, NeighborR)
	SHADER_PARAMETER(float, Lambda)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PosRO)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellKeyRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellKeyRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellSlotRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellSlotRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SortedIdxRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SortedIdxRO)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, NCountRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, DevRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, SmoothedRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, CovARW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, CovBRW)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ScanIn)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ScanOut)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ScanSumsRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ScanSumsRO)
END_SHADER_PARAMETER_STRUCT()

#define SAND_MESH_SHADER(ClassName, EntryPoint) \
	class ClassName : public FGlobalShader \
	{ \
	public: \
		using FParameters = FSandMeshParams; \
		DECLARE_GLOBAL_SHADER(ClassName); \
		SHADER_USE_PARAMETER_STRUCT(ClassName, FGlobalShader); \
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
		{ \
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
		} \
	}; \
	IMPLEMENT_GLOBAL_SHADER(ClassName, "/Project/Private/SandMesh.usf", EntryPoint, SF_Compute);

SAND_MESH_SHADER(FSandMeshScanBlocksCS,   "ScanBlocksCS")
SAND_MESH_SHADER(FSandMeshScanTopCS,      "ScanTopCS")
SAND_MESH_SHADER(FSandMeshScanAddCS,      "ScanAddCS")
SAND_MESH_SHADER(FSandMeshCellKeyCS,      "CellKeyCS")
SAND_MESH_SHADER(FSandMeshScatterCS,      "ScatterCS")
SAND_MESH_SHADER(FSandMeshNeighborStatsCS, "NeighborStatsCS")

#undef SAND_MESH_SHADER

namespace
{
	constexpr int32 ScanGroupElems = 2048;   // SandMesh.usf SCAN_GROUP × 2

	FRDGBufferRef MeshBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, uint32 Stride, int32 Num)
	{
		return GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, (uint32)FMath::Max(1, Num)), Name);
	}

	template <typename TShader>
	void AddMeshPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, const FGlobalShaderMap* ShaderMap, const FSandMeshParams& Template, int32 ThreadCount, int32 GroupSize)
	{
		FSandMeshParams* Params = GraphBuilder.AllocParameters<FSandMeshParams>();
		*Params = Template;
		TShaderMapRef<TShader> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(Name), Shader, Params, FComputeShaderUtils::GetGroupCount(FMath::Max(1, ThreadCount), GroupSize));
	}

	/** 배타 접두합. Out 은 Count + 1 원소 — 마지막이 총합 */
	void AddMeshScan(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* ShaderMap, const FSandMeshParams& Template, FRDGBufferRef In, FRDGBufferRef Out, uint32 Count)
	{
		const int32 NumGroups = FMath::DivideAndRoundUp((int32)Count, ScanGroupElems);
		checkf(NumGroups <= ScanGroupElems, TEXT("스캔 상한 4M 원소 초과 (%u)"), Count);
		FRDGBufferRef Sums = MeshBuffer(GraphBuilder, TEXT("SandMesh.ScanSums"), sizeof(uint32), ScanGroupElems);
		FSandMeshParams T = Template;
		T.ScanCount  = Count;
		T.ScanIn     = GraphBuilder.CreateSRV(In);
		T.ScanOut    = GraphBuilder.CreateUAV(Out);
		T.ScanSumsRW = GraphBuilder.CreateUAV(Sums);
		T.ScanSumsRO = GraphBuilder.CreateSRV(Sums);
		AddMeshPass<FSandMeshScanBlocksCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshScanBlocks"), ShaderMap, T, NumGroups * (ScanGroupElems / 2), ScanGroupElems / 2);
		AddMeshPass<FSandMeshScanTopCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshScanTop"), ShaderMap, T, ScanGroupElems / 2, ScanGroupElems / 2);
		AddMeshPass<FSandMeshScanAddCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshScanAdd"), ShaderMap, T, NumGroups * (ScanGroupElems / 2), ScanGroupElems / 2);
	}

	template <typename T>
	void Fetch(FRHIGPUBufferReadback& Readback, TArray<T>& Dst, int32 Num)
	{
		Dst.SetNumUninitialized(Num);
		if (Num == 0) { return; }
		const uint32 Bytes = Num * sizeof(T);
		if (const void* Src = Readback.Lock(Bytes)) { FMemory::Memcpy(Dst.GetData(), Src, Bytes); }
		Readback.Unlock();
	}

	struct FMeshReadbacks
	{
		FRHIGPUBufferReadback NCount{ TEXT("SandMesh.NCount") };
		FRHIGPUBufferReadback Dev{ TEXT("SandMesh.Dev") };
		FRHIGPUBufferReadback Smoothed{ TEXT("SandMesh.Smoothed") };
		FRHIGPUBufferReadback CovA{ TEXT("SandMesh.CovA") };
		FRHIGPUBufferReadback CovB{ TEXT("SandMesh.CovB") };
	};

	void BuildRenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FVector4f>& Pos4, const FSandGpuMeshParams& P, FSandGpuMeshResult& Out)
	{
		static FMeshReadbacks* RB = new FMeshReadbacks();   // 렌더 스레드 전용. 프로세스 수명
		const int32 N = Pos4.Num();
		const uint32 TableSize = FMath::RoundUpToPowerOfTwo((uint32)FMath::Max(64, 2 * N));
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGpuMesh"));
			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			FRDGBufferRef Pos = MeshBuffer(GraphBuilder, TEXT("SandMesh.Pos"), sizeof(FVector4f), N);
			GraphBuilder.QueueBufferUpload(Pos, Pos4.GetData(), (uint64)N * sizeof(FVector4f), ERDGInitialDataFlags::NoCopy);
			FRDGBufferRef CellKey   = MeshBuffer(GraphBuilder, TEXT("SandMesh.CellKey"), sizeof(uint32), N);
			FRDGBufferRef CellSlot  = MeshBuffer(GraphBuilder, TEXT("SandMesh.CellSlot"), sizeof(uint32), N);
			FRDGBufferRef CellCount = MeshBuffer(GraphBuilder, TEXT("SandMesh.CellCount"), sizeof(uint32), TableSize);
			FRDGBufferRef CellStart = MeshBuffer(GraphBuilder, TEXT("SandMesh.CellStart"), sizeof(uint32), TableSize + 1);
			FRDGBufferRef SortedIdx = MeshBuffer(GraphBuilder, TEXT("SandMesh.SortedIdx"), sizeof(uint32), N);
			FRDGBufferRef NCount    = MeshBuffer(GraphBuilder, TEXT("SandMesh.NCount"), sizeof(uint32), N);
			FRDGBufferRef Dev       = MeshBuffer(GraphBuilder, TEXT("SandMesh.Dev"), sizeof(float), N);
			FRDGBufferRef Smoothed  = MeshBuffer(GraphBuilder, TEXT("SandMesh.Smoothed"), sizeof(FVector4f), N);
			FRDGBufferRef CovA      = MeshBuffer(GraphBuilder, TEXT("SandMesh.CovA"), sizeof(FVector4f), N);
			FRDGBufferRef CovB      = MeshBuffer(GraphBuilder, TEXT("SandMesh.CovB"), sizeof(FVector4f), N);

			FSandMeshParams T;
			T.NumParticles = N;
			T.TableSize = TableSize;
			T.NeighborR = P.NeighborR;
			T.Lambda = P.Lambda;
			T.PosRO = GraphBuilder.CreateSRV(Pos);
			T.CellKeyRW = GraphBuilder.CreateUAV(CellKey);
			T.CellKeyRO = GraphBuilder.CreateSRV(CellKey);
			T.CellSlotRW = GraphBuilder.CreateUAV(CellSlot);
			T.CellSlotRO = GraphBuilder.CreateSRV(CellSlot);
			T.CellCountRW = GraphBuilder.CreateUAV(CellCount);
			T.CellStart = GraphBuilder.CreateSRV(CellStart);
			T.SortedIdxRW = GraphBuilder.CreateUAV(SortedIdx);
			T.SortedIdxRO = GraphBuilder.CreateSRV(SortedIdx);
			T.NCountRW = GraphBuilder.CreateUAV(NCount);
			T.DevRW = GraphBuilder.CreateUAV(Dev);
			T.SmoothedRW = GraphBuilder.CreateUAV(Smoothed);
			T.CovARW = GraphBuilder.CreateUAV(CovA);
			T.CovBRW = GraphBuilder.CreateUAV(CovB);

			AddClearUAVPass(GraphBuilder, T.CellCountRW, 0u);
			AddMeshPass<FSandMeshCellKeyCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshCellKey"), ShaderMap, T, N, 64);
			AddMeshScan(GraphBuilder, ShaderMap, T, CellCount, CellStart, TableSize);
			AddMeshPass<FSandMeshScatterCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshScatter"), ShaderMap, T, N, 64);
			AddMeshPass<FSandMeshNeighborStatsCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshNeighborStats"), ShaderMap, T, N, 64);

			AddEnqueueCopyPass(GraphBuilder, &RB->NCount, NCount, N * sizeof(uint32));
			AddEnqueueCopyPass(GraphBuilder, &RB->Dev, Dev, N * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, &RB->Smoothed, Smoothed, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->CovA, CovA, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->CovB, CovB, N * sizeof(FVector4f));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		Fetch(RB->NCount, Out.NeighborCount, N);
		Fetch(RB->Dev, Out.Deviation, N);
		Fetch(RB->Smoothed, Out.Smoothed, N);
		Fetch(RB->CovA, Out.CovA, N);
		Fetch(RB->CovB, Out.CovB, N);
		// 커맨드릿은 프레임 틱이 없다 — RDG 풀 · RHI 프레임을 직접 넘긴다 (SandGpuSolver.cpp EndGpuFrame 과 같은 이유)
		FRDGBuilder::TickPoolElements();
		RHICmdList.EndFrame();
	}
}

void SandGpuMesh::Build(const TArray<FVector3f>& Positions, const FSandGpuMeshParams& Params, FSandGpuMeshResult& Out)
{
	check(IsInGameThread());
	Out = FSandGpuMeshResult();
	if (Positions.Num() == 0) { return; }
	TArray<FVector4f> Pos4; Pos4.SetNumUninitialized(Positions.Num());
	for (int32 i = 0; i < Positions.Num(); ++i) { Pos4[i] = FVector4f(Positions[i], 0.f); }
	const double T0 = FPlatformTime::Seconds();
	ENQUEUE_RENDER_COMMAND(SandGpuMeshBuild)([&Pos4, &Params, &Out](FRHICommandListImmediate& RHICmdList)
	{
		BuildRenderThread(RHICmdList, Pos4, Params, Out);
	});
	FlushRenderingCommands();
	Out.WallMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}
