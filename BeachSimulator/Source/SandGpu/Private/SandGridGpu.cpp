#include "SandGridGpu.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"

BEGIN_SHADER_PARAMETER_STRUCT(FSandGridParams, )
	SHADER_PARAMETER(uint32, NX)
	SHADER_PARAMETER(uint32, NY)
	SHADER_PARAMETER(float, Dx)
	SHADER_PARAMETER(float, Dy)
	SHADER_PARAMETER(float, Gravity)
	SHADER_PARAMETER(float, Theta)
	SHADER_PARAMETER(float, Cfl)
	SHADER_PARAMETER(float, HDry)
	SHADER_PARAMETER(float, EpsDesing)
	SHADER_PARAMETER(uint32, bWetDry)
	SHADER_PARAMETER(uint32, Stage)
	SHADER_PARAMETER(uint32, bFixedDt)
	SHADER_PARAMETER(float, FixedDt)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, BCorner)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, URO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, U0RO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, UOutRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, LevelRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, LevelRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, FaceXRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, FaceYRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, FaceXRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, FaceYRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, DrainRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, DrainRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, AMaxRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, AMaxRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, TimeRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, TimeRO)
END_SHADER_PARAMETER_STRUCT()

#define GRID_SHADER(ClassName, EntryPoint) \
	class ClassName : public FGlobalShader \
	{ \
	public: \
		using FParameters = FSandGridParams; \
		DECLARE_GLOBAL_SHADER(ClassName); \
		SHADER_USE_PARAMETER_STRUCT(ClassName, FGlobalShader); \
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) \
		{ \
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); \
		} \
	}; \
	IMPLEMENT_GLOBAL_SHADER(ClassName, "/Project/Private/SandGrid.usf", EntryPoint, SF_Compute);

GRID_SHADER(FSandGridLevelCS,  "LevelCS")
GRID_SHADER(FSandGridFaceXCS,  "FaceXCS")
GRID_SHADER(FSandGridFaceYCS,  "FaceYCS")
GRID_SHADER(FSandGridDtCS,     "DtCS")
GRID_SHADER(FSandGridDrainCS,  "DrainCS")
GRID_SHADER(FSandGridUpdateCS, "UpdateCS")

struct FSandGridGpu::FState
{
	TRefCountPtr<FRDGPooledBuffer> BCorner, U0, U1, Level, FX, FY, Drain, AMax, Time;
	FRHIGPUBufferReadback DlU{ TEXT("SandGrid.DlU") }, DlTime{ TEXT("SandGrid.DlTime") };
	FRenderQueryRHIRef Ts[2];
};

namespace
{
	template <typename TShader>
	void AddGridPass(FRDGBuilder& GraphBuilder, FRDGEventName&& Name, const FGlobalShaderMap* ShaderMap, const FSandGridParams& Template, FIntVector Groups)
	{
		FSandGridParams* P = GraphBuilder.AllocParameters<FSandGridParams>();
		*P = Template;
		TShaderMapRef<TShader> Shader(ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, MoveTemp(Name), Shader, P, Groups);
	}

	TRefCountPtr<FRDGPooledBuffer> MakeBuffer(FRDGBuilder& GraphBuilder, uint32 Stride, int32 Num, const TCHAR* Name, const void* Data = nullptr)
	{
		FRDGBufferRef B = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(Stride, FMath::Max(1, Num)), Name);
		if (Data) { GraphBuilder.QueueBufferUpload(B, Data, Stride * Num, ERDGInitialDataFlags::None); }
		else { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(B), 0u); }
		return GraphBuilder.ConvertToExternalBuffer(B);
	}
}

FSandGridGpu::FSandGridGpu() : State(new FState) {}

FSandGridGpu::~FSandGridGpu()
{
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridFree)([S](FRHICommandListImmediate&) { delete S; });
	FlushRenderingCommands();
}

void FSandGridGpu::Init(const FSandGridGpuInit& In)
{
	Params = In;
	if (Params.EpsDesing <= 0.f) { Params.EpsDesing = FMath::Pow(FMath::Max(In.Dx, In.Dy), 4.f); }
	const int32 N = In.NX * In.NY;
	TArray<FVector4f> U; U.SetNum(N);
	for (int32 k = 0; k < N; ++k) { U[k] = FVector4f(In.W[k], In.HU[k], In.HV[k], 0.f); }
	TArray<float> BC = In.BCorner;
	FState* S = State;
	const int32 NX = In.NX, NY = In.NY;
	ENQUEUE_RENDER_COMMAND(SandGridInit)([S, U = MoveTemp(U), BC = MoveTemp(BC), N, NX, NY](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridInit"));
		S->BCorner = MakeBuffer(GraphBuilder, sizeof(float), BC.Num(), TEXT("SandGrid.BCorner"), BC.GetData());
		S->U0 = MakeBuffer(GraphBuilder, sizeof(FVector4f), N, TEXT("SandGrid.U0"), U.GetData());
		S->U1 = MakeBuffer(GraphBuilder, sizeof(FVector4f), N, TEXT("SandGrid.U1"));
		S->Level = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.Level"));
		S->FX = MakeBuffer(GraphBuilder, sizeof(FVector4f), (NX + 1) * NY, TEXT("SandGrid.FaceX"));
		S->FY = MakeBuffer(GraphBuilder, sizeof(FVector4f), NX * (NY + 1), TEXT("SandGrid.FaceY"));
		S->Drain = MakeBuffer(GraphBuilder, sizeof(float), N, TEXT("SandGrid.Drain"));
		S->AMax = MakeBuffer(GraphBuilder, sizeof(uint32), 1, TEXT("SandGrid.AMax"));
		S->Time = MakeBuffer(GraphBuilder, sizeof(float), 4, TEXT("SandGrid.Time"));
		GraphBuilder.Execute();
		if (!S->Ts[0].IsValid()) { for (FRenderQueryRHIRef& Q : S->Ts) { Q = RHICreateRenderQuery(RQT_AbsoluteTime); } }
	});
	FlushRenderingCommands();
	Params.BCorner.Empty(); Params.W.Empty(); Params.HU.Empty(); Params.HV.Empty();
}

void FSandGridGpu::StepFixed(float Dt, int32 NumSteps) { Run(true, Dt, NumSteps); }
void FSandGridGpu::Advance(float FrameTime, int32 MaxSub) { Run(false, FrameTime, MaxSub); }

void FSandGridGpu::Run(bool bFixed, float DtOrFrame, int32 NumSub)
{
	FState* S = State;
	const FSandGridGpuInit P = Params;
	float Ms = 0.f;
	ENQUEUE_RENDER_COMMAND(SandGridRun)([S, P, bFixed, DtOrFrame, NumSub, &Ms](FRHICommandListImmediate& RHICmdList)
	{
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridRun"));
			FRDGBufferRef BC = GraphBuilder.RegisterExternalBuffer(S->BCorner);
			FRDGBufferRef U0 = GraphBuilder.RegisterExternalBuffer(S->U0), U1 = GraphBuilder.RegisterExternalBuffer(S->U1);
			FRDGBufferRef Lv = GraphBuilder.RegisterExternalBuffer(S->Level);
			FRDGBufferRef FX = GraphBuilder.RegisterExternalBuffer(S->FX), FY = GraphBuilder.RegisterExternalBuffer(S->FY);
			FRDGBufferRef Dr = GraphBuilder.RegisterExternalBuffer(S->Drain);
			FRDGBufferRef AM = GraphBuilder.RegisterExternalBuffer(S->AMax);
			FRDGBufferRef Tm = GraphBuilder.RegisterExternalBuffer(S->Time);
			{
				// 남은 시간 = 이번 프레임 (적응) — [0][1] 만 바꾸고 [2] 누적은 둔다
				const float Init2[2] = { 0.f, bFixed ? 0.f : DtOrFrame };
				FRDGBufferRef T2 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 2), TEXT("SandGrid.TimeInit"));
				GraphBuilder.QueueBufferUpload(T2, Init2, sizeof(Init2), ERDGInitialDataFlags::None);
				AddCopyBufferPass(GraphBuilder, Tm, 0, T2, 0, sizeof(Init2));
			}
			GraphBuilder.AddPass(RDG_EVENT_NAME("SandGridTs0"), ERDGPassFlags::NeverCull, [Q = S->Ts[0].GetReference()](FRHICommandListImmediate& Cmd) { Cmd.EndRenderQuery(Q); });

			FSandGridParams B;
			B.NX = P.NX; B.NY = P.NY; B.Dx = P.Dx; B.Dy = P.Dy; B.Gravity = P.Gravity; B.Theta = P.Theta; B.Cfl = P.Cfl; B.HDry = P.HDry;
			B.EpsDesing = P.EpsDesing; B.bWetDry = P.bWetDry ? 1u : 0u; B.bFixedDt = bFixed ? 1u : 0u; B.FixedDt = bFixed ? DtOrFrame : 0.f;
			B.BCorner = GraphBuilder.CreateSRV(BC);
			const FIntVector GC(FMath::DivideAndRoundUp(P.NX, 8), FMath::DivideAndRoundUp(P.NY, 8), 1);
			const FIntVector GX(FMath::DivideAndRoundUp(P.NX + 1, 8), FMath::DivideAndRoundUp(P.NY, 8), 1);
			const FIntVector GY(FMath::DivideAndRoundUp(P.NX, 8), FMath::DivideAndRoundUp(P.NY + 1, 8), 1);
			for (int32 n = 0; n < NumSub; ++n)
			{
				for (uint32 Stage = 1; Stage <= 2; ++Stage)
				{
					FRDGBufferRef In = Stage == 1 ? U0 : U1;
					FRDGBufferRef Out = Stage == 1 ? U1 : U0;
					FSandGridParams L = B; L.Stage = Stage;
					L.URO = GraphBuilder.CreateSRV(In);
					L.LevelRW = GraphBuilder.CreateUAV(Lv);
					AddGridPass<FSandGridLevelCS>(GraphBuilder, RDG_EVENT_NAME("SandGridLevel"), ShaderMap, L, GC);
					if (Stage == 1) { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AM), 0u); }
					FSandGridParams F = B; F.Stage = Stage;
					F.URO = GraphBuilder.CreateSRV(In);
					F.LevelRO = GraphBuilder.CreateSRV(Lv);
					F.AMaxRW = GraphBuilder.CreateUAV(AM);
					F.FaceXRW = GraphBuilder.CreateUAV(FX);
					AddGridPass<FSandGridFaceXCS>(GraphBuilder, RDG_EVENT_NAME("SandGridFaceX"), ShaderMap, F, GX);
					FSandGridParams FYp = F; FYp.FaceXRW = nullptr; FYp.FaceYRW = GraphBuilder.CreateUAV(FY);
					AddGridPass<FSandGridFaceYCS>(GraphBuilder, RDG_EVENT_NAME("SandGridFaceY"), ShaderMap, FYp, GY);
					if (Stage == 1)
					{
						FSandGridParams T = B; T.AMaxRO = GraphBuilder.CreateSRV(AM); T.TimeRW = GraphBuilder.CreateUAV(Tm);
						AddGridPass<FSandGridDtCS>(GraphBuilder, RDG_EVENT_NAME("SandGridDt"), ShaderMap, T, FIntVector(1, 1, 1));
					}
					FSandGridParams D = B;
					D.URO = GraphBuilder.CreateSRV(In);
					D.FaceXRO = GraphBuilder.CreateSRV(FX); D.FaceYRO = GraphBuilder.CreateSRV(FY);
					D.TimeRO = GraphBuilder.CreateSRV(Tm);
					D.DrainRW = GraphBuilder.CreateUAV(Dr);
					AddGridPass<FSandGridDrainCS>(GraphBuilder, RDG_EVENT_NAME("SandGridDrain"), ShaderMap, D, GC);
					FSandGridParams Up = B; Up.Stage = Stage;
					Up.URO = GraphBuilder.CreateSRV(In);
					Up.U0RO = GraphBuilder.CreateSRV(U0);   // 2 단계만 읽는다 (1 단계는 URO 와 같은 U0 — 출력 U1 과 겹치지 않게)
					Up.LevelRO = GraphBuilder.CreateSRV(Lv);
					Up.FaceXRO = GraphBuilder.CreateSRV(FX); Up.FaceYRO = GraphBuilder.CreateSRV(FY);
					Up.DrainRO = GraphBuilder.CreateSRV(Dr);
					Up.TimeRO = GraphBuilder.CreateSRV(Tm);
					if (Stage == 2)
					{
						// 2 단계: U0 = ½(U0 + U1 + Δt·L(U1)) — U0 를 읽으며 쓴다 → 임시에 쓰고 복사
						FRDGBufferRef Tmp = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), P.NX * P.NY), TEXT("SandGrid.UTmp"));
						Up.UOutRW = GraphBuilder.CreateUAV(Tmp);
						AddGridPass<FSandGridUpdateCS>(GraphBuilder, RDG_EVENT_NAME("SandGridUpdate2"), ShaderMap, Up, GC);
						AddCopyBufferPass(GraphBuilder, U0, Tmp);
					}
					else
					{
						Up.UOutRW = GraphBuilder.CreateUAV(Out);
						AddGridPass<FSandGridUpdateCS>(GraphBuilder, RDG_EVENT_NAME("SandGridUpdate1"), ShaderMap, Up, GC);
					}
				}
			}
			GraphBuilder.AddPass(RDG_EVENT_NAME("SandGridTs1"), ERDGPassFlags::NeverCull, [Q = S->Ts[1].GetReference()](FRHICommandListImmediate& Cmd) { Cmd.EndRenderQuery(Q); });
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		uint64 A = 0, Bt = 0;
		if (RHIGetRenderQueryResult(S->Ts[0].GetReference(), A, true) && RHIGetRenderQueryResult(S->Ts[1].GetReference(), Bt, true)) { Ms = (float)(Bt - A) * 0.001f; }
		FRDGBuilder::TickPoolElements();
		RHICmdList.EndFrame();
	});
	FlushRenderingCommands();
	GpuMs = Ms;
}

void FSandGridGpu::Download(TArray<float>& OutW, TArray<float>& OutHU, TArray<float>& OutHV, float* OutSimTime) const
{
	const int32 N = Params.NX * Params.NY;
	TArray<FVector4f> U; U.SetNum(N);
	float T[4] = {};
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridDownload)([S, N, &U, &T](FRHICommandListImmediate& RHICmdList)
	{
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridDownload"));
			AddEnqueueCopyPass(GraphBuilder, &S->DlU, GraphBuilder.RegisterExternalBuffer(S->U0), N * sizeof(FVector4f));
			AddEnqueueCopyPass(GraphBuilder, &S->DlTime, GraphBuilder.RegisterExternalBuffer(S->Time), 4 * sizeof(float));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (const void* Src = S->DlU.Lock(N * sizeof(FVector4f))) { FMemory::Memcpy(U.GetData(), Src, N * sizeof(FVector4f)); }
		S->DlU.Unlock();
		if (const void* Src = S->DlTime.Lock(4 * sizeof(float))) { FMemory::Memcpy(T, Src, 4 * sizeof(float)); }
		S->DlTime.Unlock();
	});
	FlushRenderingCommands();
	OutW.SetNum(N); OutHU.SetNum(N); OutHV.SetNum(N);
	for (int32 k = 0; k < N; ++k) { OutW[k] = U[k].X; OutHU[k] = U[k].Y; OutHV[k] = U[k].Z; }
	if (OutSimTime) { *OutSimTime = T[2]; }
}
