#include "SpikeSweepCommandlet.h"

#include "SpikeBatch.h"
#include "Async/ParallelFor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpikeSweep, Log, All);

namespace
{
	// 앵커 셀 — 실시간 뷰에서 26.5° 가 나온 설정. 배치가 이걸 재현해야 나머지를 믿는다
	constexpr float AnchorMu    = 0.7f;
	constexpr int32 AnchorSubs  = 10;
	constexpr int32 AnchorIters = 2;

	template <typename T>
	TArray<T> ParseList(const FString& Params, const TCHAR* Key, const TArray<T>& Fallback)
	{
		FString Raw;
		if (!FParse::Value(*Params, Key, Raw) || Raw.IsEmpty())
		{
			return Fallback;
		}

		TArray<FString> Tokens;
		Raw.ParseIntoArray(Tokens, TEXT(","), true);

		TArray<T> Out;
		for (const FString& Token : Tokens)
		{
			Out.Add((T)FCString::Atod(*Token));
		}
		return Out.Num() > 0 ? Out : Fallback;
	}
}

USpikeSweepCommandlet::USpikeSweepCommandlet()
{
	IsClient      = false;
	IsServer      = false;
	IsEditor      = true;
	LogToConsole  = true;
	ShowErrorCount = false;
}

int32 USpikeSweepCommandlet::Main(const FString& Params)
{
	int32 TargetCount = 5000;
	int32 MaxFrames   = 3000;
	FParse::Value(*Params, TEXT("particles="), TargetCount);
	FParse::Value(*Params, TEXT("frames="),    MaxFrames);
	const bool bWriteCsv = !FParse::Param(*Params, TEXT("nocsv"));

	const TArray<float> MuList   = ParseList<float>(Params, TEXT("mu="),         { 0.7f, 1.0f, 1.3f });
	const TArray<int32> SubsList = ParseList<int32>(Params, TEXT("substeps="),   { 5, 10, 20 });
	const TArray<int32> IterList = ParseList<int32>(Params, TEXT("iterations="), { 1, 2, 4 });

	// --- 설정 목록 ---
	TArray<FSandParams> Configs;
	for (float Mu : MuList)
	for (int32 Subs : SubsList)
	for (int32 Iters : IterList)
	{
		FSandParams P;
		P.Friction   = Mu;
		P.Substeps   = Subs;
		P.Iterations = Iters;
		P.WGlobal    = 0.f;   // 3-0 은 마른 모래
		Configs.Add(P);
	}

	FColumnSetup Column;
	Column.TargetCount = TargetCount;

	UE_LOG(LogSpikeSweep, Display, TEXT("스윕 %d 실행 — 입자 목표 %d, 프레임 상한 %d"),
		Configs.Num(), TargetCount, MaxFrames);

	// --- 실행. 각 실행은 단일 스레드라 결정성이 유지되고, 실행끼리만 병렬이다 ---
	const double T0 = FPlatformTime::Seconds();
	TArray<FSpikeRunResult> Results;
	Results.SetNum(Configs.Num());
	ParallelFor(Configs.Num(), [&](int32 i)
	{
		Results[i] = SpikeBatch::RunOne(Configs[i], Column, MaxFrames, bWriteCsv);
	});
	const double Elapsed = FPlatformTime::Seconds() - T0;

	// --- 저장 (주 스레드) ---
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("SpikeRuns");
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

	FString Summary;
	Summary += FString::Printf(TEXT("# 스윕 %d 실행 — 입자 %d, 프레임 상한 %d, 벽시계 %.1f초\n"),
		Results.Num(), TargetCount, MaxFrames, Elapsed);
	Summary += FString::Printf(TEXT("# 통과 기준 34° ± 3°. 앵커 mu=%.1f ss=%d it=%d\n"),
		AnchorMu, AnchorSubs, AnchorIters);
	Summary += SpikeBatch::SummaryHeader() + TEXT("\n");
	for (const FSpikeRunResult& R : Results)
	{
		Summary += SpikeBatch::SummaryRow(R) + TEXT("\n");
		if (bWriteCsv && !R.PerFrameCsv.IsEmpty())
		{
			FFileHelper::SaveStringToFile(R.PerFrameCsv, *(Dir / R.RunName + TEXT(".csv")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	}

	const FString SummaryPath = Dir / (TEXT("sweep_") + Stamp + TEXT(".csv"));
	FFileHelper::SaveStringToFile(Summary, *SummaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// --- 콘솔 표 ---
	UE_LOG(LogSpikeSweep, Display, TEXT("  mu   ss  it |  최빈   중앙 | 표본 | 정지 | 크리프  | 초"));
	for (const FSpikeRunResult& R : Results)
	{
		UE_LOG(LogSpikeSweep, Display,
			TEXT("%5.2f %4d %3d | %5.1f° %5.1f° | %4d | %4s | %6.3f | %5.1f%s"),
			R.Friction, R.Substeps, R.Iterations,
			R.Angle.ModeDeg, R.Angle.MedianDeg, R.Angle.SampledNum,
			R.bSettled ? TEXT("예") : TEXT("상한"),
			R.CreepRate, R.WallSeconds,
			FMath::Abs(R.Angle.ModeDeg - 34.f) <= 3.f ? TEXT("  ★통과") : TEXT(""));
	}

	// --- 앵커 대조 ---
	for (const FSpikeRunResult& R : Results)
	{
		if (FMath::IsNearlyEqual(R.Friction, AnchorMu) && R.Substeps == AnchorSubs && R.Iterations == AnchorIters)
		{
			UE_LOG(LogSpikeSweep, Display,
				TEXT("[앵커] mu=%.1f ss=%d it=%d → %.1f° (실시간 뷰 26.5° 와 대조할 것)"),
				R.Friction, R.Substeps, R.Iterations, R.Angle.ModeDeg);
			break;
		}
	}

	UE_LOG(LogSpikeSweep, Display, TEXT("요약 → %s  (%.1f초)"), *SummaryPath, Elapsed);
	return 0;
}
