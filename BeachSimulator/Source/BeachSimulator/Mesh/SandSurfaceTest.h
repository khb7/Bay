// M1 G2 시험 — `-game -sandsurfacetest=<입자 파일>` 로 띄우면 입자를 읽어 USandSurfaceComponent 로 그리고,
// 카메라를 더미에 맞춰 스크린샷을 찍은 뒤 종료한다 (사람 없이 화면 확인). 입자 파일은 커맨드릿 `-mesh -dumppos` 가 쓴다.
//   -sandshot=<파일>   스크린샷 경로 (기본 Saved/MeshRuns/sandsurface.png)
//   -sandwire          와이어프레임으로

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Spike/SandSpike.h"
#include "SandSurfaceTest.generated.h"

UCLASS()
class USandSurfaceTestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void StartSim(UWorld& World);

	TUniquePtr<class FSandSpike> Sim;          // -sandsim: G3 매 프레임 시뮬 → 메시
	UPROPERTY()
	TObjectPtr<class USandSurfaceComponent> SimComp = nullptr;
	TArray<int32> ShotFrames;
	int32 WarmFrames = 120;                    // 벽을 치우기 전 대기 — 재질 셰이더가 준비될 때까지 (첫 프레임들은 기본 재질로 그려진다)

	FString ShotPath;
	int32 Frames = 0;
	FTSTicker::FDelegateHandle TickHandle;
};
