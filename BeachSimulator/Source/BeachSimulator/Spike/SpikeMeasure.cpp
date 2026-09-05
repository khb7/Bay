#include "SpikeMeasure.h"

#include "SandSpike.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// 이웃 탐색 반경 = 입자 지름의 2배. 조밀 충전에서 내부 입자는 40개 안팎,
	// 표면 입자는 그 절반쯤이 잡힌다 — 그 격차로 표면을 가른다
	constexpr float NeighborRadiusInDiameters = 2.f;

	// 표면 판정 — 이웃 중심이 자기 위치에서 얼마나 밀려나 있는가.
	//
	// 이웃 "수"를 이 더미의 중앙값과 비교하던 방식은 얇은 더미에서 기준이 같이 내려가
	// 아무도 표면으로 안 뽑혔다 (합성 원뿔 15~34°에서 표본 0개).
	// 반구형으로 이웃이 있으면 중심이 반경의 3/8 만큼 안쪽에 놓인다 — 그 절반을 임계로 쓴다.
	constexpr float SurfaceOffsetRatio = 0.19f;   // × 탐색 반경

	// 제외 구간 (§3-0 판정 ③) — 더미 크기에 대한 비율. 절대값이면 작은 더미가 통째로 잘린다
	constexpr float ExcludeBottomFraction = 0.10f;   // 높이의 10% (바닥 마찰)
	constexpr float ExcludeTopFraction    = 0.15f;   // 반지름의 15% (꼭대기 노이즈)
	constexpr float ExcludeRimFraction    = 0.97f;   // 최외곽 3% (밑동이 수평으로 눕는 구간)

	FORCEINLINE uint64 GridKey(int32 X, int32 Y, int32 Z)
	{
		return ((uint64)(uint16)X << 32) | ((uint64)(uint16)Y << 16) | (uint64)(uint16)Z;
	}
}

FAngleStats SpikeMeasure::AngleOfRepose(const TArray<FVector3f>& Pos, float Radius)
{
	FAngleStats Out;
	Out.Histogram.SetNumZeroed(91);

	const int32 N = Pos.Num();
	if (N == 0)
	{
		return Out;
	}

	const float D       = 2.f * Radius;
	const float SearchR = NeighborRadiusInDiameters * D;
	const float Cell    = SearchR;

	// --- 균일 격자. 한 번만 도는 계측이라 단순하게 간다 ---
	TMap<uint64, TArray<int32>> Grid;
	Grid.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const uint64 K = GridKey(
			FMath::FloorToInt(Pos[i].X / Cell),
			FMath::FloorToInt(Pos[i].Y / Cell),
			FMath::FloorToInt(Pos[i].Z / Cell));
		Grid.FindOrAdd(K).Add(i);
	}

	// --- ① 이웃 수와 이웃 중심 ---
	TArray<int32>     Counts;
	TArray<FVector3f> NeighborMean;
	Counts.SetNumZeroed(N);
	NeighborMean.SetNumZeroed(N);

	const float SearchR2 = SearchR * SearchR;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 CX = FMath::FloorToInt(Pos[i].X / Cell);
		const int32 CY = FMath::FloorToInt(Pos[i].Y / Cell);
		const int32 CZ = FMath::FloorToInt(Pos[i].Z / Cell);

		FVector3f Sum(0.f);
		int32 Count = 0;
		for (int32 dz = -1; dz <= 1; ++dz)
		for (int32 dy = -1; dy <= 1; ++dy)
		for (int32 dx = -1; dx <= 1; ++dx)
		{
			if (const TArray<int32>* Bucket = Grid.Find(GridKey(CX + dx, CY + dy, CZ + dz)))
			{
				for (int32 j : *Bucket)
				{
					if (j == i)
					{
						continue;
					}
					if (FVector3f::DistSquared(Pos[i], Pos[j]) < SearchR2)
					{
						Sum += Pos[j];
						++Count;
					}
				}
			}
		}
		Counts[i]       = Count;
		NeighborMean[i] = Count > 0 ? Sum / (float)Count : Pos[i];
	}

	const float SurfaceOffset = SurfaceOffsetRatio * SearchR;

	// --- 제외 구간은 더미 자체의 크기에서 뽑는다 ---
	FVector3f Centroid(0.f);
	float PileHeight = 0.f;
	for (const FVector3f& P : Pos)
	{
		Centroid += P;
		PileHeight = FMath::Max(PileHeight, P.Z);
	}
	Centroid /= (float)N;

	float PileRadius = 0.f;
	for (const FVector3f& P : Pos)
	{
		PileRadius = FMath::Max(PileRadius,
			FMath::Sqrt(FMath::Square(P.X - Centroid.X) + FMath::Square(P.Y - Centroid.Y)));
	}

	const float MinZ         = FMath::Max(D, ExcludeBottomFraction * PileHeight);
	const float MinAxisDist2 = FMath::Square(ExcludeTopFraction * PileRadius);
	const float MaxAxisDist2 = FMath::Square(ExcludeRimFraction * PileRadius);

	// --- ② 지역 법선 → 경사각 ---
	TArray<float> Angles;
	Angles.Reserve(N / 4);
	for (int32 i = 0; i < N; ++i)
	{
		if (Counts[i] == 0)
		{
			continue;   // 외톨이
		}

		// 이웃 중심에서 바깥을 향하는 벡터가 곧 표면 법선이고, 그 길이가 노출 정도다
		const FVector3f Dir = Pos[i] - NeighborMean[i];
		const float Len = Dir.Size();
		if (Len < SurfaceOffset)
		{
			continue;   // 이웃에 둘러싸임 = 내부 입자
		}
		++Out.SurfaceNum;

		if (Pos[i].Z < MinZ)
		{
			continue;   // 바닥 마찰 구간
		}
		const float AxisDist2 = FMath::Square(Pos[i].X - Centroid.X) + FMath::Square(Pos[i].Y - Centroid.Y);
		if (AxisDist2 < MinAxisDist2)
		{
			continue;   // 꼭대기 — 단일 입자 노이즈
		}
		if (AxisDist2 > MaxAxisDist2)
		{
			continue;   // 최외곽 — 경사가 수평으로 눕는다
		}
		const float NZ = FMath::Clamp(Dir.Z / Len, -1.f, 1.f);

		// 법선이 수직에서 벗어난 각 = 표면 경사각
		Angles.Add(FMath::RadiansToDegrees(FMath::Acos(NZ)));
	}

	Out.SampledNum = Angles.Num();
	if (Out.SampledNum == 0)
	{
		return Out;
	}

	double Sum = 0.0;
	for (float A : Angles)
	{
		Sum += A;
		Out.Histogram[FMath::Clamp(FMath::FloorToInt(A), 0, 90)]++;
	}
	Out.MeanDeg = (float)(Sum / Out.SampledNum);

	Angles.Sort();
	Out.MedianDeg = Angles[Out.SampledNum / 2];

	// 최빈값 — 1° 빈은 잡음이 심하므로 3빈 이동합의 최대를 쓴다
	int32 BestBin = 0, BestScore = -1;
	for (int32 b = 0; b <= 90; ++b)
	{
		const int32 Score = Out.Histogram[b]
			+ (b > 0  ? Out.Histogram[b - 1] : 0)
			+ (b < 90 ? Out.Histogram[b + 1] : 0);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestBin   = b;
		}
	}
	Out.ModeDeg = BestBin + 0.5f;

	// --- ③ 표면 높이 프로파일의 기울기 ---
	//
	// 반경 구간마다 가장 높은 입자를 뽑으면 그게 옆면이다. 최소제곱으로 직선을 맞춘다.
	// 개별 법선과 달리 격자 계단이 평균으로 상쇄되어, 완만한 경사에서도 편향이 없다.
	{
		const int32 NumBins = FMath::Max(4, FMath::FloorToInt(PileRadius / D));
		TArray<float> BinTop;
		BinTop.Init(-1.f, NumBins);

		for (const FVector3f& P : Pos)
		{
			const float Rad = FMath::Sqrt(FMath::Square(P.X - Centroid.X) + FMath::Square(P.Y - Centroid.Y));
			const int32 B = FMath::Clamp(FMath::FloorToInt(Rad / PileRadius * NumBins), 0, NumBins - 1);
			BinTop[B] = FMath::Max(BinTop[B], P.Z);
		}

		// 꼭대기·최외곽은 같은 이유로 제외한다
		const int32 First = FMath::FloorToInt(ExcludeTopFraction * NumBins);
		const int32 Last  = FMath::CeilToInt(ExcludeRimFraction * NumBins) - 1;

		double SumX = 0, SumY = 0, SumXY = 0, SumXX = 0;
		int32 Count = 0;
		for (int32 b = First; b <= Last && b < NumBins; ++b)
		{
			if (BinTop[b] < 0.f)
			{
				continue;
			}
			const double X = (b + 0.5) * PileRadius / NumBins;
			const double Y = BinTop[b];
			SumX += X; SumY += Y; SumXY += X * Y; SumXX += X * X;
			++Count;
		}

		if (Count >= 3)
		{
			const double Denom = Count * SumXX - SumX * SumX;
			if (FMath::Abs(Denom) > UE_DOUBLE_SMALL_NUMBER)
			{
				const double Slope = (Count * SumXY - SumX * SumY) / Denom;
				Out.ProfileDeg = FMath::RadiansToDegrees((float)FMath::Atan(-Slope));
			}
		}
	}

	return Out;
}

TArray<float> SpikeMeasure::RadialProfile(const TArray<FVector3f>& Pos, float Radius, float& OutBinWidth)
{
	TArray<float> Bins;
	OutBinWidth = 2.f * Radius;
	if (Pos.Num() == 0)
	{
		return Bins;
	}

	FVector3f Centroid(0.f);
	for (const FVector3f& P : Pos)
	{
		Centroid += P;
	}
	Centroid /= (float)Pos.Num();

	float PileRadius = 0.f;
	for (const FVector3f& P : Pos)
	{
		PileRadius = FMath::Max(PileRadius,
			FMath::Sqrt(FMath::Square(P.X - Centroid.X) + FMath::Square(P.Y - Centroid.Y)));
	}

	const int32 NumBins = FMath::Max(1, FMath::CeilToInt(PileRadius / OutBinWidth));
	Bins.Init(-1.f, NumBins);
	for (const FVector3f& P : Pos)
	{
		const float Rad = FMath::Sqrt(FMath::Square(P.X - Centroid.X) + FMath::Square(P.Y - Centroid.Y));
		const int32 B = FMath::Clamp(FMath::FloorToInt(Rad / OutBinWidth), 0, NumBins - 1);
		Bins[B] = FMath::Max(Bins[B], P.Z);
	}
	return Bins;
}

float SpikeMeasure::PeakHeight(const TArray<FVector3f>& Pos)
{
	float Peak = 0.f;
	for (const FVector3f& P : Pos)
	{
		Peak = FMath::Max(Peak, P.Z);
	}
	return Peak;
}

float SpikeMeasure::ComHeight(const TArray<FVector3f>& Pos)
{
	if (Pos.Num() == 0)
	{
		return 0.f;
	}
	double Sum = 0.0;
	for (const FVector3f& P : Pos)
	{
		Sum += P.Z;
	}
	return (float)(Sum / Pos.Num());
}

// ---------------------------------------------------------------------------

void FSpikeRecorder::Begin(const FString& RunName, const FSandSpike& Sim)
{
	FilePath = FPaths::ProjectSavedDir() / TEXT("SpikeRuns") / (RunName + TEXT(".csv"));

	Header = TEXT("frame,simTime,W_global,avgSpeed,maxSpeed,")
	         TEXT("bondsActive,bondsBrokenThisFrame,bondsBrokenTotal,")
	         TEXT("peakHeight,comHeight,movedParticles,stepMs");

	Rows.Reset();
	ReleasePos.Reset();
	bHaveReleasePos = false;
	bOpen           = true;
	bFinished       = false;
}

void FSpikeRecorder::RecordFrame(const FSandSpike& Sim)
{
	if (!bOpen || bFinished)
	{
		return;
	}

	const TArray<FVector3f>& Pos = Sim.GetPositions();

	// 벽이 빠진 순간이 변위의 기준점이다. 그 전의 다지기는 붕괴가 아니다
	if (!bHaveReleasePos && !Sim.WallsActive())
	{
		ReleasePos      = Pos;
		bHaveReleasePos = true;
	}

	int32 Moved = 0;
	if (bHaveReleasePos)
	{
		const float Thresh2 = FMath::Square(2.f * Sim.GetParams().Radius);
		for (int32 i = 0; i < Pos.Num(); ++i)
		{
			if (FVector3f::DistSquared(Pos[i], ReleasePos[i]) > Thresh2)
			{
				++Moved;
			}
		}
	}

	Rows.Add(FString::Printf(TEXT("%d,%.4f,%.3f,%.4f,%.4f,%d,%d,%d,%.3f,%.3f,%d,%.3f"),
		Sim.FrameIndex(), Sim.SimTime(), Sim.GetParams().WGlobal,
		Sim.AvgSpeed(), Sim.MaxSpeed(),
		0, 0, 0,                          // 점착 결합 — 3-2에서 채운다
		SpikeMeasure::PeakHeight(Pos), SpikeMeasure::ComHeight(Pos),
		Moved, Sim.LastStepMs()));
}

void FSpikeRecorder::Finish(const FSandSpike& Sim, const FAngleStats& Angle)
{
	if (!bOpen || bFinished)
	{
		return;
	}
	bFinished = true;

	FFileHelper::SaveStringToFile(BuildText(Sim, Angle), *FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FString FSpikeRecorder::BuildText(const FSandSpike& Sim, const FAngleStats& Angle) const
{
	const FSandParams& P = Sim.GetParams();

	FString Text;
	Text += TEXT("# BeachSimulator M0-a — 안식각 (m0-spike.md §3-0)\n");
	Text += FString::Printf(TEXT("# particles=%d radius=%.2f substeps=%d iterations=%d friction=%.3f seed=%u\n"),
		Sim.Num(), P.Radius, P.Substeps, P.Iterations, P.Friction, P.Seed);
	Text += FString::Printf(TEXT("# column R=%.2f H=%.2f\n"),
		Sim.GetColumn().Radius, Sim.GetColumn().Height);
	Text += FString::Printf(TEXT("# angleMode=%.1f angleMedian=%.1f angleMean=%.1f surface=%d sampled=%d\n"),
		Angle.ModeDeg, Angle.MedianDeg, Angle.MeanDeg, Angle.SurfaceNum, Angle.SampledNum);
	Text += FString::Printf(TEXT("# settleFrame=%d settleTime=%.2f\n"), Sim.FrameIndex(), Sim.SimTime());

	Text += TEXT("# histogram(deg:count) ");
	for (int32 b = 0; b <= 90; ++b)
	{
		if (Angle.Histogram.IsValidIndex(b) && Angle.Histogram[b] > 0)
		{
			Text += FString::Printf(TEXT("%d:%d "), b, Angle.Histogram[b]);
		}
	}
	Text += TEXT("\n");

	Text += Header + TEXT("\n");
	for (const FString& Row : Rows)
	{
		Text += Row + TEXT("\n");
	}

	return Text;
}
