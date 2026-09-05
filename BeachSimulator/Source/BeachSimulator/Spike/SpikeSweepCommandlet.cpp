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

	// FParse::Value 는 쉼표를 토큰 구분자로 취급해 "0,0.7,1.3" 을 "0" 으로 자른다.
	// 공백까지 직접 읽는다
	bool ReadRaw(const FString& Params, const TCHAR* Key, FString& Out)
	{
		const int32 At = Params.Find(Key, ESearchCase::IgnoreCase, ESearchDir::FromStart);
		if (At == INDEX_NONE)
		{
			return false;
		}
		int32 Start = At + FCString::Strlen(Key);
		int32 End   = Start;
		while (End < Params.Len() && !FChar::IsWhitespace(Params[End]))
		{
			++End;
		}
		Out = Params.Mid(Start, End - Start).TrimQuotes();
		return !Out.IsEmpty();
	}

	template <typename T>
	TArray<T> ParseList(const FString& Params, const TCHAR* Key, const TArray<T>& Fallback)
	{
		FString Raw;
		if (!ReadRaw(Params, Key, Raw))
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
	const TArray<int32> IterList  = ParseList<int32>(Params, TEXT("iterations="), { 1, 2, 4 });
	const TArray<float> OmegaList = ParseList<float>(Params, TEXT("omega="),      { 1.0f });

	// --- 설정 목록 ---
	TArray<FSandParams> Configs;
	for (float Mu : MuList)
	for (float Omega : OmegaList)
	for (int32 Subs : SubsList)
	for (int32 Iters : IterList)
	{
		FSandParams P;
		P.Friction   = Mu;
		P.Relaxation = Omega;
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
	int32 ValidCount = 0;
	UE_LOG(LogSpikeSweep, Display, TEXT("  mu    w   ss  it |  최빈   중앙 | 표본 | 상태      | 크리프  | 초"));
	for (const FSpikeRunResult& R : Results)
	{
		const bool bValid = R.bReleased && R.bSettled;
		ValidCount += bValid ? 1 : 0;

		// 유효하지 않으면 각도를 아예 찍지 않는다. 실행 #2에서 89.5°(원기둥 옆면)에 속았다
		const FString Angles = bValid
			? FString::Printf(TEXT("%5.1f° %5.1f°"), R.Angle.ModeDeg, R.Angle.MedianDeg)
			: FString(TEXT("    —      —"));

		const TCHAR* State = !R.bReleased ? TEXT("벽 못 뺌 ") : (!R.bSettled ? TEXT("붕괴 상한") : TEXT("정지     "));

		UE_LOG(LogSpikeSweep, Display,
			TEXT("%5.2f %4.2f %4d %3d | %s | %4d | %s | %6.3f | %5.1f%s"),
			R.Friction, R.Relaxation, R.Substeps, R.Iterations,
			*Angles, R.Angle.SampledNum, State, R.CreepRate, R.WallSeconds,
			(bValid && FMath::Abs(R.Angle.ModeDeg - 34.f) <= 3.f) ? TEXT("  ★통과") : TEXT(""));
	}
	UE_LOG(LogSpikeSweep, Display, TEXT("유효 %d / %d"), ValidCount, Results.Num());

	// --- 앵커 대조 ---
	for (const FSpikeRunResult& R : Results)
	{
		if (FMath::IsNearlyEqual(R.Friction, AnchorMu) && R.Substeps == AnchorSubs && R.Iterations == AnchorIters)
		{
			UE_LOG(LogSpikeSweep, Display,
				TEXT("[앵커] mu=%.1f ss=%d it=%d → %.1f° (유효 %s)"),
				R.Friction, R.Substeps, R.Iterations, R.Angle.ModeDeg,
				(R.bReleased && R.bSettled) ? TEXT("예") : TEXT("아니오"));
			break;
		}
	}

	UE_LOG(LogSpikeSweep, Display, TEXT("요약 → %s  (%.1f초)"), *SummaryPath, Elapsed);
	return 0;
}
