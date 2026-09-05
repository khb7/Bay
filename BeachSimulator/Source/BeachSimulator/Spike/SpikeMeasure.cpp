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

	// 표면 판정: 이웃 수가 중앙값의 이 비율 미만이면 노출된 것으로 본다
	constexpr float SurfaceRatio = 0.7f;

	// 제외 구간 (§3-0 판정 ③) — 둘 다 입자 지름 3개분
	constexpr float ExcludeBottomInDiameters = 3.f;
	constexpr float ExcludeTopRadiusInDiameters = 3.f;

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

	TArray<int32> Sorted = Counts;
	Sorted.Sort();
	const int32 MedianCount = Sorted[N / 2];
	const int32 SurfaceThreshold = FMath::Max(1, FMath::FloorToInt(SurfaceRatio * MedianCount));

	// --- 제외 구간 기준 (바닥 3층, 꼭대기 반경 3입자) ---
	FVector3f Centroid(0.f);
	for (const FVector3f& P : Pos)
	{
		Centroid += P;
	}
	Centroid /= (float)N;

	const float MinZ         = ExcludeBottomInDiameters * D;
	const float MinAxisDist2 = FMath::Square(ExcludeTopRadiusInDiameters * D);

	// --- ② 지역 법선 → 경사각 ---
	TArray<float> Angles;
	Angles.Reserve(N / 4);
	for (int32 i = 0; i < N; ++i)
	{
		if (Counts[i] >= SurfaceThreshold)
		{
			continue;   // 내부 입자
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

		// 이웃 중심에서 바깥을 향하는 벡터가 곧 표면 법선
		const FVector3f Dir = Pos[i] - NeighborMean[i];
		const float Len = Dir.Size();
		if (Len < UE_KINDA_SMALL_NUMBER)
		{
			continue;
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

	return Out;
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

	FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
