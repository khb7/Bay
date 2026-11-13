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

// ================================================================================================
// 반복 루프 (SandSolver.usf) — 1·2단계 공용
//
// 여섯 패스가 한 파라미터 구조를 나눠 쓴다. 같은 버퍼의 SRV(…RO)와 UAV(…RW)가 함께 들어 있지만
// 셰이더가 안 쓰는 쪽은 FComputeShaderUtils::AddPass 의 ClearUnusedGraphResources 가 패스마다 비운다.
// M4 T1 지면 묶기 — 격자 바닥이 없으면 평면 (더미 버퍼 하나)
template <typename TParams>
static void BindGround(FRDGBuilder& GraphBuilder, TParams& X, const FSandGpuFrameParams* P)
{
	FRDGBufferRef B, Sb;
	if (P && P->GroundBed.IsValid())
	{
		B = GraphBuilder.RegisterExternalBuffer(P->GroundBed);
		Sb = GraphBuilder.RegisterExternalBuffer(P->GroundStruct);
		X.GroundMode = 1u; X.GroundNX = (uint32)P->GroundNX; X.GroundNY = (uint32)P->GroundNY; X.GroundCell = P->GroundCell; X.GroundOrigin = P->GroundOrigin; X.GroundZ = P->GroundZ;
	}
	else
	{
		B = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 1), TEXT("SandGpu.NoGround"));
		const float Z = 0.f;
		GraphBuilder.QueueBufferUpload(B, &Z, sizeof(float), ERDGInitialDataFlags::None);
		Sb = B;
		X.GroundMode = 0u; X.GroundNX = X.GroundNY = 1u; X.GroundCell = 1.f; X.GroundOrigin = FVector2f::ZeroVector; X.GroundZ = 0.f;
	}
	X.GroundBed = GraphBuilder.CreateSRV(B);
	X.GroundStruct = GraphBuilder.CreateSRV(Sb);
}

BEGIN_SHADER_PARAMETER_STRUCT(FSandSolverParams, )
	SHADER_PARAMETER(uint32, NumParticles)
	SHADER_PARAMETER(uint32, NumInternal)
	SHADER_PARAMETER(uint32, NumBoundary)
	SHADER_PARAMETER(uint32, NumBlocks)
	SHADER_PARAMETER(uint32, BlockBase)
	SHADER_PARAMETER(uint32, NumBonds)
	SHADER_PARAMETER(uint32, bUseFrameCounts)
	SHADER_PARAMETER(uint32, bBondsActive)
	SHADER_PARAMETER(float, Radius)
	SHADER_PARAMETER(float, StepDt)
	SHADER_PARAMETER(float, Kv)
	SHADER_PARAMETER(float, Kc)
	SHADER_PARAMETER(float, SorOmega)
	SHADER_PARAMETER(float, Friction)
	SHADER_PARAMETER(uint32, GroundMode)
	SHADER_PARAMETER(uint32, GroundNX)
	SHADER_PARAMETER(uint32, GroundNY)
	SHADER_PARAMETER(float, GroundCell)
	SHADER_PARAMETER(FVector2f, GroundOrigin)
	SHADER_PARAMETER(float, GroundZ)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundBed)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundStruct)
	SHADER_PARAMETER(float, MaxDepenVel)
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
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FrameCounts)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CountRO)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondIJ)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondDataRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondImpulseRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BondImpulseRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdjOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdj)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ContactBatchRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ContactOrderRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ContactOrderRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BatchStartRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BatchStartRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BatchStatsRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IterArgsRW)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, GroundLambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, WallLambdaRW)

	RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

#define SAND_SHADER(ClassName, ParamsType, File, EntryPoint) \
	class ClassName : public FGlobalShader \
	{ \
	public: \
		using FParameters = ParamsType; \
		DECLARE_GLOBAL_SHADER(ClassName); \
		SHADER_USE_PARAMETER_STRUCT(ClassName, FGlobalShader); \
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
		{ \
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
		} \
	}; \
	IMPLEMENT_GLOBAL_SHADER(ClassName, File, EntryPoint, SF_Compute);

#define SOLVER_USF "/Project/Private/SandSolver.usf"
SAND_SHADER(FSandSolveBlocksCS,   FSandSolverParams, SOLVER_USF, "SolveBlocksCS")
SAND_SHADER(FSandColorBatchCS,   FSandSolverParams, SOLVER_USF, "ColorBatchCS")
SAND_SHADER(FSandSolveBlocksParCS, FSandSolverParams, SOLVER_USF, "SolveBlocksParCS")
SAND_SHADER(FSandAnyAwakeCS,  FSandSolverParams, SOLVER_USF, "AnyAwakeCS")
SAND_SHADER(FSandIterArgsCS,  FSandSolverParams, SOLVER_USF, "IterArgsCS")
SAND_SHADER(FSandCountBoundaryCS, FSandSolverParams, SOLVER_USF, "CountBoundaryCS")
SAND_SHADER(FSandSolveBoundaryCS, FSandSolverParams, SOLVER_USF, "SolveBoundaryCS")
SAND_SHADER(FSandApplyBoundaryCS, FSandSolverParams, SOLVER_USF, "ApplyBoundaryCS")
SAND_SHADER(FSandSolveBondsCS,    FSandSolverParams, SOLVER_USF, "SolveBondsCS")
SAND_SHADER(FSandApplyBondsCS,    FSandSolverParams, SOLVER_USF, "ApplyBondsCS")

// ================================================================================================
// 프레임 앞뒤 (SandFrame.usf) — 2단계

BEGIN_SHADER_PARAMETER_STRUCT(FSandFrameParams, )
	SHADER_PARAMETER(uint32, NumParticles)
	SHADER_PARAMETER(uint32, TableSize)
	SHADER_PARAMETER(uint32, BlockTable)
	SHADER_PARAMETER(uint32, MaxPairs)
	SHADER_PARAMETER(uint32, NumBonds)
	SHADER_PARAMETER(uint32, ScanCount)
	SHADER_PARAMETER(uint32, FillFrom)
	SHADER_PARAMETER(uint32, FillTo)
	SHADER_PARAMETER(uint32, FillValue)
	SHADER_PARAMETER(float, CellSize)
	SHADER_PARAMETER(uint32, bParticleMargin)
	SHADER_PARAMETER(uint32, bRelMargin)
	SHADER_PARAMETER(float, MarginMinR)
	SHADER_PARAMETER(float, Radius)
	SHADER_PARAMETER(float, StepDt)
	SHADER_PARAMETER(float, BlockSize)
	SHADER_PARAMETER(uint32, bColor8)
	SHADER_PARAMETER(float, WarmStart)
	SHADER_PARAMETER(float, WarmGapTol)
	SHADER_PARAMETER(uint32, bWarmTangent)
	SHADER_PARAMETER(float, Friction)
	SHADER_PARAMETER(uint32, GroundMode)
	SHADER_PARAMETER(uint32, GroundNX)
	SHADER_PARAMETER(uint32, GroundNY)
	SHADER_PARAMETER(float, GroundCell)
	SHADER_PARAMETER(FVector2f, GroundOrigin)
	SHADER_PARAMETER(float, GroundZ)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundBed)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundStruct)
	SHADER_PARAMETER(FVector3f, Gravity)
	SHADER_PARAMETER(float, BondStretchBreak)
	SHADER_PARAMETER(uint32, bBondsDry)
	SHADER_PARAMETER(float, VelocityDamping)
	SHADER_PARAMETER(uint32, WallMode)
	SHADER_PARAMETER(uint32, WallActiveMask)
	SHADER_PARAMETER(float, WallLX)
	SHADER_PARAMETER(float, WallLY)
	SHADER_PARAMETER(float, WallLimit)
	SHADER_PARAMETER(float, WallLiftZ)
	SHADER_PARAMETER(uint32, bSleep)
	SHADER_PARAMETER(float, SleepVel)
	SHADER_PARAMETER(uint32, SleepFrames)
	SHADER_PARAMETER(float, WakeVel)
	SHADER_PARAMETER(float, WakeTouchVel)
	SHADER_PARAMETER(uint32, WakePass)
	SHADER_PARAMETER(uint32, bWakeCold)
	SHADER_PARAMETER(uint32, bWakeZeroPairs)
	SHADER_PARAMETER(uint32, bSleepColdPairs)
	SHADER_PARAMETER(float, WakeLamRatio)
	SHADER_PARAMETER(float, WakeLamAbs)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, SleepRefRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SleepRW)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PosRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PosRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, VelRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, VelRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PrevPosRW)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellKeyRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellKeyRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellSlotRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellSlotRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SortedIdxRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SortedIdxRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockKeyRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockKeyRO)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PairCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PairStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PairIJRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PairIJRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PairWarmRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PairWarmRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PairBlockRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PairBlockRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PairLocalRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PairLocalRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BlockPairCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BlockStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CountersRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CountersRO)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OldPairStart)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OldPairIJ)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OldPermInv)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, OldLambda)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SortedIJRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, LambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PermInvRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PairImpulseRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PairImpulseRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, AdjCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, AdjStart)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, AdjRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, AdjRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, AdjCursorRW)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondIJ)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondDataRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BondDataRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondImpulseRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, BondImpulseRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdjOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdj)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, GroundLambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, WallLambdaRW)

	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ScanIn)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ScanOut)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ScanSumsRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ScanSumsRO)

	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, IndirectArgsRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, FrameCountsRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FrameCountsRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, StatsRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, FillTarget)

	RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

// M2 T2 — 도구 스탬프 (SandTool.usf)
BEGIN_SHADER_PARAMETER_STRUCT(FSandToolParams, )
	SHADER_PARAMETER(uint32, NumParticles)
	SHADER_PARAMETER(uint32, NumBonds)
	SHADER_PARAMETER(uint32, NumStamps)
	SHADER_PARAMETER(uint32, StampSeed)
	SHADER_PARAMETER(uint32, HandCap)
	SHADER_PARAMETER(uint32, SleepFrames)
	SHADER_PARAMETER(float, GraveZ)
	SHADER_PARAMETER(float, GraveSpacing)
	SHADER_PARAMETER(float, StepDt)
	SHADER_PARAMETER(float, DrainTarget)
	SHADER_PARAMETER(float, DrainTau)
	SHADER_PARAMETER(uint32, bPerParticleWet)
	SHADER_PARAMETER(float, WDry)
	SHADER_PARAMETER(float, WOptStart)
	SHADER_PARAMETER(float, WOptEnd)
	SHADER_PARAMETER(float, WSat)
	SHADER_PARAMETER(uint32, WetMode)
	SHADER_PARAMETER(float, WetRadius)
	SHADER_PARAMETER(uint32, GroundMode)
	SHADER_PARAMETER(uint32, GroundNX)
	SHADER_PARAMETER(uint32, GroundNY)
	SHADER_PARAMETER(float, GroundCell)
	SHADER_PARAMETER(FVector2f, GroundOrigin)
	SHADER_PARAMETER(float, GroundZ)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundBed)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GroundStruct)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, GroundWater)
	SHADER_PARAMETER(uint32, LoadMode)
	SHADER_PARAMETER(float, LoadMassKg)
	SHADER_PARAMETER(uint32, ReturnMode)
	SHADER_PARAMETER(float, ReturnVel)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ReturnAccRW)
	SHADER_PARAMETER(uint32, BuoyMode)
	SHADER_PARAMETER(float, BuoyAccel)
	SHADER_PARAMETER(uint32, DragMode)
	SHADER_PARAMETER(float, DragK)
	SHADER_PARAMETER(float, DragDiam)
	SHADER_PARAMETER(float, WaterNu)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVector2f>, GroundLoad)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, LoadCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, LoadCountRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Stamps)
	SHADER_PARAMETER(uint32, NumPileTargets)
	SHADER_PARAMETER(float, PileDropVel)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PileTargets)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PosRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, VelRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, PrevPosRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, WetRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, WetRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, SleepRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, GroundLambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, WallLambdaRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, HandWRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, HandCountRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondIJ)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, BondDataRW)
	SHADER_PARAMETER(uint32, NumPairsMax)
	SHADER_PARAMETER(uint32, BondCap)
	SHADER_PARAMETER(uint32, BondSlots)
	SHADER_PARAMETER(float, FormRange)
	SHADER_PARAMETER(float, FormMinRest)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PairIJ)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FrameCounts)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondTrig)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondTrigRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondSlotRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OldAdjOffset)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, OldAdj)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondIJRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondTotalRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondAdjRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondCursorRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondAdjOffsetRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BondFlagRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, BondRemap)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, TempIJRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, TempDataRW)
END_SHADER_PARAMETER_STRUCT()

#define TOOL_USF "/Project/Private/SandTool.usf"
SAND_SHADER(FSandStampCS,    FSandToolParams, TOOL_USF, "StampCS")
SAND_SHADER(FSandReturnCS,   FSandToolParams, TOOL_USF, "ReturnCS")
SAND_SHADER(FSandLoadCountCS, FSandToolParams, TOOL_USF, "LoadCountCS")
SAND_SHADER(FSandBondToolCS, FSandToolParams, TOOL_USF, "BondToolCS")
SAND_SHADER(FSandPileCS,       FSandToolParams, TOOL_USF, "PileCS")
SAND_SHADER(FSandPileFinishCS, FSandToolParams, TOOL_USF, "PileFinishCS")
SAND_SHADER(FSandBondSlotCountCS, FSandToolParams, TOOL_USF, "BondSlotCountCS")
SAND_SHADER(FSandBondFormCS,      FSandToolParams, TOOL_USF, "BondFormCS")
SAND_SHADER(FSandBondFillCS,      FSandToolParams, TOOL_USF, "BondFillCS")
SAND_SHADER(FSandBondAliveCS,     FSandToolParams, TOOL_USF, "BondAliveCS")
SAND_SHADER(FSandBondCompactCS,   FSandToolParams, TOOL_USF, "BondCompactCS")
SAND_SHADER(FSandBondCompactCountCS, FSandToolParams, TOOL_USF, "BondCompactCountCS")

#define FRAME_USF "/Project/Private/SandFrame.usf"
SAND_SHADER(FSandScanBlocksCS,  FSandFrameParams, FRAME_USF, "ScanBlocksCS")
SAND_SHADER(FSandScanTopCS,     FSandFrameParams, FRAME_USF, "ScanTopCS")
SAND_SHADER(FSandScanAddCS,     FSandFrameParams, FRAME_USF, "ScanAddCS")
SAND_SHADER(FSandCellKeyCS,     FSandFrameParams, FRAME_USF, "CellKeyCS")
SAND_SHADER(FSandScatterCS,     FSandFrameParams, FRAME_USF, "ScatterCS")
SAND_SHADER(FSandPairCountCS,   FSandFrameParams, FRAME_USF, "PairCountCS")
SAND_SHADER(FSandPairFillCS,    FSandFrameParams, FRAME_USF, "PairFillCS")
SAND_SHADER(FSandBuildArgsCS,   FSandFrameParams, FRAME_USF, "BuildArgsCS")
SAND_SHADER(FSandPairScatterCS, FSandFrameParams, FRAME_USF, "PairScatterCS")
SAND_SHADER(FSandBondWarmCS,    FSandFrameParams, FRAME_USF, "BondWarmCS")
SAND_SHADER(FSandWarmPredictCS, FSandFrameParams, FRAME_USF, "WarmPredictCS")
SAND_SHADER(FSandBreakBondsCS,  FSandFrameParams, FRAME_USF, "BreakBondsCS")
SAND_SHADER(FSandWakeCS,        FSandFrameParams, FRAME_USF, "WakeCS")
SAND_SHADER(FSandWakeZeroCS,    FSandFrameParams, FRAME_USF, "WakeZeroCS")
SAND_SHADER(FSandStatsCS,       FSandFrameParams, FRAME_USF, "StatsCS")
SAND_SHADER(FSandFillTailCS,    FSandFrameParams, FRAME_USF, "FillTailCS")

#undef SAND_SHADER

namespace
{
	constexpr int32 ScanGroupElems = 2048;   // SandFrame.usf SCAN_GROUP × 2
	constexpr int32 StatsGroup = 256;
	constexpr uint32 BlockTableSize = 8192;  // 블록 해시 표. 충돌은 무해하다 — 멀리 떨어진 두 블록이 한 스레드에 묶일 뿐
	constexpr uint32 BatchStride = 33;       // SandSolver.usf BATCH_STRIDE

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

	template <typename TShader, typename TParams>
	void AddDirectPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, const FGlobalShaderMap* ShaderMap,
		const TParams& Template, int32 ThreadCount, int32 GroupSize)
	{
		TParams* Params = GraphBuilder.AllocParameters<TParams>();
		*Params = Template;
		TShaderMapRef<TShader> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(Name), Shader, Params, FComputeShaderUtils::GetGroupCount(FMath::Max(1, ThreadCount), GroupSize));
	}

	template <typename TShader, typename TParams>
	void AddIndirectPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, const FGlobalShaderMap* ShaderMap,
		const TParams& Template, FRDGBufferRef Args, uint32 ArgsOffset)
	{
		TParams* Params = GraphBuilder.AllocParameters<TParams>();
		*Params = Template;
		Params->IndirectArgs = Args;
		TShaderMapRef<TShader> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(Name), ERDGPassFlags::Compute, Shader, Params, Args, ArgsOffset);
	}

	/** 배타 접두합. Out 은 Count + 1 원소 — 마지막이 총합 */
	void AddScan(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* ShaderMap, const FSandFrameParams& Template,
		FRDGBufferRef In, FRDGBufferRef Out, uint32 Count)
	{
		const int32 NumGroups = FMath::DivideAndRoundUp((int32)Count, ScanGroupElems);
		checkf(NumGroups <= ScanGroupElems, TEXT("스캔 상한 4M 원소 초과 (%u)"), Count);
		FRDGBufferRef Sums = ScratchBuffer(GraphBuilder, TEXT("SandGpu.ScanSums"), sizeof(uint32), ScanGroupElems);

		FSandFrameParams T = Template;
		T.ScanCount  = Count;
		T.ScanIn     = GraphBuilder.CreateSRV(In);
		T.ScanOut    = GraphBuilder.CreateUAV(Out);
		T.ScanSumsRW = GraphBuilder.CreateUAV(Sums);
		T.ScanSumsRO = GraphBuilder.CreateSRV(Sums);
		AddDirectPass<FSandScanBlocksCS>(GraphBuilder, RDG_EVENT_NAME("SandScanBlocks"), ShaderMap, T, NumGroups * (ScanGroupElems / 2), ScanGroupElems / 2);
		AddDirectPass<FSandScanTopCS>(GraphBuilder, RDG_EVENT_NAME("SandScanTop"), ShaderMap, T, ScanGroupElems / 2, ScanGroupElems / 2);
		AddDirectPass<FSandScanAddCS>(GraphBuilder, RDG_EVENT_NAME("SandScanAdd"), ShaderMap, T, NumGroups * (ScanGroupElems / 2), ScanGroupElems / 2);
	}

	/** 반복 루프 — 1·2단계가 같은 패스 열을 쓴다. IndirectArgs 가 있으면 경계 패스는 간접(GPU 가 센 쌍 수), 없으면 NumBoundaryDirect */
	void AddSolverIterations(FRDGBuilder& GraphBuilder, const FGlobalShaderMap* ShaderMap, const FSandSolverParams& T,
		int32 Iterations, int32 N, int32 NumBlocks, int32 NumBonds, bool bMassSplit, FRDGBufferRef IndirectArgs, int32 NumBoundaryDirect,
		bool bColor8 = false, int32 FrameIndex = 0, bool bColor8Par = false, FRDGBufferRef IterArgs = nullptr)
	{
		for (int32 It = 0; It < Iterations; ++It)
		{
			if (bColor8)
			{
				// A안 — 색 0..7 을 차례로. 같은 색 블록은 입자를 공유하지 않아 동시에, 색 사이는 앞 색의 결과를 본다 (블록 GS). 경계 패스 없음
				const int32 Sub = NumBlocks / 8;
				for (int32 c = 0; c < 8; ++c)
				{
					FSandSolverParams TC = T;
					TC.NumBlocks = Sub;
					TC.BlockBase = ((c + FrameIndex) & 7) * Sub;   // 색 회전 (R5)
					if (bColor8Par && IterArgs)
					{
						// M4 T6 — 같은 패스를 간접으로 (모두 잠들면 0 그룹)
						FSandSolverParams TI = TC; TI.NumBlocks = Sub;
						AddIndirectPass<FSandSolveBlocksParCS>(GraphBuilder, RDG_EVENT_NAME("SandColorPar"), ShaderMap, TI, IterArgs, 0);
					}
					else if (bColor8Par)
					{
						// 단계 2 — 그룹 = 블록, 블록 안 배치 병렬
						AddDirectPass<FSandSolveBlocksParCS>(GraphBuilder, RDG_EVENT_NAME("SandColorPar"), ShaderMap, TC, Sub * 64, 64);
					}
					else
					{
						AddDirectPass<FSandSolveBlocksCS>(GraphBuilder, RDG_EVENT_NAME("SandColor"), ShaderMap, TC, Sub, 32);
					}
				}
			}
			else if (NumBlocks > 0)
			{
				AddDirectPass<FSandSolveBlocksCS>(GraphBuilder, RDG_EVENT_NAME("SandBlocks"), ShaderMap, T, NumBlocks, 32);
			}
			if (!bColor8 && (IndirectArgs || NumBoundaryDirect > 0))
			{
				if (bMassSplit)
				{
					AddDirectPass<FSandCountBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandCountBoundary"), ShaderMap, T, N, 64);
				}
				if (IndirectArgs)
				{
					AddIndirectPass<FSandSolveBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandBoundary"), ShaderMap, T, IndirectArgs, 3 * sizeof(uint32));
				}
				else
				{
					AddDirectPass<FSandSolveBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandBoundary"), ShaderMap, T, NumBoundaryDirect, 64);
				}
				AddDirectPass<FSandApplyBoundaryCS>(GraphBuilder, RDG_EVENT_NAME("SandApplyBoundary"), ShaderMap, T, N, 64);
			}
			if (IterArgs)
			{
				if (NumBonds > 0) { AddIndirectPass<FSandSolveBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandBonds"), ShaderMap, T, IterArgs, 3 * sizeof(uint32)); }
				AddIndirectPass<FSandApplyBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandApplyBonds"), ShaderMap, T, IterArgs, 6 * sizeof(uint32));
				continue;
			}
			if (NumBonds > 0)
			{
				AddDirectPass<FSandSolveBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandBonds"), ShaderMap, T, NumBonds, 64);
			}
			AddDirectPass<FSandApplyBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandApplyBonds"), ShaderMap, T, N, 64);
		}
	}

	void FetchReadback(FRHIGPUBufferReadback& Readback, void* Dst, uint32 NumBytes)
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
	}

	// 프레임 틱이 없는 커맨드릿에서는 RDG 풀도 RHI 프레임도 스스로 안 넘어간다. RHI 프레임을 안 닫으면
	// 올리기용 임시 메모리와 지연 삭제가 쌓이다가 수천 스텝 뒤 GPU 페이지 폴트로 죽는다 (stage11 첫 실행, 8분째)
	bool StatsReady(FSandGpuSim::FState* S);
	void FetchStats(FSandGpuSim::FState* S, int32 Groups, bool bWait);

	void EndGpuFrame(FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder::TickPoolElements();
		RHICmdList.EndFrame();
	}
}

// ================================================================================================
// 1단계

namespace
{
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
		const int32 NumBlocks = FMath::Max(0, D.BlockRange.Num() - 1);
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
			FRDGBufferRef FrameCounts    = UploadBuffer(GraphBuilder, TEXT("SandGpu.FrameCounts"), TArray<uint32>());   // 1단계는 상수를 쓴다

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
			T.BlockBase    = 0;
			T.NumBonds     = NumBonds;
			T.bUseFrameCounts = 0;
			T.bBondsActive = NumBonds > 0 ? 1 : 0;
			T.Radius   = D.Radius;
			T.StepDt   = D.StepDt;
			T.Kv       = D.Kv;
			T.Kc       = D.Kc;
			T.SorOmega = D.SorOmega;
			T.Friction = D.Friction;
			BindGround(GraphBuilder, T, nullptr);
			T.MaxDepenVel = 0.f;
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
			T.FrameCounts       = GraphBuilder.CreateSRV(FrameCounts);
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
			AddSolverIterations(GraphBuilder, ShaderMap, T, D.Iterations, N, NumBlocks, NumBonds, D.bMassSplit, nullptr, NumBound);

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

		FetchReadback(Readbacks->Pos, D.Pos.GetData(), N * sizeof(FVector4f));
		FetchReadback(Readbacks->Vel, D.Vel.GetData(), N * sizeof(FVector4f));
		FetchReadback(Readbacks->GroundLambda, D.GroundLambda.GetData(), N * sizeof(float));
		FetchReadback(Readbacks->WallLambda, D.WallLambda.GetData(), N * sizeof(FVector4f));
		FetchReadback(Readbacks->ContactLambda, D.ContactLambda.GetData(), NumC * sizeof(FVector4f));
		FetchReadback(Readbacks->BondData, D.BondData.GetData(), NumBonds * sizeof(FVector4f));

		EndGpuFrame(RHICmdList);
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

// ================================================================================================
// 2단계 — GPU 상주

struct FSandGpuSim::FState
{
	// 영구 (입자 수 N)
	TRefCountPtr<FRDGPooledBuffer> Pos, Vel, PrevPos, GroundLambda, WallLambda;
	TRefCountPtr<FRDGPooledBuffer> Sleep;   // C안 — 정지 프레임 수 (uint)
	TRefCountPtr<FRDGPooledBuffer> SleepRef;   // 조사 H9 (e) — 잠들 때의 Σλn
	TRefCountPtr<FRDGPooledBuffer> Wet;        // M1 W1 — 입자별 W
	TRefCountPtr<FRDGPooledBuffer> HandW, HandCount;   // M2 T2 — 손 재고 W · [0] 수 [1] 판 수 [2] 못 판 수 (올리기와 무관하게 유지)
	int32 CapHand = 0;
	FRHIGPUBufferReadback HandRB{ TEXT("SandGpu.Hand") };
	FRHIGPUBufferReadback DlWet{ TEXT("SandGpu.DlWet") }, DlBondIJ{ TEXT("SandGpu.DlBondIJ") };
	FRHIGPUBufferReadback BondTotalRB{ TEXT("SandGpu.BondTotal") };
	bool bCompactedThisStep = false;
	// A5 — 통계 비동기 (P.bAsyncStats). 렌더 스레드가 준비된 리드백을 Latest 로 옮기고, 게임 스레드는 Step 끝에 한 번 가져간다
	struct FRaw { TArray<FVector4f> Partial; uint32 Counters[4] = {}, FrameCounts[4] = {}, BatchStats[8] = {}, Hand[4] = {}, BondTot[4] = {}; float StageMs[4] = {}; bool bValid = false; };
	FRaw Latest;
	FCriticalSection StatsLock;
	bool bStatsInFlight = false;
	int32 InFlightGroups = 0;   // A2 — 이번 스텝에 결합 칸을 압축했다 → NumB 를 줄여도 된다
	// M2 T5 — 위치만 비동기 리드백 (커서 높이장). 렌더 스레드만 InFlight 를 만지고, 결과는 잠금 아래 게임 스레드로
	FRHIGPUBufferReadback AsyncPosRB{ TEXT("SandGpu.AsyncPos") };
	bool bAsyncInFlight = false;
	int32 AsyncN = 0;
	FCriticalSection AsyncLock;
	TArray<FVector4f> AsyncPos;
	bool bAsyncReady = false;
	// 영구 (결합)
	TRefCountPtr<FRDGPooledBuffer> BondIJ, BondData, BondAdjOffset, BondAdj;
	// 쌍 목록 두 벌 — [Cur] 이번 프레임 · [1-Cur] 지난 프레임 (워밍 조회)
	TRefCountPtr<FRDGPooledBuffer> PairStart[2], PairIJ[2], PermInv[2], Lambda[2], SortedIJ[2];
	int32 Cur = 0;
	int32 CapN = 0, CapB = 0, CapPairs = 0;   // 버퍼 용량. 입자를 덧붙여도 용량 안이면 재할당 없이 쌍 목록(워밍)을 유지한다

	FRHIGPUBufferReadback StatsRB{ TEXT("SandGpu.Stats") };
	FRHIGPUBufferReadback CountersRB{ TEXT("SandGpu.Counters") };
	FRHIGPUBufferReadback FrameCountsRB{ TEXT("SandGpu.FrameCounts") };
	FRHIGPUBufferReadback BatchStatsRB{ TEXT("SandGpu.BatchStats") };
	FRenderQueryRHIRef StageTs[5];   // 3-4 단계별 GPU 타임스탬프 (이웃 · 워밍예측 · 반복 · 마무리 경계)
	FRHIGPUBufferReadback DlPos{ TEXT("SandGpu.DlPos") }, DlVel{ TEXT("SandGpu.DlVel") }, DlPairIJ{ TEXT("SandGpu.DlPairIJ") },
		DlLambda{ TEXT("SandGpu.DlLambda") }, DlBond{ TEXT("SandGpu.DlBond") }, DlGround{ TEXT("SandGpu.DlGround") }, DlWall{ TEXT("SandGpu.DlWall") }, DlSleep{ TEXT("SandGpu.DlSleep") };

	TArray<FVector4f> StatsPartial;
};

FSandGpuSim::FSandGpuSim()
{
	State = new FState();
}

FSandGpuSim::~FSandGpuSim()
{
	// 버퍼는 렌더 스레드가 들고 있다 — 거기서 놓는다
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGpuSimDestroy)([S](FRHICommandListImmediate&) { delete S; });
	FlushRenderingCommands();
}

void FSandGpuSim::Upload(const FSandGpuUpload& In, bool bKeepPairs)
{
	check(IsInGameThread());
	const int32 OldN = N;
	const int32 OldPairs = LastNumPairs;
	N = In.Pos.Num();
	NumB = In.BondData.Num();
	if (N == 0)
	{
		return;
	}
	FState* S = State;
	// 쌍 목록을 지키는 조건 — 이전 올리기가 있고, 입자가 덧붙기만 했다 (제거·재배치는 호출 측이 false 를 준다)
	bKeepPairs = bKeepPairs && S->CapN > 0 && N >= OldN && OldN > 0;
	if (!bKeepPairs) { LastNumPairs = 0; }
	const bool bGrowN = N > S->CapN || S->CapN == 0;
	const bool bGrowB = NumB > S->CapB || S->CapB == 0;   // 결합 0개여도 버퍼는 있어야 한다 (등록이 유효한 버퍼를 요구한다)
	const int32 NewCapN = bGrowN ? (int32)FMath::RoundUpToPowerOfTwo((uint32)FMath::Max(1024, N)) : S->CapN;
	const bool bGrowB2 = bGrowB || S->CapB < N * 6;   // 사건 결합 (m2-log #3) — 입자당 슬롯 12 의 절반만큼은 GPU 가 덧붙일 수 있게
	const int32 NewCapB = bGrowB2 ? (int32)FMath::RoundUpToPowerOfTwo((uint32)FMath::Max3(1024, NumB, N * 6)) : S->CapB;
	const int32 NewCapPairs = NewCapN * 32;   // 탐색 반경 상한 2R(= 지름 2배)에서 입자당 ≈ 19쌍, 여유 포함
	MaxPairCount = NewCapPairs;
	const int32 LN = N, LB = NumB;

	ENQUEUE_RENDER_COMMAND(SandGpuSimUpload)([S, &In, LN, LB, OldN, OldPairs, bKeepPairs, bGrowN, bGrowB = bGrowB2, NewCapN, NewCapB, NewCapPairs](FRHICommandListImmediate& RHICmdList)
	{
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		auto Persist = [&](uint32 Stride, int32 Num, const TCHAR* Name)
		{
			return AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, FMath::Max(1, Num)), Name);
		};
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGpuSimUpload"));

		// 마지막으로 쓴 쌍 목록 벌 — Step 이 끝마다 Cur 를 넘기므로 1 − Cur. 지킬 때는 이것만 옮긴다 (Cur 벌은 다음 Step 이 통째로 다시 쓴다)
		const int32 Last = 1 - S->Cur;
		if (bGrowN)
		{
			TRefCountPtr<FRDGPooledBuffer> OldStart = S->PairStart[Last], OldIJ = S->PairIJ[Last], OldPerm = S->PermInv[Last], OldLam = S->Lambda[Last], OldSorted = S->SortedIJ[Last];
			S->Pos          = Persist(sizeof(FVector4f), NewCapN, TEXT("SandSim.Pos"));
			S->Vel          = Persist(sizeof(FVector4f), NewCapN, TEXT("SandSim.Vel"));
			S->PrevPos      = Persist(sizeof(FVector4f), NewCapN, TEXT("SandSim.PrevPos"));
			S->GroundLambda = Persist(sizeof(float), NewCapN, TEXT("SandSim.GroundLambda"));
			S->WallLambda   = Persist(sizeof(FVector4f), NewCapN, TEXT("SandSim.WallLambda"));
			S->Sleep        = Persist(sizeof(uint32), NewCapN, TEXT("SandSim.Sleep"));
			S->SleepRef     = Persist(sizeof(float), NewCapN, TEXT("SandSim.SleepRef"));
			S->Wet          = Persist(sizeof(float), NewCapN, TEXT("SandSim.Wet"));
			S->BondAdjOffset = Persist(sizeof(uint32), NewCapN + 1, TEXT("SandSim.BondAdjOffset"));
			for (int32 k = 0; k < 2; ++k)
			{
				S->PairStart[k] = Persist(sizeof(uint32), NewCapN + 1, TEXT("SandSim.PairStart"));
				S->PairIJ[k]    = Persist(sizeof(uint32), 2 * NewCapPairs, TEXT("SandSim.PairIJ"));
				S->PermInv[k]   = Persist(sizeof(uint32), NewCapPairs, TEXT("SandSim.PermInv"));
				S->Lambda[k]    = Persist(sizeof(FVector4f), NewCapPairs, TEXT("SandSim.Lambda"));
				S->SortedIJ[k]  = Persist(sizeof(uint32), 2 * NewCapPairs, TEXT("SandSim.SortedIJ"));
			}
			if (bKeepPairs && OldStart.IsValid())
			{
				auto Copy = [&](const TRefCountPtr<FRDGPooledBuffer>& Dst, const TRefCountPtr<FRDGPooledBuffer>& Src, uint64 Bytes)
				{
					if (Bytes > 0) { AddCopyBufferPass(GraphBuilder, GraphBuilder.RegisterExternalBuffer(Dst), 0, GraphBuilder.RegisterExternalBuffer(Src), 0, Bytes); }
				};
				Copy(S->PairStart[Last], OldStart, (OldN + 1) * sizeof(uint32));
				Copy(S->PairIJ[Last], OldIJ, 2 * OldPairs * sizeof(uint32));
				Copy(S->PermInv[Last], OldPerm, OldPairs * sizeof(uint32));
				Copy(S->Lambda[Last], OldLam, OldPairs * sizeof(FVector4f));
				Copy(S->SortedIJ[Last], OldSorted, 2 * OldPairs * sizeof(uint32));
			}
			S->CapN = NewCapN;
			S->CapPairs = NewCapPairs;
		}
		if (bGrowB)
		{
			S->BondIJ   = Persist(sizeof(uint32), 2 * NewCapB, TEXT("SandSim.BondIJ"));
			S->BondData = Persist(sizeof(FVector4f), NewCapB, TEXT("SandSim.BondData"));
			S->BondAdj  = Persist(sizeof(uint32), 2 * NewCapB, TEXT("SandSim.BondAdj"));
			S->CapB = NewCapB;
		}

		auto Up = [&](const TRefCountPtr<FRDGPooledBuffer>& PB, const void* Data, uint64 Bytes)
		{
			FRDGBufferRef B = GraphBuilder.RegisterExternalBuffer(PB);
			if (Bytes > 0) { GraphBuilder.QueueBufferUpload(B, Data, Bytes, ERDGInitialDataFlags::NoCopy); }
			else { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(B), 0u); }
		};
		Up(S->Pos, In.Pos.GetData(), LN * sizeof(FVector4f));
		Up(S->Vel, In.Vel.GetData(), LN * sizeof(FVector4f));
		Up(S->PrevPos, In.Pos.GetData(), LN * sizeof(FVector4f));
		Up(S->GroundLambda, In.GroundLambda.GetData(), LN * sizeof(float));
		Up(S->WallLambda, In.WallLambda.GetData(), LN * sizeof(FVector4f));
		Up(S->Sleep, nullptr, 0);   // 올리기(도구 조작) = 전부 깸 (계획 1-3 v1). w 는 CPU 의 원래 역질량으로 돌아온다
		Up(S->SleepRef, nullptr, 0);
		Up(S->Wet, In.Wet.GetData(), FMath::Min(In.Wet.Num(), LN) * sizeof(float));
		Up(S->BondIJ, In.BondIJ.GetData(), In.BondIJ.Num() * sizeof(uint32));
		Up(S->BondData, In.BondData.GetData(), LB * sizeof(FVector4f));
		Up(S->BondAdjOffset, In.BondAdjOffset.GetData(), In.BondAdjOffset.Num() * sizeof(uint32));
		Up(S->BondAdj, In.BondAdj.GetData(), In.BondAdj.Num() * sizeof(uint32));

		if (bKeepPairs)
		{
			// 덧붙인 입자의 옛 쌍 구간은 비어 있어야 한다 — PairStart[OldN+1 .. LN] = 옛 총수
			if (LN > OldN)
			{
				FSandFrameParams F;
				F.FillFrom  = OldN + 1;
				F.FillTo    = LN + 1;
				F.FillValue = OldPairs;
				F.FillTarget = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->PairStart[Last]));
				AddDirectPass<FSandFillTailCS>(GraphBuilder, RDG_EVENT_NAME("SandFillTail"), ShaderMap, F, LN - OldN, 64);
			}
		}
		else
		{
			// 옛 쌍 목록은 비어 있어야 한다 (구간 전부 0) — 첫 프레임은 차가운 시작
			for (int32 k = 0; k < 2; ++k)
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->PairStart[k])), 0u);
			}
		}
		GraphBuilder.Execute();
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		EndGpuFrame(RHICmdList);
	});
	FlushRenderingCommands();
}

void FSandGpuSim::Step(const FSandGpuFrameParams& P, FSandGpuStats& Out)
{
	check(IsInGameThread());
	Out = FSandGpuStats();
	if (N == 0)
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	FState* S = State;
	const int32 LN = N, LB = NumB, LMax = MaxPairCount;
	uint32 TableSize = 1;
	while ((int32)TableSize < LN * 2) { TableSize <<= 1; }
	const int32 NumStatGroups = FMath::DivideAndRoundUp(LN, StatsGroup);
	// A5 — 결합 칸 수(NumB)가 바뀌는 사건 결합 프레임은 늘 동기 (다음 프레임 디스패치 크기)
	const bool bSync = !P.bAsyncStats || P.bFormBonds;

	ENQUEUE_RENDER_COMMAND(SandGpuSimStep)([S, PCopy = P, bSync, LN, LB, LMax, TableSize, NumStatGroups](FRHICommandListImmediate& RHICmdList)
	{
		const FSandGpuFrameParams& P = PCopy;
		if (S->bStatsInFlight && StatsReady(S)) { FetchStats(S, S->InFlightGroups, false); S->bStatsInFlight = false; }
		const bool bCopyStats = bSync || !S->bStatsInFlight;   // 앞 것이 아직이면 이번 프레임 통계는 건너뛴다
		const int32 Cur = S->Cur, Old = 1 - S->Cur;
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGpuSimStep"));
			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

			// --- 영구 버퍼 ---
			FRDGBufferRef Pos          = GraphBuilder.RegisterExternalBuffer(S->Pos);
			FRDGBufferRef Vel          = GraphBuilder.RegisterExternalBuffer(S->Vel);
			FRDGBufferRef PrevPos      = GraphBuilder.RegisterExternalBuffer(S->PrevPos);
			FRDGBufferRef GroundLambda = GraphBuilder.RegisterExternalBuffer(S->GroundLambda);
			FRDGBufferRef WallLambda   = GraphBuilder.RegisterExternalBuffer(S->WallLambda);
			FRDGBufferRef SleepBuf     = GraphBuilder.RegisterExternalBuffer(S->Sleep);
			FRDGBufferRef SleepRefBuf  = GraphBuilder.RegisterExternalBuffer(S->SleepRef);
			FRDGBufferRef BondIJ        = GraphBuilder.RegisterExternalBuffer(S->BondIJ);
			FRDGBufferRef BondData      = GraphBuilder.RegisterExternalBuffer(S->BondData);
			FRDGBufferRef BondAdjOffset = GraphBuilder.RegisterExternalBuffer(S->BondAdjOffset);
			FRDGBufferRef BondAdj       = GraphBuilder.RegisterExternalBuffer(S->BondAdj);
			FRDGBufferRef PairStart    = GraphBuilder.RegisterExternalBuffer(S->PairStart[Cur]);
			FRDGBufferRef PairIJ       = GraphBuilder.RegisterExternalBuffer(S->PairIJ[Cur]);
			FRDGBufferRef PermInv      = GraphBuilder.RegisterExternalBuffer(S->PermInv[Cur]);
			FRDGBufferRef Lambda       = GraphBuilder.RegisterExternalBuffer(S->Lambda[Cur]);
			FRDGBufferRef SortedIJ     = GraphBuilder.RegisterExternalBuffer(S->SortedIJ[Cur]);
			FRDGBufferRef OldPairStart = GraphBuilder.RegisterExternalBuffer(S->PairStart[Old]);
			FRDGBufferRef OldPairIJ    = GraphBuilder.RegisterExternalBuffer(S->PairIJ[Old]);
			FRDGBufferRef OldPermInv   = GraphBuilder.RegisterExternalBuffer(S->PermInv[Old]);
			FRDGBufferRef OldLambda    = GraphBuilder.RegisterExternalBuffer(S->Lambda[Old]);

			// --- 프레임 임시 ---
			FRDGBufferRef CellKey        = ScratchBuffer(GraphBuilder, TEXT("SandSim.CellKey"), sizeof(uint32), LN);
			FRDGBufferRef CellSlot       = ScratchBuffer(GraphBuilder, TEXT("SandSim.CellSlot"), sizeof(uint32), LN);
			FRDGBufferRef CellCount      = ScratchBuffer(GraphBuilder, TEXT("SandSim.CellCount"), sizeof(uint32), TableSize);
			FRDGBufferRef CellStart      = ScratchBuffer(GraphBuilder, TEXT("SandSim.CellStart"), sizeof(uint32), TableSize + 1);
			FRDGBufferRef SortedIdx      = ScratchBuffer(GraphBuilder, TEXT("SandSim.SortedIdx"), sizeof(uint32), LN);
			FRDGBufferRef BlockKey       = ScratchBuffer(GraphBuilder, TEXT("SandSim.BlockKey"), sizeof(uint32), LN);
			FRDGBufferRef PairCount      = ScratchBuffer(GraphBuilder, TEXT("SandSim.PairCount"), sizeof(uint32), LN);
			FRDGBufferRef PairWarm       = ScratchBuffer(GraphBuilder, TEXT("SandSim.PairWarm"), sizeof(FVector4f), LMax);
			FRDGBufferRef PairBlock      = ScratchBuffer(GraphBuilder, TEXT("SandSim.PairBlock"), sizeof(uint32), LMax);
			FRDGBufferRef PairLocal      = ScratchBuffer(GraphBuilder, TEXT("SandSim.PairLocal"), sizeof(uint32), LMax);
			FRDGBufferRef BlockPairCount = ScratchBuffer(GraphBuilder, TEXT("SandSim.BlockPairCount"), sizeof(uint32), BlockTableSize);
			FRDGBufferRef BlockStart     = ScratchBuffer(GraphBuilder, TEXT("SandSim.BlockStart"), sizeof(uint32), BlockTableSize + 1);
			FRDGBufferRef CountersBuf    = ScratchBuffer(GraphBuilder, TEXT("SandSim.Counters"), sizeof(uint32), 4);
			FRDGBufferRef AdjCount       = ScratchBuffer(GraphBuilder, TEXT("SandSim.AdjCount"), sizeof(uint32), LN);
			FRDGBufferRef AdjStart       = ScratchBuffer(GraphBuilder, TEXT("SandSim.AdjStart"), sizeof(uint32), LN + 1);
			FRDGBufferRef Adj            = ScratchBuffer(GraphBuilder, TEXT("SandSim.Adj"), sizeof(uint32), 2 * LMax);
			FRDGBufferRef AdjCursor      = ScratchBuffer(GraphBuilder, TEXT("SandSim.AdjCursor"), sizeof(uint32), LN);
			FRDGBufferRef PairImpulse    = ScratchBuffer(GraphBuilder, TEXT("SandSim.PairImpulse"), sizeof(FVector4f), LMax);
			FRDGBufferRef BondImpulse    = LB > 0 ? ScratchBuffer(GraphBuilder, TEXT("SandSim.BondImpulse"), sizeof(FVector4f), LB)
			                                      : UploadBuffer(GraphBuilder, TEXT("SandSim.BondImpulse"), TArray<FVector4f>());
			FRDGBufferRef FrameCountsBuf = ScratchBuffer(GraphBuilder, TEXT("SandSim.FrameCounts"), sizeof(uint32), 4);
			FRDGBufferRef Stats          = ScratchBuffer(GraphBuilder, TEXT("SandSim.Stats"), sizeof(FVector4f), NumStatGroups);
			FRDGBufferRef Count          = ScratchBuffer(GraphBuilder, TEXT("SandSim.Count"), sizeof(uint32), LN);
			FRDGBufferRef BoundaryImpulse = ScratchBuffer(GraphBuilder, TEXT("SandSim.BoundaryImpulse"), sizeof(FVector4f), LMax);
			FRDGBufferRef IndirectArgs   = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4u, 12), TEXT("SandSim.IndirectArgs"));

			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CellCount), 0u);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BlockPairCount), 0u);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CountersBuf), 0u);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AdjCount), 0u);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AdjCursor), 0u);

			// --- 프레임 파라미터 ---
			FSandFrameParams F;
			F.NumParticles = LN;
			F.TableSize    = TableSize;
			F.BlockTable   = BlockTableSize;
			F.MaxPairs     = LMax;
			F.NumBonds     = LB;
			F.ScanCount    = 0;
			F.CellSize     = P.SearchRange;
			F.Radius       = P.Radius;
			F.StepDt       = P.StepDt;
			F.BlockSize    = P.BlockSize;
			F.bColor8      = P.bColor8 ? 1 : 0;
			F.WarmStart    = P.WarmStart;
			F.WarmGapTol   = P.WarmGapTol;
			F.bWarmTangent = P.bWarmTangent ? 1 : 0;
			F.Friction     = P.Friction;
			BindGround(GraphBuilder, F, &P);
			F.Gravity      = P.Gravity;
			F.BondStretchBreak = P.BreakStretch;
			F.bBondsDry    = P.bBondsDry ? 1 : 0;
			F.VelocityDamping = P.VelocityDamping;
			F.WallMode       = P.WallMode;
			F.WallActiveMask = P.WallActiveMask;
			F.WallLX    = P.WallLX;
			F.WallLY    = P.WallLY;
			F.WallLimit = P.WallLimit;
			F.WallLiftZ = P.WallLiftZ;
			F.bSleep      = P.bSleep ? 1 : 0;
			F.bParticleMargin = P.bParticleMargin ? 1 : 0; F.bRelMargin = P.bRelMargin ? 1 : 0;
			F.MarginMinR = P.MarginMinR;
			F.bWakeCold = P.bWakeCold ? 1 : 0;
			F.bWakeZeroPairs = P.bWakeZeroPairs ? 1 : 0;
			F.bSleepColdPairs = P.bSleepColdPairs ? 1 : 0;
			F.WakeLamRatio = P.WakeLamRatio;
			F.WakeLamAbs   = P.WakeLamAbs;
			F.SleepRefRW   = GraphBuilder.CreateUAV(SleepRefBuf);
			F.SleepVel    = P.SleepVel;
			F.SleepFrames = (uint32)FMath::Max(1, P.SleepFrames);
			F.WakeVel     = P.WakeVel;
			F.WakeTouchVel = P.WakeTouchVel;
			F.WakePass    = 0;
			F.SleepRW     = GraphBuilder.CreateUAV(SleepBuf);

			F.PosRW = GraphBuilder.CreateUAV(Pos);           F.PosRO = GraphBuilder.CreateSRV(Pos);
			F.VelRW = GraphBuilder.CreateUAV(Vel);           F.VelRO = GraphBuilder.CreateSRV(Vel);
			F.PrevPosRW = GraphBuilder.CreateUAV(PrevPos);
			F.CellKeyRW = GraphBuilder.CreateUAV(CellKey);   F.CellKeyRO = GraphBuilder.CreateSRV(CellKey);
			F.CellSlotRW = GraphBuilder.CreateUAV(CellSlot); F.CellSlotRO = GraphBuilder.CreateSRV(CellSlot);
			F.CellCountRW = GraphBuilder.CreateUAV(CellCount);
			F.CellStart = GraphBuilder.CreateSRV(CellStart);
			F.SortedIdxRW = GraphBuilder.CreateUAV(SortedIdx); F.SortedIdxRO = GraphBuilder.CreateSRV(SortedIdx);
			F.BlockKeyRW = GraphBuilder.CreateUAV(BlockKey);   F.BlockKeyRO = GraphBuilder.CreateSRV(BlockKey);
			F.PairCountRW = GraphBuilder.CreateUAV(PairCount);
			F.PairStart = GraphBuilder.CreateSRV(PairStart);
			F.PairIJRW = GraphBuilder.CreateUAV(PairIJ);       F.PairIJRO = GraphBuilder.CreateSRV(PairIJ);
			F.PairWarmRW = GraphBuilder.CreateUAV(PairWarm);   F.PairWarmRO = GraphBuilder.CreateSRV(PairWarm);
			F.PairBlockRW = GraphBuilder.CreateUAV(PairBlock); F.PairBlockRO = GraphBuilder.CreateSRV(PairBlock);
			F.PairLocalRW = GraphBuilder.CreateUAV(PairLocal); F.PairLocalRO = GraphBuilder.CreateSRV(PairLocal);
			F.BlockPairCountRW = GraphBuilder.CreateUAV(BlockPairCount);
			F.BlockStart = GraphBuilder.CreateSRV(BlockStart);
			F.CountersRW = GraphBuilder.CreateUAV(CountersBuf);   F.CountersRO = GraphBuilder.CreateSRV(CountersBuf);
			F.OldPairStart = GraphBuilder.CreateSRV(OldPairStart);
			F.OldPairIJ    = GraphBuilder.CreateSRV(OldPairIJ);
			F.OldPermInv   = GraphBuilder.CreateSRV(OldPermInv);
			F.OldLambda    = GraphBuilder.CreateSRV(OldLambda);
			F.SortedIJRW = GraphBuilder.CreateUAV(SortedIJ);
			F.LambdaRW   = GraphBuilder.CreateUAV(Lambda);
			F.PermInvRW  = GraphBuilder.CreateUAV(PermInv);
			F.PairImpulseRW = GraphBuilder.CreateUAV(PairImpulse); F.PairImpulseRO = GraphBuilder.CreateSRV(PairImpulse);
			F.AdjCountRW = GraphBuilder.CreateUAV(AdjCount);
			F.AdjStart   = GraphBuilder.CreateSRV(AdjStart);
			F.AdjRW = GraphBuilder.CreateUAV(Adj);             F.AdjRO = GraphBuilder.CreateSRV(Adj);
			F.AdjCursorRW = GraphBuilder.CreateUAV(AdjCursor);
			F.BondIJ = GraphBuilder.CreateSRV(BondIJ);
			F.BondDataRW = GraphBuilder.CreateUAV(BondData);   F.BondDataRO = GraphBuilder.CreateSRV(BondData);
			F.BondImpulseRW = GraphBuilder.CreateUAV(BondImpulse); F.BondImpulseRO = GraphBuilder.CreateSRV(BondImpulse);
			F.BondAdjOffset = GraphBuilder.CreateSRV(BondAdjOffset);
			F.BondAdj = GraphBuilder.CreateSRV(BondAdj);
			F.GroundLambdaRW = GraphBuilder.CreateUAV(GroundLambda);
			F.WallLambdaRW   = GraphBuilder.CreateUAV(WallLambda);
			F.IndirectArgsRW = GraphBuilder.CreateUAV(IndirectArgs, PF_R32_UINT);
			F.FrameCountsRW = GraphBuilder.CreateUAV(FrameCountsBuf); F.FrameCountsRO = GraphBuilder.CreateSRV(FrameCountsBuf);
			F.StatsRW = GraphBuilder.CreateUAV(Stats);

			// --- 이웃 탐색 · 쌍 생성 · 워밍 · 예측 ---
			// 3-4 — 단계 경계 GPU 타임스탬프 (UE 5.8 은 옛 GPU 프로파일러가 없어 RHI 쿼리를 직접, papers C11)
			const bool bStamps = GSupportsTimestampRenderQueries;
			if (bStamps && !S->StageTs[0].IsValid()) { for (FRenderQueryRHIRef& Q : S->StageTs) { Q = RHICreateRenderQuery(RQT_AbsoluteTime); } }
			auto Stamp = [&](int32 Idx)
			{
				if (!bStamps) { return; }
				FRHIRenderQuery* Q = S->StageTs[Idx].GetReference();
				GraphBuilder.AddPass(RDG_EVENT_NAME("SandStamp%d", Idx), ERDGPassFlags::NeverCull, [Q](FRHICommandListImmediate& Cmd) { Cmd.EndRenderQuery(Q); });
			};
			Stamp(0);
			FRDGBufferRef BondTrigBuf = nullptr;
			// M2 T2 — 도구 스탬프 · 배수 (패스 0). 손 재고 버퍼는 처음 한 번 만든다
			{
				const int32 HandCap = FMath::Max(1, P.HandCap);
				if (!S->HandCount.IsValid() || S->CapHand < HandCap)
				{
					S->HandW = AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), HandCap), TEXT("SandSim.HandW"));
					const bool bNewCount = !S->HandCount.IsValid();
					if (bNewCount) { S->HandCount = AllocatePooledBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4), TEXT("SandSim.HandCount")); }
					if (bNewCount) { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->HandCount)), 0u); }
					S->CapHand = HandCap;
				}
				FRDGBufferRef HandCountBuf = GraphBuilder.RegisterExternalBuffer(S->HandCount);
				// 이 프레임 계수 [1] [2] [3] 만 0 으로 — [0] 재고는 유지
				{
					TArray<uint32> Zero = { 0u, 0u, 0u };
					FRDGBufferRef Z = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 3), TEXT("SandSim.HandZero"));
					GraphBuilder.QueueBufferUpload(Z, Zero.GetData(), 3 * sizeof(uint32), ERDGInitialDataFlags::None);   // 지역 배열 — 복사해 둔다 (NoCopy 면 실행 전에 사라진다)
					AddCopyBufferPass(GraphBuilder, HandCountBuf, sizeof(uint32), Z, 0, 3 * sizeof(uint32));
				}
				const bool bWetting = P.bWetting && P.GroundBed.IsValid() && P.GroundWater.IsValid();
				const bool bLoad = P.GroundLoad.IsValid() && P.GroundBed.IsValid() && P.GroundWater.IsValid() && P.LoadMassKg > 0.f;
				const bool bSubOK = P.GroundBed.IsValid() && P.GroundWater.IsValid() && P.SubMassKg > 0.f;
				const bool bBuoy = P.bBuoyancy && bSubOK, bDragOn = P.bDrag && bSubOK;
				const bool bReturn = P.GroundReturn.IsValid() && P.GroundBed.IsValid() && P.GroundWater.IsValid();
				const bool bTool = P.Stamps.Num() > 0 || P.DrainTau > 0.f || P.PileTargets.Num() > 0 || bWetting || bLoad || bBuoy || bDragOn || bReturn;
				BondTrigBuf = ScratchBuffer(GraphBuilder, TEXT("SandSim.BondTrig"), sizeof(uint32), LN);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BondTrigBuf), 0u);
				if (bTool)
				{
					FRDGBufferRef WetBuf = GraphBuilder.RegisterExternalBuffer(S->Wet);
					TArray<FVector4f> St = P.Stamps;
					if (St.Num() == 0) { St.Add(FVector4f(0, 0, 0, 0)); St.Add(FVector4f(0, 0, 0, 0)); }
					FSandToolParams TP;
					TP.NumParticles = LN;
					TP.NumBonds = LB;
					TP.NumStamps = P.Stamps.Num() / 2;
					TP.StampSeed = P.StampSeed;
					TP.HandCap = HandCap;
					TP.SleepFrames = P.bSleep ? (uint32)FMath::Max(1, P.SleepFrames) : 0u;
					TP.GraveZ = P.GraveZ; TP.GraveSpacing = P.GraveSpacing;
					TP.StepDt = P.StepDt;
					TP.DrainTarget = P.DrainTarget; TP.DrainTau = P.DrainTau;
					TP.bPerParticleWet = P.bPerParticleWet ? 1u : 0u;
					TP.WDry = P.WDry; TP.WOptStart = P.WOptStart; TP.WOptEnd = P.WOptEnd; TP.WSat = P.WSat;
					BindGround(GraphBuilder, TP, &P);
					TP.WetMode = bWetting ? 1u : 0u; TP.WetRadius = P.Radius;
					TP.GroundWater = GraphBuilder.CreateSRV((bWetting || bLoad || bBuoy || bDragOn || bReturn) ? GraphBuilder.RegisterExternalBuffer(P.GroundWater) : GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 1), TEXT("SandSim.NoWater")));
					// M4 T4 ① — 칸별 잠겨 닿은 입자 수를 먼저 센다
					TP.LoadMode = bLoad ? 1u : 0u; TP.LoadMassKg = FMath::Max(P.LoadMassKg, 1e-6f);
					// M4 T4 ② — 부력 가속 = g ρw/ρs (알갱이 부피에만), 항력 K = ½ ρw π R² / m (R = 입자 반지름)
					const float Rm = P.Radius / 100.f;
					TP.BuoyMode = bBuoy ? 1u : 0u; TP.BuoyAccel = 980.f * 1000.f / FMath::Max(P.GrainDensity, 1.f);
					TP.DragMode = bDragOn ? 1u : 0u; TP.DragK = 0.5f * 1000.f * UE_PI * Rm * Rm / FMath::Max(P.SubMassKg, 1e-6f);
					TP.DragDiam = 2.f * Rm; TP.WaterNu = 1.0e-6f;
					const int32 NCell = bLoad ? P.GroundNX * P.GroundNY : 1;
					FRDGBufferRef LoadCnt = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NCell), TEXT("SandSim.LoadCount"));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(LoadCnt), 0u);
					if (bLoad)
					{
						TP.GroundLoad = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(P.GroundLoad));
					}
					else
					{
						FRDGBufferRef Lz = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector2f), 1), TEXT("SandSim.NoLoad"));
						AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Lz), 0u);
						TP.GroundLoad = GraphBuilder.CreateSRV(Lz);
					}
					FRDGBufferRef StBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), St.Num()), TEXT("SandSim.Stamps"));
					GraphBuilder.QueueBufferUpload(StBuf, St.GetData(), St.Num() * sizeof(FVector4f), ERDGInitialDataFlags::None);   // 지역 배열 — 복사
					TP.Stamps = GraphBuilder.CreateSRV(StBuf);
					TArray<FVector4f> PT = P.PileTargets;
					if (PT.Num() == 0) { PT.Add(FVector4f(0, 0, 0, 0)); }
					FRDGBufferRef PtBuf = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), PT.Num()), TEXT("SandSim.PileTargets"));
					GraphBuilder.QueueBufferUpload(PtBuf, PT.GetData(), PT.Num() * sizeof(FVector4f), ERDGInitialDataFlags::None);
					TP.PileTargets = GraphBuilder.CreateSRV(PtBuf);
					TP.NumPileTargets = P.PileTargets.Num();
					TP.PileDropVel = P.PileDropVel;
					TP.PosRW = GraphBuilder.CreateUAV(Pos);
					TP.VelRW = GraphBuilder.CreateUAV(Vel);
					TP.PrevPosRW = GraphBuilder.CreateUAV(PrevPos);
					TP.WetRW = GraphBuilder.CreateUAV(WetBuf);
					TP.SleepRW = GraphBuilder.CreateUAV(SleepBuf);
					TP.GroundLambdaRW = GraphBuilder.CreateUAV(GroundLambda);
					TP.WallLambdaRW = GraphBuilder.CreateUAV(WallLambda);
					TP.HandWRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->HandW));
					TP.HandCountRW = GraphBuilder.CreateUAV(HandCountBuf);
					TP.BondIJ = GraphBuilder.CreateSRV(BondIJ);
					TP.BondTrigRW = GraphBuilder.CreateUAV(BondTrigBuf);
					if (bLoad)
					{
						FSandToolParams TC = TP; TC.LoadCountRW = GraphBuilder.CreateUAV(LoadCnt);
						TC.VelRW = nullptr; TC.PrevPosRW = nullptr; TC.WetRW = nullptr; TC.GroundLambdaRW = nullptr; TC.WallLambdaRW = nullptr; TC.HandWRW = nullptr; TC.HandCountRW = nullptr; TC.BondTrigRW = nullptr;
						AddDirectPass<FSandLoadCountCS>(GraphBuilder, RDG_EVENT_NAME("SandLoadCount"), ShaderMap, TC, LN, 64);
					}
					TP.LoadCountRO = GraphBuilder.CreateSRV(LoadCnt);
					if (bReturn)
					{
						// M4 T5 파괴 환원 — 산 결합 확인에 결합 데이터 · 인접 목록
						FSandToolParams TR = TP; TR.ReturnMode = 1u; TR.ReturnVel = P.SleepVel; TR.ReturnAccRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(P.GroundReturn));
						TR.HandWRW = nullptr; TR.HandCountRW = nullptr; TR.BondTrigRW = nullptr;
						TR.BondDataRW = GraphBuilder.CreateUAV(BondData); TR.OldAdjOffset = GraphBuilder.CreateSRV(BondAdjOffset); TR.OldAdj = GraphBuilder.CreateSRV(BondAdj);
						AddDirectPass<FSandReturnCS>(GraphBuilder, RDG_EVENT_NAME("SandReturn"), ShaderMap, TR, LN, 64);
					}
					AddDirectPass<FSandStampCS>(GraphBuilder, RDG_EVENT_NAME("SandStamp"), ShaderMap, TP, LN, 64);
					if (P.PileTargets.Num() > 0)
					{
						// M2 T3 — 무덤 칸이 손 재고를 하나씩 받아 놓을 자리로 (파기 뒤 — 같은 프레임에 판 것도 쌓을 수 있다)
						AddDirectPass<FSandPileCS>(GraphBuilder, RDG_EVENT_NAME("SandPile"), ShaderMap, TP, LN, 64);
						AddDirectPass<FSandPileFinishCS>(GraphBuilder, RDG_EVENT_NAME("SandPileFinish"), ShaderMap, TP, 1, 1);
					}
					if (LB > 0)
					{
						FSandToolParams TB = TP;
						TB.WetRW = nullptr;
						TB.WetRO = GraphBuilder.CreateSRV(WetBuf);
						TB.VelRW = nullptr; TB.PrevPosRW = nullptr; TB.SleepRW = nullptr; TB.GroundLambdaRW = nullptr; TB.WallLambdaRW = nullptr; TB.HandWRW = nullptr; TB.HandCountRW = nullptr; TB.BondTrigRW = nullptr;
						TB.BondDataRW = GraphBuilder.CreateUAV(BondData);
						AddDirectPass<FSandBondToolCS>(GraphBuilder, RDG_EVENT_NAME("SandBondTool"), ShaderMap, TB, LB, 64);
					}
				}
				if (bCopyStats) { AddEnqueueCopyPass(GraphBuilder, &S->HandRB, HandCountBuf, 4 * sizeof(uint32)); }
			}
			AddDirectPass<FSandCellKeyCS>(GraphBuilder, RDG_EVENT_NAME("SandCellKey"), ShaderMap, F, LN, 64);
			AddScan(GraphBuilder, ShaderMap, F, CellCount, CellStart, TableSize);
			AddDirectPass<FSandScatterCS>(GraphBuilder, RDG_EVENT_NAME("SandScatter"), ShaderMap, F, LN, 64);
			AddDirectPass<FSandPairCountCS>(GraphBuilder, RDG_EVENT_NAME("SandPairCount"), ShaderMap, F, LN, 64);
			AddScan(GraphBuilder, ShaderMap, F, PairCount, PairStart, LN);
			AddDirectPass<FSandPairFillCS>(GraphBuilder, RDG_EVENT_NAME("SandPairFill"), ShaderMap, F, LN, 64);
			AddScan(GraphBuilder, ShaderMap, F, BlockPairCount, BlockStart, BlockTableSize);
			AddScan(GraphBuilder, ShaderMap, F, AdjCount, AdjStart, LN);
			AddDirectPass<FSandBuildArgsCS>(GraphBuilder, RDG_EVENT_NAME("SandBuildArgs"), ShaderMap, F, 1, 1);
			AddIndirectPass<FSandPairScatterCS>(GraphBuilder, RDG_EVENT_NAME("SandPairScatter"), ShaderMap, F, IndirectArgs, 0);
			if (P.bSleep)
			{
				// C안 깨우기 — 충격 → 방금 깬 입자의 이웃 (깊이 2). 워밍·예측 앞이라 깬 입자는 이번 프레임부터 움직인다
				FSandFrameParams FW = F;
				AddIndirectPass<FSandWakeCS>(GraphBuilder, RDG_EVENT_NAME("SandWake"), ShaderMap, FW, IndirectArgs, 0);
				FW.WakePass = 1;
				AddIndirectPass<FSandWakeCS>(GraphBuilder, RDG_EVENT_NAME("SandWakeSpread"), ShaderMap, FW, IndirectArgs, 0);
				if (P.bWakeZeroPairs) { AddIndirectPass<FSandWakeZeroCS>(GraphBuilder, RDG_EVENT_NAME("SandWakeZero"), ShaderMap, F, IndirectArgs, 0); }
			}
			if (LB > 0)
			{
				AddDirectPass<FSandBondWarmCS>(GraphBuilder, RDG_EVENT_NAME("SandBondWarm"), ShaderMap, F, LB, 64);
			}
			Stamp(1);
			AddDirectPass<FSandWarmPredictCS>(GraphBuilder, RDG_EVENT_NAME("SandWarmPredict"), ShaderMap, F, LN, 64);

			// --- 반복 ---
			FSandSolverParams T;
			T.NumParticles = LN;
			T.NumInternal  = 0;
			T.NumBoundary  = 0;
			T.NumBlocks    = BlockTableSize;
			T.BlockBase    = 0;
			T.NumBonds     = LB;   // 결합이 있으면 항상 돈다 — 꺼져 있을 땐 임펄스 0 을 써야 ApplyBonds 가 워밍 임펄스를 다시 읽지 않는다
			T.bUseFrameCounts = 1;
			T.bBondsActive = P.bBondsActive ? 1 : 0;
			T.Radius   = P.Radius;
			T.StepDt   = P.StepDt;
			T.Kv       = P.Kv;
			T.Kc       = P.Kc;
			T.SorOmega = P.SorOmega;
			T.Friction = P.Friction;
			BindGround(GraphBuilder, T, &P);
			T.MaxDepenVel = P.MaxDepenVel;
			T.bMassSplit     = P.bMassSplit ? 1 : 0;
			T.BondAlphaTilde = P.BondAlphaTilde;
			T.BondLambdaMax  = P.BondLambdaMax;
			T.BondOmega      = P.BondOmega;
			T.WallMode       = P.WallMode;
			T.WallActiveMask = P.WallActiveMask;
			T.WallLX       = P.WallLX;
			T.WallLY       = P.WallLY;
			T.WallLimit    = P.WallLimit;
			T.WallLiftZ    = P.WallLiftZ;
			T.WallFriction = P.WallFriction;
			T.PosRO = F.PosRO;  T.PosRW = F.PosRW;
			T.VelRO = F.VelRO;  T.VelRW = F.VelRW;
			T.PrevPos = GraphBuilder.CreateSRV(PrevPos);
			T.ContactIJ       = GraphBuilder.CreateSRV(SortedIJ);
			T.ContactLambdaRO = GraphBuilder.CreateSRV(Lambda);
			T.ContactLambdaRW = F.LambdaRW;
			T.BlockRange      = F.BlockStart;
			T.BoundaryImpulseRW = GraphBuilder.CreateUAV(BoundaryImpulse);
			T.BoundaryImpulseRO = GraphBuilder.CreateSRV(BoundaryImpulse);
			T.BoundaryAdjOffset = F.AdjStart;
			T.BoundaryAdj       = F.AdjRO;
			T.FrameCounts       = F.FrameCountsRO;
			T.CountRW = GraphBuilder.CreateUAV(Count);
			T.CountRO = GraphBuilder.CreateSRV(Count);
			T.BondIJ        = F.BondIJ;
			T.BondDataRW    = F.BondDataRW;
			T.BondImpulseRW = F.BondImpulseRW;
			T.BondImpulseRO = F.BondImpulseRO;
			T.BondAdjOffset = F.BondAdjOffset;
			T.BondAdj       = F.BondAdj;
			T.GroundLambdaRW = F.GroundLambdaRW;
			T.WallLambdaRW   = F.WallLambdaRW;

			// 단계 2 — 블록 안 배치 나누기 (프레임당 1회, 반복 앞)
			const bool bColor8Par = P.bColor8 && P.bColor8Par;
			FRDGBufferRef BatchStatsBuf = ScratchBuffer(GraphBuilder, TEXT("SandSim.BatchStats"), sizeof(uint32), 8);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BatchStatsBuf), 0u);
			if (bColor8Par)
			{
				FRDGBufferRef ContactBatch = ScratchBuffer(GraphBuilder, TEXT("SandSim.ContactBatch"), sizeof(uint32), LMax);
				FRDGBufferRef ContactOrder = ScratchBuffer(GraphBuilder, TEXT("SandSim.ContactOrder"), sizeof(uint32), LMax);
				FRDGBufferRef BatchStart   = ScratchBuffer(GraphBuilder, TEXT("SandSim.BatchStart"), sizeof(uint32), BlockTableSize * BatchStride);
				T.ContactBatchRW = GraphBuilder.CreateUAV(ContactBatch);
				T.ContactOrderRW = GraphBuilder.CreateUAV(ContactOrder);
				T.ContactOrderRO = GraphBuilder.CreateSRV(ContactOrder);
				T.BatchStartRW   = GraphBuilder.CreateUAV(BatchStart);
				T.BatchStartRO   = GraphBuilder.CreateSRV(BatchStart);
				T.BatchStatsRW   = GraphBuilder.CreateUAV(BatchStatsBuf);
				AddDirectPass<FSandColorBatchCS>(GraphBuilder, RDG_EVENT_NAME("SandColorBatch"), ShaderMap, T, BlockTableSize * 64, 64);
			}
			FRDGBufferRef IterArgs = nullptr;
			if (bColor8Par && P.bSleep)
			{
				// M4 T6 — 깬 것이 없으면 반복 패스 0 그룹
				IterArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(3), TEXT("SandSim.IterArgs"));
				FSandSolverParams TA = T; TA.BatchStatsRW = GraphBuilder.CreateUAV(BatchStatsBuf); TA.NumBlocks = BlockTableSize / 8;
				AddDirectPass<FSandAnyAwakeCS>(GraphBuilder, RDG_EVENT_NAME("SandAnyAwake"), ShaderMap, TA, LN, 64);
				TA.IterArgsRW = GraphBuilder.CreateUAV(IterArgs, PF_R32_UINT);
				AddDirectPass<FSandIterArgsCS>(GraphBuilder, RDG_EVENT_NAME("SandIterArgs"), ShaderMap, TA, 1, 1);
			}
			Stamp(2);
			AddSolverIterations(GraphBuilder, ShaderMap, T, P.Iterations, LN, BlockTableSize, LB, P.bMassSplit, IndirectArgs, 0, P.bColor8, P.FrameIndex, bColor8Par, IterArgs);
			Stamp(3);

			// --- 파단 · 통계 ---
			if (LB > 0)
			{
				AddDirectPass<FSandBreakBondsCS>(GraphBuilder, RDG_EVENT_NAME("SandBreakBonds"), ShaderMap, F, LB, 64);
			}
			AddDirectPass<FSandStatsCS>(GraphBuilder, RDG_EVENT_NAME("SandStats"), ShaderMap, F, NumStatGroups * StatsGroup, StatsGroup);

			// 사건 결합 생성 (m2-log #3) — 이번 쌍 목록 · 파단 뒤. 새 결합은 다음 프레임부터 푼다
			{
				TArray<uint32> Tot = { (uint32)LB, 0u, 0u, 0u };
				FRDGBufferRef BondTotal = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 4), TEXT("SandSim.BondTotal"));
				GraphBuilder.QueueBufferUpload(BondTotal, Tot.GetData(), 4 * sizeof(uint32), ERDGInitialDataFlags::None);
				if (P.bFormBonds && BondTrigBuf)
				{
					FRDGBufferRef WetBuf = GraphBuilder.RegisterExternalBuffer(S->Wet);
					FRDGBufferRef BondSlot = ScratchBuffer(GraphBuilder, TEXT("SandSim.BondSlot"), sizeof(uint32), LN);
					FRDGBufferRef Cursor   = ScratchBuffer(GraphBuilder, TEXT("SandSim.BondCursor"), sizeof(uint32), LN);
					FRDGBufferRef OldAdjOff = ScratchBuffer(GraphBuilder, TEXT("SandSim.OldBondAdjOffset"), sizeof(uint32), LN + 1);
					FRDGBufferRef OldAdjBuf = ScratchBuffer(GraphBuilder, TEXT("SandSim.OldBondAdj"), sizeof(uint32), FMath::Max(1, 2 * LB));
					AddCopyBufferPass(GraphBuilder, OldAdjOff, 0, BondAdjOffset, 0, (LN + 1) * sizeof(uint32));
					if (LB > 0) { AddCopyBufferPass(GraphBuilder, OldAdjBuf, 0, BondAdj, 0, 2 * LB * sizeof(uint32)); }
					else { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OldAdjBuf), 0u); }   // 결합 0 개 — 읽기 전에 만들어져 있어야 한다
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BondSlot), 0u);
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Cursor), 0u);
					FSandToolParams BP;
					BP.NumParticles = LN; BP.NumBonds = LB;
					BP.GraveZ = P.GraveZ; BP.GraveSpacing = P.GraveSpacing;
					BP.WDry = P.WDry; BP.WOptStart = P.WOptStart; BP.WOptEnd = P.WOptEnd; BP.WSat = P.WSat;
					BP.NumPairsMax = LMax; BP.BondCap = S->CapB; BP.BondSlots = P.BondSlots;
					BP.FormRange = P.BondFormRange; BP.FormMinRest = 2.f * P.Radius;
					BP.PosRW = GraphBuilder.CreateUAV(Pos);
					BP.WetRO = GraphBuilder.CreateSRV(WetBuf);
					BP.BondDataRW = GraphBuilder.CreateUAV(BondData);
					BP.BondIJRW = GraphBuilder.CreateUAV(BondIJ);
					BP.BondSlotRW = GraphBuilder.CreateUAV(BondSlot);
					BP.BondTotalRW = GraphBuilder.CreateUAV(BondTotal);
					// A2 — 죽은 결합 칸 압축 (사건 결합 프레임마다). 산 것만 앞으로 모으고 칸 수 = 산 수. 옛 인접 목록은 Remap 으로 새 번호를 읽는다
					FRDGBufferRef Remap = ScratchBuffer(GraphBuilder, TEXT("SandSim.BondRemap"), sizeof(uint32), LB + 1);
					if (LB > 0)
					{
						FRDGBufferRef Flag = ScratchBuffer(GraphBuilder, TEXT("SandSim.BondFlag"), sizeof(uint32), LB);
						FRDGBufferRef TmpIJ = ScratchBuffer(GraphBuilder, TEXT("SandSim.BondTmpIJ"), sizeof(uint32), 2 * LB);
						FRDGBufferRef TmpData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), LB), TEXT("SandSim.BondTmpData"));
						FSandToolParams CP = BP;
						CP.BondFlagRW = GraphBuilder.CreateUAV(Flag);
						CP.TempDataRW = GraphBuilder.CreateUAV(TmpData);
						AddDirectPass<FSandBondAliveCS>(GraphBuilder, RDG_EVENT_NAME("SandBondAlive"), ShaderMap, CP, LB, 64);
						AddScan(GraphBuilder, ShaderMap, F, Flag, Remap, LB);
						FSandToolParams CC = BP;
						CC.BondRemap = GraphBuilder.CreateSRV(Remap);
						CC.TempIJRW = GraphBuilder.CreateUAV(TmpIJ);
						CC.TempDataRW = GraphBuilder.CreateUAV(TmpData);
						AddDirectPass<FSandBondCompactCS>(GraphBuilder, RDG_EVENT_NAME("SandBondCompact"), ShaderMap, CC, LB, 64);
						AddCopyBufferPass(GraphBuilder, BondIJ, 0, TmpIJ, 0, 2 * LB * sizeof(uint32));
						AddCopyBufferPass(GraphBuilder, BondData, 0, TmpData, 0, LB * sizeof(FVector4f));
						FSandToolParams CN = BP;
						CN.BondRemap = GraphBuilder.CreateSRV(Remap);
						AddDirectPass<FSandBondCompactCountCS>(GraphBuilder, RDG_EVENT_NAME("SandBondCompactCount"), ShaderMap, CN, 1, 1);
						S->bCompactedThisStep = true;
					}
					else { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Remap), 0u); }
					if (LB > 0) { AddDirectPass<FSandBondSlotCountCS>(GraphBuilder, RDG_EVENT_NAME("SandBondSlotCount"), ShaderMap, BP, LB, 64); }
					FSandToolParams BF = BP;
					BF.PairIJ = GraphBuilder.CreateSRV(SortedIJ);
					BF.FrameCounts = GraphBuilder.CreateSRV(FrameCountsBuf);
					BF.BondTrig = GraphBuilder.CreateSRV(BondTrigBuf);
					BF.OldAdjOffset = GraphBuilder.CreateSRV(OldAdjOff);
					BF.OldAdj = GraphBuilder.CreateSRV(OldAdjBuf);
					BF.BondRemap = GraphBuilder.CreateSRV(Remap);
					AddDirectPass<FSandBondFormCS>(GraphBuilder, RDG_EVENT_NAME("SandBondForm"), ShaderMap, BF, LMax, 64);
					AddScan(GraphBuilder, ShaderMap, F, BondSlot, BondAdjOffset, LN);
					FSandToolParams BL = BP;
					BL.BondSlotRW = nullptr; BL.PosRW = nullptr; BL.WetRO = nullptr;
					BL.BondAdjRW = GraphBuilder.CreateUAV(BondAdj);
					BL.BondCursorRW = GraphBuilder.CreateUAV(Cursor);
					BL.BondAdjOffsetRO = GraphBuilder.CreateSRV(BondAdjOffset);
					AddDirectPass<FSandBondFillCS>(GraphBuilder, RDG_EVENT_NAME("SandBondFill"), ShaderMap, BL, S->CapB, 64);
				}
				if (bCopyStats) { AddEnqueueCopyPass(GraphBuilder, &S->BondTotalRB, BondTotal, 4 * sizeof(uint32)); }
			}

			Stamp(4);
			if (bCopyStats)
			{
				AddEnqueueCopyPass(GraphBuilder, &S->StatsRB, Stats, NumStatGroups * sizeof(FVector4f));
				AddEnqueueCopyPass(GraphBuilder, &S->CountersRB, CountersBuf, 4 * sizeof(uint32));
				AddEnqueueCopyPass(GraphBuilder, &S->FrameCountsRB, FrameCountsBuf, 4 * sizeof(uint32));
				AddEnqueueCopyPass(GraphBuilder, &S->BatchStatsRB, BatchStatsBuf, 8 * sizeof(uint32));
			}
			GraphBuilder.Execute();
		}
		if (bSync)
		{
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			FetchStats(S, NumStatGroups, true);
			S->bStatsInFlight = false;
		}
		else if (bCopyStats) { S->bStatsInFlight = true; S->InFlightGroups = NumStatGroups; }
		S->Cur = Old;
		EndGpuFrame(RHICmdList);
	});
	if (bSync) { FlushRenderingCommands(); }

	// 결과 — 동기면 이번 프레임, 비동기면 준비된 지난 프레임 것 (없으면 0 · NumB 그대로)
	FState::FRaw R;
	{
		FScopeLock L(&S->StatsLock);
		if (!S->Latest.bValid) { Out.GpuMs = (FPlatformTime::Seconds() - T0) * 1000.0; return; }
		R = MoveTemp(S->Latest);
		S->Latest = FState::FRaw();
	}
	const uint32* Counters = R.Counters; const uint32* FrameCounts = R.FrameCounts; const uint32* BatchStats = R.BatchStats;
	const uint32* Hand = R.Hand; const uint32* BondTot = R.BondTot; const float* StageMs = R.StageMs;

	for (const FVector4f& G : R.Partial)
	{
		Out.SumSpeed  += G.X;
		Out.MaxSpeed   = FMath::Max(Out.MaxSpeed, G.Y);
		Out.FreeCount += (int32)*reinterpret_cast<const uint32*>(&G.Z);
		Out.SumLambda += G.W;
	}
	Out.BondsBroken  = Counters[1];
	Out.PairOverflow = Counters[2];
	Out.NumPairs     = FrameCounts[0];
	Out.NumInternal  = FrameCounts[1];
	Out.NumBoundary  = FrameCounts[2];
	Out.MaxBatch        = (int32)BatchStats[0];
	Out.SumBatch        = (int32)BatchStats[1];
	Out.NumBatchBlocks  = (int32)BatchStats[2];
	Out.BatchOverflow   = (int32)BatchStats[3];
	Out.NumAwakeBlocks  = (int32)BatchStats[4];
	Out.NumAsleep       = (int32)Counters[3];
	Out.HandCount = (int32)Hand[0]; Out.Dug = (int32)Hand[1]; Out.HandFull = (int32)Hand[2]; Out.Piled = (int32)Hand[3];
	Out.BondsFormed = (int32)BondTot[1]; Out.BondFormRejected = (int32)BondTot[2];
	if (State->bCompactedThisStep) { NumB = (int32)BondTot[0]; Out.BondsCompacted = (int32)BondTot[3]; State->bCompactedThisStep = false; }   // A2 압축 — 죽은 칸이 빠졌다
	else { NumB = FMath::Max(NumB, (int32)BondTot[0]); }   // 사건 결합이 덧붙인 칸 (죽은 칸 포함 — CPU 가 내려받아 다시 올릴 때 압축)
	for (int32 k = 0; k < 4; ++k) { Out.StageMs[k] = StageMs[k]; }
	LastNumPairs = Out.NumPairs;
	Out.GpuMs = (FPlatformTime::Seconds() - T0) * 1000.0;
}

namespace
{
	bool StatsReady(FSandGpuSim::FState* S)
	{
		return S->StatsRB.IsReady() && S->CountersRB.IsReady() && S->FrameCountsRB.IsReady() && S->BatchStatsRB.IsReady() && S->HandRB.IsReady() && S->BondTotalRB.IsReady();
	}

	void FetchStats(FSandGpuSim::FState* S, int32 Groups, bool bWait)
	{
		FSandGpuSim::FState::FRaw R;
		R.Partial.SetNumUninitialized(Groups);
		FetchReadback(S->StatsRB, R.Partial.GetData(), Groups * sizeof(FVector4f));
		FetchReadback(S->CountersRB, R.Counters, 4 * sizeof(uint32));
		FetchReadback(S->FrameCountsRB, R.FrameCounts, 4 * sizeof(uint32));
		FetchReadback(S->BatchStatsRB, R.BatchStats, 8 * sizeof(uint32));
		FetchReadback(S->HandRB, R.Hand, 4 * sizeof(uint32));
		FetchReadback(S->BondTotalRB, R.BondTot, 4 * sizeof(uint32));
		if (S->StageTs[0].IsValid())
		{
			uint64 Us[5] = {};
			bool bOk = true;
			for (int32 k = 0; k < 5; ++k) { bOk &= RHIGetRenderQueryResult(S->StageTs[k].GetReference(), Us[k], bWait); }
			if (bOk) { for (int32 k = 0; k < 4; ++k) { R.StageMs[k] = (float)(Us[k + 1] - Us[k]) * 0.001f; } }
		}
		R.bValid = true;
		FScopeLock L(&S->StatsLock);
		S->Latest = MoveTemp(R);
	}
}

void FSandGpuSim::RequestPositionsAsync() const
{
	if (N == 0) { return; }
	FState* S = State;
	const int32 LN = N;
	ENQUEUE_RENDER_COMMAND(SandGpuAsyncPos)([S, LN](FRHICommandListImmediate& RHICmdList)
	{
		if (S->bAsyncInFlight)
		{
			if (!S->AsyncPosRB.IsReady()) { return; }   // 아직 — 다음 요청 때 다시 본다
			TArray<FVector4f> Tmp; Tmp.SetNumUninitialized(S->AsyncN);
			FetchReadback(S->AsyncPosRB, Tmp.GetData(), S->AsyncN * sizeof(FVector4f));
			{
				FScopeLock L(&S->AsyncLock);
				S->AsyncPos = MoveTemp(Tmp);
				S->bAsyncReady = true;
			}
			S->bAsyncInFlight = false;
		}
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGpuAsyncPos"));
		AddEnqueueCopyPass(GraphBuilder, &S->AsyncPosRB, GraphBuilder.RegisterExternalBuffer(S->Pos), LN * sizeof(FVector4f));
		GraphBuilder.Execute();
		S->AsyncN = LN;
		S->bAsyncInFlight = true;
	});
}

bool FSandGpuSim::TakePositionsAsync(TArray<FVector4f>& Out) const
{
	FScopeLock L(&State->AsyncLock);
	if (!State->bAsyncReady) { return false; }
	Out = MoveTemp(State->AsyncPos);
	State->bAsyncReady = false;
	return true;
}

void FSandGpuSim::Download(FSandGpuDownload& Out) const
{
	check(IsInGameThread());
	Out.Pos.SetNumUninitialized(N);
	Out.Vel.SetNumUninitialized(N);
	Out.PairIJ.SetNumUninitialized(2 * LastNumPairs);
	Out.PairLambda.SetNumUninitialized(LastNumPairs);
	Out.BondData.SetNumUninitialized(NumB);
	Out.BondIJ.SetNumUninitialized(2 * NumB);
	Out.GroundLambda.SetNumUninitialized(N);
	Out.WallLambda.SetNumUninitialized(N);
	Out.Sleep.SetNumZeroed(N);
	Out.Wet.SetNumZeroed(N);
	if (N == 0)
	{
		return;
	}
	FState* S = State;
	const int32 LN = N, LB = NumB, LP = LastNumPairs;
	ENQUEUE_RENDER_COMMAND(SandGpuSimDownload)([S, &Out, LN, LB, LP](FRHICommandListImmediate& RHICmdList)
	{
		const int32 Last = 1 - S->Cur;   // Step 끝에서 Cur 를 넘겼으므로 마지막으로 쓴 벌은 1 − Cur
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGpuSimDownload"));
			AddEnqueueCopyPass(GraphBuilder, &S->DlPos, GraphBuilder.RegisterExternalBuffer(S->Pos), LN * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &S->DlVel, GraphBuilder.RegisterExternalBuffer(S->Vel), LN * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &S->DlGround, GraphBuilder.RegisterExternalBuffer(S->GroundLambda), LN * sizeof(float));
			AddEnqueueCopyPass(GraphBuilder, &S->DlWall, GraphBuilder.RegisterExternalBuffer(S->WallLambda), LN * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &S->DlSleep, GraphBuilder.RegisterExternalBuffer(S->Sleep), LN * sizeof(uint32));
			AddEnqueueCopyPass(GraphBuilder, &S->DlWet, GraphBuilder.RegisterExternalBuffer(S->Wet), LN * sizeof(float));
			if (LP > 0)
			{
				AddEnqueueCopyPass(GraphBuilder, &S->DlPairIJ, GraphBuilder.RegisterExternalBuffer(S->SortedIJ[Last]), 2 * LP * sizeof(uint32));
				AddEnqueueCopyPass(GraphBuilder, &S->DlLambda, GraphBuilder.RegisterExternalBuffer(S->Lambda[Last]), LP * sizeof(FVector4f));
			}
			if (LB > 0)
			{
				AddEnqueueCopyPass(GraphBuilder, &S->DlBond, GraphBuilder.RegisterExternalBuffer(S->BondData), LB * sizeof(FVector4f));
				AddEnqueueCopyPass(GraphBuilder, &S->DlBondIJ, GraphBuilder.RegisterExternalBuffer(S->BondIJ), 2 * LB * sizeof(uint32));
			}
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		FetchReadback(S->DlPos, Out.Pos.GetData(), LN * sizeof(FVector4f));
		FetchReadback(S->DlVel, Out.Vel.GetData(), LN * sizeof(FVector4f));
		FetchReadback(S->DlGround, Out.GroundLambda.GetData(), LN * sizeof(float));
		FetchReadback(S->DlWall, Out.WallLambda.GetData(), LN * sizeof(FVector4f));
		FetchReadback(S->DlSleep, Out.Sleep.GetData(), LN * sizeof(uint32));
		FetchReadback(S->DlWet, Out.Wet.GetData(), LN * sizeof(float));
		FetchReadback(S->DlPairIJ, Out.PairIJ.GetData(), 2 * LP * sizeof(uint32));
		FetchReadback(S->DlLambda, Out.PairLambda.GetData(), LP * sizeof(FVector4f));
		FetchReadback(S->DlBond, Out.BondData.GetData(), LB * sizeof(FVector4f));
		FetchReadback(S->DlBondIJ, Out.BondIJ.GetData(), 2 * LB * sizeof(uint32));
		EndGpuFrame(RHICmdList);
	});
	FlushRenderingCommands();
}

TRefCountPtr<FRDGPooledBuffer> FSandGpuSim::GetPositionBufferRT() const
{
	check(IsInRenderingThread());
	return State ? State->Pos : nullptr;
}

TRefCountPtr<FRDGPooledBuffer> FSandGpuSim::GetWetBufferRT() const
{
	check(IsInRenderingThread());
	return State ? State->Wet : nullptr;
}

TRefCountPtr<FRDGPooledBuffer> FSandGpuSim::GetVelocityBufferRT() const
{
	check(IsInRenderingThread());
	return State ? State->Vel : nullptr;
}
