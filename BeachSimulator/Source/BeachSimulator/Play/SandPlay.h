// M2 T4 — 플레이 맵 (m2-plan.md). `-game -sandplay` 로 띄우면 모래 바닥을 깔고 자유 카메라 · 마우스 브러시로 직접 만진다.
//   좌클릭 파기 · 우클릭 쌓기 (손 재고에서) · F 물붓기 · 휠 반지름 · Shift 세기 ×3
//   가운데 버튼 끌기 = 둘러보기 · WASD QE = 이동 (Shift 빠르게)
//   -sandcount= (기본 20000) · -sandboxhalf= (기본 80) · -sandplayshot= (자동 시험: 정해진 브러시 동작 후 스크린샷 · 종료)
//
// 커서 픽킹 = 입자 높이장 광선 행진 (CPU). 높이장은 몇 프레임마다 위치를 내려받아 다시 짓는다 — 비동기 리드백은 T5 비용 보고 정한다.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "SandPlay.generated.h"

UCLASS()
class USandPlaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void RebuildHeight();
	bool Pick(const FVector& WorldOrigin, const FVector& WorldDir, FVector3f& OutSim) const;
	void UpdateCamera(class APlayerController* PC, float Dt);
	void ApplyBrush(int32 Tool, const FVector3f& At);   // 0 파기 · 1 쌓기 · 2 물붓기

	TUniquePtr<class FSandSpike> Sim;
	UPROPERTY()
	TObjectPtr<class USandSurfaceComponent> SimComp = nullptr;
	UPROPERTY()
	TObjectPtr<class ACameraActor> Cam = nullptr;
	FVector Offset = FVector::ZeroVector;      // 시뮬 좌표 + Offset = 월드

	// 높이장 — 칸마다 입자 윗면 높이 (중심 z + 반지름). 없으면 바닥
	FVector2f HMin = FVector2f::ZeroVector;
	float HCell = 2.f;
	int32 HNX = 0, HNY = 0;
	float Floor = 0.f;
	TArray<float> Height;
	int32 HeightEvery = 4;                     // 동기 모드 (-sandsyncheight) 에서만
	bool bAsyncHeight = true;
	TArray<FVector4f> AsyncPos;

	float BrushR = 10.f;
	float Yaw = 0.f, Pitch = -35.f;
	FVector2D LastMouse = FVector2D::ZeroVector;
	bool bLooking = false;

	struct FCost { double Stage[4] = {}; double Gpu = 0, Height = 0, HeightMax = 0; int32 N = 0, NH = 0; };
	FCost Cost[4];

	// 자동 시험 (-sandplayshot)
	FString ShotPath;
	int32 Frames = 0;
	int32 WarmFrames = 120;
	FTSTicker::FDelegateHandle TickHandle;
};
