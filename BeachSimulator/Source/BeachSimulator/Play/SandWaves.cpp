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
	TArray<FVector4f> MakeWaves(double Hs, double SwellFrac, double SwellTp, double WindTp, int32 Seed)
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
			const double Sf = M0 * ((1.0 - SwellFrac) * Jonswap(F[n], WindTp, 3.3) / Mw + SwellFrac * Jonswap(F[n], SwellTp, 20.0) / Ms);
			const double A = FMath::Sqrt(2.0 * Sf * Df);
			W.Add(FVector4f((float)A, (float)(UE_TWO_PI * F[n]), (float)(Rng.FRand() * UE_TWO_PI), 0.f));
		}
		return W;
	}
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
	NX = 512; FParse::Value(Cmd, TEXT("wavecells="), NX);
	NY = NX / 2;
	// 해변 단면 (papers D-M3-6): 물가 x_s 에서 바다 쪽(+x) Dean h = A·(x − x_s)^(2/3), 땅 쪽 1:15
	const double A = 0.1, Xs = 0.45 * NX * Dx, Slope = 1.0 / 15.0;   // 땅 45% — 처오름(Stockdon 어림 ~0.6m)이 뒷벽에 닿지 않게 (m3-log #5)
	BCorner.SetNum((NX + 1) * (NY + 1));
	for (int32 j = 0; j <= NY; ++j)
	for (int32 i = 0; i <= NX; ++i)
	{
		const double x = i * Dx, y = j * Dx;
		double B = x >= Xs ? -A * FMath::Pow(x - Xs, 2.0 / 3.0) : (Xs - x) * Slope;
		B += 0.01 * FMath::Sin(1.3 * y + 0.4 * x) + 0.006 * FMath::Sin(3.1 * y - 1.7 * x);   // 1cm 남짓 굴곡 (완전 평평하면 밋밋)
		BCorner[j * (NX + 1) + i] = (float)B;
	}
	H0 = -BCorner[NX];   // 경계 정지 수심 (j = 0 꼭짓점 근사)
	Hs = (float)(1.416 * 0.42 * H0);   // 쇄파대 포화 H_rms = 0.42·h (Thornton & Guza 1982), Hs ≈ √2·H_rms
	FParse::Value(Cmd, TEXT("wavehs="), Hs);
	double SwellFrac = 0.6, SwellTp = 12.0, WindTp = 5.0;
	FParse::Value(Cmd, TEXT("swellfrac="), SwellFrac);
	FParse::Value(Cmd, TEXT("swelltp="), SwellTp);
	FParse::Value(Cmd, TEXT("windtp="), WindTp);
	float Manning = 0.025f; FParse::Value(Cmd, TEXT("manning="), Manning);

	FSandGridGpuInit G;
	G.NX = NX; G.NY = NY; G.Dx = Dx; G.Dy = Dx; G.Manning = Manning;
	G.bOpenEast = true; G.SeaLevel = 0.f; G.BoundaryDepth = H0;
	G.Waves = MakeWaves(Hs, SwellFrac, SwellTp, WindTp, 1234);
	G.BCorner = BCorner;
	BCellV.SetNum(NX * NY);
	G.W.SetNum(NX * NY); G.HU.Init(0.f, NX * NY); G.HV.Init(0.f, NX * NY);
	for (int32 j = 0; j < NY; ++j)
	for (int32 i = 0; i < NX; ++i)
	{
		const double Bc[4] = { BCorner[j * (NX + 1) + i], BCorner[j * (NX + 1) + i + 1], BCorner[(j + 1) * (NX + 1) + i], BCorner[(j + 1) * (NX + 1) + i + 1] };
		const double Bm = 0.25 * (Bc[0] + Bc[1] + Bc[2] + Bc[3]);
		BCellV[j * NX + i] = (float)Bm;
		G.W[j * NX + i] = (float)(Bm + FSandGridCpu::CellVolume(Bc, 0.0));   // 정지 수면 0 (이산 평형)
	}
	Grid = MakeUnique<FSandGridGpu>();
	Grid->Init(G);
	UE_LOG(LogSandWaves, Display, TEXT("T3 파도 — 격자 %d×%d (칸 %.0fcm, %.1f×%.1fm) · 경계 수심 %.2fm · Hs %.2fm · 너울 %.0f%% Tp %.0fs · 풍파 Tp %.0fs · Manning %.3f"),
		NX, NY, Dx * 100, NX * Dx, NY * Dx, H0, Hs, SwellFrac * 100, SwellTp, WindTp, Manning);

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
