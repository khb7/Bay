#include "SandSurfaceTest.h"

#include "Camera/CameraActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SandSurfaceComponent.h"
#include "../Spike/SpikeBatch.h"
#include "UnrealClient.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSandSurfaceTest, Log, All);

bool USandSurfaceTestSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return FString(FCommandLine::Get()).Contains(TEXT("-sandsurfacetest"));
}

void USandSurfaceTestSubsystem::OnWorldBeginPlay(UWorld& World)
{
	Super::OnWorldBeginPlay(World);
	if (!World.IsGameWorld()) { return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("sandsim"))) { StartSim(World); return; }
	FString File;
	FParse::Value(FCommandLine::Get(), TEXT("sandsurfacetest="), File);
	if (File.IsEmpty()) { File = FPaths::ProjectSavedDir() / TEXT("MeshRuns/column3000.pos"); }
	ShotPath = FPaths::ProjectSavedDir() / TEXT("MeshRuns/sandsurface.png");
	FParse::Value(FCommandLine::Get(), TEXT("sandshot="), ShotPath);

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() == 0 || Bytes.Num() % sizeof(FVector3f) != 0)
	{
		UE_LOG(LogSandSurfaceTest, Error, TEXT("입자 파일을 못 읽음: %s"), *File);
		FPlatformMisc::RequestExit(false);
		return;
	}
	TArray<FVector3f> Pos; Pos.SetNumUninitialized(Bytes.Num() / sizeof(FVector3f));
	FMemory::Memcpy(Pos.GetData(), Bytes.GetData(), Bytes.Num());
	FBox Box(ForceInit);
	for (const FVector3f& X : Pos) { Box += FVector(X); }
	// 더미 바닥을 z = 0 (바닥 메시 윗면) 위로, 템플릿 맵의 구를 피해 옆으로
	double Lift = 1.0; FParse::Value(FCommandLine::Get(), TEXT("sandlift="), Lift);   // 그림자 확인용으로 띄울 때
	const FVector Shift(600.0 - Box.GetCenter().X, -Box.GetCenter().Y, -Box.Min.Z + Lift);
	for (FVector3f& X : Pos) { X += FVector3f(Shift); }
	Box = Box.ShiftBy(Shift);
	UE_LOG(LogSandSurfaceTest, Display, TEXT("입자 %d 개 · 상자 %s"), Pos.Num(), *Box.ToString());

	AActor* Actor = World.SpawnActor<AActor>();
	USandSurfaceComponent* Comp = NewObject<USandSurfaceComponent>(Actor);
	Actor->SetRootComponent(Comp);
	Comp->RegisterComponent();
	Comp->SetParticles(Pos);

	// 템플릿 맵의 라이트는 정적(구운 그림자)이라 움직이는 물체가 그림자를 못 드리운다 — Movable 방향광을 비스듬히 하나 띄운다
	if (ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-25.0, 120.0, 0.0)))
	{
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sun->GetLightComponent()->SetCastShadows(true);
		Sun->GetLightComponent()->SetIntensity(15.f);
	}

	// -sandref: 비교용 엔진 큐브(50cm, Movable)를 같은 높이 옆에 — 그림자 · 조명 대조
	if (FParse::Param(FCommandLine::Get(), TEXT("sandref")))
	{
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			AActor* Ref = World.SpawnActor<AActor>();
			UStaticMeshComponent* Smc = NewObject<UStaticMeshComponent>(Ref);
			Smc->SetMobility(EComponentMobility::Movable);
			Smc->SetStaticMesh(Cube);
			Ref->SetRootComponent(Smc);
			Smc->RegisterComponent();
			Ref->SetActorLocation(Box.GetCenter() + FVector(0.0, -Box.GetExtent().Y * 2.5, 0.0));
			Ref->SetActorScale3D(FVector(0.5));
		}
	}

	// 비스듬히 위에서 본다
	const FVector Target = Box.GetCenter();
	const double Dist = Box.GetExtent().Size() * 2.4;
	// -sandview=1 이면 반대쪽에서 (그림자 쪽 확인)
	int32 View = 0; FParse::Value(FCommandLine::Get(), TEXT("sandview="), View);
	const FVector Dir = View == 2 ? FVector(1.0, 0.3, 0.12) : (View == 1 ? FVector(0.75, 0.55, 0.55) : FVector(-0.75, -0.55, 0.55));   // 2 = 낮게 (띄운 더미 밑 그림자)
	const FVector Eye = Target + Dir.GetSafeNormal() * Dist;
	ACameraActor* Cam = World.SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(&World, 0)) { PC->SetViewTarget(Cam); }
	if (FParse::Param(FCommandLine::Get(), TEXT("sandwire"))) { GEngine->Exec(&World, TEXT("viewmode wireframe")); }

	Frames = 0;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USandSurfaceTestSubsystem::Tick));
}

void USandSurfaceTestSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (SimComp) { SimComp->SetParticles({}); }   // 시뮬보다 컴포넌트가 오래 살지 않게 원천을 끊는다
	FlushRenderingCommands();
	Sim.Reset();
	Super::Deinitialize();
}

void USandSurfaceTestSubsystem::StartSim(UWorld& World)
{
	// G3: M0-b 기준 프로파일 기둥(사방 벽) → 600 프레임 정지 → 벽 치움. 이후 매 틱 Step 하고 같은 프레임에 메시를 다시 짓는다
	int32 Count = 3000; FParse::Value(FCommandLine::Get(), TEXT("sandcount="), Count);
	FSandParams SP;
	SP.Substeps = 1; SP.WarmStart = 1.f; SP.WarmGapTol = 0.05f; SP.bGpuResident = true;
	ApplySandGpuProfile(TEXT("-gpu2"), SP);
	SP.bGatherApply = false; SP.bMassSplit = false;
	// -sandscene=pour: 상자에 무작위로 부어 다진 블록 (기본은 격자 기둥)
	FString SceneKind = TEXT("lattice"); FParse::Value(FCommandLine::Get(), TEXT("sandscene="), SceneKind);
	Sim = MakeUnique<FSandSpike>();
	if (SceneKind == TEXT("pour"))
	{
		SpikeBatch::PourBoxBlock(*Sim, SP, Count, 38.f);
		for (int32 f = 0; f < 300; ++f) { Sim->Step(1.f / 60.f); }
	}
	else
	{
		FTiltSetup S; S.TargetCount = Count; S.Layers = FMath::Max(4, FMath::RoundToInt(27.f * FMath::Pow(Count / 20000.f, 1.f / 3.f))); S.FixedLayers = 1; S.BaseJitter = 0.3f; S.bOpenDownhill = false;
		Sim->InitTiltBox(SP, S);
		for (int32 f = 0; f < 600; ++f) { Sim->Step(1.f / 60.f); }
	}
	FBox Box(ForceInit);
	for (const FVector3f& X : Sim->GetPositions()) { Box += FVector(X); }
	const float Pad = 2.f * 5.f * 2.f + 1.25f;   // 지지(2h·MaxStretch) + 셀
	const FVector3f DMin = FVector3f(Box.Min) - FVector3f(60.f, 60.f, 0.f) - FVector3f(Pad);
	const FVector3f DMax = FVector3f(Box.Max) + FVector3f(60.f, 60.f, 10.f) + FVector3f(Pad);
	UE_LOG(LogSandSurfaceTest, Display, TEXT("G3 시뮬 — 입자 %d · 정지 상자 %s"), Sim->GetPositions().Num(), *Box.ToString());

	// 시뮬 좌표 그대로 컴포넌트 로컬 — 액터를 옮겨 템플릿 맵의 구를 피하고 바닥(z = 0) 위에
	const FVector Offset(600.0 - Box.GetCenter().X, -Box.GetCenter().Y, -Box.Min.Z + 1.0);
	AActor* Actor = World.SpawnActor<AActor>();
	SimComp = NewObject<USandSurfaceComponent>(Actor);
	Actor->SetRootComponent(SimComp);
	SimComp->RegisterComponent();
	Actor->SetActorLocation(Offset);
	SimComp->SetSimSource(Sim->GetGpuSim(), DMin, DMax);
	Sim->ReleaseWalls();

	if (ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-25.0, 120.0, 0.0)))
	{
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sun->GetLightComponent()->SetCastShadows(true);
		Sun->GetLightComponent()->SetIntensity(15.f);
	}
	const FVector Target = Box.GetCenter() + Offset;
	const double Dist = Box.GetExtent().Size() * 3.2;
	const FVector Eye = Target + FVector(-0.75, -0.55, 0.55).GetSafeNormal() * Dist;
	ACameraActor* Cam = World.SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(&World, 0)) { PC->SetViewTarget(Cam); }

	ShotPath = FPaths::ProjectSavedDir() / TEXT("MeshRuns/sandsim.png");
	FParse::Value(FCommandLine::Get(), TEXT("sandshot="), ShotPath);
	ShotFrames = { 5, 45, 150 };
	Frames = 0;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USandSurfaceTestSubsystem::Tick));
}

bool USandSurfaceTestSubsystem::Tick(float)
{
	++Frames;
	if (Sim.IsValid())
	{
		// 시뮬 한 스텝 → 같은 프레임 메시 (렌더 명령 순서: 시뮬 · 메시 · 이 프레임 그리기)
		Sim->Step(1.f / 60.f);
		SimComp->UpdateFromSim();
		const int32 Shot = ShotFrames.IndexOfByKey(Frames);
		if (Shot != INDEX_NONE)
		{
			const FString Path = FPaths::GetPath(ShotPath) / FString::Printf(TEXT("%s_f%03d.png"), *FPaths::GetBaseFilename(ShotPath), Frames);
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			UE_LOG(LogSandSurfaceTest, Display, TEXT("스크린샷 요청: %s"), *Path);
		}
		if (Frames >= ShotFrames.Last() + 10)
		{
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}

	if (Frames == 90)
	{
		FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
		UE_LOG(LogSandSurfaceTest, Display, TEXT("스크린샷 요청: %s"), *ShotPath);
	}
	if (Frames == 120)
	{
		FPlatformMisc::RequestExit(false);
		return false;
	}
	return true;
}
