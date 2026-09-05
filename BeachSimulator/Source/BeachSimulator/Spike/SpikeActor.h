// 실시간 뷰. 코어(FSandSpike)를 1× 로 재생하고 구로 그린다.
// 덩어리 붕괴(3-2)는 결국 보고 판단해야 하는 것이라 이 경로가 필요하다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandSpike.h"
#include "SpikeMeasure.h"
#include "SpikeActor.generated.h"

class IConsoleObject;
class UInstancedStaticMeshComponent;

UCLASS()
class BEACHSIMULATOR_API ASpikeActor : public AActor
{
	GENERATED_BODY()

public:
	ASpikeActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 벽 제거 = 붕괴 시작. 기둥이 정지하면 자동으로 걸린다. 콘솔: spike.release */
	UFUNCTION(BlueprintCallable, Category = "Spike")
	void ReleaseWalls();

	/** 같은 시드로 처음부터. 스윕 없이 상수만 바꿔볼 때. 콘솔: spike.restart */
	UFUNCTION(BlueprintCallable, Category = "Spike")
	void RestartSpike();

	UPROPERTY(EditAnywhere, Category = "Spike|Solver")
	float Radius = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Spike|Solver")
	int32 Substeps = 20;

	UPROPERTY(EditAnywhere, Category = "Spike|Solver")
	int32 Iterations = 1;

	UPROPERTY(EditAnywhere, Category = "Spike|Solver")
	float Friction = 0.7f;

	/** Jacobi 평균에 곱하는 완화 계수 ω. 1이 순수 평균, 크면 빨리 수렴하고 거칠어진다. */
	UPROPERTY(EditAnywhere, Category = "Spike|Solver")
	float Relaxation = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Spike|Setup")
	int32 TargetCount = 5000;

	UPROPERTY(EditAnywhere, Category = "Spike|Setup")
	int32 Seed = 1337;

	/** 고정 스텝. 프레임률과 무관하게 1/60로 푼다 — 재현성이 요구사항이다. */
	UPROPERTY(EditAnywhere, Category = "Spike|Setup")
	float FixedDt = 1.f / 60.f;

	/** 기둥이 정지하면 몇 초 뒤에 벽을 뺄지. 0이면 수동. */
	UPROPERTY(EditAnywhere, Category = "Spike|Setup")
	float AutoReleaseDelay = 0.5f;

	/** 정지 시점에 Saved/SpikeRuns/ 로 CSV를 남긴다. */
	UPROPERTY(EditAnywhere, Category = "Spike|Setup")
	bool bWriteCsv = true;

private:
	void ResetSim();
	void SyncInstances();

	UPROPERTY(VisibleAnywhere, Category = "Spike")
	TObjectPtr<UInstancedStaticMeshComponent> Instances;

	FSandSpike     Sim;
	FSpikeRecorder Recorder;
	FAngleStats    LastAngle;

	// 콘솔 명령(spike.release / spike.restart). Exec 함수는 임의 액터로 라우팅되지 않는다
	TArray<IConsoleObject*> ConsoleCommands;

	float Accumulator      = 0.f;
	float SettledTimer     = 0.f;
	bool  bReportedSettled = false;
};
