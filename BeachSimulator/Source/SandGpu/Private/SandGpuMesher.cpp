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
	SHADER_PARAMETER(float, GridCell)
	SHADER_PARAMETER(float, H)
	SHADER_PARAMETER(float, Kr)
	SHADER_PARAMETER(uint32, NEps)
	SHADER_PARAMETER(float, Kn)
	SHADER_PARAMETER(uint32, KnRamp)
	SHADER_PARAMETER(float, KsFixed)
	SHADER_PARAMETER(float, MinSigma)
	SHADER_PARAMETER(float, MaxStretch)
	SHADER_PARAMETER(float, PVol)
	SHADER_PARAMETER(float, IsoFrac)
	SHADER_PARAMETER(uint32, SampleStep)
	SHADER_PARAMETER(uint32, NumSamples)
	SHADER_PARAMETER(uint32, NumPartials)
	SHADER_PARAMETER(FVector3f, GOrigin)
	SHADER_PARAMETER(FUintVector3, GD)
	SHADER_PARAMETER(FUintVector3, BD)
	SHADER_PARAMETER(float, Cell)
	SHADER_PARAMETER(float, BandReach)
	SHADER_PARAMETER(uint32, BandNEps)
	SHADER_PARAMETER(float, SurfDev)
	SHADER_PARAMETER(uint32, MaxPairsK)
	SHADER_PARAMETER(uint32, MaxVerts)
	SHADER_PARAMETER(uint32, MaxIndices)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PosRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, GridPosRO)

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

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, NCountRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SmoothedRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CovARO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, CovBRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CountersRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CountersRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, EigValRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, EigValRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, EigVecRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, EigVecRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, PartialRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, PartialRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, ScalarsRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, ScalarsRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, KernelRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, KernelRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, SamplePhiRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, SamplePhiRO)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, DevRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BandRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BandRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockActiveRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockActiveRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockKCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockKStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockKCursorRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockKItemsRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockKItemsRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, FieldRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, FieldRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockVCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockVStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockTCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockTStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellVertRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellVertRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, VertexRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, NormalRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, IndexRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawArgsRW)

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
SAND_MESH_SHADER(FSandMeshSortCellsCS,    "SortCellsCS")
SAND_MESH_SHADER(FSandMeshNeighborStatsCS, "NeighborStatsCS")
SAND_MESH_SHADER(FSandMeshMaxCountCS,     "MaxCountCS")
SAND_MESH_SHADER(FSandMeshEigenCS,        "EigenCS")
SAND_MESH_SHADER(FSandMeshFinalizeKsCS,   "FinalizeKsCS")
SAND_MESH_SHADER(FSandMeshKernelCS,       "KernelCS")
SAND_MESH_SHADER(FSandMeshIsoSampleCS,    "IsoSampleCS")
SAND_MESH_SHADER(FSandMeshFinalizeIsoCS,  "FinalizeIsoCS")
SAND_MESH_SHADER(FSandMeshMarkBandCS,     "MarkBandCS")
SAND_MESH_SHADER(FSandMeshKCountCS,       "KCountCS")
SAND_MESH_SHADER(FSandMeshKFillCS,        "KFillCS")
SAND_MESH_SHADER(FSandMeshFieldCS,        "FieldCS")
SAND_MESH_SHADER(FSandMeshVertCountCS,    "VertCountCS")
SAND_MESH_SHADER(FSandMeshVertWriteCS,    "VertWriteCS")
SAND_MESH_SHADER(FSandMeshFaceCountCS,    "FaceCountCS")
SAND_MESH_SHADER(FSandMeshFaceWriteCS,    "FaceWriteCS")
SAND_MESH_SHADER(FSandMeshMeshArgsCS,     "MeshArgsCS")

#undef SAND_MESH_SHADER

namespace
{
	constexpr int32 ScanGroupElems = 2048;   // SandMesh.usf SCAN_GROUP × 2

	FRDGBufferRef MeshBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, uint32 Stride, int32 Num)
	{
		return GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, (uint32)FMath::Max(1, Num)), Name);
	}

	/** 그룹 수를 직접 준다 (영역 = 그룹) */
	template <typename TShader>
	void AddMeshGroupPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, const FGlobalShaderMap* ShaderMap, const FSandMeshParams& Template, FIntVector Groups)
	{
		FSandMeshParams* Params = GraphBuilder.AllocParameters<FSandMeshParams>();
		*Params = Template;
		TShaderMapRef<TShader> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(Name), Shader, Params, Groups);
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
		FRHIGPUBufferReadback Kernel{ TEXT("SandMesh.Kernel") };
		FRHIGPUBufferReadback Scalars{ TEXT("SandMesh.Scalars") };
		FRHIGPUBufferReadback Counters{ TEXT("SandMesh.Counters") };
		FRHIGPUBufferReadback Band{ TEXT("SandMesh.Band") };
		FRHIGPUBufferReadback Field{ TEXT("SandMesh.Field") };
		FRHIGPUBufferReadback BlockActive{ TEXT("SandMesh.BlockActive") };
		FRHIGPUBufferReadback BlockKStart{ TEXT("SandMesh.BlockKStart") };
		FRHIGPUBufferReadback Vertex{ TEXT("SandMesh.Vertex") };
		FRHIGPUBufferReadback Normal{ TEXT("SandMesh.Normal") };
		FRHIGPUBufferReadback Index{ TEXT("SandMesh.Index") };
		FRenderQueryRHIRef Ts[5];
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

			// G1-5 — 단계 경계 GPU 타임스탬프 (SandGpuSolver 와 같은 방식, papers C11)
			const bool bStamps = GSupportsTimestampRenderQueries;
			if (bStamps && !RB->Ts[0].IsValid()) { for (FRenderQueryRHIRef& Q : RB->Ts) { Q = RHICreateRenderQuery(RQT_AbsoluteTime); } }
			auto Stamp = [&](int32 Idx)
			{
				if (!bStamps) { return; }
				FRHIRenderQuery* Q = RB->Ts[Idx].GetReference();
				GraphBuilder.AddPass(RDG_EVENT_NAME("SandMeshStamp%d", Idx), ERDGPassFlags::NeverCull, [Q](FRHICommandListImmediate& Cmd) { Cmd.EndRenderQuery(Q); });
			};
			Stamp(0);

			FSandMeshParams T;
			T.NumParticles = N;
			T.TableSize = TableSize;
			T.NeighborR = P.NeighborR;
			T.Lambda = P.Lambda;
			T.GridCell = P.NeighborR;
			T.H = P.H; T.Kr = P.Kr; T.NEps = P.NEps; T.Kn = P.Kn; T.KnRamp = P.KnRamp; T.KsFixed = P.Ks;
			T.MinSigma = P.MinSigma; T.MaxStretch = P.MaxStretch; T.IsoFrac = P.IsoFrac;
			T.PVol = (4.f / 3.f) * PI * P.Radius * P.Radius * P.Radius;
			const int32 SampleStep = FMath::Max(1, N / 2000);
			const int32 NumSamples = FMath::DivideAndRoundUp(N, SampleStep);
			const int32 NumPartials = FMath::DivideAndRoundUp(N, 64);
			T.SampleStep = SampleStep; T.NumSamples = NumSamples; T.NumPartials = NumPartials;
			T.PosRO = GraphBuilder.CreateSRV(Pos);
			T.GridPosRO = T.PosRO;
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
			AddMeshPass<FSandMeshSortCellsCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshSortCells"), ShaderMap, T, TableSize, 64);
			AddMeshPass<FSandMeshNeighborStatsCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshNeighborStats"), ShaderMap, T, N, 64);

			Stamp(1);
			// ── G1-2 커널 · k_s · 등위값 ──
			FRDGBufferRef Counters  = MeshBuffer(GraphBuilder, TEXT("SandMesh.Counters"), sizeof(uint32), 8);
			FRDGBufferRef EigVal    = MeshBuffer(GraphBuilder, TEXT("SandMesh.EigVal"), sizeof(FVector4f), N);
			FRDGBufferRef EigVec    = MeshBuffer(GraphBuilder, TEXT("SandMesh.EigVec"), sizeof(FVector4f), 3 * N);
			FRDGBufferRef Partial   = MeshBuffer(GraphBuilder, TEXT("SandMesh.Partial"), sizeof(FVector2f), NumPartials);
			FRDGBufferRef Scalars   = MeshBuffer(GraphBuilder, TEXT("SandMesh.Scalars"), sizeof(float), 8);
			FRDGBufferRef Kernel    = MeshBuffer(GraphBuilder, TEXT("SandMesh.Kernel"), sizeof(FVector4f), 4 * N);
			FRDGBufferRef SamplePhi = MeshBuffer(GraphBuilder, TEXT("SandMesh.SamplePhi"), sizeof(FVector2f), NumSamples);
			T.NCountRO = GraphBuilder.CreateSRV(NCount);
			T.SmoothedRO = GraphBuilder.CreateSRV(Smoothed);
			T.CovARO = GraphBuilder.CreateSRV(CovA);
			T.CovBRO = GraphBuilder.CreateSRV(CovB);
			T.CountersRW = GraphBuilder.CreateUAV(Counters);
			T.CountersRO = GraphBuilder.CreateSRV(Counters);
			T.EigValRW = GraphBuilder.CreateUAV(EigVal);
			T.EigValRO = GraphBuilder.CreateSRV(EigVal);
			T.EigVecRW = GraphBuilder.CreateUAV(EigVec);
			T.EigVecRO = GraphBuilder.CreateSRV(EigVec);
			T.PartialRW = GraphBuilder.CreateUAV(Partial);
			T.PartialRO = GraphBuilder.CreateSRV(Partial);
			T.ScalarsRW = GraphBuilder.CreateUAV(Scalars);
			T.ScalarsRO = GraphBuilder.CreateSRV(Scalars);
			T.KernelRW = GraphBuilder.CreateUAV(Kernel);
			T.KernelRO = GraphBuilder.CreateSRV(Kernel);
			T.SamplePhiRW = GraphBuilder.CreateUAV(SamplePhi);
			T.SamplePhiRO = GraphBuilder.CreateSRV(SamplePhi);
			AddClearUAVPass(GraphBuilder, T.CountersRW, 0u);
			AddClearUAVPass(GraphBuilder, T.ScalarsRW, 0u);
			AddMeshPass<FSandMeshMaxCountCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshMaxCount"), ShaderMap, T, N, 64);
			AddMeshPass<FSandMeshEigenCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshEigen"), ShaderMap, T, N, 64);
			AddMeshPass<FSandMeshFinalizeKsCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshFinalizeKs"), ShaderMap, T, 1, 1);
			AddMeshPass<FSandMeshKernelCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshKernel"), ShaderMap, T, N, 64);

			// 커널 격자 — 점 = x̄, 셀 = 장 지지 반경 (CPU 의 KGrid)
			const float Support = 2.f * P.H * P.MaxStretch;
			FRDGBufferRef KCellKey   = MeshBuffer(GraphBuilder, TEXT("SandMesh.KCellKey"), sizeof(uint32), N);
			FRDGBufferRef KCellSlot  = MeshBuffer(GraphBuilder, TEXT("SandMesh.KCellSlot"), sizeof(uint32), N);
			FRDGBufferRef KCellCount = MeshBuffer(GraphBuilder, TEXT("SandMesh.KCellCount"), sizeof(uint32), TableSize);
			FRDGBufferRef KCellStart = MeshBuffer(GraphBuilder, TEXT("SandMesh.KCellStart"), sizeof(uint32), TableSize + 1);
			FRDGBufferRef KSortedIdx = MeshBuffer(GraphBuilder, TEXT("SandMesh.KSortedIdx"), sizeof(uint32), N);
			FSandMeshParams TK = T;
			TK.GridCell = Support;
			TK.GridPosRO = T.SmoothedRO;
			TK.CellKeyRW = GraphBuilder.CreateUAV(KCellKey);
			TK.CellKeyRO = GraphBuilder.CreateSRV(KCellKey);
			TK.CellSlotRW = GraphBuilder.CreateUAV(KCellSlot);
			TK.CellSlotRO = GraphBuilder.CreateSRV(KCellSlot);
			TK.CellCountRW = GraphBuilder.CreateUAV(KCellCount);
			TK.CellStart = GraphBuilder.CreateSRV(KCellStart);
			TK.SortedIdxRW = GraphBuilder.CreateUAV(KSortedIdx);
			TK.SortedIdxRO = GraphBuilder.CreateSRV(KSortedIdx);
			AddClearUAVPass(GraphBuilder, TK.CellCountRW, 0u);
			AddMeshPass<FSandMeshCellKeyCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshKCellKey"), ShaderMap, TK, N, 64);
			AddMeshScan(GraphBuilder, ShaderMap, TK, KCellCount, KCellStart, TableSize);
			AddMeshPass<FSandMeshScatterCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshKScatter"), ShaderMap, TK, N, 64);
			AddMeshPass<FSandMeshSortCellsCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshKSortCells"), ShaderMap, TK, TableSize, 64);
			AddMeshPass<FSandMeshIsoSampleCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshIsoSample"), ShaderMap, TK, NumSamples, 64);
			AddMeshPass<FSandMeshFinalizeIsoCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshFinalizeIso"), ShaderMap, TK, 1, 1);
			Stamp(2);

			// ── G1-3 띠 · 영역 · 장 ──
			const FIntVector GD = P.GridDims;
			const int32 NumPts = GD.X * GD.Y * GD.Z;
			const FIntVector BD(FMath::DivideAndRoundUp(GD.X, 8), FMath::DivideAndRoundUp(GD.Y, 8), FMath::DivideAndRoundUp(GD.Z, 8));
			const int32 NumBlocks = BD.X * BD.Y * BD.Z;
			const int32 MaxPairsK = FMath::Max(1 << 16, N * 48);
			const int32 MaxVerts = FMath::Max(1 << 16, N * 16);
			const int32 MaxIndices = MaxVerts * 6;
			FSandMeshParams TB = T;
			TB.GOrigin = P.GridOrigin;
			TB.GD = FUintVector3(GD.X, GD.Y, GD.Z);
			TB.BD = FUintVector3(BD.X, BD.Y, BD.Z);
			TB.Cell = P.Cell;
			TB.BandReach = P.BandR;
			TB.BandNEps = P.BandNEps;
			TB.SurfDev = P.SurfDev;
			TB.MaxPairsK = MaxPairsK;
			TB.MaxVerts = MaxVerts;
			TB.MaxIndices = MaxIndices;
			FRDGBufferRef BandB      = MeshBuffer(GraphBuilder, TEXT("SandMesh.Band"), sizeof(uint32), NumPts);
			FRDGBufferRef BlockAct   = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockActive"), sizeof(uint32), NumBlocks);
			FRDGBufferRef BlockKCnt  = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockKCount"), sizeof(uint32), NumBlocks);
			FRDGBufferRef BlockKSt   = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockKStart"), sizeof(uint32), NumBlocks + 1);
			FRDGBufferRef BlockKCur  = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockKCursor"), sizeof(uint32), NumBlocks);
			FRDGBufferRef BlockKIt   = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockKItems"), sizeof(uint32), MaxPairsK);
			FRDGBufferRef FieldB     = MeshBuffer(GraphBuilder, TEXT("SandMesh.Field"), sizeof(float), NumPts);
			TB.DevRO = GraphBuilder.CreateSRV(Dev);
			TB.BandRW = GraphBuilder.CreateUAV(BandB);
			TB.BandRO = GraphBuilder.CreateSRV(BandB);
			TB.BlockActiveRW = GraphBuilder.CreateUAV(BlockAct);
			TB.BlockActiveRO = GraphBuilder.CreateSRV(BlockAct);
			TB.BlockKCountRW = GraphBuilder.CreateUAV(BlockKCnt);
			TB.BlockKStart = GraphBuilder.CreateSRV(BlockKSt);
			TB.BlockKCursorRW = GraphBuilder.CreateUAV(BlockKCur);
			TB.BlockKItemsRW = GraphBuilder.CreateUAV(BlockKIt);
			TB.BlockKItemsRO = GraphBuilder.CreateSRV(BlockKIt);
			TB.FieldRW = GraphBuilder.CreateUAV(FieldB);
			TB.FieldRO = GraphBuilder.CreateSRV(FieldB);
			AddClearUAVPass(GraphBuilder, TB.BandRW, 0u);
			AddClearUAVPass(GraphBuilder, TB.BlockActiveRW, 0u);
			AddClearUAVPass(GraphBuilder, TB.BlockKCountRW, 0u);
			AddClearUAVPass(GraphBuilder, TB.BlockKCursorRW, 0u);
			AddClearUAVPass(GraphBuilder, TB.FieldRW, 0xBF800000u);   // −1.0f — 띠 밖 (CPU 와 같다)
			AddMeshPass<FSandMeshMarkBandCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshMarkBand"), ShaderMap, TB, N, 64);
			AddMeshPass<FSandMeshKCountCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshKCount"), ShaderMap, TB, N, 64);
			AddMeshScan(GraphBuilder, ShaderMap, TB, BlockKCnt, BlockKSt, NumBlocks);
			AddMeshPass<FSandMeshKFillCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshKFill"), ShaderMap, TB, N, 64);
			AddMeshGroupPass<FSandMeshFieldCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshField"), ShaderMap, TB, BD);
			Stamp(3);

			// ── G1-4 Surface Nets ──
			FRDGBufferRef BlockVCnt = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockVCount"), sizeof(uint32), NumBlocks);
			FRDGBufferRef BlockVSt  = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockVStart"), sizeof(uint32), NumBlocks + 1);
			FRDGBufferRef BlockTCnt = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockTCount"), sizeof(uint32), NumBlocks);
			FRDGBufferRef BlockTSt  = MeshBuffer(GraphBuilder, TEXT("SandMesh.BlockTStart"), sizeof(uint32), NumBlocks + 1);
			FRDGBufferRef CellVert  = MeshBuffer(GraphBuilder, TEXT("SandMesh.CellVert"), sizeof(uint32), NumPts);
			FRDGBufferRef VertexB   = MeshBuffer(GraphBuilder, TEXT("SandMesh.Vertex"), sizeof(FVector4f), MaxVerts);
			FRDGBufferRef NormalB   = MeshBuffer(GraphBuilder, TEXT("SandMesh.Normal"), sizeof(FVector4f), MaxVerts);
			FRDGBufferRef IndexB    = MeshBuffer(GraphBuilder, TEXT("SandMesh.Index"), sizeof(uint32), MaxIndices);
			FRDGBufferRef DrawArgs  = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(5), TEXT("SandMesh.DrawArgs"));
			TB.BlockVCountRW = GraphBuilder.CreateUAV(BlockVCnt);
			TB.BlockVStart = GraphBuilder.CreateSRV(BlockVSt);
			TB.BlockTCountRW = GraphBuilder.CreateUAV(BlockTCnt);
			TB.BlockTStart = GraphBuilder.CreateSRV(BlockTSt);
			TB.CellVertRW = GraphBuilder.CreateUAV(CellVert);
			TB.CellVertRO = GraphBuilder.CreateSRV(CellVert);
			TB.VertexRW = GraphBuilder.CreateUAV(VertexB);
			TB.NormalRW = GraphBuilder.CreateUAV(NormalB);
			TB.IndexRW = GraphBuilder.CreateUAV(IndexB);
			TB.DrawArgsRW = GraphBuilder.CreateUAV(DrawArgs, PF_R32_UINT);
			AddMeshGroupPass<FSandMeshVertCountCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshVertCount"), ShaderMap, TB, BD);
			AddMeshScan(GraphBuilder, ShaderMap, TB, BlockVCnt, BlockVSt, NumBlocks);
			AddMeshGroupPass<FSandMeshVertWriteCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshVertWrite"), ShaderMap, TB, BD);
			AddMeshGroupPass<FSandMeshFaceCountCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshFaceCount"), ShaderMap, TB, BD);
			AddMeshScan(GraphBuilder, ShaderMap, TB, BlockTCnt, BlockTSt, NumBlocks);
			AddMeshGroupPass<FSandMeshFaceWriteCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshFaceWrite"), ShaderMap, TB, BD);
			AddMeshPass<FSandMeshMeshArgsCS>(GraphBuilder, RDG_EVENT_NAME("SandMeshArgs"), ShaderMap, TB, 1, 1);
			Stamp(4);

			AddEnqueueCopyPass(GraphBuilder, &RB->Band, BandB, NumPts * sizeof(uint32));
			AddEnqueueCopyPass(GraphBuilder, &RB->Field, FieldB, NumPts * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, &RB->BlockActive, BlockAct, NumBlocks * sizeof(uint32));
			AddEnqueueCopyPass(GraphBuilder, &RB->BlockKStart, BlockKSt, (NumBlocks + 1) * sizeof(uint32));
			AddEnqueueCopyPass(GraphBuilder, &RB->Vertex, VertexB, MaxVerts * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->Normal, NormalB, MaxVerts * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->Index, IndexB, MaxIndices * sizeof(uint32));
			Out.NumBlocks = NumBlocks;

			AddEnqueueCopyPass(GraphBuilder, &RB->NCount, NCount, N * sizeof(uint32));
			AddEnqueueCopyPass(GraphBuilder, &RB->Dev, Dev, N * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, &RB->Smoothed, Smoothed, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->CovA, CovA, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->CovB, CovB, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->Kernel, Kernel, 4 * N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &RB->Scalars, Scalars, 8 * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, &RB->Counters, Counters, 8 * sizeof(uint32));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		Fetch(RB->NCount, Out.NeighborCount, N);
		Fetch(RB->Dev, Out.Deviation, N);
		Fetch(RB->Smoothed, Out.Smoothed, N);
		Fetch(RB->CovA, Out.CovA, N);
		Fetch(RB->CovB, Out.CovB, N);
		Fetch(RB->Kernel, Out.Kernel, 4 * N);
		TArray<float> Sc; Fetch(RB->Scalars, Sc, 8);
		TArray<uint32> Ct; Fetch(RB->Counters, Ct, 8);
		Out.Ks = Sc[0]; Out.InteriorPhi = Sc[1]; Out.Iso = Sc[2]; Out.NumInteriorKs = (int32)Sc[3]; Out.NumIsoSamples = (int32)Sc[4];
		Out.MaxNeighborCount = (int32)Ct[0];
		Out.KListOverflowBlocks = (int32)Ct[1]; Out.KPairOverflow = (int32)Ct[2];
		Out.NumVertsTotal = (int32)Ct[3]; Out.NumIndicesTotal = (int32)Ct[4];
		{
			const FIntVector GD = P.GridDims;
			const int32 NumPts = GD.X * GD.Y * GD.Z;
			const int32 MaxVerts = FMath::Max(1 << 16, N * 16);
			const int32 MaxIndices = MaxVerts * 6;
			Fetch(RB->Band, Out.Band, NumPts);
			Fetch(RB->Field, Out.Field, NumPts);
			TArray<uint32> Act; Fetch(RB->BlockActive, Act, Out.NumBlocks);
			TArray<uint32> KSt; Fetch(RB->BlockKStart, KSt, Out.NumBlocks + 1);
			for (uint32 A : Act) { Out.NumActiveBlocks += A ? 1 : 0; }
			Out.NumKPairs = (int32)KSt[Out.NumBlocks];
			for (int32 I = 0; I < NumPts; ++I)
			{
				if (!Out.Band[I]) { continue; }
				++Out.NumBandPoints; Out.FieldSum += Out.Field[I]; Out.FieldPositive += Out.Field[I] > 0.f ? 1 : 0;
			}
			const int32 NV = FMath::Min(Out.NumVertsTotal, MaxVerts), NI = FMath::Min(Out.NumIndicesTotal, MaxIndices);
			TArray<FVector4f> V4, N4; TArray<uint32> Ix;
			Fetch(RB->Vertex, V4, NV); Fetch(RB->Normal, N4, NV); Fetch(RB->Index, Ix, NI);
			Out.Vertices.SetNumUninitialized(NV); Out.Normals.SetNumUninitialized(NV); Out.Indices.SetNumUninitialized(NI);
			for (int32 v = 0; v < NV; ++v) { Out.Vertices[v] = FVector3f(V4[v]); Out.Normals[v] = FVector3f(N4[v]); }
			for (int32 k = 0; k < NI; ++k) { Out.Indices[k] = (int32)Ix[k]; }
		}
		if (RB->Ts[0].IsValid())
		{
			uint64 Us[5] = {};
			bool bOk = true;
			for (int32 k = 0; k < 5; ++k) { bOk &= RHIGetRenderQueryResult(RB->Ts[k].GetReference(), Us[k], true); }
			if (bOk) { for (int32 k = 0; k < 4; ++k) { Out.StageMs[k] = (float)(Us[k + 1] - Us[k]) * 0.001f; } }
		}
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
