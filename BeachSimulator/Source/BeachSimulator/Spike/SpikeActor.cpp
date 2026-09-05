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
	P.Relaxation = Relaxation;
	P.WGlobal    = 0.f;              // 3-0 은 마른 모래. 점착 비활성
	P.Seed       = (uint32)Seed;

	FColumnSetup C;
	C.TargetCount = TargetCount;

	Sim.Init(P, C);

	Accumulator      = 0.f;
	SettledTimer     = 0.f;
	bReportedSettled = false;
	LastAngle        = FAngleStats();

	if (bWriteCsv)
	{
		Recorder.Begin(FString::Printf(TEXT("live_mu%.2f_w%.2f_ss%d_it%d"),
			P.Friction, P.Relaxation, P.Substeps, P.Iterations), Sim);
	}

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

	// 측정까지 끝났으면 멈춘다. 결과를 들여다보는 동안 프레임을 잡아먹을 이유가 없다.
	// 다시 보려면 spike.restart
	int32 Steps = 0;
	if (!bReportedSettled)
	{
		// 고정 스텝. 프레임률이 결과를 바꾸면 재현성이 없다
		Accumulator += DeltaSeconds;
		while (Accumulator >= FixedDt && Steps < MaxStepsPerFrame)
		{
			Sim.Step(FixedDt);
			Recorder.RecordFrame(Sim);
			Accumulator -= FixedDt;
			++Steps;
		}
		if (Steps == MaxStepsPerFrame)
		{
			Accumulator = 0.f;   // 밀린 시간은 버린다. 따라잡으려다 더 밀리는 것을 막는다
		}
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

			LastAngle = SpikeMeasure::AngleOfRepose(Sim.GetPositions(), Radius);

			const bool bPass = FMath::Abs(LastAngle.ModeDeg - 34.f) <= 3.f;
			UE_LOG(LogTemp, Display,
				TEXT("[Spike] 정지 — frame %d, t=%.2fs, 최고점 %.1fcm"),
				Sim.FrameIndex(), Sim.SimTime(), SpikeMeasure::PeakHeight(Sim.GetPositions()));
			UE_LOG(LogTemp, Display,
				TEXT("[Spike] 안식각 최빈 %.1f° (중앙 %.1f° 평균 %.1f°) 표면 %d / 표본 %d — %s"),
				LastAngle.ModeDeg, LastAngle.MedianDeg, LastAngle.MeanDeg,
				LastAngle.SurfaceNum, LastAngle.SampledNum,
				bPass ? TEXT("통과 (34±3)") : TEXT("불통과 (34±3)"));

			Recorder.Finish(Sim, LastAngle);
			if (Recorder.IsFinished())
			{
				UE_LOG(LogTemp, Display, TEXT("[Spike] CSV → %s"), *Recorder.GetFilePath());
			}
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White,
			FString::Printf(TEXT("입자 %d  frame %d  t=%.2fs  step %.1fms"),
				Sim.Num(), Sim.FrameIndex(), Sim.SimTime(), Sim.LastStepMs()));
		if (LastAngle.SampledNum > 0)
		{
			const bool bPass = FMath::Abs(LastAngle.ModeDeg - 34.f) <= 3.f;
			GEngine->AddOnScreenDebugMessage(3, 0.f, bPass ? FColor::Green : FColor::Red,
				FString::Printf(TEXT("안식각 최빈 %.1f°  중앙 %.1f°  표본 %d  —  %s"),
					LastAngle.ModeDeg, LastAngle.MedianDeg, LastAngle.SampledNum,
					bPass ? TEXT("통과") : TEXT("불통과 (34±3)")));
		}

		GEngine->AddOnScreenDebugMessage(2, 0.f, Sim.WallsActive() ? FColor::Yellow : FColor::Green,
			FString::Printf(TEXT("%s  |  평균 |v| %.2f  최대 %.2f cm/s  %s"),
				Sim.WallsActive() ? TEXT("벽 있음") : TEXT("벽 제거됨"),
				Sim.AvgSpeed(), Sim.MaxSpeed(),
				Sim.IsSettled() ? TEXT("정지") : TEXT("움직임")));
	}
}
