#include "SandPlay.h"

#include "Camera/CameraActor.h"
#include "Components/LightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "../Mesh/SandSurface.h"
#include "../Mesh/SandSurfaceComponent.h"
#include "../Spike/SandSpike.h"
#include "../Spike/SpikeBatch.h"

DEFINE_LOG_CATEGORY_STATIC(LogSandPlay, Log, All);

namespace
{
	enum { ToolDig = 0, ToolPile = 1, ToolPour = 2 };

	void MakeSunMovable(UWorld& World)
	{
		for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
		{
			ULightComponent* L = It->GetLightComponent();
			L->SetMobility(EComponentMobility::Movable);
			L->SetCastShadows(true);
			It->SetActorRotation(FRotator(-35.0, 120.0, 0.0));
			return;
		}
		if (ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-35.0, 120.0, 0.0)))
		{
			Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		}
	}
}

bool USandPlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FString(FCommandLine::Get()).Contains(TEXT("-sandplay"));
}

void USandPlaySubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) { return; }

	// 바닥 = 상자에 부어 다진 넓은 블록. 벽을 치우면 낮게 퍼진 모래 바닥이 된다
	int32 Count = 20000; FParse::Value(FCommandLine::Get(), TEXT("sandcount="), Count);
	float BoxHalf = 80.f; FParse::Value(FCommandLine::Get(), TEXT("sandboxhalf="), BoxHalf);
	FSandParams SP;
	SP.Substeps = 1; SP.WarmStart = 1.f; SP.WarmGapTol = 0.05f; SP.bGpuResident = true;
	ApplySandGpuProfile(TEXT("-gpu2"), SP);
	SP.bGatherApply = false; SP.bMassSplit = false;
	Sim = MakeUnique<FSandSpike>();
	SpikeBatch::PourBoxBlock(*Sim, SP, Count, BoxHalf);
	for (int32 f = 0; f < 300; ++f) { Sim->Step(1.f / 60.f); }
	Sim->ReleaseWalls();

	FBox Box(ForceInit);
	for (const FVector3f& X : Sim->GetPositions()) { if (!SandPool::IsGrave(X)) { Box += FVector(X); } }
	const float R = Sim->GetParams().Radius;
	Floor = (float)Box.Min.Z - R;
	const float Pad = 2.f * 5.f * 2.f + 1.25f;
	const FVector3f DMin = FVector3f(Box.Min) - FVector3f(120.f, 120.f, 0.f) - FVector3f(Pad);
	const FVector3f DMax = FVector3f(Box.Max) + FVector3f(120.f, 120.f, 40.f) + FVector3f(Pad);
	HMin = FVector2f(DMin.X, DMin.Y);
	HNX = FMath::CeilToInt((DMax.X - DMin.X) / HCell);
	HNY = FMath::CeilToInt((DMax.Y - DMin.Y) / HCell);
	UE_LOG(LogSandPlay, Display, TEXT("T4 플레이 — 입자 %d · 반지름 %.2f · 상자 %s · 높이장 %d×%d"), Sim->GetPositions().Num(), R, *Box.ToString(), HNX, HNY);

	Offset = FVector(600.0 - Box.GetCenter().X, -Box.GetCenter().Y, -Floor + 1.0);
	AActor* Actor = World.SpawnActor<AActor>();
	SimComp = NewObject<USandSurfaceComponent>(Actor);
	Actor->SetRootComponent(SimComp);
	SimComp->RegisterComponent();
	Actor->SetActorLocation(Offset);
	SimComp->SetSimSource(Sim->GetGpuSim(), DMin, DMax);
	MakeSunMovable(World);

	const FVector Target = Box.GetCenter() + Offset;
	const FVector Eye = Target + FVector(-0.75, -0.55, 0.6).GetSafeNormal() * (Box.GetExtent().Size2D() * 2.2);
	const FRotator Rot = (Target - Eye).Rotation();
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
	FParse::Value(FCommandLine::Get(), TEXT("sandplayshot="), ShotPath);
	bAsyncHeight = !FParse::Param(FCommandLine::Get(), TEXT("sandsyncheight"));   // 대조: 예전 동기 내려받기
	{ const bool bA = bAsyncHeight; bAsyncHeight = false; RebuildHeight(); bAsyncHeight = bA; }   // 처음 한 번은 동기
	Frames = 0;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USandPlaySubsystem::Tick));
}

void USandPlaySubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (SimComp) { SimComp->SetParticles({}); }
	FlushRenderingCommands();
	Sim.Reset();
	Super::Deinitialize();
}

void USandPlaySubsystem::RebuildHeight()
{
	// 칸마다 입자 윗면 최대. 위치 = 비동기 리드백 결과 (T5 — 동기 내려받기는 4 프레임마다 12ms 였다)
	TArray<FVector3f> P;
	if (bAsyncHeight) { P.Reserve(AsyncPos.Num()); for (const FVector4f& X : AsyncPos) { P.Add(FVector3f(X)); } }
	else { P = Sim->GetPositions(); }
	const float R = Sim->GetParams().Radius;
	Height.Init(Floor, HNX * HNY);
	const int32 Rc = FMath::CeilToInt(R / HCell);
	for (const FVector3f& X : P)
	{
		if (SandPool::IsGrave(X)) { continue; }
		const int32 Cx = FMath::FloorToInt((X.X - HMin.X) / HCell), Cy = FMath::FloorToInt((X.Y - HMin.Y) / HCell);
		// 알갱이 원판 (반지름 R) 이 덮는 칸들 — 가장자리는 구 높이로
		for (int32 dy = -Rc; dy <= Rc; ++dy)
		for (int32 dx = -Rc; dx <= Rc; ++dx)
		{
			const int32 x = Cx + dx, y = Cy + dy;
			if (x < 0 || y < 0 || x >= HNX || y >= HNY) { continue; }
			const float Qx = HMin.X + (x + 0.5f) * HCell - X.X, Qy = HMin.Y + (y + 0.5f) * HCell - X.Y;
			const float D2 = Qx * Qx + Qy * Qy;
			if (D2 > R * R) { continue; }
			float& H = Height[y * HNX + x];
			H = FMath::Max(H, X.Z + FMath::Sqrt(R * R - D2));
		}
	}
}

bool USandPlaySubsystem::Pick(const FVector& WorldOrigin, const FVector& WorldDir, FVector3f& OutSim) const
{
	// 높이장 광선 행진 (반 칸 걸음) → 이분법 다듬기
	const FVector3f O = FVector3f(WorldOrigin - Offset), D = FVector3f(WorldDir.GetSafeNormal());
	auto Below = [&](const FVector3f& X)
	{
		const int32 x = FMath::FloorToInt((X.X - HMin.X) / HCell), y = FMath::FloorToInt((X.Y - HMin.Y) / HCell);
		const float H = (x < 0 || y < 0 || x >= HNX || y >= HNY) ? Floor : Height[y * HNX + x];
		return X.Z <= H;
	};
	const float Step = 0.5f * HCell;
	float T0 = 0.f;
	for (float T = Step; T < 8000.f; T += Step)
	{
		if (Below(O + D * T))
		{
			float A = T0, B = T;
			for (int32 k = 0; k < 8; ++k) { const float M = 0.5f * (A + B); (Below(O + D * M) ? B : A) = M; }
			OutSim = O + D * B;
			return true;
		}
		T0 = T;
	}
	return false;
}

void USandPlaySubsystem::UpdateCamera(APlayerController* PC, float Dt)
{
	// 가운데 버튼 끌기 = 둘러보기, WASD QE = 이동
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
	const float Speed = PC->IsInputKeyDown(EKeys::LeftShift) ? 300.f : 100.f;
	Cam->SetActorLocationAndRotation(Cam->GetActorLocation() + Move.GetSafeNormal() * Speed * Dt, Rot);
}

void USandPlaySubsystem::ApplyBrush(int32 Tool, const FVector3f& At)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	const float Mul = (PC && PC->IsInputKeyDown(EKeys::LeftShift)) ? 3.f : 1.f;
	FSandToolStamp T;
	T.Center = At;
	if (Tool == ToolDig)
	{
		T.Kind = ESandTool::Dig; T.Radius = BrushR; T.Strength = FMath::Min(1.f, 0.15f * Mul);
	}
	else if (Tool == ToolPour)
	{
		T.Kind = ESandTool::Pour; T.Radius = BrushR; T.WetAdd = 0.02f * Mul;
	}
	else
	{
		// 표면 위에 한 번에 조금씩 (T3 의 원기둥이 무너지지 않을 만큼)
		const int32 N = FMath::Min(Sim->GetHandCount(), FMath::RoundToInt(4.f * Mul));
		if (N <= 0) { return; }
		T.Kind = ESandTool::Pile; T.Radius = FMath::Max(2.f * Sim->GetParams().Radius, 0.4f * BrushR); T.Strength = (float)N;
	}
	Sim->QueueStamp(T);
}

bool USandPlaySubsystem::Tick(float DeltaTime)
{
	++Frames;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!Sim.IsValid() || !PC || !Cam) { return true; }
	const bool bAuto = !ShotPath.IsEmpty();
	const float Dt = FMath::Min(DeltaTime, 1.f / 30.f);
	if (!bAuto) { UpdateCamera(PC, Dt); }

	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) { BrushR = FMath::Min(40.f, BrushR * 1.15f); }
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) { BrushR = FMath::Max(3.f, BrushR / 1.15f); }

	// 커서 → 표면
	FVector O, Dir;
	bool bHit = false;
	FVector3f Hit = FVector3f::ZeroVector;
	int32 Tool = -1;
	if (bAuto)
	{
		// 자동 시험: 카메라 앞 표면 세 곳 — 파기 · 물붓기 · (판 모래로) 쌓기
		const int32 F = Frames - WarmFrames;
		const FVector Fwd = Cam->GetActorForwardVector(), Right = Cam->GetActorRightVector();
		FVector Aim = Fwd;
		if (F >= 0 && F < 60) { Tool = ToolDig; Aim = Fwd; }
		else if (F >= 60 && F < 100) { Tool = ToolPour; Aim = (Fwd - 0.18 * Right).GetSafeNormal(); }
		else if (F >= 100 && F < 220) { Tool = ToolPile; Aim = (Fwd + 0.18 * Right).GetSafeNormal(); }
		bHit = Pick(Cam->GetActorLocation(), Aim, Hit);
	}
	else if (PC->DeprojectMousePositionToWorld(O, Dir))
	{
		bHit = Pick(O, Dir, Hit);
		if (PC->IsInputKeyDown(EKeys::LeftMouseButton)) { Tool = ToolDig; }
		else if (PC->IsInputKeyDown(EKeys::RightMouseButton)) { Tool = ToolPile; }
		else if (PC->IsInputKeyDown(EKeys::F)) { Tool = ToolPour; }
	}
	if (bHit && Tool >= 0 && Frames > WarmFrames) { ApplyBrush(Tool, Hit); }

	Sim->Step(1.f / 60.f);
	SimComp->UpdateFromSim();
	double HMs = -1.0;
	if (bAsyncHeight)
	{
		const double T0 = FPlatformTime::Seconds();
		Sim->GetGpuSim()->RequestPositionsAsync();
		if (Sim->GetGpuSim()->TakePositionsAsync(AsyncPos)) { RebuildHeight(); HMs = (FPlatformTime::Seconds() - T0) * 1000.0; }
	}
	else if (Frames % HeightEvery == 0) { const double T0 = FPlatformTime::Seconds(); RebuildHeight(); HMs = (FPlatformTime::Seconds() - T0) * 1000.0; }
	// T5 비용 — 자동 시험 구간별 평균 (도구 -1 쉼 · 0 파기 · 1 쌓기 · 2 물붓기)
	if (bAuto && Frames > WarmFrames + 5)
	{
		FCost& C = Cost[Tool + 1];
		for (int32 k = 0; k < 4; ++k) { C.Stage[k] += Sim->LastStageMs(k); }
		C.Gpu += Sim->LastGpuMs(); ++C.N;
		if (HMs >= 0.0) { C.Height += HMs; ++C.NH; C.HeightMax = FMath::Max(C.HeightMax, HMs); }
	}

	if (bHit)
	{
		const FColor C = Tool == ToolDig ? FColor::Red : Tool == ToolPile ? FColor::Green : Tool == ToolPour ? FColor::Cyan : FColor::White;
		DrawDebugCircle(World, FVector(Hit) + Offset + FVector(0, 0, 0.5), BrushR, 48, C, false, -1.f, 0, 0.3f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White, FString::Printf(TEXT("좌 파기 · 우 쌓기 · F 물붓기 · 휠 반지름 %.0fcm · Shift ×3 · 가운데 끌기 둘러보기 · WASDQE"), BrushR));
		GEngine->AddOnScreenDebugMessage(2, 0.f, FColor::Yellow, FString::Printf(TEXT("손 %d / %d · 산 입자 %d · 합 %d · GPU %.2f ms"),
			Sim->GetHandCount(), Sim->HandCap, Sim->NumAlive(), Sim->NumAlive() + Sim->GetHandCount(), Sim->LastGpuMs()));
	}

	if (bAuto)
	{
		const int32 F = Frames - WarmFrames;
		if (F % 20 == 0 && F >= 0)
		{
			UE_LOG(LogSandPlay, Display, TEXT("T4 f %3d: 도구 %d · 명중 %d (%.1f %.1f %.1f) · 손 %d · 산 %d · 합 %d · 결합 %d"),
				F, Tool, bHit ? 1 : 0, Hit.X, Hit.Y, Hit.Z, Sim->GetHandCount(), Sim->NumAlive(), Sim->NumAlive() + Sim->GetHandCount(), Sim->GetBonds().Num());
		}
		if (F == 280)
		{
			FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
			UE_LOG(LogSandPlay, Display, TEXT("스크린샷 요청: %s"), *ShotPath);
		}
		if (F >= 300)
		{
			static const TCHAR* Names[] = { TEXT("쉼"), TEXT("파기"), TEXT("쌓기"), TEXT("물붓기") };
			for (int32 t = 0; t < 4; ++t)
			{
				const FCost& C = Cost[t];
				if (C.N == 0) { continue; }
				UE_LOG(LogSandPlay, Display, TEXT("T5 %s (%d 프레임): 단계 ms [이웃·도구 %.3f · 워밍 %.3f · 반복 %.3f · 파단 %.3f] · 스텝 벽시계 %.2f · 높이장(내려받기 포함) 평균 %.2f 최대 %.2f ms"),
					Names[t], C.N, C.Stage[0] / C.N, C.Stage[1] / C.N, C.Stage[2] / C.N, C.Stage[3] / C.N, C.Gpu / C.N, C.NH ? C.Height / C.NH : 0.0, C.HeightMax);
			}
			FPlatformMisc::RequestExit(false); return false;
		}
	}
	return true;
}
