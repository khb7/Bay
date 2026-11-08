#include "SandWaves.h"

#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "ProceduralMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SandGridGpu.h"
#include "../Grid/SandGridCpu.h"
#include "../Mesh/SandMaterialBuilder.h"
#include "../Spike/SandSpike.h"
#include "../Mesh/SandSurfaceComponent.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSandWaves, Log, All);

namespace
{
	const double Gm = 9.81;

	// JONSWAP 모양 (α 무시 — Hs 로 정규화, papers G-7)
	double Jonswap(double f, double Tp, double Gamma)
	{
		const double fp = 1.0 / Tp;
		const double Sig = f <= fp ? 0.07 : 0.09;
		const double R = FMath::Exp(-FMath::Square(f - fp) / (2.0 * Sig * Sig * fp * fp));
		return FMath::Pow(f, -5.0) * FMath::Exp(-1.25 * FMath::Pow(fp / f, 4.0)) * FMath::Pow(Gamma, R);
	}

	// 이봉 스펙트럼 (G-6c): 풍파 γ 3.3 + 너울 γ 20, 에너지 비율 SwellFrac. 32 성분 — 주파수를 칸 안에서 무작위로 (공약수가 없어 반복 주기가 사라진다)
	// 방향 분산 (G-6c): 너울 10~20° · 풍파 30~60° — 성분마다 그 주파수에서 너울/풍파 몫으로 섞은 폭 σ 의 정규분포에서 θ.
	// 해안선 방향 파수 k_y = k·sinθ (경계 수심 얕은 물 k = ω/√(gh₀)) 를 주기 경계에 맞게 2π m / L_y 로 반올림
	TArray<FVector4f> MakeWaves(double Hs, double SwellFrac, double SwellTp, double WindTp, int32 Seed, double H0, double Ly, double MeanDeg, double SwellSpreadDeg, double WindSpreadDeg, bool bDisp = false)
	{
		const int32 N = 32;
		const double F0 = 0.04, F1 = 0.40, Df = (F1 - F0) / N;
		FRandomStream Rng(Seed);
		TArray<double> F, S;
		double Mw = 0, Ms = 0;
		for (int32 n = 0; n < N; ++n) { const double f = F0 + (n + Rng.FRand()) * Df; F.Add(f); Mw += Jonswap(f, WindTp, 3.3) * Df; Ms += Jonswap(f, SwellTp, 20.0) * Df; }
		const double M0 = FMath::Square(Hs / 4.0);   // Hs = 4√m0
		TArray<FVector4f> W;
		for (int32 n = 0; n < N; ++n)
		{
			const double Sw = (1.0 - SwellFrac) * Jonswap(F[n], WindTp, 3.3) / Mw, Ss = SwellFrac * Jonswap(F[n], SwellTp, 20.0) / Ms;
			const double Sf = M0 * (Sw + Ss);
			const double A = FMath::Sqrt(2.0 * Sf * Df);
			const double Om = UE_TWO_PI * F[n];
			const double Sig = FMath::Lerp(WindSpreadDeg, SwellSpreadDeg, Ss / FMath::Max(1e-12, Sw + Ss));
			const double G1 = FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(1e-9, (double)Rng.FRand()))) * FMath::Cos(UE_TWO_PI * Rng.FRand());   // Box–Muller
			const double Th = FMath::DegreesToRadians(FMath::Clamp(MeanDeg + Sig * G1, -70.0, 70.0));
			double K = Om / FMath::Sqrt(Gm * H0);
			if (bDisp) { for (int32 It = 0; It < 60; ++It) { K = Om * Om / (Gm * FMath::Tanh(K * H0)); } }   // 분산 k (바깥 격자)
			const double Dk = UE_TWO_PI / Ly;
			const double Ky = FMath::RoundToDouble(K * FMath::Sin(Th) / Dk) * Dk;
			W.Add(FVector4f((float)A, (float)Om, (float)(Rng.FRand() * UE_TWO_PI), (float)Ky));
		}
		return W;
	}
}

float FSwashZone::Update(const TArray<float>& W, const TArray<float>& BCellV, int32 NX, int32 NY, float Dx, float T)
{
	if (LastDry.Num() != NX) { LastDry.Init(-1e9f, NX); }
	for (int32 i = 0; i < NX; ++i)
	{
		for (int32 j = 0; j < NY; ++j) { const int32 k = j * NX + i; if (W[k] - BCellV[k] <= 0.001f) { LastDry[i] = T; break; } }   // 젖음 문턱 = 단면 측정과 같은 1 mm
	}
	for (int32 i = NX - 1; i >= 0; --i) { if (T - LastDry[i] <= Mem) { return (i + 1) * Dx; } }
	return 0.f;
}

void BuildWaveScene(const TCHAR* Cmd, FWaveScene& Out)
{
	const float Dx = 0.05f;
	int32 NX = 512; FParse::Value(Cmd, TEXT("wavecells="), NX);
	const int32 NY = NX / 2;
	// 땅 쪽 추가 폭 (m3-log #16: 처오름 최대 ~1.06 m 가 뒷벽 바닥 0.77 m 를 넘음) — 기본 8 m → 뒷벽 1:15 에서 1.30 m. 바다 쪽 · NY 는 그대로
	float LandM = 8.f; FParse::Value(Cmd, TEXT("landm="), LandM);
	const int32 LandCells = FMath::Max(0, FMath::RoundToInt(LandM / Dx));
	const double XsBase = 0.45 * NX * Dx;
	NX += LandCells;
	// 해변 단면 (papers D-M3-6): 물가 x_s 에서 바다 쪽(+x) Dean h = A·(x − x_s)^(2/3), 땅 쪽 직선 (기본 1:15)
	double Slope = 1.0 / 15.0; { double Inv = 0; if (FParse::Value(Cmd, TEXT("beachslope="), Inv) && Inv > 0) { Slope = 1.0 / Inv; } }   // -beachslope=10 → 1:10
	const double A = 0.1, Xs = XsBase + LandCells * Dx;   // 땅 45% + 추가 폭 — 처오름이 뒷벽에 닿지 않게 (m3-log #5 · #16)
	TArray<float>& BCorner = Out.BCorner;
	BCorner.SetNum((NX + 1) * (NY + 1));
	for (int32 j = 0; j <= NY; ++j)
	for (int32 i = 0; i <= NX; ++i)
	{
		const double x = i * Dx, y = j * Dx;
		double B = x >= Xs ? -A * FMath::Pow(x - Xs, 2.0 / 3.0) : (Xs - x) * Slope;
		const double Ky = UE_TWO_PI / (NY * Dx);   // 해안선 방향 주기 경계와 맞는 파수
		B += 0.01 * FMath::Sin(2.0 * Ky * y + 0.4 * x) + 0.006 * FMath::Sin(6.0 * Ky * y - 1.7 * x);   // 1cm 남짓 굴곡
		BCorner[j * (NX + 1) + i] = (float)B;
	}
	const float H0 = -BCorner[NX];   // 경계 정지 수심
	float Hs = (float)(1.416 * 0.42 * H0);   // 쇄파대 포화 H_rms = 0.42·h (Thornton & Guza 1982)
	FParse::Value(Cmd, TEXT("wavehs="), Hs);
	double SwellFrac = 0.6, SwellTp = 12.0, WindTp = 5.0;
	FParse::Value(Cmd, TEXT("swellfrac="), SwellFrac);
	FParse::Value(Cmd, TEXT("swelltp="), SwellTp);
	FParse::Value(Cmd, TEXT("windtp="), WindTp);
	// 바닥 마찰 (papers D-M3-13/14 · m3-plan c7): 쇄파대 = Strickler 낟알 n = d50^⅙/21.1 (d50 0.3 mm → 0.0123), swash 구역 = 0.025 (현장 c_f 0.03)
	double D50 = 0.3e-3; { double Mm = 0; if (FParse::Value(Cmd, TEXT("d50="), Mm) && Mm > 0) { D50 = Mm * 1e-3; } }   // -d50= (mm)
	float Manning = (float)(FMath::Pow(D50, 1.0 / 6.0) / 21.1); FParse::Value(Cmd, TEXT("manning="), Manning);
	FSandGridGpuInit& G = Out.G;
	G.NX = NX; G.NY = NY; G.Dx = Dx; G.Dy = Dx; G.Manning = Manning;
	G.ManningSwash = 0.025f; FParse::Value(Cmd, TEXT("manswash="), G.ManningSwash);
	G.FricX = (float)Xs;   // 시작 물가선 — 실행 중 FSwashZone 이 되밀림 한계로 옮긴다
	if (FParse::Param(Cmd, TEXT("onefric"))) { G.ManningSwash = Manning; G.FricX = 0.f; }   // 구역 없이 한 값
	// 기본: White-Colebrook 영역 전체, k_s = 3·D90 (XBeach-G · Howe 2019 원형 크기 측정과 맞음, D-M3-15 · m3-log #17), D90 ≈ 1.5·d50 가정.
	// -ks= (m) 직접 · -zonefric: c7 구역 Manning (진단용)
	if (!FParse::Param(Cmd, TEXT("zonefric"))) { G.Ks = (float)(3.0 * 1.5 * D50); }
	FParse::Value(Cmd, TEXT("ks="), G.Ks);
	if (G.Ks > 0.f) { G.FricX = 0.f; }
	// 쇄파 판정 (papers D-M3-18): 기본 Tonelli-Petti |η|/h 0.8. -breakdedt: SWASH HFA (∂η/∂t)/√(gh) > 0.6 · 지속 0.3 — 바깥 격자가 40~60 s 에 무너짐 (m3-log #20), 실험용
	// 기본: Kennedy et al. (2000) 와점성 (전환 없음, 900 s 안정) — 시작 η_t > 0.65√(gH), 종료 0.15√(gH), δ_b 1.2 (D-M3-19 · m3-plan c9 · m3-log #21).
	// -breakeps: Tonelli-Petti 전환 (정지 수심 ε 0.8 — 바깥 격자 ~375 s 붕괴, 실험용) · -breakdedt: SWASH 식 (붕괴, 실험용)
	G.BreakMode = 3; G.BreakRatio = 0.3f; G.BreakStop = 0.15f; G.BreakTbrk = (float)SwellTp;   // 시작 γ 0.3: MS92 는 덜 천수돼 앞면이 덜 섬 — 같은 MS92 의 Bacigaluppi 2019 · Kazolea 2018 막대 경우 값 (papers D-M3-21, m3-log #28). 기본 = FUNWAVE-TVD breaker.F 기본 (m3-log #23). -breakkennedy: 이전 칸별 Kennedy ν = δ²Hη_t
	if (FParse::Param(Cmd, TEXT("breakkennedy"))) { G.BreakMode = 2; }
	// -breakkr: Kazolea & Ricchiuto (2018) 판정 — γ 0.6 + 기울기 φ_c 30° (경사면 · reef 시험 값, D-M3-20). 종료는 Kennedy 0.15
	if (FParse::Param(Cmd, TEXT("breakkr"))) { G.BreakRatio = 0.6f; G.BreakSlope = FMath::Tan(FMath::DegreesToRadians(30.f)); }
	// -breakfw: FUNWAVE-TVD breaker.F 기본 (나이 · 위쪽 이웃 번짐, ν = H·C2·√(gH), T_brk = 너울 주기) — papers D-M3-21
	if (FParse::Param(Cmd, TEXT("breakfw"))) { G.BreakMode = 3; G.BreakRatio = 0.65f; G.BreakStop = 0.15f; G.BreakTbrk = (float)SwellTp; }
	if (FParse::Param(Cmd, TEXT("breakeps"))) { G.BreakMode = 0; G.BreakRatio = 0.8f; G.BreakStop = 0.3f; }
	if (FParse::Param(Cmd, TEXT("breakdedt"))) { G.BreakMode = 1; G.BreakRatio = 0.6f; G.BreakStop = 0.3f; }
	FParse::Value(Cmd, TEXT("breakratio="), G.BreakRatio);
	FParse::Value(Cmd, TEXT("eddydelta="), G.EddyDelta);
	FParse::Value(Cmd, TEXT("breaktbrk="), G.BreakTbrk);
	if (FParse::Param(Cmd, TEXT("breakroller"))) { G.bBreakRoller = true; }   // papers D-M3-22   // T_brk (s). FUNWAVE-TVD init.F 기본 20 s
	FParse::Value(Cmd, TEXT("breakstop="), G.BreakStop);   // 쇄파 지속 종료 ε (0.3, D-M3-17 · m3-plan c8). 0 이면 이전 순간 판정
	G.bOpenEast = true; G.SeaLevel = 0.f; G.BoundaryDepth = H0;
	// T3c c6 지하수 · 침투 (m3-plan c6): -groundoff 로 끔 · -gwk= (5e-4 m/s)
	G.bGround = !FParse::Param(Cmd, TEXT("groundoff"));   // 기본 켬 (m3-log #14)
	FParse::Value(Cmd, TEXT("gwk="), G.GwK);
	// T4a 모래 (m3-plan T4): -morph 로 켬 · -morfac= (1) · -sedd= (D50 m, 2.5e-4)
	G.bMorph = FParse::Param(Cmd, TEXT("morph"));
	G.bSus = G.bMorph && FParse::Param(Cmd, TEXT("sus"));   // T4b 부유사
	FParse::Value(Cmd, TEXT("morfac="), G.Morfac);
	FParse::Value(Cmd, TEXT("sedd="), G.SedD50);
	double MeanDeg = 10.0, SwellSpread = 15.0, WindSpread = 40.0;
	FParse::Value(Cmd, TEXT("wavedir="), MeanDeg);
	FParse::Value(Cmd, TEXT("swellspread="), SwellSpread);
	FParse::Value(Cmd, TEXT("windspread="), WindSpread);
	G.bPeriodicY = true;
	int32 Seed = 1234; FParse::Value(Cmd, TEXT("waveseed="), Seed);
	G.Waves = MakeWaves(Hs, SwellFrac, SwellTp, WindTp, Seed, H0, NY * Dx, MeanDeg, SwellSpread, WindSpread);
	// T3b 장주기파 (B 안, m3-plan): 경계 IG 유의파고 = IgCoef · H₀(먼바다). 이론 식(LH&S 1962)은 쇄파 수심에선 발산 · 깊은 곳 + Green 법칙은
	// 관측의 수십 분의 1 이라 (m3-log #7) 관측(Stockdon IG swash)에 맞춰 계수 하나를 정한다. 부호 = 파군 밑 수위 하강이 풀려 들어온다
	double DeepH0 = 1.0, IgCoef = 0.25;
	FParse::Value(Cmd, TEXT("waveh0="), DeepH0);
	FParse::Value(Cmd, TEXT("igcoef="), IgCoef);
	{
		// A² − Σa² 의 표준편차 (y = 0, 1 시간 1 초 간격) → 이득
		double S2 = 0, Sum2 = 0; int32 Nn = 0;
		for (int32 t = 0; t < 3600; ++t)
		{
			double Re = 0, Im = 0, Am = 0;
			for (const FVector4f& W : G.Waves) { const double Ph = W.Z - W.Y * t; Re += W.X * FMath::Cos(Ph); Im += W.X * FMath::Sin(Ph); Am += W.X * W.X; }
			const double D = Re * Re + Im * Im - Am; S2 += D * D; Sum2 += D; ++Nn;
		}
		const double Sd = FMath::Sqrt(FMath::Max(1e-12, S2 / Nn - FMath::Square(Sum2 / Nn)));
		G.IgGain = FParse::Param(Cmd, TEXT("igoff")) ? 0.f : (float)(-IgCoef * DeepH0 / (4.0 * Sd));
	}
	// T3c c4 한 방향 nesting (m3-plan c4, -nest): 바깥 격자 = 같은 단면을 수심 -nestdepth= (3.3 m, Hm0/d < 0.3) 까지, 칸 -nestdx= (25 cm), 분산 · 묶인 IG.
	// 안쪽 동쪽 입사는 바깥 격자 x = NX·Dx 값으로 (B 안 IG 는 끔)
	Out.bNest = FParse::Param(Cmd, TEXT("nest"));
	if (Out.bNest)
	{
		double NestDepth = 3.3, NestDx = 0.25;
		FParse::Value(Cmd, TEXT("nestdepth="), NestDepth);
		FParse::Value(Cmd, TEXT("nestdx="), NestDx);
		const double Ly = NY * Dx, XEnd = Xs + FMath::Pow(NestDepth / A, 1.5);
		const int32 NXo = FMath::CeilToInt(XEnd / NestDx), NYo = FMath::Max(8, FMath::RoundToInt(Ly / NestDx));
		const double Dyo = Ly / NYo;
		FSandGridGpuInit& O = Out.GO;
		O.NX = NXo; O.NY = NYo; O.Dx = (float)NestDx; O.Dy = (float)Dyo; O.Manning = Manning; O.Ks = G.Ks; O.BreakStop = G.BreakStop; O.BreakMode = G.BreakMode; O.BreakRatio = G.BreakRatio; O.BreakSlope = G.BreakSlope; O.EddyDelta = G.EddyDelta; O.BreakTbrk = G.BreakTbrk; O.bBreakRoller = G.bBreakRoller;
		O.bOpenEast = true; O.bPeriodicY = true; O.bDispersive = true; O.bBoundIg = !FParse::Param(Cmd, TEXT("igoff"));
		O.BCorner.SetNum((NXo + 1) * (NYo + 1));
		for (int32 j = 0; j <= NYo; ++j)
		for (int32 i = 0; i <= NXo; ++i)
		{
			const double x = i * NestDx, y = j * Dyo;
			double B = x >= Xs ? -A * FMath::Pow(x - Xs, 2.0 / 3.0) : (Xs - x) * Slope;
			const double Ky = UE_TWO_PI / Ly;
			B += 0.01 * FMath::Sin(2.0 * Ky * y + 0.4 * x) + 0.006 * FMath::Sin(6.0 * Ky * y - 1.7 * x);
			O.BCorner[j * (NXo + 1) + i] = (float)B;
		}
		O.BoundaryDepth = -O.BCorner[NXo];
		O.Waves = MakeWaves(DeepH0, SwellFrac, SwellTp, WindTp, Seed, O.BoundaryDepth, Ly, MeanDeg, SwellSpread, WindSpread, true);
		O.W.SetNum(NXo * NYo); O.HU.Init(0.f, NXo * NYo); O.HV.Init(0.f, NXo * NYo);
		for (int32 j = 0; j < NYo; ++j)
		for (int32 i = 0; i < NXo; ++i)
		{
			const double Bc[4] = { O.BCorner[j * (NXo + 1) + i], O.BCorner[j * (NXo + 1) + i + 1], O.BCorner[(j + 1) * (NXo + 1) + i], O.BCorner[(j + 1) * (NXo + 1) + i + 1] };
			O.W[j * NXo + i] = (float)(0.25 * (Bc[0] + Bc[1] + Bc[2] + Bc[3]) + FSandGridCpu::CellVolume(Bc, 0.0));
		}
		G.Waves.Empty(); G.IgGain = 0.f; G.NestX = NX * Dx;   // NestOuter 는 만든 쪽이 넣는다
	}
	Out.BCellV.SetNum(NX * NY);
	G.W.SetNum(NX * NY); G.HU.Init(0.f, NX * NY); G.HV.Init(0.f, NX * NY);
	for (int32 j = 0; j < NY; ++j)
	for (int32 i = 0; i < NX; ++i)
	{
		const double Bc[4] = { BCorner[j * (NX + 1) + i], BCorner[j * (NX + 1) + i + 1], BCorner[(j + 1) * (NX + 1) + i], BCorner[(j + 1) * (NX + 1) + i + 1] };
		const double Bm = 0.25 * (Bc[0] + Bc[1] + Bc[2] + Bc[3]);
		Out.BCellV[j * NX + i] = (float)Bm;
		G.W[j * NX + i] = (float)(Bm + FSandGridCpu::CellVolume(Bc, 0.0));   // 정지 수면 0 (이산 평형)
	}
	G.BCorner = BCorner;
	Out.NX = NX; Out.NY = NY; Out.Dx = Dx; Out.H0 = H0; Out.Hs = Hs; Out.Xs = Xs; Out.Slope = Slope; Out.DeepH0 = DeepH0;
	Out.Tp = SwellFrac >= 0.5 ? SwellTp : WindTp;
	const FString NestInfo = Out.bNest ? FString::Printf(TEXT(" · nesting 바깥 %d×%d (칸 %.0fcm, %.0fm, 경계 수심 %.2fm, Hs %.2fm, 묶인 IG %s)"),
		Out.GO.NX, Out.GO.NY, Out.GO.Dx * 100, Out.GO.NX * Out.GO.Dx, Out.GO.BoundaryDepth, DeepH0, Out.GO.bBoundIg ? TEXT("켬") : TEXT("끔")) : FString();
	Out.Info = FString::Printf(TEXT("T3 파도 — 격자 %d×%d (칸 %.0fcm, %.1f×%.1fm) · 해변 1:%.0f · 경계 수심 %.2fm · 경계 Hs %.2fm%s · 먼바다 H0 %.2fm · 너울 %.0f%% Tp %.0fs · 풍파 Tp %.0fs · Manning %.3f · IG 계수 %.2f (이득 %.2f)"),
		NX, NY, Dx * 100, NX * Dx, NY * Dx, 1.0 / Slope, H0, Hs, Out.bNest ? TEXT(" (포화 가정값 — nesting 이면 안 씀)") : TEXT(""), DeepH0, SwellFrac * 100, SwellTp, WindTp, Manning, IgCoef, G.IgGain) + NestInfo
		+ (G.bGround ? FString::Printf(TEXT(" · 지하수 K %.1e m/s"), G.GwK) : FString());
}

bool USandWavesSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FString(FCommandLine::Get()).Contains(TEXT("-sandwaves"));
}

void USandWavesSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) { return; }
	const TCHAR* Cmd = FCommandLine::Get();
	FWaveScene Sc;
	BuildWaveScene(Cmd, Sc);
	NX = Sc.NX; NY = Sc.NY; BCorner = Sc.BCorner; BCellV = Sc.BCellV; H0 = Sc.H0; Hs = Sc.Hs; bZone = Sc.G.FricX > 0.f; Xs0 = (float)Sc.Xs;
	FSandGridGpuInit& G = Sc.G;
	const double Xs = Sc.Xs;
	// T6: 게임은 모래 켬 — 판 구덩이가 사태로 무너져야 한다. 측정 경로 기본은 그대로 (BuildWaveScene). -nomorph 로 끔
	if (!FParse::Param(Cmd, TEXT("nomorph"))) { G.bMorph = true; G.bSus = FParse::Param(Cmd, TEXT("sus")); }
	FParse::Value(Cmd, TEXT("handcap="), HandCapM3);
	bToolTest = FParse::Param(Cmd, TEXT("wavetooltest"));
	Xs1 = (float)Xs;
	// 맵에 놓인 예전 입자 시험 액터는 끈다 (프레임마다 14ms — 이 화면과 무관)
	for (TActorIterator<AActor> It(&World); It; ++It)
	{
		if (It->GetClass()->GetName().Contains(TEXT("SpikeActor"))) { It->SetActorTickEnabled(false); It->SetActorHiddenInGame(true); }
	}
	if (Sc.bNest) { GridOuter = MakeUnique<FSandGridGpu>(); GridOuter->Init(Sc.GO); G.NestOuter = GridOuter.Get(); }
	Grid = MakeUnique<FSandGridGpu>();
	Grid->Init(G);
	UE_LOG(LogSandWaves, Display, TEXT("%s"), *Sc.Info);

	// 메시: 지형(모래) · 물 — Step 칸마다 정점
	const int32 VX = NX / Step + 1, VY = NY / Step + 1;
	AActor* Actor = World.SpawnActor<AActor>();
	Mesh = NewObject<UProceduralMeshComponent>(Actor);
	Actor->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Actor->SetActorLocation(FVector(300.0, -NY * Dx * 50.0, H0 * 100.0 + 2.0));   // 가장 깊은 곳이 바닥판(z = 0) 위로
	GridBase = Actor->GetActorLocation();
	TArray<FVector> TV; TArray<int32> Tri; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FProcMeshTangent> Tan;
	auto Corner = [&](int32 vi, int32 vj) { return BCorner[FMath::Min(vj * Step, NY) * (NX + 1) + FMath::Min(vi * Step, NX)]; };
	for (int32 vj = 0; vj < VY; ++vj)
	for (int32 vi = 0; vi < VX; ++vi)
	{
		TV.Add(FVector(vi * Step * Dx * 100.0, vj * Step * Dx * 100.0, Corner(vi, vj) * 100.0));
		const float zx = Corner(FMath::Min(vi + 1, VX - 1), vj) - Corner(FMath::Max(vi - 1, 0), vj);
		const float zy = Corner(vi, FMath::Min(vj + 1, VY - 1)) - Corner(vi, FMath::Max(vj - 1, 0));
		Nrm.Add(FVector(-zx, -zy, 2.0 * Step * Dx).GetSafeNormal());
		UV.Add(FVector2D(vi, vj));
	}
	for (int32 vj = 0; vj + 1 < VY; ++vj)
	for (int32 vi = 0; vi + 1 < VX; ++vi)
	{
		const int32 a = vj * VX + vi, b = a + 1, c = a + VX, d = c + 1;
		Tri.Append({ a, c, b, b, c, d });
	}
	// T5: 정점은 처음 바닥 (경계 상자용), 실제 높이 · 색은 재질이 GridRt 에서 읽는다 (WPO)
	for (int32 v = 0; v < TV.Num(); ++v) { UV[v] = FVector2D((double)(v % VX) * Step / NX, (double)(v / VX) * Step / NY); }
	TArray<FColor> NoCol;
	Mesh->CreateMeshSection(0, TV, Tri, Nrm, UV, NoCol, Tan, false);
	Mesh->CreateMeshSection(1, TV, Tri, Nrm, UV, NoCol, Tan, false);
	Mesh->SetBoundsScale(2.f);
	GridRt = NewObject<UTextureRenderTarget2D>(this);
	GridRt->bCanCreateUAV = true;
	GridRt->AddressX = GridRt->AddressY = TA_Clamp;
	GridRt->ClearColor = FLinearColor::Black;
	GridRt->InitCustomFormat(NX, NY, PF_A32B32G32R32F, false);
	GridRt->UpdateResourceImmediate(true);
	Grid->SetExportTarget(GridRt->GameThread_GetRenderTargetResource(), 1.f / 60.f / 20.f);   // 물이 빠지면 20 초에 걸쳐 마른 색으로 (M5 증발 전까지의 눈요기)
	auto Mid = [&](const FString& Path, int32 Section, const TCHAR* Name)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, *(Path + TEXT(".") + Name));
		if (!Base) { UE_LOG(LogSandWaves, Warning, TEXT("%s 없음 — 커맨드릿 -makewatermat 로 만들 것"), Name); return; }
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, this);
		D->SetTextureParameterValue(TEXT("GridTex"), GridRt);
		D->SetScalarParameterValue(TEXT("CellCm"), Dx * 100.f);
		Mesh->SetMaterial(Section, D);
	};
	Mid(SandMaterial::SandGridPath(), 0, TEXT("M_SandGrid"));
	Mid(SandMaterial::WaterPath(), 1, TEXT("M_Water"));

	for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
	{
		It->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		It->SetActorRotation(FRotator(-40.0, 150.0, 0.0));
		break;
	}
	// 카메라: 땅 쪽 위에서 바다를 비스듬히
	const FVector Base = Actor->GetActorLocation();
	const FVector Eye = Base + FVector(Xs * 100.0 - 700.0, NY * Dx * 50.0, 300.0);
	const FVector Look = Base + FVector(Xs * 100.0 + 500.0, NY * Dx * 50.0, 0.0);
	const FRotator Rot = (Look - Eye).Rotation();
	Yaw = (float)Rot.Yaw; Pitch = (float)Rot.Pitch;
	Cam = World.SpawnActor<ACameraActor>(Eye, Rot);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(&World, 0))
	{
		PC->SetViewTarget(Cam);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	if (FParse::Param(Cmd, TEXT("wavesand"))) { InitSand(Cmd); }
	FParse::Value(Cmd, TEXT("sandwavesshot="), ShotPath);
	ShotTimes = { 20.f, 35.f, 50.f };
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USandWavesSubsystem::Tick));
}

void USandWavesSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (SandComp) { SandComp->SetParticles({}); }
	FlushRenderingCommands();
	Sand.Reset();
	Grid.Reset();
	GridOuter.Reset();
	Super::Deinitialize();
}

void USandWavesSubsystem::UpdateCamera(APlayerController* PC, float Dt)
{
	float Mx = 0.f, My = 0.f;
	PC->GetMousePosition(Mx, My);
	const FVector2D Mouse(Mx, My);
	const bool bMid = PC->IsInputKeyDown(EKeys::MiddleMouseButton);
	if (bMid && bLooking)
	{
		const FVector2D Dm = Mouse - LastMouse;
		Yaw += 0.25f * (float)Dm.X;
		Pitch = FMath::Clamp(Pitch - 0.25f * (float)Dm.Y, -89.f, 89.f);
	}
	bLooking = bMid;
	LastMouse = Mouse;
	const FRotator Rot(Pitch, Yaw, 0.f);
	FVector Move = FVector::ZeroVector;
	if (PC->IsInputKeyDown(EKeys::W)) { Move += Rot.Vector(); }
	if (PC->IsInputKeyDown(EKeys::S)) { Move -= Rot.Vector(); }
	if (PC->IsInputKeyDown(EKeys::D)) { Move += FRotationMatrix(Rot).GetUnitAxis(EAxis::Y); }
	if (PC->IsInputKeyDown(EKeys::A)) { Move -= FRotationMatrix(Rot).GetUnitAxis(EAxis::Y); }
	if (PC->IsInputKeyDown(EKeys::E)) { Move += FVector::UpVector; }
	if (PC->IsInputKeyDown(EKeys::Q)) { Move -= FVector::UpVector; }
	const float Speed = PC->IsInputKeyDown(EKeys::LeftShift) ? 600.f : 200.f;
	Cam->SetActorLocationAndRotation(Cam->GetActorLocation() + Move.GetSafeNormal() * Speed * Dt, Rot);
}

bool USandWavesSubsystem::PickBed(const FVector& O, const FVector& Dir, FVector2f& OutXY, float& OutZ) const
{
	// 처음 바닥과 광선 교차 (칸 크기 행진). 스탬프는 (x, y) 만 쓰므로 그 뒤 바뀐 바닥 높이 차는 커서 위치의 작은 오차로만 남는다
	const FVector P0 = (O - GridBase) / 100.0;
	const double Step1 = Dx * 0.5;
	for (double T = 0.0; T < 200.0; T += Step1)
	{
		const FVector P = P0 + Dir * T;
		const int32 i = FMath::RoundToInt(P.X / Dx), j = FMath::RoundToInt(P.Y / Dx);
		if (i < 0 || j < 0 || i > NX || j > NY) { continue; }
		const float B = BCorner[j * (NX + 1) + i];
		if (P.Z <= B) { OutXY = FVector2f((float)P.X, (float)P.Y); OutZ = B; return true; }
	}
	return false;
}

void USandWavesSubsystem::ApplyStamp(const FSandToolStamp& T)
{
	// M2 스탬프와 같은 구조체 · 같은 뜻 (m3-plan T6), 단위 m. 파기 Strength = 구 아래 반쪽을 퍼 담는 비율 · 쌓기 Strength = 입자 수 · 물 WetAdd = 포화도 증가 × 공극
	const float Hemi = 2.f / 3.f * UE_PI * T.Radius * T.Radius * T.Radius;
	if (T.Kind == ESandTool::Dig)
	{
		const float V = FMath::Min(T.Strength * Hemi, HandCapM3 - HandM3);   // 손이 차면 더 못 판다 (M2 와 같음)
		if (V <= 0.f) { return; }
		Grid->QueueBedStamp(T.Center.X, T.Center.Y, T.Radius, -V);
		HandM3 += V; DugM3 += V;
	}
	else if (T.Kind == ESandTool::Pile)
	{
		const float V = FMath::Min(T.Strength * 4.19e-3f / 600.f, HandM3);   // 입자 하나 = 손 용량 600 입자 ≈ 반지름 10 cm 구 (m2-plan)
		if (V <= 0.f) { return; }
		Grid->QueueBedStamp(T.Center.X, T.Center.Y, T.Radius, V);
		HandM3 -= V; PiledM3 += V;
	}
	else
	{
		const float V = T.WetAdd * 0.4f * Hemi;
		Grid->QueueWaterStamp(T.Center.X, T.Center.Y, T.Radius, V);
		ToolWaterM3 += V;
	}
}

void USandWavesSubsystem::UpdateTools(APlayerController* PC, float Mul)
{
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) { BrushR = FMath::Min(0.8f, BrushR * 1.15f); }
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) { BrushR = FMath::Max(2.f * Dx, BrushR / 1.15f); }   // 칸 두 개 아래는 격자가 못 그린다
	int32 Tool = -1;
	FVector2f XY = FVector2f::ZeroVector;
	float Z = 0.f;
	bool bHit = false;
	if (bToolTest)
	{
		// 자동 시험: 물가 위 마른 땅 — 20~25 s 파기 · 25~30 s 옆에 쌓기 · 30~33 s 구덩이에 물
		XY = FVector2f(Xs1 - 2.5f, NY * Dx * (SimTime < 25.f || SimTime >= 30.f ? 0.5f : 0.56f));
		Tool = SimTime >= 20.f && SimTime < 25.f ? 0 : SimTime >= 25.f && SimTime < 30.f ? 1 : SimTime >= 30.f && SimTime < 33.f ? 2 : -1;
		Z = BCorner[FMath::RoundToInt(XY.Y / Dx) * (NX + 1) + FMath::RoundToInt(XY.X / Dx)];
		bHit = true;
	}
	else
	{
		FVector O, Dir;
		if (PC->DeprojectMousePositionToWorld(O, Dir)) { bHit = PickBed(O, Dir, XY, Z); }
		if (PC->IsInputKeyDown(EKeys::LeftMouseButton)) { Tool = 0; }
		else if (PC->IsInputKeyDown(EKeys::RightMouseButton)) { Tool = 1; }
		else if (PC->IsInputKeyDown(EKeys::F)) { Tool = 2; }
	}
	if (bHit && Tool >= 0)
	{
		// 연속 브러시 (M2 ApplyStroke): 같은 도구로 끌면 지난 점 → 지금 점을 반지름 0.4 배 간격으로. 한 프레임 양을 나눠 놓는다. 너무 먼 점프는 잇지 않는다
		const float Len = FVector2f::Distance(LastToolXY, XY), Gap = 0.4f * BrushR;
		const int32 K = (Tool == LastTool && Len > Gap && Len < 1.5f) ? FMath::Min(32, FMath::CeilToInt(Len / Gap)) : 1;
		for (int32 k = 1; k <= K; ++k)
		{
			FSandToolStamp T;
			const FVector2f C = K == 1 ? XY : FMath::Lerp(LastToolXY, XY, (float)k / K);
			T.Center = FVector3f(C.X, C.Y, 0.f);
			T.Radius = BrushR;
			// 세기는 격자 손맛 임시값 — 사용자 화면 판정 (M2 세기 0.15 는 5 cm 칸에서 프레임당 3 cm 로 너무 빠름)
			if (Tool == 0) { T.Kind = ESandTool::Dig; T.Strength = 0.02f * Mul / K; }
			else if (Tool == 1) { T.Kind = ESandTool::Pile; T.Radius = FMath::Max(2.f * Dx, 0.4f * BrushR); T.Strength = 40.f * Mul / K; }
			else { T.Kind = ESandTool::Pour; T.WetAdd = 0.02f * Mul / K; }
			ApplyStamp(T);
		}
	}
	LastTool = bHit ? Tool : -1;
	LastToolXY = XY;
	if (bHit)
	{
		const FColor C = Tool == 0 ? FColor::Red : Tool == 1 ? FColor::Green : Tool == 2 ? FColor::Cyan : FColor::White;
		DrawDebugCircle(GetWorld(), GridBase + FVector(XY.X * 100.0, XY.Y * 100.0, Z * 100.0 + 1.0), BrushR * 100.f, 48, C, false, -1.f, SDPG_Foreground, 0.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
	if (GEngine)
	{
		const int32 Fill = FMath::Clamp(FMath::RoundToInt(20.f * HandM3 / HandCapM3), 0, 20);
		GEngine->AddOnScreenDebugMessage(2, 0.f, FColor::Green, FString::Printf(TEXT("좌 파기 · 우 쌓기 · F 물붓기 · 휠 반지름 %.0f cm · Shift ×3 · 손 [%s%s] %.1f L"), BrushR * 100.f, *FString::ChrN(Fill, TEXT('#')), *FString::ChrN(20 - Fill, TEXT('.')), HandM3 * 1e3f));
	}
}

float USandWavesSubsystem::BedAt(const TArray<float>& Bc, float X, float Y) const
{
	const float Gx = FMath::Clamp(X / Dx, 0.f, (float)NX), Gy = FMath::Clamp(Y / Dx, 0.f, (float)NY);
	const int32 i = FMath::Min((int32)Gx, NX - 1), j = FMath::Min((int32)Gy, NY - 1);
	const float u = Gx - i, v = Gy - j; const int32 W1 = NX + 1;
	return FMath::Lerp(FMath::Lerp(Bc[j * W1 + i], Bc[j * W1 + i + 1], u), FMath::Lerp(Bc[(j + 1) * W1 + i], Bc[(j + 1) * W1 + i + 1], u), v);
}

void USandWavesSubsystem::InitSand(const TCHAR* Cmd)
{
	// 젖은 모래 블록 (기본 50 × 50 × 40 cm) 을 물가 위 마른 땅에 — 기둥마다 바닥은 그 자리 지면 위 (경사를 따른다). 결합을 맺어 선다
	float Half = 25.f, Tall = 40.f, Wet = 0.4f;
	FParse::Value(Cmd, TEXT("sandhalf="), Half); FParse::Value(Cmd, TEXT("sandtall="), Tall); FParse::Value(Cmd, TEXT("sandwet="), Wet);
	bSandTest = FParse::Param(Cmd, TEXT("wavesandtest"));
	FSandParams SP;
	SP.Substeps = 1; SP.WarmStart = 1.f; SP.WarmGapTol = 0.05f; SP.bGpuResident = true;
	ApplySandGpuProfile(TEXT("-gpu2"), SP);
	SP.bGatherApply = false; SP.bMassSplit = false;
	const float R = SP.Radius, D = 2.f * R;
	SandC = FVector2f(Xs1 - 2.5f, NY * Dx * 0.5f);
	TArray<FVector3f> Pos; TArray<float> Inv;
	const int32 Nh = FMath::Max(1, FMath::RoundToInt(Half / D)), Nz = FMath::Max(1, FMath::RoundToInt(Tall / D));
	for (int32 a = -Nh; a < Nh; ++a)
	for (int32 c = -Nh; c < Nh; ++c)
	{
		const float X = SandC.X * 100.f + (a + 0.5f) * D, Y = SandC.Y * 100.f + (c + 0.5f) * D;
		const float Z0 = 100.f * BedAt(BCorner, X / 100.f, Y / 100.f) + R + 0.05f;
		for (int32 k = 0; k < Nz; ++k) { Pos.Add(FVector3f(X, Y, Z0 + k * D)); Inv.Add(1.f); }
	}
	Sand = MakeUnique<FSandSpike>();
	Sand->GroundSrc.GroundBed = Grid->GetBedBuffer(); Sand->GroundSrc.GroundNX = NX; Sand->GroundSrc.GroundNY = NY;
	Sand->GroundSrc.GroundCell = Dx * 100.f; Sand->GroundSrc.GroundOrigin = FVector2f::ZeroVector; Sand->GroundSrc.GroundZ = 0.f;
	Sand->InitCustom(SP, Pos, Inv);
	Sand->SetWGlobal(Wet);
	const int32 Bonds = Sand->FormBonds();
	Sand->bAsyncStats = true;
	Sand->Step(1.f / 60.f);   // GPU 상주 시뮬은 첫 스텝에서 만들어진다 — 메셔에 넘기기 전에
	AActor* Actor = GetWorld()->SpawnActor<AActor>();
	SandComp = NewObject<USandSurfaceComponent>(Actor);
	Actor->SetRootComponent(SandComp);
	SandComp->RegisterComponent();
	Actor->SetActorLocation(GridBase);
	const FVector3f C3(SandC.X * 100.f, SandC.Y * 100.f, 100.f * BedAt(BCorner, SandC.X, SandC.Y));
	SandComp->SetSimSource(Sand->GetGpuSim(), C3 - FVector3f(150.f, 150.f, 60.f), C3 + FVector3f(150.f, 150.f, Tall + 60.f));
	UE_LOG(LogSandWaves, Display, TEXT("M4 T1 모래 블록: 입자 %d · 반지름 %.2f cm · %d×%d×%d · 가운데 (%.2f, %.2f) m · W %.2f · 결합 %d"), Pos.Num(), R, 2 * Nh, 2 * Nh, Nz, SandC.X, SandC.Y, Wet, Bonds);
}

void USandWavesSubsystem::UpdateSand()
{
	if (!Sand.IsValid()) { return; }
	if (bSandTest && !bSandPitDone && SimTime >= 3.f)
	{
		// 블록 +y 가장자리에 구덩이 (반지름 30 cm, 40 L) — 밑동이 걸친 쪽이 무너져 들어가야 한다
		Grid->QueueBedStamp(SandC.X, SandC.Y + 0.30f, 0.3f, -0.04f);
		bSandPitDone = true;
		UE_LOG(LogSandWaves, Display, TEXT("M4 T1 %.1fs: 구덩이 (%.2f, %.2f) R 0.30 m · 40 L"), SimTime, SandC.X, SandC.Y + 0.30f);
	}
	Sand->Step(1.f / 60.f);
	SandComp->UpdateFromSim();
	SandGpuSum += Sand->LastGpuMs(); ++SandGpuN;
	Sand->GetGpuSim()->RequestPositionsAsync();
	const bool bGot = Sand->GetGpuSim()->TakePositionsAsync(SandPos);
	if (bSandTest && bGot && SimTime >= SandNextLog)
	{
		SandNextLog = SimTime + 1.f;
		TArray<float> Bc; Grid->DownloadBed(Bc);
		const float R = Sand->GetParams().Radius;
		float MinGap = 1e9f, Top = -1e9f; int32 Alive = 0, InPit = 0, Bad = 0;
		for (const FVector4f& X : SandPos)
		{
			if (SandPool::IsGrave(FVector3f(X))) { continue; }
			if (!FMath::IsFinite(X.X + X.Y + X.Z)) { ++Bad; continue; }
			++Alive;
			const float Gx = X.X / 100.f, Gy = X.Y / 100.f, H = 100.f * BedAt(Bc, Gx, Gy), Hb0 = 100.f * BedAt(BCorner, Gx, Gy);
			MinGap = FMath::Min(MinGap, X.Z - H - R);
			Top = FMath::Max(Top, X.Z + R - Hb0);
			if (X.Z - R < Hb0 - 1.f) { ++InPit; }   // 처음 지면보다 1 cm 넘게 아래 = 구덩이로 들어감
		}
		UE_LOG(LogSandWaves, Display, TEXT("M4 T1 %.0fs: 입자 %d (비유한 %d) · 처음 지면 위 최고 %.1f cm · 지면 최소 간극 %.2f cm · 구덩이 안 %d · 입자 GPU 평균 %.2f ms · 격자 GPU %.2f ms"),
			SimTime, Alive, Bad, Top, MinGap, InPit, SandGpuSum / FMath::Max(SandGpuN, 1), Grid->LastGpuMs());
		SandGpuSum = 0.f; SandGpuN = 0;
	}
}

void USandWavesSubsystem::UpdateSwash()
{
	// T5: 렌더는 GPU 안에서 끝남 — CPU 는 swash 마찰 구역 · 진단용 수면만 1 s 마다 비동기로
	SimTime = Grid->LastSimTime();
	if (SimTime >= NextWReq) { Grid->RequestW(); NextWReq = SimTime + 1.f; }
	if (!Grid->PollW(LastW)) { return; }
	if (bZone) { Grid->SetFricX(Swash.Update(LastW, BCellV, NX, NY, Dx, SimTime)); }
	if (FMath::FloorToInt(SimTime / 5.f) != LastDiag)
	{
		LastDiag = FMath::FloorToInt(SimTime / 5.f);
		double V = 0, Lev = 0; int32 Nl = 0; int32 Shore = NX;
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i)
		{
			const int32 k = j * NX + i; const float h = LastW[k] - BCellV[k];
			V += FMath::Max(0.f, h);
			if (i > NX - 20) { Lev += LastW[k]; ++Nl; }
			if (h > 0.003f) { Shore = FMath::Min(Shore, i); }
		}
		UE_LOG(LogSandWaves, Display, TEXT("T3 진단 %.0fs: 물 부피 %.2f m³ · 경계 근처 평균 수위 %+.3f m · 물 닿은 땅 쪽 끝 x %.2f m (정지 물가 %.2f)"),
			SimTime, V * Dx * Dx, Lev / FMath::Max(1, Nl), Shore * Dx, Xs0);
	}
}

bool USandWavesSubsystem::Tick(float DeltaTime)
{
	++Frames;
	{ const double Now = FPlatformTime::Seconds(); if (SimTime > 10.f && LastWall > 0.0) { WallSum += Now - LastWall; ++WallN; } LastWall = Now; }   // 프레임 시간 (T5 비용)
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!Grid.IsValid() || !PC || !Cam) { return true; }
	const bool bAuto = !ShotPath.IsEmpty();
	const float Dt = bAuto ? 1.f / 60.f : FMath::Min(DeltaTime, 1.f / 30.f);   // 자동 시험은 고정 (프레임 속도와 무관하게 같은 장면)
	if (!bAuto) { UpdateCamera(PC, Dt); }
	UpdateTools(PC, PC->IsInputKeyDown(EKeys::LeftShift) ? 3.f : 1.f);
	if (GridOuter.IsValid()) { GridOuter->Advance(Dt, 64); }   // 바깥 먼저 (안쪽 경계가 이 프레임 끝 값을 쓴다)
	Grid->Advance(Dt, 64);
	UpdateSand();   // M4 T1: 격자 다음 (이번 프레임 바닥 위에서)
	UpdateSwash();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White, FString::Printf(TEXT("파도 %.1fs · Hs %.2fm · 격자 GPU %.2f ms (서브스텝 %d) · 가운데 끌기 둘러보기 · WASDQE (Shift 빠르게)"), SimTime, Hs, Grid->LastGpuMs(), Grid->GetLastSubsteps()));
	}
	if (bAuto && NextShot < ShotTimes.Num() && SimTime >= ShotTimes[NextShot])
	{
		const FString Path = FString::Printf(TEXT("%s_%02.0fs.png"), *ShotPath.LeftChop(4), ShotTimes[NextShot]);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogSandWaves, Display, TEXT("T3 %.1fs: 스크린샷 %s · 격자 GPU %.2f ms · 서브스텝 %d"), SimTime, *Path, Grid->LastGpuMs(), Grid->GetLastSubsteps());
		++NextShot;
	}
	if (bAuto && NextShot >= ShotTimes.Num() && SimTime > ShotTimes.Last() + 1.f) { UE_LOG(LogSandWaves, Display, TEXT("T5 프레임: 평균 %.2f ms (%d 프레임, 10 s 뒤) · 격자 GPU %.2f ms"), 1000.0 * WallSum / FMath::Max(WallN, 1), WallN, Grid->LastGpuMs()); UE_LOG(LogSandWaves, Display, TEXT("T6 도구: 파냄 %.1f L · 쌓음 %.1f L · 손 %.1f L · 부은 물 %.1f L"), DugM3 * 1e3, PiledM3 * 1e3, HandM3 * 1e3f, ToolWaterM3 * 1e3f); FPlatformMisc::RequestExit(false); return false; }   // 마지막 스크린샷이 찍힐 프레임을 남긴다
	return true;
}

// ------------------------------------------------------------------------------------------------
// T3b 측정 — `-run=SpikeSweep -gridwaves -AllowCommandletRendering [장면 옵션] [-wavesecs=1500] [-wavetag=]`
// 화면 없이 GPU 격자를 돌려 0.1 s 마다 CSV: 시각 · 해안선 4 줄 처오름 높이 · 경계 근처 η · u · 포락선 A² (m3-plan T3b, USGS DS 602 방식으로 분석)
int32 RunWaveMeasure(const FString& Params)
{
	FWaveScene Sc;
	BuildWaveScene(*Params, Sc);
	UE_LOG(LogSandWaves, Display, TEXT("%s"), *Sc.Info);
	FSandGridGpu Outer, Grid;
	if (Sc.bNest) { Outer.Init(Sc.GO); Sc.G.NestOuter = &Outer; }
	Grid.Init(Sc.G);
	float Secs = 1500.f; FParse::Value(*Params, TEXT("wavesecs="), Secs);
	TArray<float> Bed0;
	if (Sc.G.bMorph) { Grid.DownloadBed(Bed0); }
	FString Tag = TEXT("run"); FParse::Value(*Params, TEXT("wavetag="), Tag);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("MeshRuns") / FString::Printf(TEXT("waves_%s.csv"), *Tag);
	const int32 NX = Sc.NX, NY = Sc.NY;
	const int32 Rows[4] = { NY / 8, 3 * NY / 8, 5 * NY / 8, 7 * NY / 8 };
	const int32 Jb = NY / 2, Ib = NX - 2;
	FString Csv = FString::Printf(TEXT("# %s\n# beta %.4f deepH0 %.3f Tp %.2f boundary_h0 %.3f\n"), *Sc.Info, Sc.Slope, Sc.DeepH0, Sc.Tp, Sc.H0);
	// -nest: 바깥 격자 경계 근처 (NXo − 12) · 안쪽 경계 위치 수면, 성분 표 (분석에서 묶인 IG 이론값)
	const int32 NXo = Sc.GO.NX, Jo = Sc.bNest ? FMath::Clamp(FMath::FloorToInt((Jb + 0.5f) * Sc.Dx / Sc.GO.Dy), 0, Sc.GO.NY - 1) : 0;
	const int32 Io = Sc.bNest ? FMath::Clamp(FMath::FloorToInt(NX * Sc.Dx / Sc.GO.Dx - 0.5f), 0, NXo - 2) : 0, IoB = FMath::Max(0, NXo - 12);
	const float FoX = Sc.bNest ? NX * Sc.Dx / Sc.GO.Dx - 0.5f - Io : 0.f;
	if (Sc.bNest)
	{
		Csv += FString::Printf(TEXT("# nest y %.3f x_b %.2f h_b %.3f x_ob %.2f\n"), (Jo + 0.5f) * Sc.GO.Dy, (IoB + 0.5f) * Sc.GO.Dx, Sc.GO.BoundaryDepth, NXo * Sc.GO.Dx);
		for (const FVector4f& Wv : Sc.GO.Waves) { Csv += FString::Printf(TEXT("# wave %.6f %.6f %.6f %.6f\n"), Wv.X, Wv.Y, Wv.Z, Wv.W); }
	}
	Csv += Sc.bNest ? TEXT("t,R0,R1,R2,R3,eta_b,u_b,A2,Rlo,Rhi,eta_nest,eta_ob\n") : TEXT("t,R0,R1,R2,R3,eta_b,u_b,A2,Rlo,Rhi\n");
	TArray<double> ProfW, ProfW2, ProfN, ProfG; ProfW.Init(0.0, NX); ProfW2.Init(0.0, NX); ProfN.Init(0.0, NX); ProfG.Init(0.0, NX); double ProfT = 0.0, ProfGn = 0.0;
	TArray<FVector2f> Gw;
	TArray<float> NuO; TArray<double> NuFrac, NuMean; double NuN = 0.0; TArray<float> RowNuO;
	TArray<float> EnS;
	TArray<float> RowTs, RowTsO, WoPrev, HUoPrev, HVoPrev; bool bNanSaved = false;   // _row.bin: 0.1 s 마다 [x = 4i·Dx (i 0..), 가운데 줄 j = NY/2] 수위
	TArray<float> W, HU, HV, Wo, HUo, HVo;
	// 물가 판정 수심 (기본 2cm) — 침투가 없어 되돌림 뒤 얇은 막이 남는다 (m3-log #7). Stockdon 은 비디오로 본 물가
	float RunH = 0.02f; FParse::Value(*Params, TEXT("runh="), RunH);
	float T = 0.f, NextRec = 0.f, FricX = Sc.G.FricX;
	FSwashZone Zone;
	const double T0 = FPlatformTime::Seconds();
	if (FParse::Param(*Params, TEXT("puretime")))
	{
		// 순수 계산 시간 (최적화 기준): 진단 · 상태 다운로드 없이 프레임 (1/60 s) 만
		const int32 Nf = FMath::RoundToInt(Secs * 60.f);
		double GpuO = 0, GpuI = 0, SubO = 0, SubI = 0;
		for (int32 f = 0; f < Nf; ++f)
		{
			if (Sc.bNest) { Outer.Advance(1.f / 60.f, 64); GpuO += Outer.LastGpuMs(); SubO += Outer.GetLastSubsteps(); }
			Grid.Advance(1.f / 60.f, 64); GpuI += Grid.LastGpuMs(); SubI += Grid.GetLastSubsteps();
			FlushRenderingCommands();   // 게임 프레임처럼 렌더 스레드와 한 프레임씩 맞춤 (GPU 는 기다리지 않음 — 서브스텝 수는 비동기 readback, m3-plan p1)
		}
		Grid.DownloadTime(&T);   // 끝에 한 번 동기 (GPU 가 다 끝날 때까지)
		const double Wall = FPlatformTime::Seconds() - T0;
		UE_LOG(LogSandWaves, Display, TEXT("순수 계산: 시뮬 %.1f s · 벽시계 %.1f s · 실시간 배수 %.2f · 프레임당 벽시계 %.2f ms · GPU 안쪽 %.2f ms (서브스텝 %.1f) · 바깥 %.2f ms (서브스텝 %.1f)"),
			T, Wall, T / Wall, 1000.0 * Wall / Nf, GpuI / Nf, SubI / Nf, GpuO / Nf, SubO / Nf);
		return 0;
	}
	while (T < Secs)
	{
		if (Sc.bNest) { Outer.Advance(1.f / 60.f, 64); }
		Grid.Advance(1.f / 60.f, 64);
		Grid.Download(W, HU, HV, &T);   // 동기 — 측정용
		if (T + 1e-4f < NextRec) { continue; }
		NextRec += 0.1f;
		if (Sc.G.bMorph && FMath::FloorToInt(T) != FMath::FloorToInt(T - 0.1f))
		{
			// T4a: 바닥이 움직이면 물가 판정 · 처오름도 지금 바닥으로 (1 s 마다 내려받기)
			TArray<float> Bc; Grid.DownloadBed(Bc);
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const int32 c = j * (NX + 1) + i; Sc.BCellV[j * NX + i] = 0.25f * (Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2]); }
		}
		if (T > 120.f && T - 0.1f <= 120.f) { Grid.ResetBreakDiag(); if (Sc.bNest) { Outer.ResetBreakDiag(); } }   // 판정량 진단은 시작 충격 뒤부터
		if (T > 120.f)
		{
			// 가운데 줄 수위 시계열 (4 칸마다, float32) — 단면 SS · IG 분리 스펙트럼 (D-M3-16). 마른 칸은 바닥값
			for (int32 i = 0; i < NX; i += 4) { RowTs.Add(W[Jb * NX + i]); }
		}
		if (Sc.G.FricX > 0.f) { FricX = Zone.Update(W, Sc.BCellV, NX, NY, Sc.Dx, T); Grid.SetFricX(FricX); }
		// 바다 쪽에서 땅 쪽으로: 처음 3 칸 연속 마른 곳 바로 앞의 젖은 칸 수면 (뒤에 남은 웅덩이는 무시)
		auto Runup = [&](int32 j, float H)
		{
			int32 Last = NX - 1, Dry = 0;
			for (int32 i = NX - 1; i >= 0; --i)
			{
				const int32 k = j * NX + i;
				if (W[k] - Sc.BCellV[k] > H) { Last = i; Dry = 0; } else if (++Dry >= 3) { break; }
			}
			return W[j * NX + Last];
		};
		float R[4], RLo = 0.f, RHi = 0.f;
		for (int32 r = 0; r < 4; ++r) { R[r] = Runup(Rows[r], RunH); RLo += 0.25f * Runup(Rows[r], 0.005f); RHi += 0.25f * Runup(Rows[r], 0.05f); }   // 문턱 민감도 (papers D-M3-11)
		if (T > 120.f)
		{
			// 평균 수위 단면 (y 평균, 젖은 시간만) — 쇄파대 setup 기울기 (D-M3-11)
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const int32 k = j * NX + i; if (W[k] - Sc.BCellV[k] > 0.001f) { ProfW[i] += W[k]; ProfW2[i] += (double)W[k] * W[k]; ProfN[i] += 1.0; } }
			ProfT += NY;
			if (Sc.G.bGround && FMath::FloorToInt(T) != FMath::FloorToInt(T - 0.1f))
			{
				// 지하수면 단면 (1 초마다, y 평균) — c6
				Grid.DownloadGround(Gw);
				for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { ProfG[i] += Gw[j * NX + i].X; }
				ProfGn += NY;
			}
		}
		const int32 kb = Jb * NX + Ib;
		const float Hb = FMath::Max(1e-4f, W[kb] - Sc.BCellV[kb]);
		double Re = 0, Im = 0;
		for (const FVector4f& Wv : (Sc.bNest ? Sc.GO.Waves : Sc.G.Waves)) { const double Ph = Wv.Z + Wv.W * (Jb + 0.5) * Sc.Dx - Wv.Y * T; Re += Wv.X * FMath::Cos(Ph); Im += Wv.X * FMath::Sin(Ph); }
		Csv += FString::Printf(TEXT("%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f"), T, R[0], R[1], R[2], R[3], W[kb], HU[kb] / Hb, Re * Re + Im * Im);
		Csv += FString::Printf(TEXT(",%.4f,%.4f"), RLo, RHi);
		if (Sc.bNest)
		{
			Outer.Download(Wo, HUo, HVo);
			{
				// 진단 (m3-log #21): 바깥 격자에 NaN 이 처음 나오면 직전 상태 (WoPrev) 와 지금을 _nan.bin 으로 저장
				bool bBad = false; for (const float v : Wo) { if (!FMath::IsFinite(v)) { bBad = true; break; } }
				if (bBad && WoPrev.Num() == Wo.Num() && !bNanSaved)
				{
					TArray<float> Dump; Dump.Append(WoPrev); Dump.Append(HUoPrev); Dump.Append(HVoPrev); Dump.Append(Wo);
					FFileHelper::SaveArrayToFile(TArrayView<const uint8>((const uint8*)Dump.GetData(), Dump.Num() * sizeof(float)), *Path.Replace(TEXT(".csv"), TEXT("_nan.bin")));
					UE_LOG(LogSandWaves, Warning, TEXT("바깥 격자 NaN t %.2f s — _nan.bin 저장 (NXo %d NYo %d)"), T, NXo, Sc.GO.NY);
					bNanSaved = true;
				}
				if (!bBad) { WoPrev = Wo; HUoPrev = HUo; HVoPrev = HVo; }
			}
			const float En = FMath::Lerp(Wo[Jo * NXo + Io], Wo[Jo * NXo + Io + 1], FoX);
			Csv += FString::Printf(TEXT(",%.4f,%.4f"), En, Wo[Jo * NXo + IoB]);
			if (T > 120.f) { EnS.Add(En); }   // 안쪽 경계에 실제로 들어오는 바깥 수위 (m3-log #25)
			if (T > 120.f) { for (int32 i = 0; i < NXo; ++i) { RowTsO.Add(Wo[Jo * NXo + i]); } }   // 모든 칸 (m3-log #27: 앞면 폭)
			if (T > 120.f && Sc.GO.BreakMode >= 2)
			{
				// 바깥 격자 쇄파 진단 (D-M3-20): x 별 ν > 0 비율 · 평균 ν (모든 j)
				Outer.DownloadNu(NuO);
				if (NuFrac.Num() != NXo) { NuFrac.Init(0.0, NXo); NuMean.Init(0.0, NXo); }
				for (int32 j = 0; j < Sc.GO.NY; ++j) for (int32 i = 0; i < NXo; ++i) { const float v = NuO[j * NXo + i]; if (v > 0.f) { NuFrac[i] += 1.0; } NuMean[i] += v; }
				NuN += Sc.GO.NY;
				for (int32 i = 0; i < NXo; ++i) { RowNuO.Add(NuO[Jo * NXo + i]); }   // _nurowo.bin: _rowo 와 같은 칸 ν (m3-log #24 고조파 · 점성 영역 진단)
			}   // _rowo.bin: 바깥 격자 줄 (2 칸마다, D-M3-18 진단)
		}
		Csv += TEXT("\n");
		if (FMath::FloorToInt(T / 100.f) != FMath::FloorToInt((T - 0.1f) / 100.f)) { UE_LOG(LogSandWaves, Display, TEXT("T3b %.0fs / %.0fs (벽시계 %.0fs)"), T, Secs, FPlatformTime::Seconds() - T0); }
	}
	FFileHelper::SaveStringToFile(Csv, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveArrayToFile(TArrayView<const uint8>((const uint8*)RowTs.GetData(), RowTs.Num() * sizeof(float)), *Path.Replace(TEXT(".csv"), TEXT("_row.bin")));
	{
		// 쇄파 판정량 진단 (m3-log #23): x 별 — 줄 평균 · 줄 최대 의 칸별 최대 |∇η| 와 최대 η_t/√(gH). 바깥 · 안쪽
		auto DumpDiag = [&](FSandGridGpu& Gd, const FSandGridGpuInit& Pi, const TCHAR* Suffix)
		{
			TArray<FVector2f> Dg; Gd.DownloadBreakDiag(Dg);
			FString Df = TEXT("x,on_mean,on_max,etat_mean,etat_max,bed\n");   // on: ν > 0 누적 시간 (s, 120 s 뒤) · bed: 가운데 줄 바닥 (m3-log #27)
			for (int32 i = 0; i < Pi.NX; ++i)
			{
				double Sm = 0, Sx = 0, Em = 0, Ex = 0;
				for (int32 j = 0; j < Pi.NY; ++j) { const FVector2f v = Dg[j * Pi.NX + i]; Sm += v.X; Em += v.Y; Sx = FMath::Max(Sx, (double)v.X); Ex = FMath::Max(Ex, (double)v.Y); }
				const int32 Jm = Pi.NY / 2, W1 = Pi.NX + 1;
				const float Bd = 0.25f * (Pi.BCorner[Jm * W1 + i] + Pi.BCorner[Jm * W1 + i + 1] + Pi.BCorner[(Jm + 1) * W1 + i] + Pi.BCorner[(Jm + 1) * W1 + i + 1]);
				Df += FString::Printf(TEXT("%.3f,%.4f,%.4f,%.4f,%.4f,%.4f\n"), (i + 0.5f) * Pi.Dx, Sm / Pi.NY, Sx, Em / Pi.NY, Ex, Bd);
			}
			FFileHelper::SaveStringToFile(Df, *Path.Replace(TEXT(".csv"), Suffix), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		};
		DumpDiag(Grid, Sc.G, TEXT("_diag.csv"));
		if (Sc.bNest) { DumpDiag(Outer, Sc.GO, TEXT("_diago.csv")); }
	}
	if (NuN > 0.0)
	{
		FString Nf = TEXT("x,nu_frac,nu_mean\n");
		for (int32 i = 0; i < NuFrac.Num(); ++i) { Nf += FString::Printf(TEXT("%.3f,%.5f,%.5f\n"), (i + 0.5f) * Sc.GO.Dx, NuFrac[i] / NuN, NuMean[i] / NuN); }
		FFileHelper::SaveStringToFile(Nf, *Path.Replace(TEXT(".csv"), TEXT("_nuo.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	if (RowNuO.Num() > 0) { FFileHelper::SaveArrayToFile(TArrayView<const uint8>((const uint8*)RowNuO.GetData(), RowNuO.Num() * sizeof(float)), *Path.Replace(TEXT(".csv"), TEXT("_nurowo.bin"))); }
	if (RowTsO.Num() > 0) { FFileHelper::SaveArrayToFile(TArrayView<const uint8>((const uint8*)RowTsO.GetData(), RowTsO.Num() * sizeof(float)), *Path.Replace(TEXT(".csv"), TEXT("_rowo.bin"))); }
	{
		FString Pf = TEXT("x,B,eta_mean,wet_frac,gw_mean,eta_std\n");   // eta_std: 젖은 시간 수위 표준편차 — H_rms ≈ 2√2·σ (D-M3-16)
		for (int32 i = 0; i < NX; ++i) { Pf += FString::Printf(TEXT("%.3f,%.4f,%.4f,%.4f,%.4f,%.4f\n"), (i + 0.5f) * Sc.Dx, Sc.BCellV[Jb * NX + i], ProfN[i] > 0 ? ProfW[i] / ProfN[i] : 0.0, ProfT > 0 ? ProfN[i] / ProfT : 0.0, ProfGn > 0 ? ProfG[i] / ProfGn : 0.0,
			ProfN[i] > 1 ? FMath::Sqrt(FMath::Max(0.0, ProfW2[i] / ProfN[i] - FMath::Square(ProfW[i] / ProfN[i]))) : 0.0); }
		FFileHelper::SaveStringToFile(Pf, *Path.Replace(TEXT(".csv"), TEXT("_prof.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	if (EnS.Num() > 64)
	{
		// 안쪽 경계 실측 Hm0 (m3-log #25): 전체 · 입사파 대역 (> 0.05 Hz) — 로그의 "경계 Hs" 는 nesting 때 안 쓰는 포화 가정값
		const int32 Nn = EnS.Num(); double Mn = 0; for (const float v : EnS) { Mn += v; } Mn /= Nn;
		double Vt = 0; for (const float v : EnS) { Vt += FMath::Square(v - Mn); } Vt /= Nn;
		const int32 Wn = FMath::RoundToInt(20.0 / 0.1);   // 20 s 이동 평균 = IG (< 0.05 Hz) 근사
		TArray<double> Cs; Cs.SetNumZeroed(Nn + 1); for (int32 k = 0; k < Nn; ++k) { Cs[k + 1] = Cs[k] + EnS[k]; }
		double Vs = 0; int32 Ns = 0;
		for (int32 k = Wn / 2; k < Nn - Wn / 2; ++k) { const double Lo = (Cs[k + Wn / 2] - Cs[k - Wn / 2]) / Wn; Vs += FMath::Square(EnS[k] - Lo); ++Ns; }
		UE_LOG(LogSandWaves, Display, TEXT("안쪽 경계 실측 (수심 %.2f m): Hm0 전체 %.3f m · 입사파 (> 0.05 Hz) %.3f m · Hs/h %.2f"), Sc.H0, 4.0 * FMath::Sqrt(Vt), 4.0 * FMath::Sqrt(Vs / FMath::Max(Ns, 1)), 4.0 * FMath::Sqrt(Vs / FMath::Max(Ns, 1)) / Sc.H0);
	}
	if (Sc.G.bMorph)
	{
		// T4a: 모래 질량 (주기 y 라 꼭짓점 줄 NY 는 줄 0 과 같음 — 뺀다) · 단면 평균 Δb
		TArray<float> Bed1; Grid.DownloadBed(Bed1);
		double S0 = 0, S1 = 0, Abs = 0;
		FString Bf = TEXT("x,b0,b1,db_mean,db_min,db_max\n");
		for (int32 i = 0; i <= NX; ++i)
		{
			double M0 = 0, M1 = 0, Mn = 1e9, Mx = -1e9;
			for (int32 j = 0; j < NY; ++j)
			{
				const int32 c = j * (NX + 1) + i; const double d = Bed1[c] - Bed0[c];
				S0 += Bed0[c]; S1 += Bed1[c]; Abs += FMath::Abs(d); M0 += Bed0[c]; M1 += Bed1[c]; Mn = FMath::Min(Mn, d); Mx = FMath::Max(Mx, d);
			}
			Bf += FString::Printf(TEXT("%.3f,%.5f,%.5f,%.6f,%.6f,%.6f\n"), i * Sc.Dx, M0 / NY, M1 / NY, (M1 - M0) / NY, Mn, Mx);
		}
		FFileHelper::SaveStringToFile(Bf, *Path.Replace(TEXT(".csv"), TEXT("_bed.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (Sc.G.bSus) { TArray<float> Su; Grid.DownloadSus(Su); double Sm = 0, Mx = 0; for (float V : Su) { Sm += V; Mx = FMath::Max<double>(Mx, V); } UE_LOG(LogSandWaves, Display, TEXT("T4b 부유사: 물속 ΣhC %.3e m·칸 · 최대 hC %.3e m"), Sm, Mx); }
		UE_LOG(LogSandWaves, Display, TEXT("T4 모래: Σb 변화 %.3e m·칸 (|Δb| 합 %.3e, 비 %.2e) · 평균 |Δb| %.2f mm"), S1 - S0, Abs, Abs > 0 ? (S1 - S0) / Abs : 0.0, 1000.0 * Abs / ((NX + 1) * NY));
	}
	UE_LOG(LogSandWaves, Display, TEXT("T3b 측정 끝 → %s (벽시계 %.0fs) · swash 마찰 경계 x %.2f m"), *Path, FPlatformTime::Seconds() - T0, FricX);
	return 0;
}
