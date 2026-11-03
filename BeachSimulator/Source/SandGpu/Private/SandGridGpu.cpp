#include "SandGridGpu.h"
#include <atomic>

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
	SHADER_PARAMETER(float, Manning)
	SHADER_PARAMETER(float, ManningSwash)
	SHADER_PARAMETER(float, FricX)
	SHADER_PARAMETER(float, Ks)
	SHADER_PARAMETER(uint32, bOpenEast)
	SHADER_PARAMETER(float, SeaLevel)
	SHADER_PARAMETER(float, BoundaryDepth)
	SHADER_PARAMETER(uint32, NumWaves)
	SHADER_PARAMETER(uint32, bPeriodicY)
	SHADER_PARAMETER(float, IgGain)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Waves)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, BCorner)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, URO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, U0RO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, UOutRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, LevelRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, LevelRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, FaceXRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, IncRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, IncRO)
	SHADER_PARAMETER(uint32, bIncCached)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, FaceYRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, FaceXRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, FaceYRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, DrainRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, DrainRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, AMaxRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, AMaxRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, TimeRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, TimeRO)
	SHADER_PARAMETER(uint32, bDispersive)
	SHADER_PARAMETER(float, BreakRatio)
	SHADER_PARAMETER(float, BreakStop)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, BrkRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, BrkRO)
	SHADER_PARAMETER(uint32, BreakMode)
	SHADER_PARAMETER(float, EddyDelta)
	SHADER_PARAMETER(float, BreakSlope)
	SHADER_PARAMETER(float, BreakTbrk)
	SHADER_PARAMETER(uint32, BreakRoller)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, BrkDiagRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, NuRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, NuRO)
	SHADER_PARAMETER(uint32, DispMode)
	SHADER_PARAMETER(uint32, LineAxis)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, DstRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, DstRO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, UVRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, UVRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, FsPrevRW)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, FsTRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, FsTRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, DispRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, DispRO)
	SHADER_PARAMETER(uint32, NumIg)
	SHADER_PARAMETER(uint32, bNest)
	SHADER_PARAMETER(uint32, NestNY)
	SHADER_PARAMETER(float, NestDy)
	SHADER_PARAMETER(float, NestX)
	SHADER_PARAMETER(uint32, DispTaper)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, NestARO)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, NestBRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, NestOutRW)
	SHADER_PARAMETER(uint32, bGround)
	SHADER_PARAMETER(float, GwK)
	SHADER_PARAMETER(float, GwPsi)
	SHADER_PARAMETER(float, GwNe)
	SHADER_PARAMETER(float, GwZaq)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float2>, GwRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float2>, GwRW)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, GwQRO)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, GwQRW)
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

// 줄 풀이 (SolveCS · EddyCS) 의 64 스레드 변형 — 줄 길이 ≤ 64 (바깥 격자 y 줄 51 칸) 에 쓴다
#define GRID_SHADER64(ClassName, EntryPoint) 	class ClassName : public FGlobalShader 	{ 	public: 		using FParameters = FSandGridParams; 		DECLARE_GLOBAL_SHADER(ClassName); 		SHADER_USE_PARAMETER_STRUCT(ClassName, FGlobalShader); 		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 		{ 			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); 		} 		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Env) 		{ 			FGlobalShader::ModifyCompilationEnvironment(Parameters, Env); 			Env.SetDefine(TEXT("LINE_MAX"), 64); 		} 	}; 	IMPLEMENT_GLOBAL_SHADER(ClassName, "/Project/Private/SandGrid.usf", EntryPoint, SF_Compute);

GRID_SHADER(FSandGridLevelCS,  "LevelCS")
GRID_SHADER(FSandGridFaceXCS,  "FaceXCS")
GRID_SHADER(FSandGridIncCS,    "IncCS")
GRID_SHADER(FSandGridFaceYCS,  "FaceYCS")
GRID_SHADER(FSandGridDtCS,     "DtCS")
GRID_SHADER(FSandGridDrainCS,  "DrainCS")
GRID_SHADER(FSandGridUpdateCS, "UpdateCS")
GRID_SHADER(FSandGridBreakCS,  "BreakCS")
GRID_SHADER(FSandGridEddyCS,   "EddyCS")
GRID_SHADER(FSandGridRollerCS, "RollerCS")
GRID_SHADER(FSandGridToUCS,    "ToUCS")
GRID_SHADER(FSandGridDispCS,   "DispCS")
GRID_SHADER(FSandGridSolveCS,  "SolveCS")
GRID_SHADER64(FSandGridSolve64CS, "SolveCS")
GRID_SHADER64(FSandGridEddy64CS, "EddyCS")
GRID_SHADER(FSandGridFixCS,    "FixCS")
GRID_SHADER(FSandGridNestCS,   "NestCS")
GRID_SHADER(FSandGridGwFluxCS, "GwFluxCS")
GRID_SHADER(FSandGridGwApplyCS, "GwApplyCS")

struct FSandGridGpu::FState
{
	TRefCountPtr<FRDGPooledBuffer> BCorner, U0, U1, Level, FX, FY, Drain, AMax, Time, Waves;
	TRefCountPtr<FRDGPooledBuffer> Dst, UV0, UV1, FsPrev, FsT, Disp;   // T3c 분산
	TRefCountPtr<FRDGPooledBuffer> Nu, BrkDiag;                                // Kennedy 와점성 (D-M3-19)
	TRefCountPtr<FRDGPooledBuffer> Brk, BrkB;                         // 쇄파 상태 (지속, D-M3-17/18) — 핑퐁
	TRefCountPtr<FRDGPooledBuffer> NestA, NestB, Inc;   // Inc: 바다 쪽 입사파 캐시 (줄마다, m3-plan p2)                      // T3c c4 nesting 스냅샷 (앞 · 뒤)
	TRefCountPtr<FRDGPooledBuffer> Gw, GwQ;                           // T3c c6 지하수 (η_gw, δ) · Darcy 순유입
	float GwZaq = 0.f;
	FRHIGPUBufferReadback DlGw{ TEXT("SandGrid.DlGw") };
	FRHIGPUBufferReadback DlNu{ TEXT("SandGrid.DlNu") };
	FRHIGPUBufferReadback DlDiag{ TEXT("SandGrid.DlDiag") };
	uint32 NumIg = 0;
	bool bNestPrimed = false;
	FRHIGPUBufferReadback DlU{ TEXT("SandGrid.DlU") }, DlTime{ TEXT("SandGrid.DlTime") };
	FRenderQueryRHIRef Ts[2];
	// 비동기 readback 링 (m3-plan p1): 매 Run 끝에 시간 버퍼 (16 B) 복사 · 타임스탬프 — 다음 Run 시작에 준비된 것만 읽는다 (기다리지 않음)
	static constexpr int32 NumRb = 3;
	TUniquePtr<FRHIGPUBufferReadback> Rb[NumRb];
	FRenderQueryRHIRef RbTs[NumRb][2];
	int32 RbHead = 0, RbCount = 0;   // 가장 오래된 진행 중 칸 · 진행 중 개수 (렌더 스레드만)
	std::atomic<float> AMaxSeen{ 0.f }, GpuMsSeen{ 0.f }, SimTimeSeen{ 0.f };   // 게임 스레드가 읽음
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
	if (Params.bDispersive && (In.NX > 1024 || In.NY > 1024)) { UE_LOG(LogTemp, Warning, TEXT("SandGrid: 분산은 줄 길이 1024 까지 (SolveCS 그룹 하나) — 끔")); Params.bDispersive = false; }
	const int32 N = In.NX * In.NY;
	TArray<FVector4f> U; U.SetNum(N);
	for (int32 k = 0; k < N; ++k) { U[k] = FVector4f(In.W[k], In.HU[k], In.HV[k], 0.f); }
	TArray<float> BC = In.BCorner;
	// 입사 성분 표 — 성분마다 (a, ω, φ, k_y) · (u_n/η, v/η) (m3-plan c4). 위상속도: 분산이면 ω/k (Vasarmidis 2021), 아니면 √(gh₀)
	TArray<FVector4f> Wv;
	uint32 NumIg = 0;
	{
		const double G = In.Gravity, H0 = FMath::Max(1e-3, (double)In.BoundaryDepth);
		auto Kof = [&](double Om)
		{
			double K = Om / FMath::Sqrt(G * H0);
			if (Params.bDispersive) { for (int32 It = 0; It < 60; ++It) { K = Om * Om / (G * FMath::Tanh(K * H0)); } }
			return K;
		};
		struct FC { double A, Om, Ph, Kx, Ky, K, N; };
		TArray<FC> C;
		for (const FVector4f& W : In.Waves)
		{
			const double K = Kof(W.Y), S = FMath::Clamp(W.W / K, -1.0, 1.0), Cs = FMath::Sqrt(1.0 - S * S), Cp = W.Y / K;
			Wv.Add(W); Wv.Add(FVector4f((float)(Cp / H0 * Cs), (float)(Cp / H0 * S), 0.f, 0.f));
			C.Add({ W.X, W.Y, W.Z, -K * Cs, K * S, K, 0.5 * (1.0 + 2.0 * K * H0 / FMath::Sinh(2.0 * K * H0)) });
		}
		if (In.bBoundIg)
		{
			// 묶인 장주기파 쌍: η₃ = D·a₁a₂·cos(Δφ + Δk_y·y − Ωt), D = −g[n(K·k̂₁)(K·k̂₂) + (n − ½)K²]/(ghK² − Ω²) (LHS 1962 방향 일반화), u⃗₃ = η₃ΩK⃗/(hK²)
			for (int32 a = 0; a < C.Num(); ++a)
			for (int32 b = 0; b < C.Num(); ++b)
			{
				const FC& P1 = C[a]; const FC& P2 = C[b];
				if (P1.Om <= P2.Om) { continue; }
				const double Kx = P1.Kx - P2.Kx, Ky = P1.Ky - P2.Ky, K2 = Kx * Kx + Ky * Ky, Om = P1.Om - P2.Om;
				const double Den = G * H0 * K2 - Om * Om;
				if (K2 <= 0.0 || Den < 0.05 * G * H0 * K2) { continue; }   // 공명 (Ω/K → √(gh)) 근처는 뺀다
				const double Nm = 0.5 * (P1.N + P2.N);
				const double D = -G * (Nm * (Kx * P1.Kx + Ky * P1.Ky) / P1.K * (Kx * P2.Kx + Ky * P2.Ky) / P2.K + (Nm - 0.5) * K2) / Den;
				Wv.Add(FVector4f((float)(D * P1.A * P2.A), (float)Om, (float)FMath::Fmod(P1.Ph - P2.Ph + 4.0 * UE_PI, 2.0 * UE_PI), (float)Ky));
				Wv.Add(FVector4f((float)(Om * -Kx / (H0 * K2)), (float)(Om * Ky / (H0 * K2)), 0.f, 0.f));
				++NumIg;
			}
		}
	}
	if (Wv.Num() == 0) { Wv.Add(FVector4f(0, 0, 0, 0)); Wv.Add(FVector4f(0, 0, 0, 0)); }
	const int32 NestNY = In.NestOuter ? In.NestOuter->GetNY() : 1;
	FState* S = State;
	const int32 NX = In.NX, NY = In.NY;
	S->NumIg = NumIg; S->bNestPrimed = false;
	// c6 지하수: 처음 지하수면 = min(B, SeaLevel) (물 밑은 바닥에 닿아 연결), 대수층 바닥 = 최저 꼭짓점 − 1 m (m3-plan c6)
	TArray<FVector2f> Gw; Gw.SetNum(N);
	{
		float Bmin = BC.Num() ? BC[0] : 0.f;
		for (float V : BC) { Bmin = FMath::Min(Bmin, V); }
		S->GwZaq = Bmin - 1.f;
		for (int32 j = 0; j < NY; ++j)
		for (int32 i = 0; i < NX; ++i)
		{
			const float Bm = 0.25f * (BC[j * (NX + 1) + i] + BC[j * (NX + 1) + i + 1] + BC[(j + 1) * (NX + 1) + i] + BC[(j + 1) * (NX + 1) + i + 1]);
			Gw[j * NX + i] = FVector2f(In.GwInit.Num() == N ? In.GwInit[j * NX + i] : FMath::Min(Bm, In.SeaLevel), 0.f);
		}
	}
	ENQUEUE_RENDER_COMMAND(SandGridInit)([S, U = MoveTemp(U), BC = MoveTemp(BC), Wv = MoveTemp(Wv), Gw = MoveTemp(Gw), N, NX, NY, NestNY](FRHICommandListImmediate& RHICmdList)
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
		S->Time = MakeBuffer(GraphBuilder, sizeof(float), 5, TEXT("SandGrid.Time"));
		S->Dst = MakeBuffer(GraphBuilder, sizeof(float), N, TEXT("SandGrid.Dst"));
		{ TArray<FVector2f> Z; Z.SetNum(N); for (int32 k = 0; k < N; ++k) { Z[k] = FVector2f(0.f, U[k].X); } S->Brk = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.Brk"), Z.GetData()); S->BrkB = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.BrkB"), Z.GetData()); }
		{ TArray<FVector2f> Zd; Zd.Init(FVector2f::ZeroVector, N); S->BrkDiag = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.BrkDiag"), Zd.GetData()); }
		S->Nu = MakeBuffer(GraphBuilder, sizeof(float), N, TEXT("SandGrid.Nu"));
		S->UV0 = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.UV0"));
		S->UV1 = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.UV1"));
		S->FsPrev = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.FsPrev"));
		S->FsT = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.FsT"));
		S->Disp = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.Disp"));
		S->Waves = MakeBuffer(GraphBuilder, sizeof(FVector4f), Wv.Num(), TEXT("SandGrid.Waves"), Wv.GetData());
		S->NestA = MakeBuffer(GraphBuilder, sizeof(FVector4f), NestNY, TEXT("SandGrid.NestA"));
		S->NestB = MakeBuffer(GraphBuilder, sizeof(FVector4f), NestNY, TEXT("SandGrid.NestB"));
		S->Inc = MakeBuffer(GraphBuilder, sizeof(FVector4f), NY, TEXT("SandGrid.Inc"));
		S->Gw = MakeBuffer(GraphBuilder, sizeof(FVector2f), N, TEXT("SandGrid.Gw"), Gw.GetData());
		S->GwQ = MakeBuffer(GraphBuilder, sizeof(float), N, TEXT("SandGrid.GwQ"));
		GraphBuilder.Execute();
		if (!S->Ts[0].IsValid()) { for (FRenderQueryRHIRef& Q : S->Ts) { Q = RHICreateRenderQuery(RQT_AbsoluteTime); } }
		for (int32 r = 0; r < FState::NumRb; ++r)
		{
			if (!S->Rb[r].IsValid()) { S->Rb[r] = MakeUnique<FRHIGPUBufferReadback>(TEXT("SandGrid.RbTime")); }
			for (FRenderQueryRHIRef& Q : S->RbTs[r]) { if (!Q.IsValid()) { Q = RHICreateRenderQuery(RQT_AbsoluteTime); } }
		}
	});
	FlushRenderingCommands();
	Params.BCorner.Empty(); Params.W.Empty(); Params.HU.Empty(); Params.HV.Empty(); Params.GwInit.Empty();   // Waves 는 둔다 (NumWaves)
}

float FSandGridGpu::LastGpuMs() const { return State ? State->GpuMsSeen.load(std::memory_order_relaxed) : 0.f; }

void FSandGridGpu::StepFixed(float Dt, int32 NumSteps) { Run(true, Dt, NumSteps); }
void FSandGridGpu::Advance(float FrameTime, int32 MaxSub)
{
	// 서브스텝 수 = 지난 Advance 의 최대 파속으로 CFL 에 맞춰 (+25% · +1). 모자라면 시뮬 시간이 프레임보다 조금 늦을 뿐 (GPU 가 Δt 를 CFL 로 자른다)
	int32 K = MaxSub;
	const float AMax = State->AMaxSeen.load(std::memory_order_relaxed);   // 비동기 readback (1~3 프레임 전) 또는 동기 Download
	if (AMax > 0.f) { LastAMax = AMax; }
	if (LastAMax > 0.f) { K = FMath::Clamp(FMath::CeilToInt(FrameTime * LastAMax / Params.Cfl * 1.25f) + 1, 1, MaxSub); }
	LastSubsteps = K;
	Run(false, FrameTime, K);
}

void FSandGridGpu::Run(bool bFixed, float DtOrFrame, int32 NumSub)
{
	FState* S = State;
	const FSandGridGpuInit P = Params;
	FState* OS = P.NestOuter ? P.NestOuter->State : nullptr;
	const FSandGridGpuInit OP = P.NestOuter ? P.NestOuter->Params : FSandGridGpuInit();
	ENQUEUE_RENDER_COMMAND(SandGridRun)([S, P, OS, OP, bFixed, DtOrFrame, NumSub](FRHICommandListImmediate& RHICmdList)
	{
		const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		// 지난 Run 들의 readback 중 준비된 것 (오래된 순) 만 읽는다 — GPU 를 기다리지 않음 (m3-plan p1).
		// 다만 링이 차면 (GPU 가 NumRb 프레임 뒤처짐) 가장 오래된 것이 끝날 때까지 기다린다 — 프레임 속도 제한. 안 그러면 CPU 가
		// 수백 프레임 앞서 나가 임시 버퍼가 쌓여 GPU 메모리가 바닥난다 (p8, E_OUTOFMEMORY)
		if (S->RbCount == FState::NumRb) { RHICmdList.SubmitCommandsHint(); while (!S->Rb[S->RbHead]->IsReady()) { FPlatformProcess::SleepNoStats(0.f); } }
		while (S->RbCount > 0 && S->Rb[S->RbHead]->IsReady())
		{
			const int32 h = S->RbHead;
			float Tv[4] = {};
			if (const void* Src = S->Rb[h]->Lock(4 * sizeof(float))) { FMemory::Memcpy(Tv, Src, 4 * sizeof(float)); }
			S->Rb[h]->Unlock();
			S->AMaxSeen.store(Tv[3], std::memory_order_relaxed); S->SimTimeSeen.store(Tv[2], std::memory_order_relaxed);
			uint64 A = 0, Bt = 0;
			if (RHIGetRenderQueryResult(S->RbTs[h][0].GetReference(), A, false) && RHIGetRenderQueryResult(S->RbTs[h][1].GetReference(), Bt, false)) { S->GpuMsSeen.store((float)(Bt - A) * 0.001f, std::memory_order_relaxed); }
			S->RbHead = (h + 1) % FState::NumRb; --S->RbCount;
		}
		const int32 Wr = S->RbCount < FState::NumRb ? (S->RbHead + S->RbCount) % FState::NumRb : -1;   // 링이 차면 이번엔 readback 안 함
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
				const float Init2[4] = { 0.f, bFixed ? 0.f : DtOrFrame, -1.f, 0.f };   // [2] 은 아래 복사에서 빼고 [3] 최대 파속은 0 으로
				FRDGBufferRef T2 = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), 4), TEXT("SandGrid.TimeInit"));
				GraphBuilder.QueueBufferUpload(T2, Init2, sizeof(Init2), ERDGInitialDataFlags::None);
				AddCopyBufferPass(GraphBuilder, Tm, 0, T2, 0, 2 * sizeof(float));
				AddCopyBufferPass(GraphBuilder, Tm, 3 * sizeof(float), T2, 3 * sizeof(float), sizeof(float));
			}
			FRDGBufferRef NA = GraphBuilder.RegisterExternalBuffer(S->NestA), NB = GraphBuilder.RegisterExternalBuffer(S->NestB);
			if (OS)
			{
				// nesting — 바깥 격자는 이 프레임을 먼저 진행했다. 뒤 스냅샷 → 앞, 바깥 지금 상태 → 뒤 (처음엔 둘 다)
				if (S->bNestPrimed) { AddCopyBufferPass(GraphBuilder, NA, NB); }
				FSandGridParams X;
				X.NX = OP.NX; X.NY = OP.NY; X.Dx = OP.Dx; X.Dy = OP.Dy; X.HDry = OP.HDry; X.SeaLevel = OP.SeaLevel; X.NestX = P.NestX;
				X.BCorner = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(OS->BCorner));
				X.URO = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(OS->U0));
				X.TimeRO = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(OS->Time));
				X.NestOutRW = GraphBuilder.CreateUAV(NB);
				AddGridPass<FSandGridNestCS>(GraphBuilder, RDG_EVENT_NAME("SandGridNest"), ShaderMap, X, FIntVector(FMath::DivideAndRoundUp(OP.NY, 64), 1, 1));
				if (!S->bNestPrimed) { AddCopyBufferPass(GraphBuilder, NA, NB); S->bNestPrimed = true; }
			}
			GraphBuilder.AddPass(RDG_EVENT_NAME("SandGridTs0"), ERDGPassFlags::NeverCull, [Q = (Wr >= 0 ? S->RbTs[Wr][0] : S->Ts[0]).GetReference()](FRHICommandListImmediate& Cmd) { Cmd.EndRenderQuery(Q); });

			FSandGridParams B;
			B.NX = P.NX; B.NY = P.NY; B.Dx = P.Dx; B.Dy = P.Dy; B.Gravity = P.Gravity; B.Theta = P.Theta; B.Cfl = P.Cfl; B.HDry = P.HDry;
			B.EpsDesing = P.EpsDesing; B.bWetDry = P.bWetDry ? 1u : 0u; B.bFixedDt = bFixed ? 1u : 0u; B.FixedDt = bFixed ? DtOrFrame : 0.f; B.Manning = P.Manning; B.ManningSwash = P.ManningSwash; B.FricX = P.FricX; B.Ks = P.Ks;
			B.BCorner = GraphBuilder.CreateSRV(BC);
			B.bOpenEast = P.bOpenEast ? 1u : 0u; B.SeaLevel = P.SeaLevel; B.BoundaryDepth = P.BoundaryDepth; B.NumWaves = P.Waves.Num(); B.bPeriodicY = P.bPeriodicY ? 1u : 0u; B.IgGain = P.IgGain;
			B.Waves = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(S->Waves));
			B.IncRO = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(S->Inc)); B.bIncCached = 0u;   // 캐시는 FaceX 에서만 켬 (그 단계 시각으로 채운 직후)
			B.bDispersive = P.bDispersive ? 1u : 0u; B.BreakRatio = P.BreakRatio; B.BreakStop = P.BreakStop; B.BreakMode = P.BreakMode; B.EddyDelta = P.EddyDelta; B.BreakSlope = P.BreakSlope; B.BreakTbrk = P.BreakTbrk; B.BreakRoller = P.bBreakRoller ? 1u : 0u; B.DispTaper = (uint32)FMath::Max(0, P.DispTaper);
			B.NumIg = S->NumIg;
			B.bGround = P.bGround ? 1u : 0u; B.GwK = P.GwK; B.GwPsi = P.GwPsi; B.GwNe = P.GwNe; B.GwZaq = S->GwZaq;
			FRDGBufferRef GwB = GraphBuilder.RegisterExternalBuffer(S->Gw), GwQB = GraphBuilder.RegisterExternalBuffer(S->GwQ);
			B.bNest = OS ? 1u : 0u; B.NestNY = OS ? OP.NY : 1u; B.NestDy = OS ? OP.Dy : 1.f;
			B.NestARO = GraphBuilder.CreateSRV(NA); B.NestBRO = GraphBuilder.CreateSRV(NB);
			FRDGBufferRef Ds = GraphBuilder.RegisterExternalBuffer(S->Dst);
			FRDGBufferRef UV0 = GraphBuilder.RegisterExternalBuffer(S->UV0), UV1 = GraphBuilder.RegisterExternalBuffer(S->UV1);
			FRDGBufferRef FsP = GraphBuilder.RegisterExternalBuffer(S->FsPrev), FsT = GraphBuilder.RegisterExternalBuffer(S->FsT);
			FRDGBufferRef Dp = GraphBuilder.RegisterExternalBuffer(S->Disp);
			// 분산 패스 (CPU StepBoussinesq 순서). In = (w, P, Q) 상태
			auto AddDisp = [&](FRDGBufferRef In, uint32 Mode)
			{
				FSandGridParams X = B; X.DispMode = Mode;
				X.URO = GraphBuilder.CreateSRV(In); X.DstRO = GraphBuilder.CreateSRV(Ds); X.DispRW = GraphBuilder.CreateUAV(Dp);
				X.TimeRO = GraphBuilder.CreateSRV(Tm); X.FsPrevRW = GraphBuilder.CreateUAV(FsP); X.FsTRW = GraphBuilder.CreateUAV(FsT);
				AddGridPass<FSandGridDispCS>(GraphBuilder, RDG_EVENT_NAME("SandGridDisp"), ShaderMap, X, FIntVector(FMath::DivideAndRoundUp(P.NX, 8), FMath::DivideAndRoundUp(P.NY, 8), 1));
			};
			auto AddSolve = [&](FRDGBufferRef UV, FRDGBufferRef Target)   // UV → Target.yz (행 · 열 삼중대각)
			{
				for (uint32 Ax = 0; Ax < 2; ++Ax)
				{
					FSandGridParams X = B; X.LineAxis = Ax;
					X.DstRO = GraphBuilder.CreateSRV(Ds); X.UVRO = GraphBuilder.CreateSRV(UV); X.UOutRW = GraphBuilder.CreateUAV(Target);
					const FIntVector G(Ax == 0 ? P.NY : P.NX, 1, 1);
					if ((Ax == 0 ? P.NX : P.NY) <= 64) { AddGridPass<FSandGridSolve64CS>(GraphBuilder, RDG_EVENT_NAME("SandGridSolve"), ShaderMap, X, G); }
					else { AddGridPass<FSandGridSolveCS>(GraphBuilder, RDG_EVENT_NAME("SandGridSolve"), ShaderMap, X, G); }
				}
			};
			const FIntVector GC(FMath::DivideAndRoundUp(P.NX, 8), FMath::DivideAndRoundUp(P.NY, 8), 1);
			const FIntVector GX(FMath::DivideAndRoundUp(P.NX + 1, 8), FMath::DivideAndRoundUp(P.NY, 8), 1);
			const FIntVector GY(FMath::DivideAndRoundUp(P.NX, 8), FMath::DivideAndRoundUp(P.NY + 1, 8), 1);
			for (int32 n = 0; n < NumSub; ++n)
			{
				if (P.bDispersive)
				{
					// 쇄파 판정 → d · U = P − … (ToU) · 분산 항 + F*_t (지난 Δt 로 외삽). 1 단계 P, Q 는 U0 의 것 그대로 (CPU 는 SolveP(ToU(P)) = P)
					FSandGridParams X = B; X.URO = GraphBuilder.CreateSRV(U0); X.DstRW = GraphBuilder.CreateUAV(Ds); X.BrkRO = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(S->Brk)); X.BrkRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->BrkB)); X.TimeRO = GraphBuilder.CreateSRV(Tm);
					X.NuRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->Nu)); X.BrkDiagRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->BrkDiag));
					Swap(S->Brk, S->BrkB);   // 다음 판정은 이번 결과를 읽는다
					AddGridPass<FSandGridBreakCS>(GraphBuilder, RDG_EVENT_NAME("SandGridBreak"), ShaderMap, X, GC);
					if (P.bBreakRoller && P.BreakMode >= 2)
					{
						FSandGridParams Rl = B; Rl.URO = GraphBuilder.CreateSRV(U0); Rl.NuRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->Nu)); Rl.TimeRO = GraphBuilder.CreateSRV(Tm); Rl.BrkDiagRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->BrkDiag));
						AddGridPass<FSandGridRollerCS>(GraphBuilder, RDG_EVENT_NAME("SandGridRoller"), ShaderMap, Rl, FIntVector(FMath::DivideAndRoundUp(P.NY, 64), 1, 1));
					}
					FSandGridParams Y = B; Y.URO = GraphBuilder.CreateSRV(U0); Y.DstRO = GraphBuilder.CreateSRV(Ds); Y.UVRW = GraphBuilder.CreateUAV(UV0);
					AddGridPass<FSandGridToUCS>(GraphBuilder, RDG_EVENT_NAME("SandGridToU"), ShaderMap, Y, GC);
					AddDisp(U0, 1);
				}
				for (uint32 Stage = 1; Stage <= 2; ++Stage)
				{
					FRDGBufferRef In = Stage == 1 ? U0 : U1;
					FRDGBufferRef Out = Stage == 1 ? U1 : U0;
					if (P.bDispersive && Stage == 2) { AddSolve(UV1, U1); AddDisp(U1, 2); }
					FSandGridParams L = B; L.Stage = Stage;
					L.URO = GraphBuilder.CreateSRV(In);
					L.LevelRW = GraphBuilder.CreateUAV(Lv);
					AddGridPass<FSandGridLevelCS>(GraphBuilder, RDG_EVENT_NAME("SandGridLevel"), ShaderMap, L, GC);
					if (Stage == 1) { AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(AM), 0u); }
					FSandGridParams F = B; F.Stage = Stage;
					F.URO = GraphBuilder.CreateSRV(In);
					F.LevelRO = GraphBuilder.CreateSRV(Lv);
					F.AMaxRW = GraphBuilder.CreateUAV(AM);
					F.TimeRO = GraphBuilder.CreateSRV(Tm);   // 유령 칸 입사파 시각
					F.FaceXRW = GraphBuilder.CreateUAV(FX);
					if (P.bOpenEast && !OS)
					{
						// 입사파 캐시 — 이 단계의 시각으로 줄마다 한 번 (m3-plan p2)
						FSandGridParams Ic = B; Ic.IncRO = nullptr; Ic.TimeRO = GraphBuilder.CreateSRV(Tm); Ic.IncRW = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->Inc));
						AddGridPass<FSandGridIncCS>(GraphBuilder, RDG_EVENT_NAME("SandGridInc"), ShaderMap, Ic, FIntVector(P.NY, 1, 1));
						F.bIncCached = 1u;
					}
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
					// 분산 버퍼 — 얕은 물만일 때도 묶는다 (셰이더가 분기 안에서 참조)
					Up.DispRO = GraphBuilder.CreateSRV(Dp); Up.FsTRO = GraphBuilder.CreateSRV(FsT);
					Up.UVRO = GraphBuilder.CreateSRV(Stage == 1 ? UV0 : UV1); Up.UVRW = GraphBuilder.CreateUAV(Stage == 1 ? UV1 : UV0);
					if (Stage == 2)
					{
						// 2 단계: U0 = ½(U0 + U1 + Δt·L(U1)) — U0 를 읽으며 쓴다 → 임시에 쓰고 복사
						FRDGBufferRef Tmp = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), P.NX * P.NY), TEXT("SandGrid.UTmp"));
						Up.UOutRW = GraphBuilder.CreateUAV(Tmp);
						AddGridPass<FSandGridUpdateCS>(GraphBuilder, RDG_EVENT_NAME("SandGridUpdate2"), ShaderMap, Up, GC);
						AddCopyBufferPass(GraphBuilder, U0, Tmp);
						if (P.bDispersive)
						{
							AddSolve(UV0, U0);
							FSandGridParams X = B; X.TimeRO = GraphBuilder.CreateSRV(Tm); X.UOutRW = GraphBuilder.CreateUAV(U0);
							AddGridPass<FSandGridFixCS>(GraphBuilder, RDG_EVENT_NAME("SandGridFix"), ShaderMap, X, GC);
						}
					}
					else
					{
						Up.UOutRW = GraphBuilder.CreateUAV(Out);
						AddGridPass<FSandGridUpdateCS>(GraphBuilder, RDG_EVENT_NAME("SandGridUpdate1"), ShaderMap, Up, GC);
					}
				}
				if (P.bDispersive && P.BreakMode >= 2)
				{
					// Kennedy 와점성 (서브스텝 끝, 연산자 분리) — x 줄 · y 줄 암시적
					for (uint32 Ax = 0; Ax < 2; ++Ax)
					{
						FSandGridParams E = B; E.LineAxis = Ax; E.TimeRO = GraphBuilder.CreateSRV(Tm); E.UOutRW = GraphBuilder.CreateUAV(U0);
						E.NuRO = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(S->Nu));
						const FIntVector G(Ax == 0 ? P.NY : P.NX, 1, 1);   // 줄 하나 = 그룹 하나 (PCR)
						if ((Ax == 0 ? P.NX : P.NY) <= 64) { AddGridPass<FSandGridEddy64CS>(GraphBuilder, RDG_EVENT_NAME("SandGridEddy"), ShaderMap, E, G); }
						else { AddGridPass<FSandGridEddyCS>(GraphBuilder, RDG_EVENT_NAME("SandGridEddy"), ShaderMap, E, G); }
					}
				}
				if (P.bGround)
				{
					// c6 지하수 — 서브스텝 끝 U0 에서 (연산자 분리)
					FSandGridParams X = B; X.URO = GraphBuilder.CreateSRV(U0); X.GwRO = GraphBuilder.CreateSRV(GwB); X.GwQRW = GraphBuilder.CreateUAV(GwQB);
					AddGridPass<FSandGridGwFluxCS>(GraphBuilder, RDG_EVENT_NAME("SandGridGwFlux"), ShaderMap, X, GC);
					FSandGridParams Y = B; Y.TimeRO = GraphBuilder.CreateSRV(Tm); Y.GwQRO = GraphBuilder.CreateSRV(GwQB); Y.GwRW = GraphBuilder.CreateUAV(GwB); Y.UOutRW = GraphBuilder.CreateUAV(U0);
					AddGridPass<FSandGridGwApplyCS>(GraphBuilder, RDG_EVENT_NAME("SandGridGwApply"), ShaderMap, Y, GC);
				}
			}
			GraphBuilder.AddPass(RDG_EVENT_NAME("SandGridTs1"), ERDGPassFlags::NeverCull, [Q = (Wr >= 0 ? S->RbTs[Wr][1] : S->Ts[1]).GetReference()](FRHICommandListImmediate& Cmd) { Cmd.EndRenderQuery(Q); });
			if (Wr >= 0) { AddEnqueueCopyPass(GraphBuilder, S->Rb[Wr].Get(), Tm, 4 * sizeof(float)); }
			GraphBuilder.Execute();
		}
		if (Wr >= 0) { ++S->RbCount; }
		FRDGBuilder::TickPoolElements();
		RHICmdList.EndFrame();
	});
}

void FSandGridGpu::ResetBreakDiag()
{
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridResetDiag)([S](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridResetDiag"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(S->BrkDiag)), 0u);
		GraphBuilder.Execute();
	});
	FlushRenderingCommands();
}

void FSandGridGpu::DownloadBreakDiag(TArray<FVector2f>& Out) const
{
	const int32 N = Params.NX * Params.NY;
	Out.SetNum(N);
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridDownloadDiag)([S, N, &Out](FRHICommandListImmediate& RHICmdList)
	{
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridDownloadDiag"));
			AddEnqueueCopyPass(GraphBuilder, &S->DlDiag, GraphBuilder.RegisterExternalBuffer(S->BrkDiag), N * sizeof(FVector2f));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (const void* Src = S->DlDiag.Lock(N * sizeof(FVector2f))) { FMemory::Memcpy(Out.GetData(), Src, N * sizeof(FVector2f)); }
		S->DlDiag.Unlock();
	});
	FlushRenderingCommands();
}

void FSandGridGpu::DownloadNu(TArray<float>& OutNu) const
{
	const int32 N = Params.NX * Params.NY;
	OutNu.SetNum(N);
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridDownloadNu)([S, N, &OutNu](FRHICommandListImmediate& RHICmdList)
	{
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridDownloadNu"));
			AddEnqueueCopyPass(GraphBuilder, &S->DlNu, GraphBuilder.RegisterExternalBuffer(S->Nu), N * sizeof(float));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (const void* Src = S->DlNu.Lock(N * sizeof(float))) { FMemory::Memcpy(OutNu.GetData(), Src, N * sizeof(float)); }
		S->DlNu.Unlock();
	});
	FlushRenderingCommands();
}

void FSandGridGpu::DownloadGround(TArray<FVector2f>& OutGw, float* OutZaq) const
{
	const int32 N = Params.NX * Params.NY;
	OutGw.SetNum(N);
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridDownloadGw)([S, N, &OutGw](FRHICommandListImmediate& RHICmdList)
	{
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridDownloadGw"));
			AddEnqueueCopyPass(GraphBuilder, &S->DlGw, GraphBuilder.RegisterExternalBuffer(S->Gw), N * sizeof(FVector2f));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (const void* Src = S->DlGw.Lock(N * sizeof(FVector2f))) { FMemory::Memcpy(OutGw.GetData(), Src, N * sizeof(FVector2f)); }
		S->DlGw.Unlock();
	});
	FlushRenderingCommands();
	if (OutZaq) { *OutZaq = State->GwZaq; }
}

void FSandGridGpu::DownloadTime(float* OutSimTime) const
{
	float T[4] = {};
	FState* S = State;
	ENQUEUE_RENDER_COMMAND(SandGridDownloadTime)([S, &T](FRHICommandListImmediate& RHICmdList)
	{
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("SandGridDownloadTime"));
			AddEnqueueCopyPass(GraphBuilder, &S->DlTime, GraphBuilder.RegisterExternalBuffer(S->Time), 4 * sizeof(float));
			GraphBuilder.Execute();
		}
		RHICmdList.SubmitAndBlockUntilGPUIdle();
		if (const void* Src = S->DlTime.Lock(4 * sizeof(float))) { FMemory::Memcpy(T, Src, 4 * sizeof(float)); }
		S->DlTime.Unlock();
	});
	FlushRenderingCommands();
	if (OutSimTime) { *OutSimTime = T[2]; }
	const_cast<FSandGridGpu*>(this)->LastAMax = T[3];
	S->AMaxSeen.store(T[3], std::memory_order_relaxed);
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
	const_cast<FSandGridGpu*>(this)->LastAMax = T[3];
	S->AMaxSeen.store(T[3], std::memory_order_relaxed);
}
