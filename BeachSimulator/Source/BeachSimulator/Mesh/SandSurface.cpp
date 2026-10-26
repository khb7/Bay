#include "SandSurface.h"

#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

#include <atomic>
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
	// 균일 격자 — 셀 크기 CellSize, AABB 기준 조밀 배열 + counting sort
	struct FPointGrid
	{
		FVector3f Origin = FVector3f::ZeroVector;
		float CellSize = 1.f;
		FIntVector Dims = FIntVector(1);
		TArray<int32> Start;   // NumCells + 1
		TArray<int32> Items;

		int32 CellIndex(const FIntVector& C) const { return (C.Z * Dims.Y + C.Y) * Dims.X + C.X; }
		FIntVector CellOf(const FVector3f& P) const
		{
			return FIntVector(
				FMath::Clamp((int32)FMath::FloorToFloat((P.X - Origin.X) / CellSize), 0, Dims.X - 1),
				FMath::Clamp((int32)FMath::FloorToFloat((P.Y - Origin.Y) / CellSize), 0, Dims.Y - 1),
				FMath::Clamp((int32)FMath::FloorToFloat((P.Z - Origin.Z) / CellSize), 0, Dims.Z - 1));
		}
		void Build(const TArray<FVector3f>& Pts, const FVector3f& Min, const FVector3f& Max, float InCell)
		{
			CellSize = InCell;
			Origin = Min;
			const FVector3f Ext = Max - Min;
			Dims = FIntVector(FMath::Max(1, FMath::CeilToInt(Ext.X / CellSize) + 1), FMath::Max(1, FMath::CeilToInt(Ext.Y / CellSize) + 1), FMath::Max(1, FMath::CeilToInt(Ext.Z / CellSize) + 1));
			const int32 NumCells = Dims.X * Dims.Y * Dims.Z;
			Start.Init(0, NumCells + 1);
			TArray<int32> CellOfPt; CellOfPt.SetNumUninitialized(Pts.Num());
			for (int32 i = 0; i < Pts.Num(); ++i) { CellOfPt[i] = CellIndex(CellOf(Pts[i])); ++Start[CellOfPt[i] + 1]; }
			for (int32 c = 0; c < NumCells; ++c) { Start[c + 1] += Start[c]; }
			Items.SetNumUninitialized(Pts.Num());
			TArray<int32> Cursor = Start;
			for (int32 i = 0; i < Pts.Num(); ++i) { Items[Cursor[CellOfPt[i]]++] = i; }
		}
		template <typename F>
		void ForNeighbors(const FVector3f& P, float Reach, F&& Fn) const
		{
			const FIntVector Lo = CellOf(P - FVector3f(Reach)), Hi = CellOf(P + FVector3f(Reach));
			for (int32 z = Lo.Z; z <= Hi.Z; ++z)
			for (int32 y = Lo.Y; y <= Hi.Y; ++y)
			for (int32 x = Lo.X; x <= Hi.X; ++x)
			{
				const int32 C = CellIndex(FIntVector(x, y, z));
				for (int32 k = Start[C]; k < Start[C + 1]; ++k) { Fn(Items[k]); }
			}
		}
	};

	// 대칭 3×3 고유분해 (야코비). A 는 덮어쓴다. 고유값 내림차순, 열 = 고유벡터
	void Eigen3(float A[3][3], float OutVal[3], float OutVec[3][3])
	{
		float V[3][3] = { {1,0,0},{0,1,0},{0,0,1} };
		for (int32 Sweep = 0; Sweep < 12; ++Sweep)
		{
			const float Off = FMath::Abs(A[0][1]) + FMath::Abs(A[0][2]) + FMath::Abs(A[1][2]);
			if (Off < 1e-12f) { break; }
			for (int32 p = 0; p < 2; ++p)
			for (int32 q = p + 1; q < 3; ++q)
			{
				if (FMath::Abs(A[p][q]) < 1e-14f) { continue; }
				const float Theta = (A[q][q] - A[p][p]) / (2.f * A[p][q]);
				const float T = (Theta >= 0.f ? 1.f : -1.f) / (FMath::Abs(Theta) + FMath::Sqrt(Theta * Theta + 1.f));
				const float C = 1.f / FMath::Sqrt(T * T + 1.f), S = T * C;
				for (int32 k = 0; k < 3; ++k)
				{
					const float Akp = A[k][p], Akq = A[k][q];
					A[k][p] = C * Akp - S * Akq; A[k][q] = S * Akp + C * Akq;
				}
				for (int32 k = 0; k < 3; ++k)
				{
					const float Apk = A[p][k], Aqk = A[q][k];
					A[p][k] = C * Apk - S * Aqk; A[q][k] = S * Apk + C * Aqk;
				}
				for (int32 k = 0; k < 3; ++k)
				{
					const float Vkp = V[k][p], Vkq = V[k][q];
					V[k][p] = C * Vkp - S * Vkq; V[k][q] = S * Vkp + C * Vkq;
				}
			}
		}
		int32 Ord[3] = { 0, 1, 2 };
		if (A[Ord[0]][Ord[0]] < A[Ord[1]][Ord[1]]) { Swap(Ord[0], Ord[1]); }
		if (A[Ord[1]][Ord[1]] < A[Ord[2]][Ord[2]]) { Swap(Ord[1], Ord[2]); }
		if (A[Ord[0]][Ord[0]] < A[Ord[1]][Ord[1]]) { Swap(Ord[0], Ord[1]); }
		for (int32 k = 0; k < 3; ++k)
		{
			OutVal[k] = A[Ord[k]][Ord[k]];
			for (int32 r = 0; r < 3; ++r) { OutVec[r][k] = V[r][Ord[k]]; }
		}
	}

	// B-cubic 스플라인 (Yu & Turk 가 쓴 SPH 커널), 지지 q < 2, 3D 정규화 1/π
	FORCEINLINE float BCubic(float Q)
	{
		if (Q >= 2.f) { return 0.f; }
		if (Q < 1.f) { return (1.f / PI) * (1.f - 1.5f * Q * Q + 0.75f * Q * Q * Q); }
		const float T = 2.f - Q;
		return (1.f / PI) * 0.25f * T * T * T;
	}

	struct FKernel
	{
		FVector3f C;      // 중심 (이방성은 다듬은 x̄)
		float G[9];       // 행 우선
		float DetG;
		FVector3f Ext;    // 지지(‖G r‖ < 2) 의 축정렬 반폭 — 뿌리기 범위
	};

	// 한 줄 뿌리기: Row[x] += Scale·W(|GR0 + (x − xa)·DG|), x ∈ [xa, xb]. W = BCubic (1/π 은 Scale 에 포함 안 됨 — 아래에서 곱한다)
	// 스칼라 · SSE 4 폭 · AVX 8 폭 (단계 C, splashsurf 는 AVX2+FMA 8 레인 — C14). 레인 값은 증분 대신 GR0 + k·DG 로 직접 계산
	FORCEINLINE void SplatRowScalar(float* Row, int32 xa, int32 xb, FVector3f GR, const FVector3f& DG, float Scale)
	{
		for (int32 x = xa; x <= xb; ++x, GR += DG)
		{
			const float Q2 = GR.SizeSquared();
			if (Q2 < 4.f) { Row[x] += Scale * BCubic(FMath::Sqrt(Q2)); }
		}
	}

	void SplatRowSse(float* Row, int32 xa, int32 xb, const FVector3f& GR, const FVector3f& DG, float Scale)
	{
		const __m128 Lane = _mm_setr_ps(0.f, 1.f, 2.f, 3.f);
		const __m128 Gx = _mm_set1_ps(GR.X), Gy = _mm_set1_ps(GR.Y), Gz = _mm_set1_ps(GR.Z);
		const __m128 Dx = _mm_set1_ps(DG.X), Dy = _mm_set1_ps(DG.Y), Dz = _mm_set1_ps(DG.Z);
		const __m128 One = _mm_set1_ps(1.f), Two = _mm_set1_ps(2.f), C15 = _mm_set1_ps(1.5f), C075 = _mm_set1_ps(0.75f), C025 = _mm_set1_ps(0.25f);
		const __m128 Sc = _mm_set1_ps(Scale * (1.f / PI));
		int32 x = xa;
		for (; x + 3 <= xb; x += 4)
		{
			const __m128 K = _mm_add_ps(_mm_set1_ps((float)(x - xa)), Lane);
			const __m128 X = _mm_add_ps(Gx, _mm_mul_ps(K, Dx)), Y = _mm_add_ps(Gy, _mm_mul_ps(K, Dy)), Z = _mm_add_ps(Gz, _mm_mul_ps(K, Dz));
			const __m128 Q2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(X, X), _mm_mul_ps(Y, Y)), _mm_mul_ps(Z, Z));
			const __m128 Q = _mm_sqrt_ps(Q2);
			const __m128 Inner = _mm_add_ps(_mm_sub_ps(One, _mm_mul_ps(C15, Q2)), _mm_mul_ps(C075, _mm_mul_ps(Q2, Q)));
			const __m128 T = _mm_sub_ps(Two, Q);
			const __m128 Outer = _mm_mul_ps(C025, _mm_mul_ps(T, _mm_mul_ps(T, T)));
			__m128 W = _mm_blendv_ps(Outer, Inner, _mm_cmplt_ps(Q, One));
			W = _mm_and_ps(W, _mm_cmplt_ps(Q, Two));
			_mm_storeu_ps(Row + x, _mm_add_ps(_mm_loadu_ps(Row + x), _mm_mul_ps(Sc, W)));
		}
		if (x <= xb) { SplatRowScalar(Row, x, xb, GR + DG * (float)(x - xa), DG, Scale); }
	}

#if defined(_MSC_VER)
	void SplatRowAvx(float* Row, int32 xa, int32 xb, const FVector3f& GR, const FVector3f& DG, float Scale)
	{
		const __m256 Lane = _mm256_setr_ps(0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f);
		const __m256 Gx = _mm256_set1_ps(GR.X), Gy = _mm256_set1_ps(GR.Y), Gz = _mm256_set1_ps(GR.Z);
		const __m256 Dx = _mm256_set1_ps(DG.X), Dy = _mm256_set1_ps(DG.Y), Dz = _mm256_set1_ps(DG.Z);
		const __m256 One = _mm256_set1_ps(1.f), Two = _mm256_set1_ps(2.f), C15 = _mm256_set1_ps(1.5f), C075 = _mm256_set1_ps(0.75f), C025 = _mm256_set1_ps(0.25f);
		const __m256 Sc = _mm256_set1_ps(Scale * (1.f / PI));
		int32 x = xa;
		for (; x + 7 <= xb; x += 8)
		{
			const __m256 K = _mm256_add_ps(_mm256_set1_ps((float)(x - xa)), Lane);
			const __m256 X = _mm256_add_ps(Gx, _mm256_mul_ps(K, Dx)), Y = _mm256_add_ps(Gy, _mm256_mul_ps(K, Dy)), Z = _mm256_add_ps(Gz, _mm256_mul_ps(K, Dz));
			const __m256 Q2 = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(X, X), _mm256_mul_ps(Y, Y)), _mm256_mul_ps(Z, Z));
			const __m256 Q = _mm256_sqrt_ps(Q2);
			const __m256 Inner = _mm256_add_ps(_mm256_sub_ps(One, _mm256_mul_ps(C15, Q2)), _mm256_mul_ps(C075, _mm256_mul_ps(Q2, Q)));
			const __m256 T = _mm256_sub_ps(Two, Q);
			const __m256 Outer = _mm256_mul_ps(C025, _mm256_mul_ps(T, _mm256_mul_ps(T, T)));
			__m256 W = _mm256_blendv_ps(Outer, Inner, _mm256_cmp_ps(Q, One, _CMP_LT_OQ));
			W = _mm256_and_ps(W, _mm256_cmp_ps(Q, Two, _CMP_LT_OQ));
			_mm256_storeu_ps(Row + x, _mm256_add_ps(_mm256_loadu_ps(Row + x), _mm256_mul_ps(Sc, W)));
		}
		if (x <= xb) { SplatRowSse(Row, x, xb, GR + DG * (float)(x - xa), DG, Scale); }
	}

	bool CpuHasAvx()
	{
		int Info[4];
		__cpuid(Info, 1);
		const bool bOsXsave = (Info[2] & (1 << 27)) != 0, bAvx = (Info[2] & (1 << 28)) != 0;
		return bOsXsave && bAvx && ((_xgetbv(0) & 6) == 6);
	}
#endif

	// float 원자 덧셈 (CAS). 뿌리기에서 입자들이 같은 격자점에 동시에 더한다
	FORCEINLINE void AtomicAddFloat(float* Dst, float V)
	{
		int32* I = reinterpret_cast<int32*>(Dst);
		int32 Old = *I;
		for (;;)
		{
			const float New = *reinterpret_cast<const float*>(&Old) + V;
			const int32 Got = FPlatformAtomics::InterlockedCompareExchange(I, *reinterpret_cast<const int32*>(&New), Old);
			if (Got == Old) { return; }
			Old = Got;
		}
	}
}

double FSandSurfaceMesh::Volume() const
{
	double V = 0.0;
	for (int32 t = 0; t + 2 < Triangles.Num(); t += 3)
	{
		const FVector3d A(Vertices[Triangles[t]]), B(Vertices[Triangles[t + 1]]), C(Vertices[Triangles[t + 2]]);
		V += FVector3d::DotProduct(A, FVector3d::CrossProduct(B, C)) / 6.0;
	}
	return FMath::Abs(V);
}

double FSandSurfaceMesh::Area() const
{
	double S = 0.0;
	for (int32 t = 0; t + 2 < Triangles.Num(); t += 3)
	{
		const FVector3d A(Vertices[Triangles[t]]), B(Vertices[Triangles[t + 1]]), C(Vertices[Triangles[t + 2]]);
		S += 0.5 * FVector3d::CrossProduct(B - A, C - A).Size();
	}
	return S;
}

bool FSandSurfaceMesh::SaveObj(const FString& Path) const
{
	FString S;
	S.Reserve(Vertices.Num() * 64 + Triangles.Num() * 16);
	for (int32 i = 0; i < Vertices.Num(); ++i) { S += FString::Printf(TEXT("v %.3f %.3f %.3f\n"), Vertices[i].X, Vertices[i].Y, Vertices[i].Z); }
	for (int32 i = 0; i < Normals.Num(); ++i) { S += FString::Printf(TEXT("vn %.4f %.4f %.4f\n"), Normals[i].X, Normals[i].Y, Normals[i].Z); }
	for (int32 t = 0; t + 2 < Triangles.Num(); t += 3)
	{
		const int32 A = Triangles[t] + 1, B = Triangles[t + 1] + 1, C = Triangles[t + 2] + 1;
		S += FString::Printf(TEXT("f %d//%d %d//%d %d//%d\n"), A, A, B, B, C, C);
	}
	return FFileHelper::SaveStringToFile(S, *Path);
}

void FSandSurfaceBuilder::Build(const TArray<FVector3f>& Pos, const FSandSurfaceParams& P, FSandSurfaceMesh& Out)
{
	Out = FSandSurfaceMesh();
	const int32 N = Pos.Num();
	Out.NumParticles = N;
	if (N == 0) { return; }
	const double T0 = FPlatformTime::Seconds();

	// ── ① 이웃 격자 · 이웃 수 ──
	FVector3f Min(FLT_MAX), Max(-FLT_MAX);
	for (const FVector3f& X : Pos) { Min = FVector3f::Min(Min, X); Max = FVector3f::Max(Max, X); }
	// 장의 최대 영향 반경 — Z&B 는 R, 등방은 2h (B-cubic 지지), 이방성은 축 상한 2h·MaxStretch
	const float Support = (P.Field == ESandSurfaceField::ZhuBridson) ? P.ZbR : (P.Field == ESandSurfaceField::Isotropic ? 2.f * P.H : 2.f * P.H * P.MaxStretch);
	const float Pad = Support + P.Cell;
	FPointGrid NGrid;
	NGrid.Build(Pos, Min - FVector3f(Pad), Max + FVector3f(Pad), P.NeighborR);   // 이웃 반경 = 셀 (20cm 셀은 셀당 입자가 8 배 — #3)

	TArray<int32> NCount; NCount.SetNumZeroed(N);
	TArray<float> Dev; Dev.SetNumZeroed(N);    // 이웃 치우침 (C14)
	TArray<FVector3f> Smoothed = Pos;          // x̄_i
	TArray<float> Cov; Cov.SetNumZeroed(N * 6); // xx xy xz yy yz zz
	ParallelFor(N, [&](int32 i)
	{
		const FVector3f Xi = Pos[i];
		double Ws = 0.0; FVector3d Mean(0.0); FVector3f Off(0.f); int32 Cnt = 0;
		NGrid.ForNeighbors(Xi, P.NeighborR, [&](int32 j)
		{
			const float D = (Pos[j] - Xi).Size();
			if (D >= P.NeighborR) { return; }
			const double W = 1.0 - FMath::Pow(D / P.NeighborR, 3.f);
			Ws += W; Mean += W * FVector3d(Pos[j]); Off += Pos[j] - Xi; ++Cnt;
		});
		NCount[i] = Cnt - 1;   // 자기 자신 제외
		Dev[i] = Cnt > 1 ? Off.Size() / ((Cnt - 1) * P.NeighborR) : 1.f;
		if (Ws <= 0.0) { return; }
		Mean /= Ws;
		Smoothed[i] = FVector3f((1.0 - P.Lambda) * FVector3d(Xi) + P.Lambda * Mean);
		double C[6] = {};
		NGrid.ForNeighbors(Xi, P.NeighborR, [&](int32 j)
		{
			const float D = (Pos[j] - Xi).Size();
			if (D >= P.NeighborR) { return; }
			const double W = 1.0 - FMath::Pow(D / P.NeighborR, 3.f);
			const FVector3d R = FVector3d(Pos[j]) - Mean;
			C[0] += W * R.X * R.X; C[1] += W * R.X * R.Y; C[2] += W * R.X * R.Z;
			C[3] += W * R.Y * R.Y; C[4] += W * R.Y * R.Z; C[5] += W * R.Z * R.Z;
		});
		for (int32 k = 0; k < 6; ++k) { Cov[i * 6 + k] = (float)(C[k] / Ws); }
	});
	// 표면 입자: 이웃 적음(Akinci) 또는 이웃이 한쪽으로 치우침(C14 — 조밀하게 다져진 표면)
	TArray<uint8> IsSurf; IsSurf.SetNumZeroed(N);
	for (int32 i = 0; i < N; ++i) { IsSurf[i] = (NCount[i] < P.BandNEps || (P.SurfDev > 0.f && Dev[i] > P.SurfDev)) ? 1 : 0; }
	int32 NumSurf = 0, MaxCount = 0;
	for (int32 i = 0; i < N; ++i) { NumSurf += IsSurf[i]; MaxCount = FMath::Max(MaxCount, NCount[i]); }
	// 내부 입자 = 이웃 수가 최대의 85% 이상 (그리고 N_ε 초과). 판 · 기둥처럼 내부가 있을 때만 — 얇은 벽 · 부스러기엔 없다
	const int32 InteriorMin = FMath::Max(P.NEps + 1, (int32)(0.85f * MaxCount));
	auto IsInterior = [&](int32 i) { return MaxCount > P.NEps && NCount[i] >= InteriorMin; };
	Out.NumSurfaceParticles = NumSurf;
	Out.NeighborCount = NCount; Out.Deviation = Dev;
	const double T1 = FPlatformTime::Seconds();

	// ── ③ 커널 ──
	TArray<FKernel> K; K.SetNumUninitialized(N);
	// σ 들 (자동 k_s 용)
	TArray<float> Sig1; Sig1.SetNumZeroed(N);
	TArray<float> EigVal; EigVal.SetNumZeroed(N * 3);
	TArray<float> EigVec; EigVec.SetNumZeroed(N * 9);
	if (P.Field == ESandSurfaceField::Anisotropic)
	{
		ParallelFor(N, [&](int32 i)
		{
			float A[3][3] = { { Cov[i*6+0], Cov[i*6+1], Cov[i*6+2] }, { Cov[i*6+1], Cov[i*6+3], Cov[i*6+4] }, { Cov[i*6+2], Cov[i*6+4], Cov[i*6+5] } };
			float L[3], R[3][3];
			Eigen3(A, L, R);
			for (int32 k = 0; k < 3; ++k) { EigVal[i * 3 + k] = FMath::Max(L[k], 0.f); for (int32 r = 0; r < 3; ++r) { EigVec[i * 9 + r * 3 + k] = R[r][k]; } }
		});
	}
	float Ks = P.Ks;
	if (P.Field == ESandSurfaceField::Anisotropic && Ks <= 0.f)
	{
		double Sum = 0.0; int32 Cnt = 0;
		for (int32 i = 0; i < N; ++i) { if (IsInterior(i) && EigVal[i * 3] > 0.f) { Sum += EigVal[i * 3]; ++Cnt; } }
		// 내부 σ1 평균의 역수 → ‖k_s C‖ ≈ 1. 내부가 없으면 이론값: 반경 r 공에 고르게 찬 이웃, 가중 (1 − (d/r)³) 의 축당 분산 = 0.15 r²
		Ks = Cnt > 0 ? (float)(Cnt / Sum) : 1.f / (0.15f * P.NeighborR * P.NeighborR);
	}
	Out.Ks = Ks;
	ParallelFor(N, [&](int32 i)
	{
		FKernel& Kr = K[i];
		if (P.Field == ESandSurfaceField::Anisotropic)
		{
			Kr.C = Smoothed[i];
			float S[3];
			if (NCount[i] > P.NEps)
			{
				const float S1 = EigVal[i * 3];
				S[0] = Ks * S1;
				S[1] = Ks * FMath::Max(EigVal[i * 3 + 1], S1 / P.Kr);
				S[2] = Ks * FMath::Max(EigVal[i * 3 + 2], S1 / P.Kr);
			}
			else { const float KnN = P.KnRamp > 0 ? P.Kn + (1.f - P.Kn) * FMath::Min(1.f, (float)NCount[i] / P.KnRamp) : P.Kn; S[0] = S[1] = S[2] = KnN; }
			for (float& Sk : S) { Sk = FMath::Clamp(Sk, FMath::Max(1e-3f, P.MinSigma), P.MaxStretch); }   // 축 길이 = 2h·σ̃ (지지 상한)
			// G = (1/h) R Σ̃⁻¹ Rᵀ
			for (int32 r = 0; r < 3; ++r)
			for (int32 c = 0; c < 3; ++c)
			{
				float V = 0.f;
				for (int32 k = 0; k < 3; ++k) { V += EigVec[i * 9 + r * 3 + k] * (1.f / S[k]) * EigVec[i * 9 + c * 3 + k]; }
				Kr.G[r * 3 + c] = V / P.H;
			}
			Kr.DetG = 1.f / (P.H * P.H * P.H * S[0] * S[1] * S[2]);
			// G⁻¹ = h R Σ̃ Rᵀ → 축 a 의 반폭 = 2 · ‖G⁻¹ 의 a 행‖
			for (int32 a = 0; a < 3; ++a)
			{
				double S2 = 0.0;
				for (int32 c = 0; c < 3; ++c)
				{
					float V = 0.f;
					for (int32 k = 0; k < 3; ++k) { V += EigVec[i * 9 + a * 3 + k] * S[k] * EigVec[i * 9 + c * 3 + k]; }
					S2 += (double)(V * P.H) * (V * P.H);
				}
				Kr.Ext[a] = 2.f * (float)FMath::Sqrt(S2);
			}
		}
		else
		{
			Kr.C = Pos[i];
			for (int32 k = 0; k < 9; ++k) { Kr.G[k] = (k % 4 == 0) ? 1.f / P.H : 0.f; }
			Kr.DetG = 1.f / (P.H * P.H * P.H);
			Kr.Ext = FVector3f(2.f * P.H);
		}
	});
	const double T2 = FPlatformTime::Seconds();

	// ── ④ 띠 격자 · ⑤ 장 ──
	const FVector3f GOrigin = Min - FVector3f(Pad);
	const FVector3f GExt = (Max + FVector3f(Pad)) - GOrigin;
	const FIntVector GD(FMath::CeilToInt(GExt.X / P.Cell) + 1, FMath::CeilToInt(GExt.Y / P.Cell) + 1, FMath::CeilToInt(GExt.Z / P.Cell) + 1);
	const int32 NumPts = GD.X * GD.Y * GD.Z;
	auto PtIndex = [&](int32 x, int32 y, int32 z) { return (z * GD.Y + y) * GD.X + x; };
	auto PtPos = [&](int32 x, int32 y, int32 z) { return GOrigin + FVector3f(x, y, z) * P.Cell; };

	TArray<uint8> Band; Band.SetNumZeroed(NumPts);
	{
		// 표면 입자(또는 전부) 주변 Support 안의 격자점
		const float Reach = P.BandR > 0.f ? P.BandR : Support;
		const int32 RC = FMath::CeilToInt(Reach / P.Cell);
		ParallelFor(N, [&](int32 i)
		{
			if (P.bBandOnly && !IsSurf[i]) { return; }
			const FVector3f L = (Pos[i] - GOrigin) / P.Cell;
			const int32 cx = FMath::RoundToInt(L.X), cy = FMath::RoundToInt(L.Y), cz = FMath::RoundToInt(L.Z);
			for (int32 z = FMath::Max(0, cz - RC); z <= FMath::Min(GD.Z - 1, cz + RC); ++z)
			for (int32 y = FMath::Max(0, cy - RC); y <= FMath::Min(GD.Y - 1, cy + RC); ++y)
			{
				// 줄마다 구와 겹치는 x 구간을 한 번에 (같은 값 쓰기라 경쟁 무해)
				const float Dy = GOrigin.Y + y * P.Cell - Pos[i].Y, Dz = GOrigin.Z + z * P.Cell - Pos[i].Z;
				const float R2 = Reach * Reach - Dy * Dy - Dz * Dz;
				if (R2 < 0.f) { continue; }
				const float Hx = FMath::Sqrt(R2) / P.Cell;
				const int32 x0 = FMath::Max(0, FMath::CeilToInt(L.X - Hx)), x1 = FMath::Min(GD.X - 1, FMath::FloorToInt(L.X + Hx));
				if (x0 <= x1) { FMemory::Memset(&Band[PtIndex(x0, y, z)], 1, x1 - x0 + 1); }
			}
		});
	}
	const double Tf0 = FPlatformTime::Seconds();
	// 띠 점 목록 (색인 오름차순) — 메시 단계가 전역 격자 대신 이것만 돈다
	TArray<int32> BandList;
	{
		TArray<int32> SliceN; SliceN.SetNumZeroed(GD.Z + 1);
		const int32 SliceSize = GD.X * GD.Y;
		ParallelFor(GD.Z, [&](int32 z) { int32 C = 0; const uint8* Bp = &Band[z * SliceSize]; for (int32 k = 0; k < SliceSize; ++k) { C += Bp[k]; } SliceN[z + 1] = C; });
		for (int32 z = 0; z < GD.Z; ++z) { SliceN[z + 1] += SliceN[z]; }
		BandList.SetNumUninitialized(SliceN[GD.Z]);
		ParallelFor(GD.Z, [&](int32 z) { int32 W = SliceN[z]; for (int32 k = 0; k < SliceSize; ++k) { if (Band[z * SliceSize + k]) { BandList[W++] = z * SliceSize + k; } } });
	}
	const int32 NumBand = BandList.Num();
	Out.NumBandPoints = NumBand;
	Out.NumGridPoints = NumPts;

	// 커널 중심으로 격자 (장 모으기용)
	TArray<FVector3f> Centers; Centers.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i) { Centers[i] = K[i].C; }
	FPointGrid KGrid;
	KGrid.Build(Centers, GOrigin, GOrigin + GExt, Support);

	const float PVol = (4.f / 3.f) * PI * P.Radius * P.Radius * P.Radius;   // m_j/ρ_j → 고체 부피 (φ ≈ 고체 분율)
	auto EvalField = [&](const FVector3f& X) -> float
	{
		if (P.Field == ESandSurfaceField::ZhuBridson)
		{
			// φ_zb = |x − x̄| − r̄ (안 = 음수) → 양수 안쪽으로 뒤집어 f = r̄ − |x − x̄|
			double Ws = 0.0; FVector3d Xb(0.0);
			KGrid.ForNeighbors(X, P.ZbR, [&](int32 j)
			{
				const float S = (Pos[j] - X).Size() / P.ZbR;
				if (S >= 1.f) { return; }
				const double W = FMath::Pow(1.0 - S * S, 3.0);
				Ws += W; Xb += W * FVector3d(Pos[j]);
			});
			if (Ws <= 0.0) { return -P.ZbR; }
			Xb /= Ws;
			return P.Radius - (float)(FVector3d(X) - Xb).Size();
		}
		float Phi = 0.f;
		KGrid.ForNeighbors(X, Support, [&](int32 j)
		{
			const FKernel& Kj = K[j];
			const FVector3f R = X - Kj.C;
			const FVector3f GR(Kj.G[0] * R.X + Kj.G[1] * R.Y + Kj.G[2] * R.Z, Kj.G[3] * R.X + Kj.G[4] * R.Y + Kj.G[5] * R.Z, Kj.G[6] * R.X + Kj.G[7] * R.Y + Kj.G[8] * R.Z);
			Phi += PVol * Kj.DetG * BCubic(GR.Size());
		});
		return Phi;
	};

	// 등위값 — 내부 입자 위치의 φ 평균 × IsoFrac (Zhu & Bridson 은 0)
	float Iso = 0.f;
	if (P.Field != ESandSurfaceField::ZhuBridson)
	{
		TArray<int32> Samples;
		for (int32 i = 0; i < N && Samples.Num() < 2000; i += FMath::Max(1, N / 2000)) { if (IsInterior(i)) { Samples.Add(i); } }
		TArray<float> SPhi; SPhi.SetNumUninitialized(Samples.Num());
		ParallelFor(Samples.Num(), [&](int32 k) { SPhi[k] = EvalField(Pos[Samples[k]]); });
		double Sum = 0.0; const int32 Cnt = Samples.Num();
		for (float V : SPhi) { Sum += V; }
		// 내부가 없으면(얇은 벽 · 부스러기) 고체 분율 기준값 0.6 (무작위 조밀 충전) — φ 는 고체 분율 추정이다
		Out.InteriorPhi = Cnt > 0 ? (float)(Sum / Cnt) : 0.6f;
		Iso = Out.InteriorPhi * P.IsoFrac;
	}
	Out.Iso = Iso;

	const double Tf1 = FPlatformTime::Seconds();
	TArray<float> F; F.Init(-1.f, NumPts);   // f − iso, 띠 밖은 표시만 (−1)
	if (P.Field == ESandSurfaceField::ZhuBridson)
	{
		// 비선형(가중 평균) 이라 뿌릴 수 없다 — 띠 점마다 모은다
		ParallelFor(GD.Z, [&](int32 z)
		{
			for (int32 y = 0; y < GD.Y; ++y)
			for (int32 x = 0; x < GD.X; ++x)
			{
				const int32 I = PtIndex(x, y, z);
				if (Band[I]) { F[I] = EvalField(PtPos(x, y, z)) - Iso; }
			}
		});
	}
	else
	{
		// 선형 합 → 입자마다 자기 지지 AABB 안의 띠 점에 뿌린다 (모으기보다 이웃 검사가 훨씬 적다)
		TArray<float> Phi; if (P.Block <= 0) { Phi.SetNumZeroed(NumPts); }
		// 뿌릴 입자 = 띠 점에 닿을 수 있는 것만 — 표면 입자에서 (띠 반경 + 자기 지지) 안 (splashsurf 의 "표면 입자 + 두 고리" 와 같은 생각, 여기선 기하로 판정)
		TArray<uint8> Contributes; Contributes.Init(1, N);
		if (P.Block <= 0)   // 부분 영역 경로는 영역 목록 · 줄 띠 범위가 같은 일을 한다
		{
			TArray<FVector3f> SurfPos;
			for (int32 i = 0; i < N; ++i) { if (!P.bBandOnly || IsSurf[i]) { SurfPos.Add(Pos[i]); } }
			const float BandReach = P.BandR > 0.f ? P.BandR : Support;
			FPointGrid SGrid;
			SGrid.Build(SurfPos, GOrigin, GOrigin + GExt, BandReach + Support);
			ParallelFor(N, [&](int32 j)
			{
				const float Lim = BandReach + K[j].Ext.GetMax() + P.Cell;
				bool bHit = false;
				SGrid.ForNeighbors(K[j].C, Lim, [&](int32 s) { bHit = bHit || (SurfPos[s] - K[j].C).SizeSquared() <= Lim * Lim; });
				Contributes[j] = bHit ? 1 : 0;
			});
		}
		const double Tf2 = FPlatformTime::Seconds();
		Out.MsFieldFilter = (Tf2 - Tf1) * 1000.0;
		if (P.Block > 0)
		{
			// 부분 영역: 격자점을 B³ 영역으로 나누고, 입자를 지지 AABB 가 겹치는 영역마다 목록에 넣는다 (counting sort).
			// 영역마다 스레드 하나가 지역 배열에 뿌린 뒤 자기 띠 점에만 써 넣는다 — 영역끼리 점이 겹치지 않아 원자 연산이 없다
			const int32 B = P.Block;
#if defined(_MSC_VER)
			static const bool bAvx = CpuHasAvx();
			const int32 SimdW = (P.Simd >= 8 && bAvx) ? 8 : (P.Simd >= 4 ? 4 : 0);
#else
			const int32 SimdW = P.Simd >= 4 ? 4 : 0;
#endif
			Out.SimdWidth = SimdW;
			const FIntVector BD((GD.X + B - 1) / B, (GD.Y + B - 1) / B, (GD.Z + B - 1) / B);
			const int32 NumBlocks = BD.X * BD.Y * BD.Z;
			auto PtRange = [&](const FKernel& Kj, FIntVector& L, FIntVector& H)
			{
				const FVector3f Lo = (Kj.C - Kj.Ext - GOrigin) / P.Cell, Hi = (Kj.C + Kj.Ext - GOrigin) / P.Cell;
				L = FIntVector(FMath::Max(0, FMath::CeilToInt(Lo.X)), FMath::Max(0, FMath::CeilToInt(Lo.Y)), FMath::Max(0, FMath::CeilToInt(Lo.Z)));
				H = FIntVector(FMath::Min(GD.X - 1, FMath::FloorToInt(Hi.X)), FMath::Min(GD.Y - 1, FMath::FloorToInt(Hi.Y)), FMath::Min(GD.Z - 1, FMath::FloorToInt(Hi.Z)));
			};
			// 띠 점이 있는 영역만
			TArray<uint8> BlockActive; BlockActive.SetNumZeroed(NumBlocks);
			ParallelFor(NumBlocks, [&](int32 b)
			{
				const int32 bx = b % BD.X, by = (b / BD.X) % BD.Y, bz = b / (BD.X * BD.Y);
				for (int32 z = bz * B; z < FMath::Min(GD.Z, bz * B + B); ++z)
				for (int32 y = by * B; y < FMath::Min(GD.Y, by * B + B); ++y)
				for (int32 x = bx * B; x < FMath::Min(GD.X, bx * B + B); ++x)
				{
					if (Band[PtIndex(x, y, z)]) { BlockActive[b] = 1; return; }
				}
			});
			TArray<int32> BStart; BStart.Init(0, NumBlocks + 1);
			for (int32 j = 0; j < N; ++j)
			{
				if (!Contributes[j]) { continue; }
				FIntVector L, H; PtRange(K[j], L, H);
				if (L.X > H.X || L.Y > H.Y || L.Z > H.Z) { continue; }
				for (int32 bz = L.Z / B; bz <= H.Z / B; ++bz) for (int32 by = L.Y / B; by <= H.Y / B; ++by) for (int32 bx = L.X / B; bx <= H.X / B; ++bx)
				{
					const int32 b = (bz * BD.Y + by) * BD.X + bx;
					BStart[b + 1] += BlockActive[b];
				}
			}
			for (int32 b = 0; b < NumBlocks; ++b) { BStart[b + 1] += BStart[b]; }
			TArray<int32> BItems; BItems.SetNumUninitialized(BStart[NumBlocks]);
			{
				TArray<int32> Cur = BStart;
				for (int32 j = 0; j < N; ++j)
				{
					if (!Contributes[j]) { continue; }
					FIntVector L, H; PtRange(K[j], L, H);
					if (L.X > H.X || L.Y > H.Y || L.Z > H.Z) { continue; }
					for (int32 bz = L.Z / B; bz <= H.Z / B; ++bz) for (int32 by = L.Y / B; by <= H.Y / B; ++by) for (int32 bx = L.X / B; bx <= H.X / B; ++bx)
					{
						const int32 b = (bz * BD.Y + by) * BD.X + bx;
						if (BlockActive[b]) { BItems[Cur[b]++] = j; }
					}
				}
			}
			const double Tl = FPlatformTime::Seconds();
			Out.MsSplatLists = (Tl - Tf2) * 1000.0;
			std::atomic<int64> EvalCount{0};
			// 원인 측정 (#8): 영역별 시간(준비 · 뿌리기 · 써 넣기) · 줄 수
			TArray<double> BkPrep, BkSplat, BkWrite; BkPrep.SetNumZeroed(NumBlocks); BkSplat.SetNumZeroed(NumBlocks); BkWrite.SetNumZeroed(NumBlocks);
			TArray<int64> BkRows, BkRowsHit; BkRows.SetNumZeroed(NumBlocks); BkRowsHit.SetNumZeroed(NumBlocks);
			const double Tp0 = FPlatformTime::Seconds();
			ParallelFor(NumBlocks, [&](int32 b)
			{
				if (BStart[b] == BStart[b + 1]) { return; }
				const double Tb0 = FPlatformTime::Seconds();
				int64 LocalEvals = 0, Rows = 0, RowsHit = 0;
				const FIntVector B0((b % BD.X) * B, ((b / BD.X) % BD.Y) * B, (b / (BD.X * BD.Y)) * B);
				const FIntVector B1(FMath::Min(GD.X, B0.X + B) - 1, FMath::Min(GD.Y, B0.Y + B) - 1, FMath::Min(GD.Z, B0.Z + B) - 1);
				TArray<float> Local; Local.SetNumZeroed(B * B * B);
				// 줄(y,z)마다 띠 점의 x 범위 — 띠 밖 점은 계산하지 않는다
				TArray<int16> SpanLo, SpanHi; SpanLo.Init(MAX_int16, B * B); SpanHi.Init(-1, B * B);
				for (int32 z = B0.Z; z <= B1.Z; ++z)
				for (int32 y = B0.Y; y <= B1.Y; ++y)
				for (int32 x = B0.X; x <= B1.X; ++x)
				{
					if (!Band[PtIndex(x, y, z)]) { continue; }
					const int32 r = (z - B0.Z) * B + (y - B0.Y);
					SpanLo[r] = FMath::Min<int16>(SpanLo[r], x); SpanHi[r] = FMath::Max<int16>(SpanHi[r], x);
				}
				const double Tb1 = FPlatformTime::Seconds();
				for (int32 k = BStart[b]; k < BStart[b + 1]; ++k)
				{
					const FKernel& Kj = K[BItems[k]];
					FIntVector L, H; PtRange(Kj, L, H);
					L = FIntVector(FMath::Max(L.X, B0.X), FMath::Max(L.Y, B0.Y), FMath::Max(L.Z, B0.Z));
					H = FIntVector(FMath::Min(H.X, B1.X), FMath::Min(H.Y, B1.Y), FMath::Min(H.Z, B1.Z));
					const float Scale = PVol * Kj.DetG;
					for (int32 z = L.Z; z <= H.Z; ++z)
					for (int32 y = L.Y; y <= H.Y; ++y)
					{
						const int32 r = (z - B0.Z) * B + (y - B0.Y);
						const int32 xa = FMath::Max<int32>(L.X, SpanLo[r]), xb = FMath::Min<int32>(H.X, SpanHi[r]);
						++Rows;
						if (xa > xb) { continue; }
						++RowsHit;
						// 한 줄(x) 안에서 G·r 은 x 에 대해 선형 → 증분
						LocalEvals += xb - xa + 1;
						const FVector3f R0 = PtPos(xa, y, z) - Kj.C;
						FVector3f GR(Kj.G[0] * R0.X + Kj.G[1] * R0.Y + Kj.G[2] * R0.Z, Kj.G[3] * R0.X + Kj.G[4] * R0.Y + Kj.G[5] * R0.Z, Kj.G[6] * R0.X + Kj.G[7] * R0.Y + Kj.G[8] * R0.Z);
						const FVector3f DG(Kj.G[0] * P.Cell, Kj.G[3] * P.Cell, Kj.G[6] * P.Cell);
						float* Row = Local.GetData() + ((z - B0.Z) * B + (y - B0.Y)) * B - B0.X;
						if (SimdW == 8) { SplatRowAvx(Row, xa, xb, GR, DG, Scale); }
						else if (SimdW == 4) { SplatRowSse(Row, xa, xb, GR, DG, Scale); }
						else { SplatRowScalar(Row, xa, xb, GR, DG, Scale); }
					}
				}
				const double Tb2 = FPlatformTime::Seconds();
				for (int32 z = B0.Z; z <= B1.Z; ++z)
				for (int32 y = B0.Y; y <= B1.Y; ++y)
				for (int32 x = B0.X; x <= B1.X; ++x)
				{
					const int32 I = PtIndex(x, y, z);
					if (Band[I]) { F[I] = Local[((z - B0.Z) * B + (y - B0.Y)) * B + (x - B0.X)] - Iso; }
				}
				EvalCount += LocalEvals;
				BkPrep[b] = (Tb1 - Tb0) * 1000.0; BkSplat[b] = (Tb2 - Tb1) * 1000.0; BkWrite[b] = (FPlatformTime::Seconds() - Tb2) * 1000.0;
				BkRows[b] = Rows; BkRowsHit[b] = RowsHit;
			});
			{
				FSandSurfaceMesh::FSplatProfile& Pr = Out.Prof;
				Pr.WallMs = (FPlatformTime::Seconds() - Tp0) * 1000.0;
				Pr.PreMs = (Tp0 - Tf2) * 1000.0;
				TArray<double> Tot;
				for (int32 b = 0; b < NumBlocks; ++b)
				{
					if (BStart[b] == BStart[b + 1]) { continue; }
					++Pr.ActiveBlocks; Pr.Pairs += BStart[b + 1] - BStart[b];
					Pr.PrepMs += BkPrep[b]; Pr.SplatMs += BkSplat[b]; Pr.WriteMs += BkWrite[b];
					Pr.Rows += BkRows[b]; Pr.RowsHit += BkRowsHit[b];
					Tot.Add(BkPrep[b] + BkSplat[b] + BkWrite[b]);
				}
				Tot.Sort([](double A, double Bv) { return A > Bv; });
				for (int32 i = 0; i < FMath::Min(3, Tot.Num()); ++i) { Pr.TopMs[i] = Tot[i]; }
			}
			Out.SplatEvals = EvalCount.load();
			Out.NumBlocks = NumBlocks;
		}
		else
		{
		ParallelFor(N, [&](int32 j)
		{
			if (!Contributes[j]) { return; }
			const FKernel& Kj = K[j];
			const FVector3f Lo = (Kj.C - Kj.Ext - GOrigin) / P.Cell, Hi = (Kj.C + Kj.Ext - GOrigin) / P.Cell;
			const int32 x0 = FMath::Max(0, FMath::CeilToInt(Lo.X)), x1 = FMath::Min(GD.X - 1, FMath::FloorToInt(Hi.X));
			const int32 y0 = FMath::Max(0, FMath::CeilToInt(Lo.Y)), y1 = FMath::Min(GD.Y - 1, FMath::FloorToInt(Hi.Y));
			const int32 z0 = FMath::Max(0, FMath::CeilToInt(Lo.Z)), z1 = FMath::Min(GD.Z - 1, FMath::FloorToInt(Hi.Z));
			const float Scale = PVol * Kj.DetG;
			for (int32 z = z0; z <= z1; ++z)
			for (int32 y = y0; y <= y1; ++y)
			for (int32 x = x0; x <= x1; ++x)
			{
				const int32 I = PtIndex(x, y, z);
				if (!Band[I]) { continue; }
				const FVector3f R = PtPos(x, y, z) - Kj.C;
				const FVector3f GR(Kj.G[0] * R.X + Kj.G[1] * R.Y + Kj.G[2] * R.Z, Kj.G[3] * R.X + Kj.G[4] * R.Y + Kj.G[5] * R.Z, Kj.G[6] * R.X + Kj.G[7] * R.Y + Kj.G[8] * R.Z);
				const float Q = GR.Size();
				if (Q < 2.f) { AtomicAddFloat(&Phi[I], Scale * BCubic(Q)); }
			}
		});
		ParallelFor(NumPts, [&](int32 I) { if (Band[I]) { F[I] = Phi[I] - Iso; } });
		}
	}
	const double T3 = FPlatformTime::Seconds();
	Out.MsFieldBand = (Tf0 - T2) * 1000.0;
	Out.MsFieldIso = (Tf1 - Tf0) * 1000.0;

	// ── ⑥ Surface Nets ── 띠 점 목록만 돈다. 셀 = 꼭짓점 0 이 띠 점인 격자 셀, 셀 정점은 그 점 색인으로 찾는다
	static const int32 Corner[8][3] = { {0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1} };
	static const int32 Edge[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	auto Unpack = [&](int32 I, int32& x, int32& y, int32& z) { x = I % GD.X; y = (I / GD.X) % GD.Y; z = I / (GD.X * GD.Y); };
	const int32 NumChunks = FMath::Max(1, FMath::Min(256, NumBand / 1024));
	auto ChunkRange = [&](int32 c, int32& b0, int32& b1) { b0 = (int32)((int64)NumBand * c / NumChunks); b1 = (int32)((int64)NumBand * (c + 1) / NumChunks); };
	TArray<FVector3f> SlotPos; SlotPos.SetNumUninitialized(NumBand);
	TArray<uint8> SlotHas; SlotHas.SetNumZeroed(NumBand);
	TArray<int32> ChunkV; ChunkV.SetNumZeroed(NumChunks + 1);
	ParallelFor(NumChunks, [&](int32 c)
	{
		int32 b0, b1; ChunkRange(c, b0, b1);
		int32 NV = 0;
		for (int32 s = b0; s < b1; ++s)
		{
			int32 x, y, z; Unpack(BandList[s], x, y, z);
			if (x >= GD.X - 1 || y >= GD.Y - 1 || z >= GD.Z - 1) { continue; }
			float V[8]; bool bAll = true; int32 Mask = 0;
			for (int32 k = 0; k < 8; ++k)
			{
				const int32 I = PtIndex(x + Corner[k][0], y + Corner[k][1], z + Corner[k][2]);
				if (!Band[I]) { bAll = false; break; }
				V[k] = F[I];
				Mask |= (V[k] > 0.f) ? (1 << k) : 0;
			}
			if (!bAll || Mask == 0 || Mask == 0xFF) { continue; }
			FVector3f Acc(0.f); int32 Cnt = 0;
			for (int32 e = 0; e < 12; ++e)
			{
				const int32 A = Edge[e][0], B = Edge[e][1];
				if ((V[A] > 0.f) == (V[B] > 0.f)) { continue; }
				const float T = V[A] / (V[A] - V[B]);
				const FVector3f PA(Corner[A][0], Corner[A][1], Corner[A][2]), PB(Corner[B][0], Corner[B][1], Corner[B][2]);
				Acc += PA + (PB - PA) * T; ++Cnt;
			}
			SlotPos[s] = GOrigin + (FVector3f(x, y, z) + Acc / (float)Cnt) * P.Cell;
			SlotHas[s] = 1; ++NV;
		}
		ChunkV[c + 1] = NV;
	});
	for (int32 c = 0; c < NumChunks; ++c) { ChunkV[c + 1] += ChunkV[c]; }
	// 정점 번호 — 덩어리별 접두합. CellVert 는 띠 점 자리만 쓰고, 읽을 때 Band 로 먼저 거른다 (전역 초기화 없음)
	TArray<int32> CellVert; CellVert.SetNumUninitialized(NumPts);
	TArray<int32> VertPt; VertPt.SetNumUninitialized(ChunkV[NumChunks]);
	Out.Vertices.SetNumUninitialized(ChunkV[NumChunks]);
	ParallelFor(NumChunks, [&](int32 c)
	{
		int32 b0, b1; ChunkRange(c, b0, b1);
		int32 V = ChunkV[c];
		for (int32 s = b0; s < b1; ++s)
		{
			const int32 I = BandList[s];
			if (!SlotHas[s]) { CellVert[I] = INDEX_NONE; continue; }
			CellVert[I] = V; Out.Vertices[V] = SlotPos[s]; VertPt[V] = I; ++V;
		}
	});
	Out.NumCells = Out.Vertices.Num();
	auto VertAt = [&](int32 x, int32 y, int32 z) -> int32
	{
		if (x < 0 || y < 0 || z < 0 || x >= GD.X - 1 || y >= GD.Y - 1 || z >= GD.Z - 1) { return INDEX_NONE; }
		const int32 I = PtIndex(x, y, z);
		return Band[I] ? CellVert[I] : INDEX_NONE;
	};
	// 법선 = −∇f (중앙 차분, 셀 모서리 기울기 평균)
	Out.Normals.SetNumZeroed(Out.Vertices.Num());
	auto Fat = [&](int32 x, int32 y, int32 z) -> float
	{
		x = FMath::Clamp(x, 0, GD.X - 1); y = FMath::Clamp(y, 0, GD.Y - 1); z = FMath::Clamp(z, 0, GD.Z - 1);
		return F[PtIndex(x, y, z)];
	};
	ParallelFor(Out.Vertices.Num(), [&](int32 Vi)
	{
		int32 x, y, z; Unpack(VertPt[Vi], x, y, z);
		FVector3f Gsum(0.f);
		for (int32 c = 0; c < 8; ++c)
		{
			const int32 X = x + Corner[c][0], Y = y + Corner[c][1], Z = z + Corner[c][2];
			Gsum += FVector3f(Fat(X + 1, Y, Z) - Fat(X - 1, Y, Z), Fat(X, Y + 1, Z) - Fat(X, Y - 1, Z), Fat(X, Y, Z + 1) - Fat(X, Y, Z - 1));
		}
		Out.Normals[Vi] = (-Gsum).GetSafeNormal();
	}, Out.Vertices.Num() < 4096);
	// 면 — 부호가 바뀌는 격자 모서리마다, 그 모서리를 둘러싼 4 셀의 정점으로 사각형 (덩어리별 → 이어 붙이기)
	TArray<TArray<int32>> ChunkTris; ChunkTris.SetNum(NumChunks);
	ParallelFor(NumChunks, [&](int32 c)
	{
		int32 b0, b1; ChunkRange(c, b0, b1);
		TArray<int32>& Tris = ChunkTris[c];
		for (int32 s = b0; s < b1; ++s)
		{
			const int32 I0 = BandList[s];
			int32 x, y, z; Unpack(I0, x, y, z);
			if (x < 1 || y < 1 || z < 1 || x >= GD.X - 1 || y >= GD.Y - 1 || z >= GD.Z - 1) { continue; }
			const bool In0 = F[I0] > 0.f;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 I1 = PtIndex(x + (Axis == 0), y + (Axis == 1), z + (Axis == 2));
				if (!Band[I1] || (F[I1] > 0.f) == In0) { continue; }
				// 모서리를 둘러싼 4 셀 (모서리에 수직인 두 축으로 −1, 0)
				int32 Q[4];
				bool bOk = true;
				for (int32 k = 0; k < 4; ++k)
				{
					const int32 da = (k == 1 || k == 2) ? 1 : 0, db = (k >= 2) ? 1 : 0;
					int32 cx = x, cy = y, cz = z;
					if (Axis == 0) { cy -= 1 - da; cz -= 1 - db; }
					else if (Axis == 1) { cz -= 1 - da; cx -= 1 - db; }
					else { cx -= 1 - da; cy -= 1 - db; }
					Q[k] = VertAt(cx, cy, cz);
					if (Q[k] == INDEX_NONE) { bOk = false; break; }
				}
				if (!bOk) { continue; }
				// 방향: 안(f > 0) → 밖으로 법선이 나가게
				if (In0) { Tris.Append({ Q[0], Q[1], Q[2], Q[0], Q[2], Q[3] }); }
				else     { Tris.Append({ Q[0], Q[2], Q[1], Q[0], Q[3], Q[2] }); }
			}
		}
	});
	for (const TArray<int32>& Tr : ChunkTris) { Out.Triangles.Append(Tr); }
	const double T4 = FPlatformTime::Seconds();
	Out.MsNeighbors = (T1 - T0) * 1000.0;
	Out.MsKernels   = (T2 - T1) * 1000.0;
	Out.MsField     = (T3 - T2) * 1000.0;
	Out.MsMesh      = (T4 - T3) * 1000.0;
	Out.MsTotal     = (T4 - T0) * 1000.0;
}
