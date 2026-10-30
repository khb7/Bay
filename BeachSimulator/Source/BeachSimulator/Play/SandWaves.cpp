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
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SandGridGpu.h"
#include "../Grid/SandGridCpu.h"
#include "../Mesh/SandMaterialBuilder.h"

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
	// 해변 단면 (papers D-M3-6): 물가 x_s 에서 바다 쪽(+x) Dean h = A·(x − x_s)^(2/3), 땅 쪽 직선 (기본 1:15)
	double Slope = 1.0 / 15.0; { double Inv = 0; if (FParse::Value(Cmd, TEXT("beachslope="), Inv) && Inv > 0) { Slope = 1.0 / Inv; } }   // -beachslope=10 → 1:10
	const double A = 0.1, Xs = 0.45 * NX * Dx;   // 땅 45% — 처오름이 뒷벽에 닿지 않게 (m3-log #5)
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
	G.FricX = (float)(0.45 * NX * Dx);   // 시작 물가선 — 실행 중 FSwashZone 이 되밀림 한계로 옮긴다
	if (FParse::Param(Cmd, TEXT("onefric"))) { G.ManningSwash = Manning; G.FricX = 0.f; }   // 구역 없이 한 값
	G.bOpenEast = true; G.SeaLevel = 0.f; G.BoundaryDepth = H0;
	// T3c c6 지하수 · 침투 (m3-plan c6): -groundoff 로 끔 · -gwk= (5e-4 m/s)
	G.bGround = !FParse::Param(Cmd, TEXT("groundoff"));   // 기본 켬 (m3-log #14)
	FParse::Value(Cmd, TEXT("gwk="), G.GwK);
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
		O.NX = NXo; O.NY = NYo; O.Dx = (float)NestDx; O.Dy = (float)Dyo; O.Manning = Manning;
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
	Out.Info = FString::Printf(TEXT("T3 파도 — 격자 %d×%d (칸 %.0fcm, %.1f×%.1fm) · 해변 1:%.0f · 경계 수심 %.2fm · 경계 Hs %.2fm · 먼바다 H0 %.2fm · 너울 %.0f%% Tp %.0fs · 풍파 Tp %.0fs · Manning %.3f · IG 계수 %.2f (이득 %.2f)"),
		NX, NY, Dx * 100, NX * Dx, NY * Dx, 1.0 / Slope, H0, Hs, DeepH0, SwellFrac * 100, SwellTp, WindTp, Manning, IgCoef, G.IgGain) + NestInfo
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
	NX = Sc.NX; NY = Sc.NY; BCorner = Sc.BCorner; BCellV = Sc.BCellV; H0 = Sc.H0; Hs = Sc.Hs; bZone = Sc.G.FricX > 0.f;
	FSandGridGpuInit& G = Sc.G;
	const double Xs = Sc.Xs;
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
	SandColors.Init(FColor(0, 0, 0, 255), TV.Num());
	Wet.Init(0.f, TV.Num());
	Mesh->CreateMeshSection(0, TV, Tri, Nrm, UV, SandColors, Tan, false);
	WaterVerts = TV;
	WaterColors.Init(FColor(0, 0, 0, 255), TV.Num());
	Mesh->CreateMeshSection(1, WaterVerts, Tri, Nrm, UV, WaterColors, Tan, false);
	if (UMaterialInterface* MS = LoadObject<UMaterialInterface>(nullptr, *(FString(SandMaterial::DefaultPath()) + TEXT(".M_Sand")))) { Mesh->SetMaterial(0, MS); }
	UMaterialInterface* MW = LoadObject<UMaterialInterface>(nullptr, *(FString(SandMaterial::WaterPath()) + TEXT(".M_Water")));
	if (MW) { Mesh->SetMaterial(1, MW); } else { UE_LOG(LogSandWaves, Warning, TEXT("물 재질 없음 — 커맨드릿 -makewatermat 로 만들 것")); }

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
	FParse::Value(Cmd, TEXT("sandwavesshot="), ShotPath);
	ShotTimes = { 20.f, 35.f, 50.f };
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USandWavesSubsystem::Tick));
}

void USandWavesSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
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

void USandWavesSubsystem::UpdateWaterMesh()
{
	TArray<float> W, HU, HV;
	Grid->Download(W, HU, HV, &SimTime);
	if (bZone) { Grid->SetFricX(Swash.Update(W, BCellV, NX, NY, Dx, SimTime)); }
	const int32 VX = NX / Step + 1, VY = NY / Step + 1;
	const float DryRate = 1.f / 60.f / 20.f;   // 물이 빠지면 20 초에 걸쳐 마른 색으로 (M5 증발 전까지의 눈요기)
	for (int32 vj = 0; vj < VY; ++vj)
	for (int32 vi = 0; vi < VX; ++vi)
	{
		const int32 v = vj * VX + vi;
		const int32 i = FMath::Min(vi * Step, NX - 1), j = FMath::Min(vj * Step, NY - 1);
		const int32 k = j * NX + i;
		const float h = W[k] - BCellV[k];
		const float B = BCorner[FMath::Min(vj * Step, NY) * (NX + 1) + FMath::Min(vi * Step, NX)];
		const bool bWater = h > 0.003f;
		WaterVerts[v].Z = (bWater ? FMath::Max(W[k], B + 0.002f) : B - 0.03f) * 100.0;
		WaterColors[v] = FColor((uint8)FMath::Clamp(h / 0.3f * 255.f, 0.f, 255.f), 0, 0, 255);
		Wet[v] = bWater ? 1.f : FMath::Max(0.f, Wet[v] - DryRate);
		SandColors[v] = FColor((uint8)(Wet[v] * 200.f), 0, 0, 255);
	}
	// 진단 (5초마다): 물 부피 · 평균 수위(경계 근처) · 물이 닿은 가장 땅 쪽 x
	if (FMath::FloorToInt(SimTime / 5.f) != LastDiag)
	{
		LastDiag = FMath::FloorToInt(SimTime / 5.f);
		double V = 0, Lev = 0; int32 Nl = 0; int32 Shore = NX;
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i)
		{
			const int32 k = j * NX + i; const float h = W[k] - BCellV[k];
			V += FMath::Max(0.f, h);
			if (i > NX - 20) { Lev += W[k]; ++Nl; }
			if (h > 0.003f) { Shore = FMath::Min(Shore, i); }
		}
		UE_LOG(LogSandWaves, Display, TEXT("T3 진단 %.0fs: 물 부피 %.2f m³ · 경계 근처 평균 수위 %+.3f m · 물 닿은 땅 쪽 끝 x %.2f m (정지 물가 %.2f)"),
			SimTime, V * Dx * Dx, Lev / FMath::Max(1, Nl), Shore * Dx, 0.45 * NX * Dx);
	}
	// 수면 법선 — 파도 모양에 빛이 맞게 (지형 법선을 그대로 쓰면 파봉이 안 보인다)
	WaterNormals.SetNum(WaterVerts.Num());
	for (int32 vj = 0; vj < VY; ++vj)
	for (int32 vi = 0; vi < VX; ++vi)
	{
		const double zx = WaterVerts[vj * VX + FMath::Min(vi + 1, VX - 1)].Z - WaterVerts[vj * VX + FMath::Max(vi - 1, 0)].Z;
		const double zy = WaterVerts[FMath::Min(vj + 1, VY - 1) * VX + vi].Z - WaterVerts[FMath::Max(vj - 1, 0) * VX + vi].Z;
		WaterNormals[vj * VX + vi] = FVector(-zx, -zy, 2.0 * Step * Dx * 100.0).GetSafeNormal();
	}
	Mesh->UpdateMeshSection(1, WaterVerts, WaterNormals, TArray<FVector2D>(), WaterColors, TArray<FProcMeshTangent>());
	Mesh->UpdateMeshSection(0, TArray<FVector>(), TArray<FVector>(), TArray<FVector2D>(), SandColors, TArray<FProcMeshTangent>());
}

bool USandWavesSubsystem::Tick(float DeltaTime)
{
	++Frames;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!Grid.IsValid() || !PC || !Cam) { return true; }
	const bool bAuto = !ShotPath.IsEmpty();
	const float Dt = bAuto ? 1.f / 60.f : FMath::Min(DeltaTime, 1.f / 30.f);   // 자동 시험은 고정 (프레임 속도와 무관하게 같은 장면)
	if (!bAuto) { UpdateCamera(PC, Dt); }
	if (GridOuter.IsValid()) { GridOuter->Advance(Dt, 64); }   // 바깥 먼저 (안쪽 경계가 이 프레임 끝 값을 쓴다)
	Grid->Advance(Dt, 64);
	UpdateWaterMesh();
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
	if (bAuto && NextShot >= ShotTimes.Num() && SimTime > ShotTimes.Last() + 1.f) { FPlatformMisc::RequestExit(false); return false; }   // 마지막 스크린샷이 찍힐 프레임을 남긴다
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
	TArray<double> ProfW, ProfN, ProfG; ProfW.Init(0.0, NX); ProfN.Init(0.0, NX); ProfG.Init(0.0, NX); double ProfT = 0.0, ProfGn = 0.0;
	TArray<FVector2f> Gw;
	TArray<float> W, HU, HV, Wo, HUo, HVo;
	// 물가 판정 수심 (기본 2cm) — 침투가 없어 되돌림 뒤 얇은 막이 남는다 (m3-log #7). Stockdon 은 비디오로 본 물가
	float RunH = 0.02f; FParse::Value(*Params, TEXT("runh="), RunH);
	float T = 0.f, NextRec = 0.f, FricX = Sc.G.FricX;
	FSwashZone Zone;
	const double T0 = FPlatformTime::Seconds();
	while (T < Secs)
	{
		if (Sc.bNest) { Outer.Advance(1.f / 60.f, 64); }
		Grid.Advance(1.f / 60.f, 64);
		Grid.Download(W, HU, HV, &T);   // 동기 — 측정용
		if (T + 1e-4f < NextRec) { continue; }
		NextRec += 0.1f;
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
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const int32 k = j * NX + i; if (W[k] - Sc.BCellV[k] > 0.001f) { ProfW[i] += W[k]; ProfN[i] += 1.0; } }
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
			const float En = FMath::Lerp(Wo[Jo * NXo + Io], Wo[Jo * NXo + Io + 1], FoX);
			Csv += FString::Printf(TEXT(",%.4f,%.4f"), En, Wo[Jo * NXo + IoB]);
		}
		Csv += TEXT("\n");
		if (FMath::FloorToInt(T / 100.f) != FMath::FloorToInt((T - 0.1f) / 100.f)) { UE_LOG(LogSandWaves, Display, TEXT("T3b %.0fs / %.0fs (벽시계 %.0fs)"), T, Secs, FPlatformTime::Seconds() - T0); }
	}
	FFileHelper::SaveStringToFile(Csv, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	{
		FString Pf = TEXT("x,B,eta_mean,wet_frac,gw_mean\n");
		for (int32 i = 0; i < NX; ++i) { Pf += FString::Printf(TEXT("%.3f,%.4f,%.4f,%.4f,%.4f\n"), (i + 0.5f) * Sc.Dx, Sc.BCellV[Jb * NX + i], ProfN[i] > 0 ? ProfW[i] / ProfN[i] : 0.0, ProfT > 0 ? ProfN[i] / ProfT : 0.0, ProfGn > 0 ? ProfG[i] / ProfGn : 0.0); }
		FFileHelper::SaveStringToFile(Pf, *Path.Replace(TEXT(".csv"), TEXT("_prof.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	UE_LOG(LogSandWaves, Display, TEXT("T3b 측정 끝 → %s (벽시계 %.0fs) · swash 마찰 경계 x %.2f m"), *Path, FPlatformTime::Seconds() - T0, FricX);
	return 0;
}
