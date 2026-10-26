// M1 — 입자 → 메시. 상세는 Docs/design/m1-plan.md, 근거는 papers.md C12.
//
// ① 이웃 격자 → ② 표면 입자(이웃 < N_ε, Akinci 2012) → ③ 커널(Yu & Turk 2010 이방성 / 등방 / Zhu & Bridson 2005)
// → ④ 표면 입자 주변 띠 격자 → ⑤ 장 φ → ⑥ Surface Nets → 정점 · 법선 · 삼각형.
// UE 렌더링과 분리된 순수 CPU 코드다 (TArray · ParallelFor · FVector3f 만 쓴다).

#pragma once

#include "CoreMinimal.h"

enum class ESandSurfaceField : uint8
{
	Isotropic,     // 등방 B-cubic 커널 (Müller 2003 식) — 대조군
	Anisotropic,   // Yu & Turk 2010 — 중심 다듬기 + 가중 PCA 이방성 커널
	ZhuBridson,    // Zhu & Bridson 2005 — φ = |x − x̄| − r̄ (모래 쪽 원전)
};

struct FSandSurfaceParams
{
	ESandSurfaceField Field = ESandSurfaceField::Anisotropic;
	float Radius    = 2.5f;    // 입자 반경 (cm)
	float H         = 5.f;     // 커널 길이 단위 h = 입자 간격 (Yu & Turk 의 SPH h)
	float NeighborR = 10.f;    // PCA · 다듬기 이웃 반경 r_i = 2h
	float Lambda    = 0.9f;    // 중심 다듬기 (Yu & Turk 0.9~1)
	float Kr        = 4.f;     // 축비 상한
	int32 NEps      = 25;      // 이 이웃 수 이하면 구로 (Yu & Turk · Akinci 표면 입자 기준)
	float Kn        = 0.5f;    // 구 복귀 크기
	int32 KnRamp    = 0;       // >0 이면 구 복귀 크기를 이웃 수로 키운다: k_n(N) = Kn + (1 − Kn)·min(1, N/KnRamp). 고립 입자는 작게(보이게), 얇은 벽(이웃 10~20)은 등방 h 크기로 — 우리 추가
	float Ks        = 0.f;     // 공분산 스케일. 0 이면 내부 입자(이웃 > 1.4·N_ε)의 평균 σ1 로 ‖k_s C‖ ≈ 1 이 되게 자동
	float MinSigma  = 0.f;     // 커널 축 길이 하한 (σ̃ 하한, 축 지지 = 2h·σ̃). 얇은 벽이 입자 반경보다 얇아지지 않게 — Yu & Turk 에 없는 우리 추가
	float MaxStretch = 2.f;    // 커널 축 길이 상한 (h 배) — 지지 반경을 묶어 이웃 격자를 유한하게
	float Cell      = 2.5f;    // 장 격자 셀 (cm)
	float IsoFrac   = 0.5f;    // 등위값 = 내부 φ 평균 × 이 비율 (등방 · 이방성). Zhu & Bridson 은 0
	float ZbR       = 10.f;    // Zhu & Bridson 이웃 반경 R (간격의 2 배)
	int32 BandNEps  = 25;      // 띠를 정하는 표면 입자 기준 (Akinci 2012). 커널의 N_ε 와 따로 — N_ε 를 낮추면 띠가 사라진다
	float SurfDev   = 0.15f;   // >0 이면 이웃 수가 BandNEps 이상이어도 치우침 |Σ(x_j − x_i)| / (N·r_i) 이 이보다 크면 표면 입자 (Barecasco 2013 · Liu 2022, C14). 판 실측 윗층 p1 0.187 · 내부 p99 0.107 의 가운데 (m1-log #4). 0 = 끔
	float BandR     = 0.f;     // 띠 반경 (표면 입자에서). 0 이면 장 지지 반경 전체. 등위면은 표면 입자 근처라 좁혀도 된다 (#3)
	int32 Block     = 32;      // 장 뿌리기 부분 영역 한 변 (격자점). 영역마다 스레드 하나가 지역 조밀 배열에 뿌린다 — 원자 연산 없음 (splashsurf, C14). 0 = 입자별 원자 뿌리기 (#3)
	int32 Simd      = 8;       // 부분 영역 뿌리기 줄 루프 폭: 8 = AVX (CPU 가 지원하면), 4 = SSE, 0 = 스칼라 (단계 C)
	bool  bBandOnly = true;    // 표면 입자 주변 격자점만 (Akinci). false 면 입자 영향권 전부
};

struct FSandSurfaceMesh
{
	TArray<FVector3f> Vertices;
	TArray<FVector3f> Normals;
	TArray<int32>     Triangles;   // 3 개씩

	// 입자별 진단 (표면 판정 문턱 측정용)
	TArray<int32> NeighborCount;
	TArray<float> Deviation;     // |Σ(x_j − x_i)| / (N·r_i)

	// 통계
	int32 NumParticles = 0, NumSurfaceParticles = 0, NumBandPoints = 0, NumCells = 0;
	float Ks = 0.f, Iso = 0.f, InteriorPhi = 0.f;
	double MsSplatLists = 0.0; int64 SplatEvals = 0; int32 NumBlocks = 0, NumGridPoints = 0, SimdWidth = 0;
	double MsFieldBand = 0.0, MsFieldIso = 0.0, MsFieldFilter = 0.0;   // 장 안쪽: 띠 표시 · 등위값 · 뿌릴 입자 거르기 (나머지 = 뿌리기)
	double MsNeighbors = 0.0, MsKernels = 0.0, MsField = 0.0, MsMesh = 0.0, MsTotal = 0.0;

	/** 닫힌 메시 부피 (발산 정리). 열린 경계가 있으면 근사 */
	double Volume() const;
	double Area() const;
	bool SaveObj(const FString& Path) const;
};

class FSandSurfaceBuilder
{
public:
	static void Build(const TArray<FVector3f>& Positions, const FSandSurfaceParams& Params, FSandSurfaceMesh& Out);
};
