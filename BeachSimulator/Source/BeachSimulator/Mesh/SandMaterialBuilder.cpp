// M1 — 모래 재질 에셋을 코드로 만든다 (`-run=SpikeSweep -makesandmat`). system-design §6-2 재질:
//   알베도 · 러프니스 (나중에 f(W)) · 알갱이 디테일 노멀. 서브서피스(Burley)는 젖은 모래 때.
// 텍스처 없이 월드 좌표 노이즈만 쓴다 — 메시에 UV 가 없다 (G2 는 UV 를 0 으로 둔다).
//   색   : 두 모래색을 큰 노이즈(약 20cm)로 섞고, 알갱이 노이즈(약 3mm)로 밝기를 흔든다
//   노멀 : 알갱이 높이 노이즈 → PerturbNormalLQ (화면 미분) → 월드 노멀
//   거칠기 0.9 · 스페큘러 0.35 (마른 모래는 거의 난반사)

#include "SandMaterialBuilder.h"

#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	template <typename T>
	T* AddExpr(UMaterial* M, int32 X, int32 Y)
	{
		T* E = NewObject<T>(M);
		E->MaterialExpressionEditorX = X;
		E->MaterialExpressionEditorY = Y;
		M->GetExpressionCollection().AddExpression(E);
		return E;
	}

	UMaterialExpressionNoise* AddNoise(UMaterial* M, float Scale, int32 Levels, float OutMin, float OutMax, int32 Y)
	{
		UMaterialExpressionNoise* N = AddExpr<UMaterialExpressionNoise>(M, -800, Y);
		N->Scale = Scale;          // 위치(cm) × Scale → 무늬 크기 ≈ 1/Scale cm
		N->Levels = Levels;
		N->OutputMin = OutMin;
		N->OutputMax = OutMax;
		N->bTurbulence = false;
		N->Quality = 1;
		return N;
	}
}

bool SandMaterial::Build(const FString& PackagePath, FString& OutMessage)
{
	// 이미 있으면 완전히 로드하고 옛 재질을 임시 패키지로 치운 뒤 같은 이름으로 다시 만든다 (일부만 로드된 패키지는 저장이 막힌다)
	UPackage* Package = FPackageName::DoesPackageExist(PackagePath) ? LoadPackage(nullptr, *PackagePath, LOAD_None) : CreatePackage(*PackagePath);
	if (!Package) { Package = CreatePackage(*PackagePath); }
	Package->FullyLoad();
	if (UObject* Old = StaticFindObject(UMaterial::StaticClass(), Package, *FPackageName::GetShortName(PackagePath)))
	{
		Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	const FString Name = FPackageName::GetShortName(PackagePath);
	UMaterial* M = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
	UMaterialEditorOnlyData* Ed = M->GetEditorOnlyData();

	// 색: 밝은 모래 · 조금 어두운 모래 (선형 색, sRGB ≈ (173,158,132) / (162,146,121)). 얼룩 대비가 크면 구운 과자처럼 보여 작게 (#16)
	UMaterialExpressionConstant3Vector* Light = AddExpr<UMaterialExpressionConstant3Vector>(M, -500, -300);
	Light->Constant = FLinearColor(0.42f, 0.34f, 0.23f);
	UMaterialExpressionConstant3Vector* Dark = AddExpr<UMaterialExpressionConstant3Vector>(M, -500, -200);
	Dark->Constant = FLinearColor(0.36f, 0.29f, 0.19f);
	UMaterialExpressionNoise* Patch = AddNoise(M, 0.05f, 3, 0.f, 1.f, -250);          // ~20cm 얼룩
	UMaterialExpressionLinearInterpolate* Mix = AddExpr<UMaterialExpressionLinearInterpolate>(M, -300, -250);
	Mix->A.Connect(0, Dark); Mix->B.Connect(0, Light); Mix->Alpha.Connect(0, Patch);
	UMaterialExpressionNoise* Speck = AddNoise(M, 3.f, 2, 0.8f, 1.12f, -100);         // ~3mm 알갱이 밝기
	// 젖음 Wet (0 마름 ~ 1 젖음) — papers L-3 ① (Jensen 1999 · Lekner & Dorf 1988):
	//   알베도 × 0.5 · 거칠기 0.9 → 0.2 · 알갱이 반짝임 사라짐 · 물이 틈을 메워 알갱이 요철 줄어듦 (#22)
	UMaterialExpressionScalarParameter* Wet = AddExpr<UMaterialExpressionScalarParameter>(M, -900, 400);
	Wet->ParameterName = TEXT("Wet");
	Wet->DefaultValue = 0.f;
	UMaterialExpressionConstant* One = AddExpr<UMaterialExpressionConstant>(M, -500, -50);
	One->R = 1.f;
	UMaterialExpressionLinearInterpolate* SpeckW = AddExpr<UMaterialExpressionLinearInterpolate>(M, -300, -100);
	SpeckW->A.Connect(0, Speck); SpeckW->B.Connect(0, One); SpeckW->Alpha.Connect(0, Wet);
	UMaterialExpressionMultiply* ColDry = AddExpr<UMaterialExpressionMultiply>(M, -150, -200);
	ColDry->A.Connect(0, Mix); ColDry->B.Connect(0, SpeckW);
	UMaterialExpressionConstant* Half = AddExpr<UMaterialExpressionConstant>(M, -300, -150);
	Half->R = 0.5f;
	UMaterialExpressionMultiply* ColWet = AddExpr<UMaterialExpressionMultiply>(M, -150, -120);
	ColWet->A.Connect(0, ColDry); ColWet->B.Connect(0, Half);
	UMaterialExpressionLinearInterpolate* Col = AddExpr<UMaterialExpressionLinearInterpolate>(M, -50, -200);
	Col->A.Connect(0, ColDry); Col->B.Connect(0, ColWet); Col->Alpha.Connect(0, Wet);
	Ed->BaseColor.Connect(0, Col);

	// 알갱이 노멀: 높이 노이즈 (cm) → PerturbNormalLQ → 월드 노멀
	UMaterialFunction* Perturb = LoadObject<UMaterialFunction>(nullptr, TEXT("/Engine/Functions/Engine_MaterialFunctions03/Procedurals/PerturbNormalLQ.PerturbNormalLQ"));
	if (!Perturb) { OutMessage = TEXT("PerturbNormalLQ 를 못 찾음"); return false; }
	UMaterialExpressionNoise* GrainH = AddNoise(M, 2.f, 2, 0.f, 0.06f, 100);          // 높이 0~0.6mm, 무늬 ~5mm
	UMaterialExpressionMaterialFunctionCall* Call = AddExpr<UMaterialExpressionMaterialFunctionCall>(M, -300, 100);
	Call->SetMaterialFunction(Perturb);
	if (Call->FunctionInputs.Num() == 0) { OutMessage = TEXT("PerturbNormalLQ 입력 없음"); return false; }
	UMaterialExpressionConstant* Seventy = AddExpr<UMaterialExpressionConstant>(M, -700, 200);
	Seventy->R = 0.3f;
	UMaterialExpressionLinearInterpolate* GrainScale = AddExpr<UMaterialExpressionLinearInterpolate>(M, -550, 200);   // 1 → 0.3
	GrainScale->A.Connect(0, One); GrainScale->B.Connect(0, Seventy); GrainScale->Alpha.Connect(0, Wet);
	UMaterialExpressionMultiply* GrainHW = AddExpr<UMaterialExpressionMultiply>(M, -450, 120);
	GrainHW->A.Connect(0, GrainH); GrainHW->B.Connect(0, GrainScale);
	Call->FunctionInputs[0].Input.Connect(0, GrainHW);
	Ed->Normal.Connect(0, Call);
	M->bTangentSpaceNormal = false;   // PerturbNormalLQ 는 월드 노멀을 낸다

	UMaterialExpressionConstant* RoughDry = AddExpr<UMaterialExpressionConstant>(M, -300, 250);
	RoughDry->R = 0.9f;
	UMaterialExpressionConstant* RoughWet = AddExpr<UMaterialExpressionConstant>(M, -300, 300);
	RoughWet->R = 0.2f;
	UMaterialExpressionLinearInterpolate* Rough = AddExpr<UMaterialExpressionLinearInterpolate>(M, -150, 250);
	Rough->A.Connect(0, RoughDry); Rough->B.Connect(0, RoughWet); Rough->Alpha.Connect(0, Wet);
	Ed->Roughness.Connect(0, Rough);
	UMaterialExpressionConstant* Spec = AddExpr<UMaterialExpressionConstant>(M, -150, 330);
	Spec->R = 0.35f;
	Ed->Specular.Connect(0, Spec);

	M->PreEditChange(nullptr);
	M->PostEditChange();

	const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bOk = UPackage::SavePackage(Package, M, *File, Args);
	OutMessage = FString::Printf(TEXT("%s → %s (%s) · PerturbNormalLQ 입력 %d 개"), *PackagePath, *File, bOk ? TEXT("저장") : TEXT("저장 실패"), Call->FunctionInputs.Num());
	return bOk;
}
#else
bool SandMaterial::Build(const FString&, FString& OutMessage) { OutMessage = TEXT("에디터 빌드에서만"); return false; }
#endif
