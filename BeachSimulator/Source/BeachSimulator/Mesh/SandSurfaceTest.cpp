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
	Super::Deinitialize();
}

bool USandSurfaceTestSubsystem::Tick(float)
{
	++Frames;
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
