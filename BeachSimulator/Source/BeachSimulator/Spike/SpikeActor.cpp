#include "SpikeActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// /Engine/BasicShapes/Sphere 는 반지름 50uu. 입자 반경에 맞춰 줄인다
	constexpr float EngineSphereRadius = 50.f;

	// 프레임 하나에서 따라잡을 고정 스텝 상한. 못 따라가면 느려질 뿐 발산하지는 않는다
	constexpr int32 MaxStepsPerFrame = 4;
}

ASpikeActor::ASpikeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	RootComponent = Instances;
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Instances->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Instances->SetStaticMesh(SphereMesh.Object);
	}
}

void ASpikeActor::BeginPlay()
{
	Super::BeginPlay();

	IConsoleManager& Console = IConsoleManager::Get();
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("spike.release"), TEXT("가상 벽 제거 — 붕괴 시작"),
		FConsoleCommandDelegate::CreateUObject(this, &ASpikeActor::ReleaseWalls), ECVF_Default));
	ConsoleCommands.Add(Console.RegisterConsoleCommand(
		TEXT("spike.restart"), TEXT("같은 시드로 재시작"),
		FConsoleCommandDelegate::CreateUObject(this, &ASpikeActor::RestartSpike), ECVF_Default));

	ResetSim();
}

void ASpikeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (IConsoleObject* Command : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Command);
	}
	ConsoleCommands.Reset();

	Super::EndPlay(EndPlayReason);
}

void ASpikeActor::ResetSim()
{
	FSandParams P;
	P.Radius     = Radius;
	P.Substeps   = FMath::Max(1, Substeps);
	P.Iterations = FMath::Max(1, Iterations);
	P.Friction   = Friction;
	P.WGlobal    = 0.f;              // 3-0 은 마른 모래. 점착 비활성
	P.Seed       = (uint32)Seed;

	FColumnSetup C;
	C.TargetCount = TargetCount;

	Sim.Init(P, C);

	Accumulator      = 0.f;
	SettledTimer     = 0.f;
	bReportedSettled = false;

	const float Scale = Radius / EngineSphereRadius;
	Instances->ClearInstances();
	TArray<FTransform> Xf;
	Xf.Reserve(Sim.Num());
	for (const FVector3f& P3 : Sim.GetPositions())
	{
		Xf.Add(FTransform(FRotator::ZeroRotator, FVector(P3), FVector(Scale)));
	}
	Instances->AddInstances(Xf, /*bReturnIndices*/ false);

	UE_LOG(LogTemp, Display, TEXT("[Spike] 입자 %d개, 기둥 R=H=%.1fcm, substep %d × 반복 %d, μ=%.2f"),
		Sim.Num(), Sim.GetColumn().Radius, P.Substeps, P.Iterations, P.Friction);
}

void ASpikeActor::ReleaseWalls()
{
	if (Sim.WallsActive())
	{
		Sim.ReleaseWalls();
		UE_LOG(LogTemp, Display, TEXT("[Spike] 벽 제거 — frame %d"), Sim.FrameIndex());
	}
}

void ASpikeActor::RestartSpike()
{
	ResetSim();
}

void ASpikeActor::SyncInstances()
{
	const float Scale = Radius / EngineSphereRadius;
	const TArray<FVector3f>& Positions = Sim.GetPositions();

	TArray<FTransform> Xf;
	Xf.SetNumUninitialized(Positions.Num());
	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		Xf[i] = FTransform(FRotator::ZeroRotator, FVector(Positions[i]), FVector(Scale));
	}
	Instances->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace*/ false,
		/*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}

void ASpikeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 고정 스텝. 프레임률이 결과를 바꾸면 재현성이 없다
	Accumulator += DeltaSeconds;
	int32 Steps = 0;
	while (Accumulator >= FixedDt && Steps < MaxStepsPerFrame)
	{
		Sim.Step(FixedDt);
		Accumulator -= FixedDt;
		++Steps;
	}
	if (Steps == MaxStepsPerFrame)
	{
		Accumulator = 0.f;   // 밀린 시간은 버린다. 따라잡으려다 더 밀리는 것을 막는다
	}

	if (Steps > 0)
	{
		SyncInstances();

		if (Sim.WallsActive())
		{
			if (Sim.IsSettled() && AutoReleaseDelay >= 0.f)
			{
				SettledTimer += FixedDt * Steps;
				if (SettledTimer >= AutoReleaseDelay)
				{
					ReleaseWalls();
				}
			}
		}
		else if (Sim.IsSettled() && !bReportedSettled)
		{
			bReportedSettled = true;

			float PeakZ = 0.f;
			for (const FVector3f& P : Sim.GetPositions())
			{
				PeakZ = FMath::Max(PeakZ, P.Z);
			}
			UE_LOG(LogTemp, Display,
				TEXT("[Spike] 정지 — frame %d, t=%.2fs, 최고점 %.1fcm, 평균 |v| %.3f cm/s"),
				Sim.FrameIndex(), Sim.SimTime(), PeakZ, Sim.AvgSpeed());
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White,
			FString::Printf(TEXT("입자 %d  frame %d  t=%.2fs  step %.1fms"),
				Sim.Num(), Sim.FrameIndex(), Sim.SimTime(), Sim.LastStepMs()));
		GEngine->AddOnScreenDebugMessage(2, 0.f, Sim.WallsActive() ? FColor::Yellow : FColor::Green,
			FString::Printf(TEXT("%s  |  평균 |v| %.2f  최대 %.2f cm/s  %s"),
				Sim.WallsActive() ? TEXT("벽 있음") : TEXT("벽 제거됨"),
				Sim.AvgSpeed(), Sim.MaxSpeed(),
				Sim.IsSettled() ? TEXT("정지") : TEXT("움직임")));
	}
}
