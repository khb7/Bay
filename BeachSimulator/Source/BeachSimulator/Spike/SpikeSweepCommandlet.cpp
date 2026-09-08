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

	int32 RunTiltSweep(const FString& Params, const TArray<float>& MuList,
	                   const TArray<float>& RateList, const TArray<int32>& IterList,
	                   const TArray<int32>& SubsList)
	{
		int32 Count = 3000;
		float MaxDeg = 70.f;
		float HoldSec = 4.f;
		FParse::Value(*Params, TEXT("particles="), Count);
		FParse::Value(*Params, TEXT("maxdeg="), MaxDeg);
		FParse::Value(*Params, TEXT("hold="), HoldSec);
		const int32 HoldFrames = FMath::Max(30, FMath::RoundToInt(HoldSec * 60.f));

		FTiltSetup Setup;
		Setup.TargetCount = Count;
		Setup.bOpenDownhill = !FParse::Param(*Params, TEXT("closed"));

		const bool bGS = FParse::Param(*Params, TEXT("gs"));
		const bool bNoPosFric = FParse::Param(*Params, TEXT("noposfric"));
		const bool bLegacy = FParse::Param(*Params, TEXT("legacy"));   // SP 이전 경로

		float Mb = 0.6f, Omega = 0.5f, Kv = 0.1f;
		FParse::Value(*Params, TEXT("mb="), Mb);
		FParse::Value(*Params, TEXT("omega="), Omega);
		FParse::Value(*Params, TEXT("kv="), Kv);
		const TArray<float> MaList = ParseList<float>(Params, TEXT("ma="), { 0.f });

		TArray<FSandParams> Configs;
		TArray<float> Rates;
		for (float Mu : MuList)
		for (int32 Subs : SubsList)
		for (int32 Iters : IterList)
		for (float Ma : MaList)
		for (float Rate : RateList)
		{
			FSandParams P;
			P.Friction     = Mu;
			P.Substeps     = Subs;
			P.Iterations   = Iters;
			P.bGaussSeidel = bGS;
			P.bPositionFriction = !bNoPosFric;
			P.bSequentialPositions = !bLegacy;
			P.MomentumA    = Ma;
			P.MomentumB    = Mb;
			P.SorOmega     = Omega;
			P.Kv           = Kv;
			Configs.Add(P);
			Rates.Add(Rate);
		}

		TArray<FTiltRunResult> Results;
		Results.SetNum(Configs.Num());
		const double T0 = FPlatformTime::Seconds();
		ParallelFor(Configs.Num(), [&](int32 i)
		{
			Results[i] = SpikeBatch::RunTiltStatic(Configs[i], Setup, MaxDeg, HoldFrames);
		});
		const double Elapsed = FPlatformTime::Seconds() - T0;

		UE_LOG(LogSpikeSweep, Display, TEXT("경사판 %d 실행 — 입자 %d, 상한 %.0f°, 유지 %.1f초, 벽시계 %.1f초"),
			Results.Num(), Results.Num() > 0 ? Results[0].Particles : 0, MaxDeg, HoldSec, Elapsed);
		UE_LOG(LogSpikeSweep, Display, TEXT("   mu  atan(mu) | ss  it   a  | 개시각    오차 | 초"));

		FString Text = SpikeBatch::TiltHeader() + TEXT("\n");
		for (const FTiltRunResult& R : Results)
		{
			Text += SpikeBatch::TiltRow(R) + TEXT("\n");
			const float AtanMu = FMath::RadiansToDegrees(FMath::Atan(R.Friction));
			const FSandParams& Cfg = Configs[&R - Results.GetData()];
			UE_LOG(LogSpikeSweep, Display, TEXT("%5.2f    %5.1f° | %2d %3d %4.1f | %s | %4.1f"),
				R.Friction, AtanMu, Cfg.Substeps, Cfg.Iterations, Cfg.MomentumA,
				R.bOnset ? *FString::Printf(TEXT("%6.2f° %+6.2f"), R.OnsetDeg, R.OnsetDeg - AtanMu)
				         : TEXT("  상한까지 안 무너짐"),
				R.WallSeconds);
		}

		const FString Path = FPaths::ProjectSavedDir() / TEXT("SpikeRuns") /
			(TEXT("tilt_") + FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT(".csv"));
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogSpikeSweep, Display, TEXT("→ %s"), *Path);
		return 0;
	}

	/** 부어 쌓기 — θr 측정 (m0-spike.md §3-0-A). */
	int32 RunPourSweep(const FString& Params, const TArray<float>& MuList,
	                   const TArray<int32>& IterList, const TArray<int32>& SubsList)
	{
		int32 Count = 4000, SettleCap = 3000;
		float Drop = 3.f;
		FParse::Value(*Params, TEXT("particles="), Count);
		FParse::Value(*Params, TEXT("drop="), Drop);
		FParse::Value(*Params, TEXT("settlecap="), SettleCap);
		const TArray<int32> RateList = ParseList<int32>(Params, TEXT("perdrop="), { 2 });
		const TArray<float> DropList = ParseList<float>(Params, TEXT("drop="), { Drop });

		TArray<FSandParams> Cfgs;
		TArray<FPourSetup>  Setups;
		for (float Mu : MuList)
		for (int32 Subs : SubsList)
		for (int32 Iters : IterList)
		for (float Dr : DropList)
		for (int32 Per : RateList)
		{
			// 낙하 충격 속도 √(2gh) 로 한 서브스텝 이동이 지름의 0.2배를 넘으면
			// 입자가 한 스텝에 깊이 박히고, 그 침투를 해소하는 보정이 속도가 되어 튕긴다.
			// 반복이 아니라 dt 로 고쳐야 하는 병이다 (papers.md C4)
			FSandParams P;
			P.Friction   = Mu;
			P.Substeps   = Subs;
			P.Iterations = Iters;
			Cfgs.Add(P);

			FPourSetup S;
			S.TargetCount          = Count;
			S.DropHeightDiameters  = Dr;
			S.PerDrop              = Per;
			Setups.Add(S);
		}

		TArray<FPourResult> Results;
		Results.SetNum(Cfgs.Num());
		const double T0 = FPlatformTime::Seconds();
		ParallelFor(Cfgs.Num(), [&](int32 i)
		{
			Results[i] = SpikeBatch::RunPour(Cfgs[i], Setups[i], SettleCap);
		});
		const double Elapsed = FPlatformTime::Seconds() - T0;

		UE_LOG(LogSpikeSweep, Display,
			TEXT("부어 쌓기 %d 실행 — 목표 %d개, 낙하 %.1f지름, 바닥 %d개, 벽시계 %.1f초"),
			Results.Num(), Count, Drop, Results.Num() > 0 ? Results[0].BaseCount : 0, Elapsed);
		UE_LOG(LogSpikeSweep, Display,
			TEXT("   mu ss  it 낙하 속도 | 정지 | 프로파일  법선중앙  차이 | 원뿔 h/r      | 붓는중 최대 | 초"));

		for (int32 i = 0; i < Results.Num(); ++i)
		{
			const FPourResult& R = Results[i];
			const float Diff = FMath::Abs(R.Angle.ProfileDeg - R.Angle.MedianDeg);
			UE_LOG(LogSpikeSweep, Display,
				TEXT("%5.2f %2d %3d %4.1f %4.2f | %4s | %7.1f°  %7.1f°  %4.1f | %5.1f/%5.1fcm | %9.1f | %5.1f%s"),
				R.Friction, Cfgs[i].Substeps, R.Iterations, R.DropHeightDiameters, R.RatePerFrame,
				R.bSettled ? TEXT("○") : TEXT("★미완"),
				R.Angle.ProfileDeg, R.Angle.MedianDeg, Diff,
				R.ConeHeight, R.ConeRadius, R.MaxSpeedDuringPour, R.WallSeconds,
				(R.bSettled && Diff < 3.f && FMath::Abs(R.Angle.ProfileDeg - 34.f) <= 3.f)
					? TEXT("  ★통과") : TEXT(""));
		}

		if (Results.Num() == 1)
		{
			const FPourResult& R = Results[0];
			UE_LOG(LogSpikeSweep, Display, TEXT(""));
			UE_LOG(LogSpikeSweep, Display, TEXT("최종 단면 —  반경(cm)  최고점(cm)  구간기울기(°)"));
			for (int32 b = 0; b < R.FinalProfile.Num(); ++b)
			{
				float LocalDeg = 0.f;
				if (b + 1 < R.FinalProfile.Num() && R.FinalProfile[b] >= 0.f && R.FinalProfile[b + 1] >= 0.f)
				{
					LocalDeg = FMath::RadiansToDegrees(
						FMath::Atan((R.FinalProfile[b] - R.FinalProfile[b + 1]) / R.ProfileBinWidth));
				}
				UE_LOG(LogSpikeSweep, Display, TEXT("            %8.1f %11.1f %14.1f"),
					R.ProfileBinWidth * (b + 0.5f), R.FinalProfile[b], LocalDeg);
			}
		}
		return 0;
	}

	/** 원통 들어올리기 — θr 측정 (m0-spike.md §3-0-D — #20 기각, 대조용). */
	int32 RunConeLift(const FString& Params, const TArray<float>& MuList,
	                  const TArray<int32>& IterList, const TArray<int32>& SubsList)
	{
		int32 Count = 5000, MaxFrames = 900;
		float Aspect = 2.5f;
		FParse::Value(*Params, TEXT("particles="), Count);
		FParse::Value(*Params, TEXT("aspect="), Aspect);
		FParse::Value(*Params, TEXT("maxframes="), MaxFrames);
		const TArray<float> StepList = ParseList<float>(Params, TEXT("step="), { 1.0f });

		FColumnSetup C;
		C.TargetCount = Count;
		C.AspectRatio = Aspect;
		C.FixedLayers = FParse::Param(*Params, TEXT("smoothbase")) ? 0 : 1;
		FParse::Value(*Params, TEXT("basescale="), C.BaseRadiusScale);

		TArray<FSandParams> Configs;
		TArray<float> Steps;
		for (float Mu : MuList)
		for (int32 Subs : SubsList)
		for (int32 Iters : IterList)
		for (float St : StepList)
		{
			FSandParams P;
			P.Friction   = Mu;
			P.Substeps   = Subs;
			P.Iterations = Iters;
			Configs.Add(P);
			Steps.Add(St);
		}

		TArray<FConeLiftResult> Results;
		Results.SetNum(Configs.Num());
		const double T0 = FPlatformTime::Seconds();
		ParallelFor(Configs.Num(), [&](int32 i)
		{
			Results[i] = SpikeBatch::RunConeLift(Configs[i], C, Steps[i], MaxFrames);
		});
		const double Elapsed = FPlatformTime::Seconds() - T0;

		UE_LOG(LogSpikeSweep, Display,
			TEXT("원통 들어올리기 %d 실행 — 입자 %d, H/R=%.1f, 바닥 %s, 벽시계 %.1f초"),
			Results.Num(), Results.Num() > 0 ? Results[0].Particles : 0, Aspect,
			C.FixedLayers > 0 ? TEXT("거침") : TEXT("매끄러움"), Elapsed);
		UE_LOG(LogSpikeSweep, Display,
			TEXT("   mu ss  it 계단 | 다지기          | 프로파일  법선중앙  차이 | 단계(초과) | 초"));

		for (int32 i = 0; i < Results.Num(); ++i)
		{
			const FConeLiftResult& R = Results[i];
			if (!R.bPackStable)
			{
				UE_LOG(LogSpikeSweep, Display,
					TEXT("%5.2f %2d %3d %4.1f | ★실패 %.0f→%.0fcm %4.0f프레임 | 측정 안 함 — 다지기 미완     | %5.1f"),
					R.Friction, Configs[i].Substeps, R.Iterations, R.StepDiameters,
					R.InitialHeight, R.PackedHeight, R.PackFrames, R.WallSeconds);
				continue;
			}
			const float Diff = FMath::Abs(R.Angle.ProfileDeg - R.Angle.MedianDeg);
			UE_LOG(LogSpikeSweep, Display,
				TEXT("%5.2f %2d %3d %4.1f | %.0fcm %4.0f프레임 | %7.1f°  %7.1f°  %4.1f | %4d(%d) | %5.1f%s"),
				R.Friction, Configs[i].Substeps, R.Iterations, R.StepDiameters,
				R.PackedHeight, R.PackFrames,
				R.Angle.ProfileDeg, R.Angle.MedianDeg, Diff,
				R.Steps, R.TimedOutSteps, R.WallSeconds,
				(Diff < 3.f && FMath::Abs(R.Angle.ProfileDeg - 34.f) <= 3.f) ? TEXT("  ★통과") : TEXT(""));
		}
		// --- 단일 실행이면 계단별 기록과 최종 단면을 찍는다 ---
		if (Results.Num() == 1 && Results[0].bPackStable)
		{
			const FConeLiftResult& R = Results[0];
			UE_LOG(LogSpikeSweep, Display, TEXT(""));
			UE_LOG(LogSpikeSweep, Display,
				TEXT("계단 | 높이(cm) | 정지프레임 | 최대속력 | 끝속력  | 최고점 | 퇴적반경 | 이동입자"));
			for (int32 k = 0; k < R.StepLog.Num(); ++k)
			{
				const FConeLiftStep& L = R.StepLog[k];
				UE_LOG(LogSpikeSweep, Display,
					TEXT("%4d | %8.1f | %10d%s | %8.1f | %7.3f | %6.1f | %8.1f | %8d"),
					k + 1, L.LiftZ, L.SettleFrames, L.bTimedOut ? TEXT("★") : TEXT(" "),
					L.MaxSpeed, L.EndSpeed, L.PeakHeight, L.DepositRadius, L.MovedCount);
			}

			UE_LOG(LogSpikeSweep, Display, TEXT(""));
			UE_LOG(LogSpikeSweep, Display, TEXT("최종 단면 —  반경(cm)  최고점(cm)  구간기울기(°)"));
			for (int32 b = 0; b < R.FinalProfile.Num(); ++b)
			{
				float LocalDeg = 0.f;
				if (b + 1 < R.FinalProfile.Num() && R.FinalProfile[b] >= 0.f && R.FinalProfile[b + 1] >= 0.f)
				{
					LocalDeg = FMath::RadiansToDegrees(
						FMath::Atan((R.FinalProfile[b] - R.FinalProfile[b + 1]) / R.ProfileBinWidth));
				}
				UE_LOG(LogSpikeSweep, Display, TEXT("            %8.1f %11.1f %14.1f"),
					R.ProfileBinWidth * (b + 0.5f), R.FinalProfile[b], LocalDeg);
			}
		}

		return 0;
	}

	/** 임계 이하에서 붙잡고 있을 때 실제로 기어가는가. */
	int32 RunCreepTest(const FString& Params)
	{
		float Mu = 0.4f, Deg = 5.f;
		int32 Iters = 90, Seconds = 16;
		FParse::Value(*Params, TEXT("mu="), Mu);
		FParse::Value(*Params, TEXT("angle="), Deg);
		FParse::Value(*Params, TEXT("iterations="), Iters);
		FParse::Value(*Params, TEXT("seconds="), Seconds);

		FSandParams P;
		P.Friction   = Mu;
		P.Substeps   = 1;
		P.Iterations = Iters;

		FTiltSetup Setup;
		Setup.TargetCount   = 3000;
		Setup.bOpenDownhill = false;

		TArray<float> Slide, Speed;
		SpikeBatch::MeasureCreep(P, Setup, Deg, Seconds, Slide, Speed);

		const float Trigger = 0.5f * P.Radius;
		UE_LOG(LogSpikeSweep, Display,
			TEXT("크리프 계측 — mu=%.2f, %.1f°, 반복 %d.  트리거 = %.3fcm (0.5r)"),
			Mu, Deg, Iters, Trigger);
		UE_LOG(LogSpikeSweep, Display, TEXT("  초 | 누적변위(cm) | 그 초의 평균속력(cm/s) | 스텝당(μm)"));
		for (int32 i = 0; i < Slide.Num(); ++i)
		{
			UE_LOG(LogSpikeSweep, Display, TEXT("%4d | %12.4f | %21.4f | %10.1f%s"),
				i + 1, Slide[i], Speed[i], Speed[i] / 60.f * 10000.f,
				Slide[i] > Trigger ? TEXT("  ← 트리거 넘음") : TEXT(""));
		}
		return 0;
	}

	/**
	 * ★ 하중 전달 계측 — λn 의 깊이 프로파일을 ρgh 와 대조한다.
	 *
	 * 정지한 더미에서 접촉 법선 임펄스는 위에 얹힌 무게에 비례해야 한다(깊이에 선형).
	 * 평평하면 하중이 아래로 안 가고 있다는 직접 증거다.
	 */
	int32 RunLambdaProfile(const FString& Params)
	{
		int32 Iters = 15;
		float Mu = 0.4f;
		FParse::Value(*Params, TEXT("iterations="), Iters);
		FParse::Value(*Params, TEXT("mu="), Mu);

		FSandParams P;
		P.Friction   = Mu;
		P.Substeps   = 1;
		P.Iterations = Iters;

		FTiltSetup Setup;
		Setup.TargetCount   = 3000;
		Setup.bOpenDownhill = false;

		FSandSpike Sim;
		Sim.InitTiltBox(P, Setup);
		while (!Sim.IsSettled() && Sim.FrameIndex() < 1800)
		{
			Sim.Step(1.f / 60.f);
		}

		// 접촉을 높이 구간별로 모은다
		const float R = P.Radius;
		const TArray<FVector3f>& Pos = Sim.GetPositions();
		float Top = 0.f;
		for (const FVector3f& V : Pos) { Top = FMath::Max(Top, V.Z); }

		const int32 Bins = FMath::Max(1, FMath::CeilToInt(Top / (2.f * R)));
		TArray<double> Sum;  Sum.Init(0.0, Bins);
		TArray<int32>  Count; Count.Init(0, Bins);

		for (const FSandContact& C : Sim.GetContacts())
		{
			if (C.LambdaN <= 0.f) { continue; }
			const float Z = 0.5f * (Pos[C.I].Z + Pos[C.J].Z);
			const int32 B = FMath::Clamp(FMath::FloorToInt(Z / (2.f * R)), 0, Bins - 1);
			Sum[B] += C.LambdaN;
			++Count[B];
		}

		UE_LOG(LogSpikeSweep, Display,
			TEXT("λn 깊이 프로파일 — mu=%.2f, 반복 %d, 입자 %d, 정지 %d프레임, 높이 %.1fcm"),
			Mu, Iters, Sim.Num(), Sim.FrameIndex(), Top);
		UE_LOG(LogSpikeSweep, Display, TEXT("  높이(cm)  깊이(cm) | 접촉수 | 평균 λn    | ρgh 대비"));

		// 맨 위 구간을 1로 두고 선형(깊이 비례)이면 각 구간이 깊이 비율과 같아야 한다
		double RefLambda = 0.0;
		double RefDepth  = 0.0;
		for (int32 b = Bins - 2; b >= 0; --b)
		{
			if (Count[b] > 0) { RefLambda = Sum[b] / Count[b]; RefDepth = Top - (b + 0.5) * 2.f * R; break; }
		}

		for (int32 b = Bins - 1; b >= 0; --b)
		{
			if (Count[b] == 0) { continue; }
			const double Mean  = Sum[b] / Count[b];
			const double Depth = Top - (b + 0.5) * 2.f * R;
			const double Ideal = (RefDepth > 0.0) ? RefLambda * (Depth / RefDepth) : 0.0;
			UE_LOG(LogSpikeSweep, Display, TEXT("%9.1f %9.1f | %6d | %10.5f | %6.2f"),
				(b + 0.5) * 2.f * R, Depth, Count[b], Mean, Ideal > 0.0 ? Mean / Ideal : 0.0);
		}
		return 0;
	}

	/**
	 * ★ 마찰의 최소 단위 시험 — 거친 바닥 위 입자 하나.
	 *
	 * 쿨롱 마찰이면 정확히 atan(μ) 에서 미끄러져야 한다.
	 * 여기서 안 맞으면 더미·측정·평균은 볼 필요가 없다.
	 */
	int32 RunFrictionTest()
	{
		UE_LOG(LogSpikeSweep, Display, TEXT("단일 입자 마찰 시험 — 거친 바닥 위 입자 1개"));
		UE_LOG(LogSpikeSweep, Display, TEXT("   mu | atan(mu) |  개시각    오차"));

		for (float Mu : { 0.1f, 0.2f, 0.4f, 0.7f, 1.0f, 1.5f })
		{
			FSandParams P;
			P.Friction = Mu;

			FTiltSetup Setup;
			Setup.TargetCount = 25;   // 바닥 한 층(5×5) + 그 위 한 개
			Setup.Layers      = 2;
			Setup.FixedLayers = 1;

			const FTiltRunResult R = SpikeBatch::RunTiltSingle(P, Setup, 1.0f, 80.f);
			const float AtanMu = FMath::RadiansToDegrees(FMath::Atan(Mu));
			UE_LOG(LogSpikeSweep, Display, TEXT("%5.2f |   %5.1f° | %s"),
				Mu, AtanMu,
				R.bOnset ? *FString::Printf(TEXT("%6.2f°  %+6.2f"), R.OnsetDeg, R.OnsetDeg - AtanMu)
				         : TEXT(" 80°까지 안 미끄러짐"));
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
	if (FParse::Param(*Params, TEXT("creeptest")))
	{
		return RunCreepTest(Params);
	}
	if (FParse::Param(*Params, TEXT("lambdaprofile")))
	{
		return RunLambdaProfile(Params);
	}
	if (FParse::Param(*Params, TEXT("frictiontest")))
	{
		return RunFrictionTest();
	}
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
	const TArray<float> RateList  = ParseList<float>(Params, TEXT("rate="),       { 2.0f });

	if (FParse::Param(*Params, TEXT("tilt")))
	{
		return RunTiltSweep(Params, MuList, RateList, IterList, SubsList);
	}
	if (FParse::Param(*Params, TEXT("cone")))
	{
		return RunConeLift(Params, MuList, IterList, SubsList);
	}
	if (FParse::Param(*Params, TEXT("pour")))
	{
		return RunPourSweep(Params, MuList, IterList, SubsList);
	}

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
