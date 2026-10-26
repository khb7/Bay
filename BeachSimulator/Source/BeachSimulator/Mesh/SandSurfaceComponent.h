// M1 G2 — GPU 메시화 결과를 화면에 그리는 컴포넌트 (m1-plan-gpu.md §2).
// 입자 위치를 받으면 렌더 스레드에서 SandGpuMesh 가 프록시의 버퍼(위치 · 접선 · 색인 · 간접 인자)에 직접 메시를 쓰고,
// 프록시는 FLocalVertexFactory 로 용량 전체를 그린다 (꼬리는 퇴화 삼각형 — 간접 그리기는 VSM 그림자에서 0 개로 덮어써진다, m1-log #13). CPU 로 메시가 오가지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SandSurfaceComponent.generated.h"

class UMaterialInterface;

UCLASS(ClassGroup = (Sand), meta = (BlueprintSpawnableComponent))
class USandSurfaceComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	USandSurfaceComponent();

	/** 입자 위치(월드 = 컴포넌트 로컬, 컴포넌트는 원점에 둔다)로 메시를 다시 만든다. 게임 스레드 */
	void SetParticles(const TArray<FVector3f>& Positions);

	UPROPERTY(EditAnywhere, Category = "Sand")
	TObjectPtr<UMaterialInterface> Material;

	// 메시 매개변수 — M1 품질 기준 (이방성 · KnRamp 8 · IsoFrac 0.25 · 셀 1.25 · 띠 7.5 · SurfDev 0.15)
	float Cell = 1.25f, IsoFrac = 0.25f, BandR = 7.5f, SurfDev = 0.15f;
	int32 KnRamp = 8;

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;

private:
	TArray<FVector3f> Particles;
	FBox ParticleBox = FBox(ForceInit);
};
