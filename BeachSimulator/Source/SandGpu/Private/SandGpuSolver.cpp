#include "SandGpuSolver.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "Modules/ModuleManager.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, SandGpu);

// 여섯 패스가 한 파라미터 구조를 나눠 쓴다. 같은 버퍼의 SRV(…RO)와 UAV(…RW)가 함께 들어 있지만
// 셰이더가 안 쓰는 쪽은 FComputeShaderUtils::AddPass 의 ClearUnusedGraphResources 가 패스마다 비운다.
BEGIN_SHADER_PARAMETER_STRUCT(FSandSolverParams, )
	SHADER_PARAMETER(uint32, NumParticles)
	SHADER_PARAMETER(uint32, NumInternal)
	SHADER_PARAMETER(uint32, NumBoundary)
	SHADER_PARAMETER(uint32, NumBlocks)
	SHADER_PARAMETER(uint32, NumBonds)
	SHADER_PARAMETER(float, Radius)
	SHADER_PARAMETER(float, StepDt)
	SHADER_PARAMETER(float, Kv)
	SHADER_PARAMETER(float, Kc)
	SHADER_PARAMETER(float, SorOmega)
	SHADER_PARAMETER(float, Friction)
	SHADER_PARAMETER(uint32, bMassSplit)
	SHADER_PARAMETER(float, BondAlphaTilde)
	SHADER_PARAMETER(float, BondLambdaMax)
	SHADER_PARAMETER(float, BondOmega)
	SHADER_PARAMETER(uint32, WallMode)
	SHADER_PARAMETER(uint32, WallActiveMask)
	SHADER_PARAMETER(float, WallLX)
	SHADER_PARAMETER(float, WallLY)
	SHADER_PARAMETER(float, WallLimit)
	SHADER_PARAMETER(float, WallLiftZ)
	SHADER_PARAMETER(float, WallFriction)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PosRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PosRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, VelRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, VelRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PrevPos)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ContactIJ)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ContactLambdaRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ContactLambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockRange)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BoundaryImpulseRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BoundaryImpulseRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BoundaryAdjOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BoundaryAdj)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CountRO)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondIJ)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondDataRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondImpulseRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BondImpulseRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdjOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdj)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, GroundLambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, WallLambdaRW)
END_SHADER_PARAMETER_STRUCT()

#define SAND_SOLVER_SHADER(ClassName, EntryPoint) \
	class ClassName : public FGlobalShader \
	{ \
	public: \
		using FParameters = FSandSolverParams; \
		DECLARE_GLOBAL_SHADER(ClassName); \
		SHADER_USE_PARAMETER_STRUCT(ClassName, FGlobalShader); \
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
		{ \
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
		} \
	}; \
	IMPLEMENT_GLOBAL_SHADER(ClassName, "/Project/Private/SandSolver.usf", EntryPoint, SF_Compute);

SAND_SOLVER_SHADER(FSandSolveBlocksCS,   "SolveBlocksCS")
SAND_SOLVER_SHADER(FSandCountBoundaryCS, "CountBoundaryCS")
SAND_SOLVER_SHADER(FSandSolveBoundaryCS, "SolveBoundaryCS")
SAND_SOLVER_SHADER(FSandApplyBoundaryCS, "ApplyBoundaryCS")
SAND_SOLVER_SHADER(FSandSolveBondsCS,    "SolveBondsCS")
SAND_SOLVER_SHADER(FSandApplyBondsCS,    "ApplyBondsCS")

#undef SAND_SOLVER_SHADER

namespace
{
	// 버퍼 크기를 2의 거듭제곱으로 올린다 — 접촉 수가 프레임마다 달라도 RDG 풀이 같은 버퍼를 다시 쓴다
	template <typename T>
	FRDGBufferRef UploadBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, const TArray<T>& Data)
	{
		static const uint8 Zero[16] = {};
		static_assert(sizeof(T) <= sizeof(Zero), "더미 초기값보다 큰 원소");
		const uint32 Capacity = FMath::RoundUpToPowerOfTwo((uint32)FMath::Max(1, Data.Num()));
		FRDGBufferRef Buffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(T), Capacity), Name);
		if (Data.Num() > 0)
		{
			GraphBuilder.QueueBufferUpload(Buffer, Data.GetData(), (uint64)Data.Num() * sizeof(T), ERDGInitialDataFlags::NoCopy);
		}
		else
		{
			GraphBuilder.QueueBufferUpload(Buffer, Zero, sizeof(T), ERDGInitialDataFlags::NoCopy);
		}
		return Buffer;
	}

	FRDGBufferRef ScratchBuffer(FRDGBuilder& GraphBuilder, const TCHAR* Name, uint32 Stride, int32 Num)
	{
		const uint32 Capacity = FMath::RoundUpToPowerOfTwo((uint32)FMath::Max(1, Num));
		return GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, Capacity), Name);
	}

	template <typename TShader>
	void AddSolverPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, const FGlobalShaderMap* ShaderMap,
		const FSandSolverParams& Template, int32 ThreadCount, int32 GroupSize)
	{
		FSandSolverParams* Params = GraphBuilder.AllocParameters<FSandSolverParams>();
		*Params = Template;
		TShaderMapRef<TShader> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(Name), Shader, Params, FComputeShaderUtils::GetGroupCount(ThreadCount, GroupSize));
	}

	struct FReadbacks
	{
		FRHIGPUBufferReadback Pos{ TEXT("SandGpu.Pos") };
		FRHIGPUBufferReadback Vel{ TEXT("SandGpu.Vel") };
		FRHIGPUBufferReadback ContactLambda{ TEXT("SandGpu.ContactLambda") };
		FRHIGPUBufferReadback BondData{ TEXT("SandGpu.BondData") };
		FRHIGPUBufferReadback GroundLambda{ TEXT("SandGpu.GroundLambda") };
		FRHIGPUBufferReadback WallLambda{ TEXT("SandGpu.WallLambda") };
	};

	void SolveRenderThread(FRHICommandListImmediate& RHICmdList, FSandGpuSolveData& D)
	{
		static FReadbacks* Readbacks = new FReadbacks();   // 렌더 스레드 전용. 프로세스 수명

		const int32 N         = D.Pos.Num();
		const int32 NumC      = D.ContactLambda.Num();
		const int32 NumBound  = NumC - D.NumInternal;
		const int32 NumBlocks = D.BlockRange.Num() / 2;
		const int32 NumBonds  = D.bBondsActive ? D.BondData.Num() : 0;

		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGpuSolve"));

			FRDGBufferRef Pos            = UploadBuffer(GraphBuilder, TEXT("SandGpu.Pos"), D.Pos);
			FRDGBufferRef Vel            = UploadBuffer(GraphBuilder, TEXT("SandGpu.Vel"), D.Vel);
			FRDGBufferRef PrevPos        = UploadBuffer(GraphBuilder, TEXT("SandGpu.PrevPos"), D.PrevPos);
			FRDGBufferRef ContactIJ      = UploadBuffer(GraphBuilder, TEXT("SandGpu.ContactIJ"), D.ContactIJ);
			FRDGBufferRef ContactLambda  = UploadBuffer(GraphBuilder, TEXT("SandGpu.ContactLambda"), D.ContactLambda);
			FRDGBufferRef BlockRange     = UploadBuffer(GraphBuilder, TEXT("SandGpu.BlockRange"), D.BlockRange);
			FRDGBufferRef BoundAdjOffset = UploadBuffer(GraphBuilder, TEXT("SandGpu.BoundaryAdjOffset"), D.BoundaryAdjOffset);
			FRDGBufferRef BoundAdj       = UploadBuffer(GraphBuilder, TEXT("SandGpu.BoundaryAdj"), D.BoundaryAdj);
			FRDGBufferRef BondIJ         = UploadBuffer(GraphBuilder, TEXT("SandGpu.BondIJ"), D.BondIJ);
			FRDGBufferRef BondData       = UploadBuffer(GraphBuilder, TEXT("SandGpu.BondData"), D.BondData);
			FRDGBufferRef BondAdjOffset  = UploadBuffer(GraphBuilder, TEXT("SandGpu.BondAdjOffset"), D.BondAdjOffset);
			FRDGBufferRef BondAdj        = UploadBuffer(GraphBuilder, TEXT("SandGpu.BondAdj"), D.BondAdj);
			FRDGBufferRef GroundLambda   = UploadBuffer(GraphBuilder, TEXT("SandGpu.GroundLambda"), D.GroundLambda);
			FRDGBufferRef WallLambda     = UploadBuffer(GraphBuilder, TEXT("SandGpu.WallLambda"), D.WallLambda);

			FRDGBufferRef BoundaryImpulse = ScratchBuffer(GraphBuilder, TEXT("SandGpu.BoundaryImpulse"), sizeof(FVector4f), NumBound);
			FRDGBufferRef Count           = ScratchBuffer(GraphBuilder, TEXT("SandGpu.Count"), sizeof(uint32), N);
			// 결합이 꺼져 있어도 ApplyBondsCS 는 지면·벽 때문에 돈다 — 읽지는 않지만 묶이므로 내용이 있어야 한다
			FRDGBufferRef BondImpulse = NumBonds > 0
				? ScratchBuffer(GraphBuilder, TEXT("SandGpu.BondImpulse"), sizeof(FVector4f), NumBonds)
				: UploadBuffer(GraphBuilder, TEXT("SandGpu.BondImpulse"), TArray<FVector4f>());

			FSandSolverParams T;
			T.NumParticles = N;
			T.NumInternal  = D.NumInternal;
			T.NumBoundary  = NumBound;
			T.NumBlocks    = NumBlocks;
			T.NumBonds     = NumBonds;
			T.Radius   = D.Radius;
			T.StepDt   = D.StepDt;
			T.Kv       = D.Kv;
			T.Kc       = D.Kc;
			T.SorOmega = D.SorOmega;
			T.Friction = D.Friction;
			T.bMassSplit     = D.bMassSplit ? 1 : 0;
			T.BondAlphaTilde = D.BondAlphaTilde;
			T.BondLambdaMax  = D.BondLambdaMax;
			T.BondOmega      = D.BondOmega;
			T.WallMode       = D.WallMode;
			T.WallActiveMask = D.WallActiveMask;
			T.WallLX       = D.WallLX;
			T.WallLY       = D.WallLY;
			T.WallLimit    = D.WallLimit;
			T.WallLiftZ    = D.WallLiftZ;
			T.WallFriction = D.WallFriction;

			T.PosRO   = GraphBuilder.CreateSRV(Pos);
			T.PosRW   = GraphBuilder.CreateUAV(Pos);
			T.VelRO   = GraphBuilder.CreateSRV(Vel);
			T.VelRW   = GraphBuilder.CreateUAV(Vel);
			T.PrevPos = GraphBuilder.CreateSRV(PrevPos);
			T.ContactIJ         = GraphBuilder.CreateSRV(ContactIJ);
			T.ContactLambdaRO   = GraphBuilder.CreateSRV(ContactLambda);
			T.ContactLambdaRW   = GraphBuilder.CreateUAV(ContactLambda);
			T.BlockRange        = GraphBuilder.CreateSRV(BlockRange);
			T.BoundaryImpulseRW = GraphBuilder.CreateUAV(BoundaryImpulse);
			T.BoundaryImpulseRO = GraphBuilder.CreateSRV(BoundaryImpulse);
			T.BoundaryAdjOffset = GraphBuilder.CreateSRV(BoundAdjOffset);
			T.BoundaryAdj       = GraphBuilder.CreateSRV(BoundAdj);
			T.CountRW = GraphBuilder.CreateUAV(Count);
			T.CountRO = GraphBuilder.CreateSRV(Count);
			T.BondIJ        = GraphBuilder.CreateSRV(BondIJ);
			T.BondDataRW    = GraphBuilder.CreateUAV(BondData);
			T.BondImpulseRW = GraphBuilder.CreateUAV(BondImpulse);
			T.BondImpulseRO = GraphBuilder.CreateSRV(BondImpulse);
			T.BondAdjOffset = GraphBuilder.CreateSRV(BondAdjOffset);
			T.BondAdj       = GraphBuilder.CreateSRV(BondAdj);
			T.GroundLambdaRW = GraphBuilder.CreateUAV(GroundLambda);
			T.WallLambdaRW   = GraphBuilder.CreateUAV(WallLambda);

			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			for (int32 It = 0; It < D.Iterations; ++It)
			{
				if (NumBlocks > 0)
				{
					AddSolverPass<FSandSolveBlocksCS>(GraphBuilder, RDG_EVENT_NAME("SandBlocks"), ShaderMap, T, NumBlocks, 32);
				}
				if (NumBound > 0)
				{
					if (D.bMassSplit)
					{
						AddSolverPass<FSandCountBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandCountBoundary"), ShaderMap, T, N, 64);
					}
					AddSolverPass<FSandSolveBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandBoundary"), ShaderMap, T, NumBound, 64);
					AddSolverPass<FSandApplyBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandApplyBoundary"), ShaderMap, T, N, 64);
				}
				if (NumBonds > 0)
				{
					AddSolverPass<FSandSolveBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandBonds"), ShaderMap, T, NumBonds, 64);
				}
				AddSolverPass<FSandApplyBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandApplyBonds"), ShaderMap, T, N, 64);
			}

			AddEnqueueCopyPass(GraphBuilder, &Readbacks->Pos, Pos, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &Readbacks->Vel, Vel, N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &Readbacks->GroundLambda, GroundLambda, N * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, &Readbacks->WallLambda, WallLambda, N * sizeof(FVector4f));
			if (NumC > 0)
			{
				AddEnqueueCopyPass(GraphBuilder, &Readbacks->ContactLambda, ContactLambda, NumC * sizeof(FVector4f));
			}
			if (NumBonds > 0)
			{
				AddEnqueueCopyPass(GraphBuilder, &Readbacks->BondData, BondData, NumBonds * sizeof(FVector4f));
			}

			GraphBuilder.Execute();
		}

		// 계측용 전량 리드백 — 프레임 지연 없이 여기서 기다린다 (출하 경로는 M1 의 비동기 36KB)
		RHICmdList.SubmitAndBlockUntilGPUIdle();

		auto Fetch = [](FRHIGPUBufferReadback& Readback, void* Dst, uint32 NumBytes)
		{
			if (NumBytes == 0)
			{
				return;
			}
			if (const void* Src = Readback.Lock(NumBytes))
			{
				FMemory::Memcpy(Dst, Src, NumBytes);
			}
			Readback.Unlock();
		};
		Fetch(Readbacks->Pos, D.Pos.GetData(), N * sizeof(FVector4f));
		Fetch(Readbacks->Vel, D.Vel.GetData(), N * sizeof(FVector4f));
		Fetch(Readbacks->GroundLambda, D.GroundLambda.GetData(), N * sizeof(float));
		Fetch(Readbacks->WallLambda, D.WallLambda.GetData(), N * sizeof(FVector4f));
		Fetch(Readbacks->ContactLambda, D.ContactLambda.GetData(), NumC * sizeof(FVector4f));
		Fetch(Readbacks->BondData, D.BondData.GetData(), NumBonds * sizeof(FVector4f));

		// 프레임 틱이 없는 커맨드릿에서는 RDG 풀도 RHI 프레임도 스스로 안 넘어간다. RHI 프레임을 안 닫으면
		// 올리기용 임시 메모리와 지연 삭제가 쌓이다가 수천 스텝 뒤 GPU 페이지 폴트로 죽는다 (stage11 첫 실행, 8분째)
		FRDGBuilder::TickPoolElements();
		RHICmdList.EndFrame();
	}
}

bool SandGpu::IsAvailable()
{
	return FApp::CanEverRender() && GDynamicRHI != nullptr && !GUsingNullRHI;
}

void SandGpu::Solve(FSandGpuSolveData& Data)
{
	check(IsInGameThread());
	if (Data.Pos.Num() == 0 || Data.Iterations <= 0)
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	ENQUEUE_RENDER_COMMAND(SandGpuSolve)([&Data](FRHICommandListImmediate& RHICmdList)
	{
		SolveRenderThread(RHICmdList, Data);
	});
	FlushRenderingCommands();
	Data.GpuMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}
