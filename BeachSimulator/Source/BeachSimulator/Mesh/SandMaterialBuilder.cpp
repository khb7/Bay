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
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionPreviousFrameSwitch.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionActorPositionWS.h"
#include "Engine/Texture.h"
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

	UMaterialExpressionTextureObjectParameter* AddGridTex(UMaterial* M)
	{
		UMaterialExpressionTextureObjectParameter* T = AddExpr<UMaterialExpressionTextureObjectParameter>(M, -1500, 900);
		T->ParameterName = TEXT("GridTex");
		T->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		return T;
	}
	// 격자 텍스처 (w, h, b, 젖음) 를 읽는 Custom. 입력 Tex · Uv 는 공통, Extra 는 이름 있는 추가 입력
	UMaterialExpressionCustom* AddGridCustom(UMaterial* M, UMaterialExpression* Tex, ECustomMaterialOutputType Type, const TCHAR* Code, int32 Y)
	{
		UMaterialExpressionTextureCoordinate* Uv = AddExpr<UMaterialExpressionTextureCoordinate>(M, -1300, Y);
		UMaterialExpressionCustom* C = AddExpr<UMaterialExpressionCustom>(M, -1100, Y);
		C->OutputType = Type;
		C->Code = Code;
		C->Inputs.SetNum(2);
		C->Inputs[0].InputName = TEXT("Tex"); C->Inputs[0].Input.Connect(0, Tex);
		C->Inputs[1].InputName = TEXT("Uv");  C->Inputs[1].Input.Connect(0, Uv);
		return C;
	}
	// WPO: 정점을 격자 높이 (m → cm, 액터 기준) 로. Code 는 float Z (m) 를 return 하는 대신 Z 변수를 채운다
	void ConnectGridWpo(UMaterial* M, UMaterialExpression* Tex, const FString& ZCode, FExpressionInput& Out)
	{
		UMaterialExpressionCustom* C = AddGridCustom(M, Tex, CMOT_Float3, *(TEXT("const float4 t = Tex.SampleLevel(TexSampler, Uv, 0);\n") + ZCode + TEXT("\nreturn float3(0, 0, Z * 100.0 + Ap.z - Wp.z);")), 1000);
		UMaterialExpressionWorldPosition* Wp = AddExpr<UMaterialExpressionWorldPosition>(M, -1300, 1100);
		UMaterialExpressionActorPositionWS* Ap = AddExpr<UMaterialExpressionActorPositionWS>(M, -1300, 1200);
		C->Inputs.SetNum(4);
		C->Inputs[2].InputName = TEXT("Wp"); C->Inputs[2].Input.Connect(0, Wp);
		C->Inputs[3].InputName = TEXT("Ap"); C->Inputs[3].Input.Connect(0, Ap);
		Out.Connect(0, C);
	}
}

bool SandMaterial::Build(const FString& PackagePath, FString& OutMessage, bool bGrid)
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
	// 입자별 W 는 정점 색 R 로 온다 (M1 W2) — 전역 Wet 파라미터와 더한다
	UMaterialExpressionScalarParameter* WetParam = AddExpr<UMaterialExpressionScalarParameter>(M, -1100, 400);
	WetParam->ParameterName = TEXT("Wet");
	WetParam->DefaultValue = 0.f;
	UMaterialExpressionVertexColor* VCol = AddExpr<UMaterialExpressionVertexColor>(M, -1100, 500);
	UMaterialExpressionAdd* WetSum = AddExpr<UMaterialExpressionAdd>(M, -1000, 450);
	WetSum->A.Connect(0, WetParam); WetSum->B.Connect(1, VCol);   // 출력 1 = R
	UMaterialExpressionTextureObjectParameter* GridTex = bGrid ? AddGridTex(M) : nullptr;
	if (bGrid) { WetSum->B.Connect(0, AddGridCustom(M, GridTex, CMOT_Float1, TEXT("return Tex.SampleLevel(TexSampler, Uv, 0).a;"), 500)); }   // T5: 젖음은 격자 텍스처 A
	UMaterialExpressionSaturate* Wet = AddExpr<UMaterialExpressionSaturate>(M, -900, 400);
	Wet->Input.Connect(0, WetSum);
	// 젖음 곡선 Wc — 색 (#33). WetModel 0 이면 예전처럼 선형 W
	//   Wc = (1 − e^(−6W)) / (1 − e^(−6)) — 해변 모래는 조금 젖을 때 급변 후 평탄 (Nolet 2014, papers L-3 ①-a)
	UMaterialExpressionScalarParameter* WetModel = AddExpr<UMaterialExpressionScalarParameter>(M, -1100, 300);
	WetModel->ParameterName = TEXT("WetModel");
	WetModel->DefaultValue = 1.f;
	UMaterialExpressionCustom* WetC = AddExpr<UMaterialExpressionCustom>(M, -800, 400);
	WetC->OutputType = CMOT_Float1;
	WetC->Code = TEXT("return Model < 0.5 ? W : (1 - exp(-6 * W)) / (1 - exp(-6.0));");
	WetC->Inputs.SetNum(2);
	WetC->Inputs[0].InputName = TEXT("W");     WetC->Inputs[0].Input.Connect(0, Wet);
	WetC->Inputs[1].InputName = TEXT("Model"); WetC->Inputs[1].Input.Connect(0, WetModel);
	// 광택 곡선 Wg — 러프니스 · 반짝임 · 알갱이 요철 (#35). 물이 빠진 모래(pendular · funicular)는 어둡지만 무광,
	//   포화 근처에서 틈이 표면까지 차 물막이 생길 때만 반들거린다 (Jensen 1999 두 효과 분리, papers L-3 ①-a). 문턱 0.8 은 화면으로 정한 값
	UMaterialExpressionCustom* WetG = AddExpr<UMaterialExpressionCustom>(M, -800, 480);
	WetG->OutputType = CMOT_Float1;
	WetG->Code = TEXT("return Model < 0.5 ? W : smoothstep(0.8, 1.0, W);");
	WetG->Inputs.SetNum(2);
	WetG->Inputs[0].InputName = TEXT("W");     WetG->Inputs[0].Input.Connect(0, Wet);
	WetG->Inputs[1].InputName = TEXT("Model"); WetG->Inputs[1].Input.Connect(0, WetModel);
	UMaterialExpressionConstant* One = AddExpr<UMaterialExpressionConstant>(M, -500, -50);
	One->R = 1.f;
	UMaterialExpressionLinearInterpolate* SpeckW = AddExpr<UMaterialExpressionLinearInterpolate>(M, -300, -100);
	SpeckW->A.Connect(0, Speck); SpeckW->B.Connect(0, One); SpeckW->Alpha.Connect(0, WetG);
	UMaterialExpressionMultiply* ColDry = AddExpr<UMaterialExpressionMultiply>(M, -150, -200);
	ColDry->A.Connect(0, Mix); ColDry->B.Connect(0, SpeckW);
	// 젖은 색 (papers L-3 ①-a). WetModel 0 = 예전 (선형 W · 알베도 × 0.5), 1 = 채널별 Ångström 식 + 빨리 포화하는 W 곡선
	//   A_wet = (1 − r_e)(1 − x)·A / (1 − A·x), r_e 0.04 · x 0.6 (n 1.33 물막 안 전반사) — 어두운 채널이 더 어두워져 채도가 오른다
	//   곡선 Wc = (1 − e^(−6W)) / (1 − e^(−6)) — 해변 모래는 조금 젖을 때 급락 후 평탄 (Nolet 2014)
	UMaterialExpressionCustom* WetCol = AddExpr<UMaterialExpressionCustom>(M, -150, -120);
	WetCol->OutputType = CMOT_Float3;
	WetCol->Code = TEXT(
		"if (Model < 0.5) return lerp(A, A * 0.5, W);\n"
		"const float X = 0.6;\n"
		"const float3 Aw = (1 - 0.04) * (1 - X) * A / (1 - A * X);\n"
		"return lerp(A, Aw, W);");
	WetCol->Inputs.SetNum(3);
	WetCol->Inputs[0].InputName = TEXT("A");     WetCol->Inputs[0].Input.Connect(0, ColDry);
	WetCol->Inputs[1].InputName = TEXT("W");     WetCol->Inputs[1].Input.Connect(0, WetC);
	WetCol->Inputs[2].InputName = TEXT("Model"); WetCol->Inputs[2].Input.Connect(0, WetModel);
	Ed->BaseColor.Connect(0, WetCol);

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
	GrainScale->A.Connect(0, One); GrainScale->B.Connect(0, Seventy); GrainScale->Alpha.Connect(0, WetG);
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
	Rough->A.Connect(0, RoughDry); Rough->B.Connect(0, RoughWet); Rough->Alpha.Connect(0, WetG);
	Ed->Roughness.Connect(0, Rough);

	// 모션 벡터 (m1-plan-render R1): 정점이 버퍼 안에서 움직이므로 엔진은 정지로 본다.
	// UV0 = (dx, dy), UV1.x = dz = 이번 프레임 변위 → 지난 프레임 위치 = 지금 − d. 이번 프레임 WPO 는 0
	UMaterialExpressionTextureCoordinate* Uv0 = AddExpr<UMaterialExpressionTextureCoordinate>(M, -900, 700);
	Uv0->CoordinateIndex = 0;
	UMaterialExpressionTextureCoordinate* Uv1 = AddExpr<UMaterialExpressionTextureCoordinate>(M, -900, 800);
	Uv1->CoordinateIndex = 1;
	UMaterialExpressionComponentMask* Dz = AddExpr<UMaterialExpressionComponentMask>(M, -750, 800);
	Dz->R = true; Dz->G = false; Dz->B = false; Dz->A = false;
	Dz->Input.Connect(0, Uv1);
	UMaterialExpressionAppendVector* D3 = AddExpr<UMaterialExpressionAppendVector>(M, -600, 750);
	D3->A.Connect(0, Uv0); D3->B.Connect(0, Dz);
	UMaterialExpressionConstant* MinusOne = AddExpr<UMaterialExpressionConstant>(M, -600, 850);
	MinusOne->R = -1.f;
	UMaterialExpressionMultiply* Back = AddExpr<UMaterialExpressionMultiply>(M, -450, 780);
	Back->A.Connect(0, D3); Back->B.Connect(0, MinusOne);
	UMaterialExpressionConstant3Vector* Zero = AddExpr<UMaterialExpressionConstant3Vector>(M, -450, 680);
	Zero->Constant = FLinearColor(0.f, 0.f, 0.f);
	UMaterialExpressionPreviousFrameSwitch* Prev = AddExpr<UMaterialExpressionPreviousFrameSwitch>(M, -300, 720);
	Prev->CurrentFrame.Connect(0, Zero); Prev->PreviousFrame.Connect(0, Back);
	if (bGrid) { ConnectGridWpo(M, GridTex, TEXT("const float Z = t.z;"), Ed->WorldPositionOffset); }
	else { Ed->WorldPositionOffset.Connect(0, Prev); }
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
bool SandMaterial::BuildWater(const FString& PackagePath, FString& OutMessage)
{
	// 얕은 물: 반투명 · 표면 앞 셰이딩 (반사 · 하이라이트). 얕은 곳은 거의 투명, 깊을수록 청록 — 정점 색 R = saturate(h / 0.3m)
	UPackage* Package = FPackageName::DoesPackageExist(PackagePath) ? LoadPackage(nullptr, *PackagePath, LOAD_None) : CreatePackage(*PackagePath);
	if (!Package) { Package = CreatePackage(*PackagePath); }
	Package->FullyLoad();
	if (UObject* Old = StaticFindObject(UMaterial::StaticClass(), Package, *FPackageName::GetShortName(PackagePath)))
	{
		Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	UMaterial* M = NewObject<UMaterial>(Package, *FPackageName::GetShortName(PackagePath), RF_Public | RF_Standalone);
	UMaterialEditorOnlyData* Ed = M->GetEditorOnlyData();
	M->BlendMode = BLEND_Translucent;
	M->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
	// T5: 모든 값은 격자 텍스처 (w, h, b, 젖음) 에서 — 정점 색 대신 깊이 h/0.3 m · 수면 법선은 w 의 중앙 차분
	UMaterialExpressionTextureObjectParameter* GridTex = AddGridTex(M);
	UMaterialExpressionCustom* VC = AddGridCustom(M, GridTex, CMOT_Float1, TEXT("return saturate(Tex.SampleLevel(TexSampler, Uv, 0).y / 0.3);"), 0);
	UMaterialExpressionScalarParameter* CellCm = AddExpr<UMaterialExpressionScalarParameter>(M, -1300, 300);
	CellCm->ParameterName = TEXT("CellCm"); CellCm->DefaultValue = 5.f;
	UMaterialExpressionCustom* Nrm = AddGridCustom(M, GridTex, CMOT_Float3, TEXT(
		"float Wd, Ht; Tex.GetDimensions(Wd, Ht);\n"
		"const float2 d = 1.0 / float2(Wd, Ht);\n"
		"const float zx = Tex.SampleLevel(TexSampler, Uv + float2(d.x, 0), 0).x - Tex.SampleLevel(TexSampler, Uv - float2(d.x, 0), 0).x;\n"
		"const float zy = Tex.SampleLevel(TexSampler, Uv + float2(0, d.y), 0).x - Tex.SampleLevel(TexSampler, Uv - float2(0, d.y), 0).x;\n"
		"return normalize(float3(-zx * 100.0, -zy * 100.0, 2.0 * Cell));"), 300);
	Nrm->Inputs.SetNum(3);
	Nrm->Inputs[2].InputName = TEXT("Cell"); Nrm->Inputs[2].Input.Connect(0, CellCm);
	Ed->Normal.Connect(0, Nrm);
	M->bTangentSpaceNormal = false;
	ConnectGridWpo(M, GridTex, TEXT("const float Z = t.y > 0.003 ? max(t.x, t.z + 0.002) : t.z - 0.03;"), Ed->WorldPositionOffset);
	UMaterialExpressionConstant3Vector* Shallow = AddExpr<UMaterialExpressionConstant3Vector>(M, -500, -200);
	Shallow->Constant = FLinearColor(0.20f, 0.26f, 0.22f);
	UMaterialExpressionConstant3Vector* Deep = AddExpr<UMaterialExpressionConstant3Vector>(M, -500, -100);
	Deep->Constant = FLinearColor(0.01f, 0.06f, 0.07f);
	UMaterialExpressionLinearInterpolate* Col = AddExpr<UMaterialExpressionLinearInterpolate>(M, -300, -150);
	Col->A.Connect(0, Shallow); Col->B.Connect(0, Deep); Col->Alpha.Connect(0, VC);
	Ed->BaseColor.Connect(0, Col);
	UMaterialExpressionConstant* OpLo = AddExpr<UMaterialExpressionConstant>(M, -500, 100); OpLo->R = 0.25f;
	UMaterialExpressionConstant* OpHi = AddExpr<UMaterialExpressionConstant>(M, -500, 160); OpHi->R = 0.85f;
	UMaterialExpressionLinearInterpolate* Op = AddExpr<UMaterialExpressionLinearInterpolate>(M, -300, 120);
	Op->A.Connect(0, OpLo); Op->B.Connect(0, OpHi); Op->Alpha.Connect(0, VC);
	Ed->Opacity.Connect(0, Op);
	UMaterialExpressionConstant* Rough = AddExpr<UMaterialExpressionConstant>(M, -300, 250); Rough->R = 0.06f;
	Ed->Roughness.Connect(0, Rough);
	UMaterialExpressionConstant* Spec = AddExpr<UMaterialExpressionConstant>(M, -300, 320); Spec->R = 0.6f;
	Ed->Specular.Connect(0, Spec);
	M->PreEditChange(nullptr);
	M->PostEditChange();
	const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bOk = UPackage::SavePackage(Package, M, *File, Args);
	OutMessage = FString::Printf(TEXT("%s → %s (%s)"), *PackagePath, *File, bOk ? TEXT("저장") : TEXT("저장 실패"));
	return bOk;
}
#else
bool SandMaterial::Build(const FString&, FString& OutMessage, bool) { OutMessage = TEXT("에디터 빌드에서만"); return false; }
bool SandMaterial::BuildWater(const FString&, FString& OutMessage) { OutMessage = TEXT("에디터 빌드에서만"); return false; }
#endif
