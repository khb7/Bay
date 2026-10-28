// M3 T3 — 파도 화면 (m3-plan.md). `-game -sandwaves` 로 띄우면 경사 해변 격자 (GPU 얕은 물) 에 이봉 스펙트럼 파도가 들어온다.
//   해변 = Dean 평형 단면 h = A·x^(2/3) (A 0.1, 모래 0.2mm) + 물 위 1:15 (papers D-M3-6)
//   바다 쪽 경계 = 리만 불변량 입사 · 반사 흡수 (D-M3-5), 경계 유의파고 = 쇄파대 포화 H_rms 0.42·h (Thornton & Guza 1982)
//   스펙트럼 = 풍파 JONSWAP γ 3.3 + 너울 γ 20 (G-6c), 32 성분, 주파수를 칸 안에서 무작위 (반복 방지)
//   가운데 끌기 둘러보기 · WASD QE 이동 · -sandwavesshot= (자동: 스크린샷 몇 장 후 종료)
//   -swellfrac= (0.6) -swelltp= (12) -windtp= (5) -wavehs= (경계 포화값) -wavecells= (512) -manning= (0.025)
//   -wavedir= (평균 방향 10°) -swellspread= (15°) -windspread= (40°) — 방향 분산 (G-6c), 해안선 방향은 주기 경계

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "SandGridGpu.h"   // TUniquePtr 소멸에 완전한 형 (generated.cpp 에서도)
#include "SandWaves.generated.h"

UCLASS()
class USandWavesSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& World) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void UpdateCamera(class APlayerController* PC, float Dt);
	void UpdateWaterMesh();

	TUniquePtr<class FSandGridGpu> Grid;
	UPROPERTY()
	TObjectPtr<class UProceduralMeshComponent> Mesh = nullptr;
	UPROPERTY()
	TObjectPtr<class ACameraActor> Cam = nullptr;

	int32 NX = 0, NY = 0, Step = 2;       // 메시는 Step 칸마다 정점
	float Dx = 0.05f;
	TArray<float> BCorner, BCellV;         // m
	TArray<float> Wet;                     // 정점별 젖음 (물이 닿은 뒤 서서히 마름) — 모래 재질 정점 색 R
	TArray<FVector> WaterVerts, WaterNormals;
	TArray<FColor> WaterColors, SandColors;
	float SimTime = 0.f, Hs = 0.f, H0 = 0.f;
	float Yaw = 0.f, Pitch = -20.f;
	FVector2D LastMouse = FVector2D::ZeroVector;
	bool bLooking = false;

	FString ShotPath;
	int32 Frames = 0;
	int32 LastDiag = -1;
	TArray<float> ShotTimes;
	int32 NextShot = 0;
	FTSTicker::FDelegateHandle TickHandle;
};
