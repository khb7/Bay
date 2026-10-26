#include "SandSurfaceComponent.h"

#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshBatch.h"
#include "PrimitiveSceneProxy.h"
#include "RenderingThread.h"
#include "SandGpuMesher.h"
#include "SandGpuSolver.h"
#include "SceneInterface.h"
#include "SceneManagement.h"

namespace
{
	// RDG 풀 버퍼를 정점 · 색인 버퍼로 감싼다 (RHI 버퍼 공유 — 복사 없음)
	class FSandExternalVB : public FVertexBuffer
	{
	public:
		void Set(FRHIBuffer* In) { VertexBufferRHI = In; }
		virtual FString GetFriendlyName() const override { return TEXT("FSandExternalVB"); }
	};

	class FSandExternalIB : public FIndexBuffer
	{
	public:
		void Set(FRHIBuffer* In) { IndexBufferRHI = In; }
		virtual FString GetFriendlyName() const override { return TEXT("FSandExternalIB"); }
	};

	class FSandSurfaceSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		FSandSurfaceSceneProxy(USandSurfaceComponent* Component)
			: FPrimitiveSceneProxy(Component)
			, VertexFactory(GetScene().GetFeatureLevel(), "FSandSurfaceSceneProxy")
			, Material(Component->GetMaterial(0))
		{
			if (!Material) { Material = UMaterial::GetDefaultMaterial(MD_Surface); }
			MaterialRelevance = Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
		}

		virtual ~FSandSurfaceSceneProxy() override
		{
			SandGpuMesh::ReleaseRenderBuffers(Buffers);
			VertexFactory.ReleaseResource();
			PositionVB.ReleaseResource();
			TangentVB.ReleaseResource();
			TexCoordVB.ReleaseResource();
			ColorVB.ReleaseResource();
			IndexIB.ReleaseResource();
		}

		/** 렌더 스레드: 버퍼를 잡고(처음 · 모자랄 때) 메시를 만든다 */
		void Build(FRHICommandListImmediate& RHICmdList, const TArray<FVector4f>& Pos4, const FSandGpuMeshParams& Params)
		{
			Reserve(RHICmdList, Pos4.Num());
			SandGpuMesh::BuildToRenderBuffers(RHICmdList, Pos4, Params, Buffers);
			bReady = true;
		}

		/** 렌더 스레드: G3 — 시뮬 위치 버퍼에서 바로 */
		void BuildFromSim(FRHICommandListImmediate& RHICmdList, const FSandGpuSim& Sim, const FSandGpuMeshParams& Params)
		{
			Reserve(RHICmdList, Sim.NumParticles());
			SandGpuMesh::BuildFromSimToRenderBuffers(RHICmdList, Sim, Params, Buffers);
			bReady = true;
			if (++NumBuilds % 60 == 0) { UE_LOG(LogTemp, Display, TEXT("SandSurface: 색인 %d · 그리는 삼각형 %d · 용량 %d"), Buffers.LastIndexCount, Buffers.DrawPrimitives(), Buffers.MaxIndices / 3); }
		}

		void Reserve(FRHICommandListImmediate& RHICmdList, int32 NumParticles)
		{
			int32 Need = FMath::Max(1 << 16, NumParticles * 16);
			// 실제 색인 수(1~2 프레임 늦게)가 용량의 80% 를 넘으면 두 배로 — 퍼진 더미는 입자당 정점이 는다 (#21)
			if (Buffers.IsValid() && Buffers.LastIndexCount > Buffers.MaxIndices * 4 / 5) { Need = FMath::Max(Need, Buffers.MaxVerts * 2); }
			if (!Buffers.IsValid() || Buffers.MaxVerts < Need)
			{
				SandGpuMesh::AllocateRenderBuffers(Need, Buffers);
				InitVertexFactory(RHICmdList);
			}
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			if (!bReady) { return; }
			const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
			FMaterialRenderProxy* MatProxy = Material->GetRenderProxy();
			if (bWireframe)
			{
				FColoredMaterialRenderProxy* WireProxy = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr, FLinearColor(0.f, 0.5f, 1.f));
				Collector.RegisterOneFrameMaterialProxy(WireProxy);
				MatProxy = WireProxy;
			}
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if (!(VisibilityMap & (1 << ViewIndex))) { continue; }
				FMeshBatch& Mesh = Collector.AllocateMesh();
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MatProxy;
				Mesh.Type = PT_TriangleList;
				Mesh.DepthPriorityGroup = SDPG_World;
				Mesh.bWireframe = bWireframe;
				Mesh.bCanApplyViewModeOverrides = false;
				// 메시 감김은 오른손 기준 바깥 = 반시계 (CPU OBJ 와 같음). UE 는 시계 방향이 앞면이라 뒤집는다
				Mesh.ReverseCulling = !IsLocalToWorldDeterminantNegative();
				Mesh.CastShadow = true;
				FMeshBatchElement& E = Mesh.Elements[0];
				E.IndexBuffer = &IndexIB;
				E.FirstIndex = 0;
				// 간접 인자(NumPrimitives = 0)는 쓰지 않는다 — GPU 장면 인스턴스 컬링 패스(VSM 그림자 등)가 그 인자를 덮어써 0 개를 그린다.
				// 대신 용량 전체를 그리고, 실제 삼각형 뒤 꼬리는 메셔가 퇴화 삼각형(0,0,0)으로 채운다 (IndexTailCS)
				E.NumPrimitives = Buffers.DrawPrimitives();
				E.MinVertexIndex = 0;
				E.MaxVertexIndex = Buffers.MaxVerts - 1;
				E.PrimitiveUniformBuffer = GetUniformBuffer();
				Collector.AddMesh(ViewIndex, Mesh);
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance R;
			R.bDrawRelevance = IsShown(View);
			R.bShadowRelevance = IsShadowCast(View);
			R.bDynamicRelevance = true;
			R.bRenderInMainPass = ShouldRenderInMainPass();
			R.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
			R.bRenderCustomDepth = ShouldRenderCustomDepth();
			MaterialRelevance.SetPrimitiveViewRelevance(R);
			R.bVelocityRelevance = DrawsVelocity() && R.bOpaque && R.bRenderInMainPass;
			return R;
		}

		virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
		virtual SIZE_T GetTypeHash() const override { static size_t UniquePointer; return reinterpret_cast<size_t>(&UniquePointer); }

	private:
		void InitVertexFactory(FRHICommandListBase& RHICmdList)
		{
			VertexFactory.ReleaseResource();
			PositionVB.ReleaseResource(); TangentVB.ReleaseResource(); TexCoordVB.ReleaseResource(); ColorVB.ReleaseResource(); IndexIB.ReleaseResource();
			PositionVB.Set(Buffers.Position->GetRHI()); PositionVB.InitResource(RHICmdList);
			TangentVB.Set(Buffers.Tangent->GetRHI());   TangentVB.InitResource(RHICmdList);
			TexCoordVB.Set(Buffers.TexCoord->GetRHI()); TexCoordVB.InitResource(RHICmdList);
			ColorVB.Set(Buffers.Color->GetRHI());       ColorVB.InitResource(RHICmdList);
			IndexIB.Set(Buffers.Index->GetRHI());       IndexIB.InitResource(RHICmdList);

			FLocalVertexFactory::FDataType Data;
			Data.PositionComponent = FVertexStreamComponent(&PositionVB, 0, sizeof(FVector3f), VET_Float3);
			Data.TangentBasisComponents[0] = FVertexStreamComponent(&TangentVB, 0, 2 * sizeof(uint32), VET_PackedNormal);
			Data.TangentBasisComponents[1] = FVertexStreamComponent(&TangentVB, sizeof(uint32), 2 * sizeof(uint32), VET_PackedNormal);
			Data.TextureCoordinates.Add(FVertexStreamComponent(&TexCoordVB, 0, 0, VET_Float2));   // 보폭 0 — 모든 정점 UV (0, 0)
			Data.NumTexCoords = 1;
			Data.LightMapCoordinateIndex = 0;
			Data.PositionComponentSRV = Buffers.Position->GetOrCreateSRV(RHICmdList, FRHIBufferSRVCreateInfo(PF_R32_FLOAT));
			Data.TangentsSRV = Buffers.Tangent->GetOrCreateSRV(RHICmdList, FRHIBufferSRVCreateInfo(PF_R8G8B8A8_SNORM));
			Data.TextureCoordinatesSRV = Buffers.TexCoord->GetOrCreateSRV(RHICmdList, FRHIBufferSRVCreateInfo(PF_G32R32F));
			// 정점 색 = 정점 W (W2). 재질이 VertexColor.R 을 Wet 에 더한다
			Data.ColorComponent = FVertexStreamComponent(&ColorVB, 0, sizeof(FColor), VET_Color, EVertexStreamUsage::ManualFetch);
			Data.ColorComponentsSRV = Buffers.Color->GetOrCreateSRV(RHICmdList, FRHIBufferSRVCreateInfo(PF_R8G8B8A8));
			Data.ColorIndexMask = ~0u;
			VertexFactory.SetData(RHICmdList, Data);
			VertexFactory.InitResource(RHICmdList);
		}

		FLocalVertexFactory VertexFactory;
		FSandExternalVB PositionVB, TangentVB, TexCoordVB, ColorVB;
		FSandExternalIB IndexIB;
		FSandGpuMeshRenderBuffers Buffers;
		UMaterialInterface* Material = nullptr;
		FMaterialRelevance MaterialRelevance;
		bool bReady = false;
		int32 NumBuilds = 0;
	};
}

USandSurfaceComponent::USandSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CastShadow = true;
	bCastDynamicShadow = true;
	bUseAsOccluder = false;
	Mobility = EComponentMobility::Movable;   // 모래는 매 프레임 바뀐다 — 미리 구운 그림자가 아니라 동적 그림자
}

UMaterialInterface* USandSurfaceComponent::GetMaterial(int32 ElementIndex) const
{
	if (WetMID) { return WetMID.Get(); }
	if (Material) { return Material.Get(); }
	// 비어 있으면 모래 재질 (-makesandmat 이 만든 /Game/Sand/M_Sand), 그것도 없으면 엔진 기본
	static TWeakObjectPtr<UMaterialInterface> Sand;
	if (!Sand.IsValid()) { Sand = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Sand/M_Sand.M_Sand"), nullptr, LOAD_NoWarn | LOAD_Quiet); }
	return Sand.IsValid() ? Sand.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
}

void USandSurfaceComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	OutMaterials.Add(GetMaterial(0));
}

FBoxSphereBounds USandSurfaceComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (SimSource) { return FBoxSphereBounds(FBox(FVector(DomainMin), FVector(DomainMax))).TransformBy(LocalToWorld); }
	if (!ParticleBox.IsValid) { return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(1.f), 1.f); }
	// 메시는 입자 AABB 에서 커널 지지(최대 20cm)까지 나갈 수 있다
	return FBoxSphereBounds(ParticleBox.ExpandBy(25.0)).TransformBy(LocalToWorld);
}

FSandGpuMeshParams USandSurfaceComponent::MakeParams() const
{
	FSandGpuMeshParams P;
	P.Cell = Cell; P.IsoFrac = IsoFrac; P.BandR = BandR; P.SurfDev = SurfDev; P.KnRamp = KnRamp; P.SmoothIters = SmoothIters;
	FVector3f Lo = DomainMin, Hi = DomainMax;
	if (!SimSource)
	{
		// 입자 원천: CPU FSandSurfaceBuilder 와 같은 격자 (원점 = AABB − (지지 + 셀))
		const float Pad = 2.f * P.H * P.MaxStretch + P.Cell;
		Lo = FVector3f(FLT_MAX); Hi = FVector3f(-FLT_MAX);
		for (const FVector3f& X : Particles) { Lo = FVector3f::Min(Lo, X); Hi = FVector3f::Max(Hi, X); }
		Lo -= FVector3f(Pad); Hi += FVector3f(Pad);
	}
	P.GridOrigin = Lo;
	const FVector3f Ext = Hi - Lo;
	P.GridDims = FIntVector(FMath::CeilToInt(Ext.X / P.Cell) + 1, FMath::CeilToInt(Ext.Y / P.Cell) + 1, FMath::CeilToInt(Ext.Z / P.Cell) + 1);
	return P;
}

void USandSurfaceComponent::EnqueueBuild(FPrimitiveSceneProxy* InProxy) const
{
	// 프록시 삭제도 렌더 명령이라 이 명령이 먼저 돈다
	FSandSurfaceSceneProxy* Proxy = static_cast<FSandSurfaceSceneProxy*>(InProxy);
	const FSandGpuMeshParams P = MakeParams();
	if (SimSource)
	{
		const FSandGpuSim* Sim = SimSource;
		ENQUEUE_RENDER_COMMAND(SandSurfaceBuildFromSim)([Proxy, Sim, P](FRHICommandListImmediate& RHICmdList) { Proxy->BuildFromSim(RHICmdList, *Sim, P); });
		return;
	}
	if (Particles.Num() == 0) { return; }
	TArray<FVector4f> Pos4; Pos4.SetNumUninitialized(Particles.Num());
	for (int32 i = 0; i < Particles.Num(); ++i) { Pos4[i] = FVector4f(Particles[i], 0.f); }
	ENQUEUE_RENDER_COMMAND(SandSurfaceBuild)([Proxy, Pos4 = MoveTemp(Pos4), P](FRHICommandListImmediate& RHICmdList) { Proxy->Build(RHICmdList, Pos4, P); });
}

FPrimitiveSceneProxy* USandSurfaceComponent::CreateSceneProxy()
{
	// 프록시는 설정 변경(그림자 방식 등) 때마다 다시 만들어진다 — 그때마다 새 프록시에 메시를 다시 짓는다
	FSandSurfaceSceneProxy* Proxy = new FSandSurfaceSceneProxy(this);
	EnqueueBuild(Proxy);
	return Proxy;
}

void USandSurfaceComponent::SetParticles(const TArray<FVector3f>& Positions)
{
	check(IsInGameThread());
	SimSource = nullptr;
	Particles = Positions;
	ParticleBox = FBox(ForceInit);
	for (const FVector3f& X : Particles) { ParticleBox += FVector(X); }
	UpdateBounds();
	MarkRenderStateDirty();   // 다음 갱신에 프록시를 새로 만들며 메시를 짓는다
}

void USandSurfaceComponent::SetSimSource(const FSandGpuSim* Sim, const FVector3f& InDomainMin, const FVector3f& InDomainMax)
{
	check(IsInGameThread());
	SimSource = Sim;
	DomainMin = InDomainMin; DomainMax = InDomainMax;
	Particles.Reset();
	UpdateBounds();
	MarkRenderStateDirty();
}

void USandSurfaceComponent::UpdateFromSim()
{
	check(IsInGameThread());
	if (SimSource && SceneProxy) { EnqueueBuild(SceneProxy); }   // 프록시는 그대로, 버퍼만 다시 쓴다
}

void USandSurfaceComponent::SetWetness(float InWet)
{
	check(IsInGameThread());
	Wetness = FMath::Clamp(InWet, 0.f, 1.f);
	if (!WetMID)
	{
		UMaterialInterface* Base = Material ? Material.Get() : nullptr;
		if (!Base) { WetMID = nullptr; Base = GetMaterial(0); }
		WetMID = UMaterialInstanceDynamic::Create(Base, this);
		MarkRenderStateDirty();
	}
	WetMID->SetScalarParameterValue(TEXT("Wet"), Wetness);
}
