#include "SpikeSweepCommandlet.h"

#include "SpikeBatch.h"
#include "Async/ParallelFor.h"
#include "Math/RandomStream.h"
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

namespace
{
	/**
	 * ★ 자 교정 — 각도를 아는 원뿔을 합성해서 측정기가 그 값을 돌려주는지 본다.
	 *
	 * 측정기(표면 추출 임계·법선 추정·제외 구간)는 전부 우리가 정한 값이고
	 * 맞다는 근거가 없었다. 여기서 통과하지 못하면 지금까지의 모든 각도가 무의미하다.
	 */
	TArray<FVector3f> MakeCone(float SlopeDeg, float BaseRadius, float ParticleRadius)
	{
		FRandomStream Rng(20260905);
		const float Jitter = 0.15f * ParticleRadius;   // 실제로 안착한 더미처럼 흐트러뜨린다
		const float S = 2.f * ParticleRadius;
		const float Tan = FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
		const float Height = BaseRadius * Tan;

		TArray<FVector3f> Out;
		for (float Z = ParticleRadius; Z <= Height; Z += S)
		{
			const float RAtZ = BaseRadius - Z / Tan;   // 높이 Z 에서의 반지름
			const int32 Lat = FMath::CeilToInt(RAtZ / S);
			for (int32 iy = -Lat; iy <= Lat; ++iy)
			for (int32 ix = -Lat; ix <= Lat; ++ix)
			{
				const float X = ix * S;
				const float Y = iy * S;
				if (X * X + Y * Y <= RAtZ * RAtZ)
				{
					Out.Add(FVector3f(
						X + Rng.FRandRange(-Jitter, Jitter),
						Y + Rng.FRandRange(-Jitter, Jitter),
						Z + Rng.FRandRange(-Jitter, Jitter)));
				}
			}
		}
		return Out;
	}

	/** 더미 단면 — 반경 구간별 최고점. 추정 대신 실제 모양을 본다. */
	int32 RunProfileDump(const FString& Params)
	{
		float Mu = 0.7f;
		FParse::Value(*Params, TEXT("mu="), Mu);

		FSandParams P;
		P.Friction = Mu;
		FColumnSetup C;
		const FSpikeRunResult R = SpikeBatch::RunOne(P, C, 1200, false);

		UE_LOG(LogSpikeSweep, Display,
			TEXT("단면 — mu=%.2f, 정지 %d프레임, 최고점 %.1fcm, 프로파일 %.1f° 중앙 %.1f°"),
			Mu, R.SettleFrame, R.PeakHeight, R.Angle.ProfileDeg, R.Angle.MedianDeg);
		UE_LOG(LogSpikeSweep, Display, TEXT("  반경(cm)   최고점(cm)   구간기울기(°)"));

		for (int32 b = 0; b < R.Profile.Num(); ++b)
		{
			const float Rad = R.ProfileBinWidth * (b + 0.5f);
			float LocalDeg = 0.f;
			if (b + 1 < R.Profile.Num() && R.Profile[b] >= 0.f && R.Profile[b + 1] >= 0.f)
			{
				LocalDeg = FMath::RadiansToDegrees(
					FMath::Atan((R.Profile[b] - R.Profile[b + 1]) / R.ProfileBinWidth));
			}
			UE_LOG(LogSpikeSweep, Display, TEXT("%9.1f %12.1f %14.1f"), Rad, R.Profile[b], LocalDeg);
		}
		return 0;
	}

	int32 RunRulerCalibration()
	{
		const float R = 2.5f;
		UE_LOG(LogSpikeSweep, Display, TEXT("자 교정 — 합성 원뿔의 실제 경사각 대 측정값"));
		UE_LOG(LogSpikeSweep, Display, TEXT(" 실제 | 프로파일  오차 |  최빈   중앙   평균  오차 | 표본"));

		for (float TrueDeg : { 15.f, 20.f, 25.f, 30.f, 34.f, 40.f, 45.f, 55.f })
		{
			const TArray<FVector3f> Cone = MakeCone(TrueDeg, 60.f, R);
			const FAngleStats A = SpikeMeasure::AngleOfRepose(Cone, R);
			UE_LOG(LogSpikeSweep, Display,
				TEXT("%5.1f |   %5.1f  %+5.1f | %5.1f  %5.1f  %5.1f  %+5.1f | %5d"),
				TrueDeg, A.ProfileDeg, A.ProfileDeg - TrueDeg,
				A.ModeDeg, A.MedianDeg, A.MeanDeg, A.MedianDeg - TrueDeg, A.SampledNum);
		}
		return 0;
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
	if (FParse::Param(*Params, TEXT("selftest")))
	{
		return RunRulerCalibration();
	}
	if (FParse::Param(*Params, TEXT("profiledump")))
	{
		return RunProfileDump(Params);
	}

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
	UE_LOG(LogSpikeSweep, Display, TEXT("  mu    w   ss  it | 프로파일  중앙 | 표본 | 상태      | 크리프  | 초"));
	for (const FSpikeRunResult& R : Results)
	{
		const bool bValid = R.bReleased && R.bSettled;
		ValidCount += bValid ? 1 : 0;

		// 유효하지 않으면 각도를 아예 찍지 않는다. 실행 #2에서 89.5°(원기둥 옆면)에 속았다
		const FString Angles = bValid
			? FString::Printf(TEXT("%7.1f° %5.1f°"), R.Angle.ProfileDeg, R.Angle.MedianDeg)
			: FString(TEXT("      —      —"));

		const TCHAR* State = !R.bReleased ? TEXT("벽 못 뺌 ") : (!R.bSettled ? TEXT("붕괴 상한") : TEXT("정지     "));

		UE_LOG(LogSpikeSweep, Display,
			TEXT("%5.2f %4.2f %4d %3d | %s | %4d | %s | %6.3f | %5.1f%s"),
			R.Friction, R.Relaxation, R.Substeps, R.Iterations,
			*Angles, R.Angle.SampledNum, State, R.CreepRate, R.WallSeconds,
			(bValid && FMath::Abs(R.Angle.ProfileDeg - 34.f) <= 3.f) ? TEXT("  ★통과") : TEXT(""));
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
