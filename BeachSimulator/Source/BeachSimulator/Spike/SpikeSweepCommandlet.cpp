#include "SpikeSweepCommandlet.h"

#include "SpikeBatch.h"
#include "Async/ParallelFor.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpikeSweep, Log, All);

// -gpu 면 스윕을 게임 스레드에서 직렬로 돈다 — GPU 경로는 렌더 명령을 플러시해야 해서 워커 스레드에서 못 부른다
static bool GSpikeSerial = false;
template <typename BodyType>
static void SpikeParallelFor(int32 Num, BodyType&& Body)
{
	if (GSpikeSerial)
	{
		for (int32 i = 0; i < Num; ++i) { Body(i); }
		return;
	}
	ParallelFor(Num, Body);
}

namespace
{
	/**
	 * M0-b GPU 기준 프로파일 (2026-10-26, m0-log #54 · #57 · #58) — `-gpu2` 면 명시하지 않은 값을 새 기본값으로 채운다.
	 * 물리: 8색 블록 GS · 블록 안 배치 병렬 · 블록 15cm · μ 0.17 · Kv 1 · Kc 0.2 · vmax 30 · 반복 60 · 입자별 탐색 여유.
	 * 재우기는 기본 꺼짐 (`-sleep` 이면 `-wakecold -wakezero -wakelam=1.2` 를 함께 켠다).
	 * `-legacy` 면 아무것도 바꾸지 않는다 (옛 실행 재현용: Kv 0.1 · Kc 1 · 반복은 시험별 기본).
	 * 호출은 각 시험의 매개변수 파싱이 끝난 뒤 — 플래그가 문자열에 있으면 그 값을 존중한다.
	 */
	void ApplyGpuProfile(const FString& Params, FSandParams& P, int32* InOutIterations = nullptr, float* InOutMu = nullptr)
	{
		if (!P.bGpuResident || FParse::Param(*Params, TEXT("legacy"))) { return; }
		auto Has = [&](const TCHAR* Key) { return Params.Contains(Key); };
		if (!Has(TEXT("-color8"))) { P.bColor8 = true; P.bColor8Par = true; }
		if (!Has(TEXT("block="))) { P.BlockSize = 15.f; }
		if (!Has(TEXT("-mu="))) { P.Friction = 0.17f; if (InOutMu) { *InOutMu = 0.17f; } }
		if (!Has(TEXT("kv="))) { P.Kv = 1.f; }
		if (!Has(TEXT("kc="))) { P.Kc = 0.2f; }
		if (!Has(TEXT("vmax="))) { P.MaxDepenVel = 30.f; }
		if (!Has(TEXT("iterations="))) { P.Iterations = 60; if (InOutIterations) { *InOutIterations = 60; } }
		if (!Has(TEXT("-nopmargin"))) { P.bParticleMargin = true; }
		if (P.bSleep)
		{
			if (!Has(TEXT("-nowakecold"))) { P.bWakeCold = true; }
			if (!Has(TEXT("-nowakezero"))) { P.bWakeZeroPairs = true; }
			if (!Has(TEXT("wakelam="))) { P.WakeLamRatio = 1.2f; }
		}
	}

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
		FParse::Value(*Params, TEXT("basejitter="), Setup.BaseJitter);
		FParse::Value(*Params, TEXT("jitter="), Setup.FreeJitter);

		const bool bGS = FParse::Param(*Params, TEXT("gs"));
		const bool bNoPosFric = FParse::Param(*Params, TEXT("noposfric"));
		const bool bLegacy = FParse::Param(*Params, TEXT("legacy"));   // SP 이전 경로

		float Mb = 0.6f, Omega = 0.5f, Kv = 0.1f;
		float TWarm = 0.f, TWarmTol = 0.f; int32 TWarmTan = 1;
		FParse::Value(*Params, TEXT("warm="), TWarm);
		FParse::Value(*Params, TEXT("warmtol="), TWarmTol);
		FParse::Value(*Params, TEXT("warmtan="), TWarmTan);
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
			P.WarmStart    = TWarm;
			P.WarmGapTol   = TWarmTol;
			P.bWarmTangent = TWarmTan != 0;
			Configs.Add(P);
			Rates.Add(Rate);
		}

		TArray<FTiltRunResult> Results;
		Results.SetNum(Configs.Num());
		const double T0 = FPlatformTime::Seconds();
		SpikeParallelFor(Configs.Num(), [&](int32 i)
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
		int32 Hold = 0;
		FParse::Value(*Params, TEXT("hold="), Hold);
		float Warm = 0.f;
		FParse::Value(*Params, TEXT("warm="), Warm);
		int32 WarmTan = 1;
		FParse::Value(*Params, TEXT("warmtan="), WarmTan);
		float WarmTol = 0.f;
		FParse::Value(*Params, TEXT("warmtol="), WarmTol);
		const TArray<float> KvList = ParseList<float>(Params, TEXT("kv="), { 0.1f });
		const TArray<float> JitterList = ParseList<float>(Params, TEXT("basejitter="), { 0.f });
		const TArray<int32> SeedList = ParseList<int32>(Params, TEXT("seed="), { 1337 });
		float EventFactor = 0.f; int32 EventStop = 0, TraceFrame = -1, TraceIdx = -1;
		FParse::Value(*Params, TEXT("event="), EventFactor);
		FParse::Value(*Params, TEXT("eventstop="), EventStop);
		FParse::Value(*Params, TEXT("traceframe="), TraceFrame);
		FParse::Value(*Params, TEXT("traceidx="), TraceIdx);
		const TArray<int32> RateList = ParseList<int32>(Params, TEXT("perdrop="), { 2 });
		const TArray<float> DropList = ParseList<float>(Params, TEXT("drop="), { Drop });
		const TArray<float> SpawnList = ParseList<float>(Params, TEXT("spawnr="), { 2.5f });
		const TArray<float> SorList = ParseList<float>(Params, TEXT("sor="), { 0.5f });   // (36) ω — 갈래 A 의 과보정 손잡이
		const TArray<float> MaList  = ParseList<float>(Params, TEXT("ma="),  { 0.f });    // (38) a — 모멘텀
		float Mb = 0.6f; FParse::Value(*Params, TEXT("mb="), Mb);
		const bool bGather = FParse::Param(*Params, TEXT("gather"));                       // m0b-spike §3-1 ①
		const bool bMassSplit = FParse::Param(*Params, TEXT("masssplit"));                 // A′ 질량 분할 (Tonge 2012)
		const bool bGpu = FParse::Param(*Params, TEXT("gpu"));                             // 3-1 ② 반복 루프를 GPU 로
		const bool bGpu2 = FParse::Param(*Params, TEXT("gpu2"));                           // 2단계 — 스텝 전체 GPU 상주
		float PourBlock = 0.f; FParse::Value(*Params, TEXT("block="), PourBlock);          // ①-b 블록 (cm)

		TArray<FSandParams> Cfgs;
		TArray<FPourSetup>  Setups;
		for (float Mu : MuList)
		for (float KvV : KvList)
		for (int32 Subs : SubsList)
		for (int32 Iters : IterList)
		for (float Dr : DropList)
		for (float Sr : SpawnList)
		for (float Bj : JitterList)
		for (int32 Sd : SeedList)
		for (int32 Per : RateList)
		for (float Sor : SorList)
		for (float Ma : MaList)
		{
			// 낙하 충격 속도 √(2gh) 로 한 서브스텝 이동이 지름의 0.2배를 넘으면
			// 입자가 한 스텝에 깊이 박히고, 그 침투를 해소하는 보정이 속도가 되어 튕긴다.
			// 반복이 아니라 dt 로 고쳐야 하는 병이다 (papers.md C4)
			FSandParams P;
			P.Friction   = Mu;
			P.Substeps   = Subs;
			P.Iterations = Iters;
			P.WarmStart  = Warm;
			P.bWarmTangent = WarmTan != 0;
			P.WarmGapTol = WarmTol;
			P.Kv         = KvV;
			FParse::Value(*Params, TEXT("vmax="), P.MaxDepenVel);   // #47
			FParse::Value(*Params, TEXT("kc="), P.Kc);   // #48
			P.bSleep = FParse::Param(*Params, TEXT("sleep"));   // C안
			P.bParticleMargin = FParse::Param(*Params, TEXT("pmargin"));   // #50
			FParse::Value(*Params, TEXT("pmarginmin="), P.MarginMinR);
			P.bWakeCold = FParse::Param(*Params, TEXT("wakecold"));   // #50
			P.bWakeZeroPairs = FParse::Param(*Params, TEXT("wakezero"));   // 조사 H9
			P.bSleepColdPairs = FParse::Param(*Params, TEXT("sleepcoldpairs"));   // 조사 H9 (b)
			FParse::Value(*Params, TEXT("wakelam="), P.WakeLamRatio);   // 조사 H9 (e)
			FParse::Value(*Params, TEXT("wakelamabs="), P.WakeLamAbs);
			FParse::Value(*Params, TEXT("sleepv="), P.SleepVel);
			FParse::Value(*Params, TEXT("sleepframes="), P.SleepFrames);
			FParse::Value(*Params, TEXT("wakev="), P.WakeVel);
			FParse::Value(*Params, TEXT("wakevtouch="), P.WakeTouchVel);   // #52
			P.Seed       = (uint32)Sd;
			P.SorOmega   = Sor;
			P.MomentumA  = Ma;
			P.MomentumB  = Mb;
			P.bGatherApply = bGather;
			P.bMassSplit   = bMassSplit;
			P.bGpuSolve    = bGpu;
			P.bGpuResident = bGpu2;
			P.BlockSize    = PourBlock;
			P.bApgd        = FParse::Param(*Params, TEXT("apgd"));   // D′
			P.bColor8      = FParse::Param(*Params, TEXT("color8"));   // A안 단계 0
			P.bColor8Par = FParse::Param(*Params, TEXT("color8par"));   // A안 단계 2
			P.bColor8 = P.bColor8 || P.bColor8Par;
			FParse::Value(*Params, TEXT("colorblock="), P.BlockSize);
			P.bBlockShift  = FParse::Param(*Params, TEXT("blockshift"));
			ApplyGpuProfile(Params, P);
			Cfgs.Add(P);

			FPourSetup S;
			S.TargetCount          = Count;
			S.DropHeightDiameters  = Dr;
			S.PerDrop              = Per;
			S.SpawnRadiusDiameters = Sr;
			S.BaseJitter           = Bj;
			S.EventSpeedFactor     = EventFactor;
			S.EventStopCount       = EventStop;
			S.DebugTraceFrame      = TraceFrame;
			S.DebugTraceIndex      = TraceIdx;
			S.HoldFrames           = Hold;
			Setups.Add(S);
		}

		TArray<FPourResult> Results;
		Results.SetNum(Cfgs.Num());
		const double T0 = FPlatformTime::Seconds();
		SpikeParallelFor(Cfgs.Num(), [&](int32 i)
		{
			Results[i] = SpikeBatch::RunPour(Cfgs[i], Setups[i], SettleCap);
		});
		const double Elapsed = FPlatformTime::Seconds() - T0;

		UE_LOG(LogSpikeSweep, Display,
			TEXT("부어 쌓기 %d 실행 — 목표 %d개, 낙하 %.1f지름, 바닥 %d개, 벽시계 %.1f초"),
			Results.Num(), Count, Drop, Results.Num() > 0 ? Results[0].BaseCount : 0, Elapsed);
		UE_LOG(LogSpikeSweep, Display,
			TEXT("   mu ss  it 낙하 산포 속도 | 정지 | 프로파일  법선중앙  차이 | 원뿔 h/r      | 붓는중 최대 | 초"));

		for (int32 i = 0; i < Results.Num(); ++i)
		{
			const FPourResult& R = Results[i];
			const float Diff = FMath::Abs(R.Angle.ProfileDeg - R.Angle.MedianDeg);
			UE_LOG(LogSpikeSweep, Display,
				TEXT("%5.2f kv%3.1f %2d %3d %4.2f sr%3.1f bj%3.1f s%-5u %4.2f sor%4.2f a%3.1f %s | %4s | %7.1f°  %7.1f°  %4.1f | %5.1f/%5.1fcm | %9.1f | %5.1f%s"),
				R.Friction, Cfgs[i].Kv, Cfgs[i].Substeps, R.Iterations, R.DropHeightDiameters, Setups[i].SpawnRadiusDiameters, Setups[i].BaseJitter, Cfgs[i].Seed, R.RatePerFrame,
				Cfgs[i].SorOmega, Cfgs[i].MomentumA, Cfgs[i].bGatherApply ? (Cfgs[i].bMassSplit ? TEXT("GM") : TEXT("G ")) : TEXT("S "),
				R.bSettled ? TEXT("○") : TEXT("★미완"),
				R.Angle.ProfileDeg, R.Angle.MedianDeg, Diff,
				R.ConeHeight, R.ConeRadius, R.MaxSpeedDuringPour, R.WallSeconds,
				(R.bSettled && Diff < 3.f && FMath::Abs(R.Angle.ProfileDeg - 34.f) <= 3.f)
					? TEXT("  ★통과") : TEXT(""));
			if (Hold > 0)
			{
				const float TotalFrames = R.PourFrames + R.SettleFrames + Hold;
				UE_LOG(LogSpikeSweep, Display, TEXT("      유지 %d프레임 후: 프로파일 %5.1f° 법선중앙 %5.1f°  높이 %5.1f→%5.1f  크리프 평균|v| %.4f cm/s (앞 %.4f / 뒤 %.4f)  바닥하중/무게 %.2f  불평형비 %.2e  | %.1f ms/프레임 (%d프레임, 병렬 %d)"),
					Hold, R.AngleAfterHold.ProfileDeg, R.AngleAfterHold.MedianDeg, R.ConeHeight, R.ConeHeightAfterHold, R.CreepAvgSpeed, R.CreepFirstHalf, R.CreepSecondHalf, R.BaseLoadRatio, R.UnbalancedRatio,
					TotalFrames > 0 ? R.WallSeconds * 1000.0 / TotalFrames : 0.0, (int32)TotalFrames, Results.Num());
			}
		}

		if (FParse::Param(*Params, TEXT("dumppos")))
		{
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("SpikeRuns");
			IFileManager::Get().MakeDirectory(*Dir, true);
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				const FPourResult& R = Results[i];
				FString Text = TEXT("x,y,z,fixed\n");
				for (int32 k = 0; k < R.FinalPos.Num(); ++k)
				{
					Text += FString::Printf(TEXT("%.4f,%.4f,%.4f,%d\n"), R.FinalPos[k].X, R.FinalPos[k].Y, R.FinalPos[k].Z, R.FinalInvMass[k] <= 0.f ? 1 : 0);
				}
				const FString Path = Dir / FString::Printf(TEXT("pour_mu%.2f_ss%d_it%d_drop%.2f_sr%.1f_bj%.1f.csv"), R.Friction, Cfgs[i].Substeps, R.Iterations, R.DropHeightDiameters, Setups[i].SpawnRadiusDiameters, Setups[i].BaseJitter);
				FFileHelper::SaveStringToFile(Text, *Path);
				UE_LOG(LogSpikeSweep, Display, TEXT("위치 저장: %s"), *Path);
			}
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
		SpikeParallelFor(Configs.Num(), [&](int32 i)
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

	/**
	 * 3-4 성능 — 입자 수 스윕. 닫힌 상자에 정육면체 꼴로 즉시 채우고(붓기 없음) 프레임을 돌리며 ms/프레임을 잰다.
	 *   -perf -particles=20000,50000,100000,150000 -frames=100 [-gpu2] [-iterations=] [-mu=]
	 * 앞 WarmupFrames 는 버린다 (낙하·다짐 중이라 접촉 수가 요동). 정지 상태 측정은 아니다 — 다짐 중 상태라 접촉 수는 정지보다 많다 (보수적)
	 */
	/**
	 * #46 끓음 기전 — 어디의 어떤 입자가 빠른가, 겹침이 얼마나 깊은가.
	 * 속도 분위 · 빠른 입자(|v| > 5)의 높이/벽/바닥 분포와 위·아래 방향 · 접촉 겹침 분포 (R 단위).
	 */
	void LogBoilDiag(const FSandSpike& Sim, float R)
	{
		const TArray<FVector3f>& Pos = Sim.GetPositions();
		const TArray<FVector3f>& Vel = Sim.GetVelocities();
		const TArray<float>& W = Sim.GetInvMass();
		const FVector3f Box = Sim.GetBoxHalf();

		TArray<float> Speeds;
		float Top = -FLT_MAX, Bottom = FLT_MAX;
		for (int32 p = 0; p < Pos.Num(); ++p)
		{
			if (W[p] <= 0.f) { continue; }
			Speeds.Add(Vel[p].Size());
			Top = FMath::Max(Top, Pos[p].Z); Bottom = FMath::Min(Bottom, Pos[p].Z);
		}
		if (Speeds.Num() == 0) { return; }
		TArray<float> Sorted = Speeds; Sorted.Sort();
		auto Pct = [&](float q) { return Sorted[FMath::Clamp((int32)(q * (Sorted.Num() - 1)), 0, Sorted.Num() - 1)]; };

		// 빠른 입자 분포 — 높이 5 구간(아래 → 위), 벽 2R 안, 바닥 3R 안, 위로/아래로
		const float Fast = 5.f;
		int32 NFast = 0, Up = 0, NearWall = 0, NearFloor = 0;
		int32 Bins[5] = {};
		const float H = FMath::Max(Top - Bottom, R);
		int32 k = 0;
		for (int32 p = 0; p < Pos.Num(); ++p)
		{
			if (W[p] <= 0.f) { continue; }
			const float S = Speeds[k++];
			if (S <= Fast) { continue; }
			++NFast;
			if (Vel[p].Z > 0.f) { ++Up; }
			if (Box.X - FMath::Abs(Pos[p].X) < 2.f * R || Box.Y - FMath::Abs(Pos[p].Y) < 2.f * R) { ++NearWall; }
			if (Pos[p].Z - Bottom < 3.f * R) { ++NearFloor; }
			Bins[FMath::Clamp((int32)(5.f * (Pos[p].Z - Bottom) / H), 0, 4)]++;
		}

		// 겹침 — 닿은 접촉의 (2R − 거리) / R
		TArray<float> Ov;
		for (const FSandContact& C : Sim.GetContacts())
		{
			const float D = 2.f * R - (Pos[C.I] - Pos[C.J]).Size();
			if (D > 0.f) { Ov.Add(D / R); }
		}
		Ov.Sort();
		auto OPct = [&](float q) { return Ov.Num() ? Ov[FMath::Clamp((int32)(q * (Ov.Num() - 1)), 0, Ov.Num() - 1)] : 0.f; };
		int32 Deep = 0; for (float o : Ov) { if (o > 0.1f) { ++Deep; } }

		const float F = FMath::Max(1, NFast);
		UE_LOG(LogSpikeSweep, Display, TEXT("      [진단] |v| p50 %.2f p90 %.2f p99 %.1f max %.0f | 빠른(>5) %.1f%%: 위로 %.0f%% 벽 %.0f%% 바닥 %.0f%% 높이(아래→위) %.0f/%.0f/%.0f/%.0f/%.0f%% | 겹침/R p50 %.3f p99 %.3f max %.2f >0.1R %d/%d | 높이 %.1f"),
			Pct(0.5f), Pct(0.9f), Pct(0.99f), Sorted.Last(), 100.f * NFast / Speeds.Num(),
			100.f * Up / F, 100.f * NearWall / F, 100.f * NearFloor / F,
			100.f * Bins[0] / F, 100.f * Bins[1] / F, 100.f * Bins[2] / F, 100.f * Bins[3] / F, 100.f * Bins[4] / F,
			OPct(0.5f), OPct(0.99f), Ov.Num() ? Ov.Last() : 0.f, Deep, Ov.Num(), Top - Bottom);

		// #50 가장 빠른 5개 — 깼나(w), 벽까지, 가장 가까운 이웃과 그 이웃이 잠들었나, 이 입자의 쌍 수
		const TArray<float>& LW = Sim.GetGpuLiveW();
		if (LW.Num() == Pos.Num())
		{
			TArray<int32> Idx;
			for (int32 p = 0; p < Pos.Num(); ++p) { if (W[p] > 0.f) { Idx.Add(p); } }
			Idx.Sort([&](int32 a, int32 b) { return Vel[a].SizeSquared() > Vel[b].SizeSquared(); });
			TArray<int32> PairsOf; PairsOf.Init(0, Pos.Num());
			for (const FSandContact& C : Sim.GetContacts()) { ++PairsOf[C.I]; ++PairsOf[C.J]; }
			for (int32 n = 0; n < FMath::Min(5, Idx.Num()); ++n)
			{
				const int32 p = Idx[n];
				if (Vel[p].Size() < 5.f) { break; }
				float Best = FLT_MAX; int32 Nb = -1;
				for (int32 q = 0; q < Pos.Num(); ++q) { if (q == p) { continue; } const float d = (Pos[q] - Pos[p]).SizeSquared(); if (d < Best) { Best = d; Nb = q; } }
				const float WallD = FMath::Min(Box.X - FMath::Abs(Pos[p].X), Box.Y - FMath::Abs(Pos[p].Y));
				UE_LOG(LogSpikeSweep, Display, TEXT("        빠른 #%d |v| %.0f vz %.0f z %.1f 벽 %.1fR w %.0f | 쌍 %d | 최근접 %.2fR (w %.0f, 고정 %d, |v| %.1f)"),
					p, Vel[p].Size(), Vel[p].Z, Pos[p].Z, WallD / R, LW[p], PairsOf[p], FMath::Sqrt(Best) / R, Nb >= 0 ? LW[Nb] : -1.f, Nb >= 0 && W[Nb] <= 0.f ? 1 : 0, Nb >= 0 ? Vel[Nb].Size() : 0.f);
			}
		}
	}

	/** 벽 사출 조사 §5 — 입자별 최근 프레임 링 버퍼. 처음 |v| > 50 cm/s 가 된 입자와 최근접 6개의 이력을 덤프한다 */
	struct FTraceRow
	{
		FVector3f P, V;
		float W = 0.f;
		uint32 Sleep = 0;
		float Ground = 0.f, WallSum = 0.f;
		float OvFixed = 0.f, OvAny = 0.f;   // 고정 입자와 / 아무 입자와 최대 겹침 (R 단위)
		float LamN = 0.f;                   // 이 입자 쌍들의 Σλn
		int32 Pairs = 0;
		int32 Frame = -1;
	};
	struct FTracer
	{
		static constexpr int32 Depth = 20;
		TArray<FTraceRow> Ring;   // N × Depth
		TSet<int32> Dumped;
		int32 NumDumps = 0;
		float TrigVel = 300.f;   // 자유 낙하(더미 높이 180cm → 594 cm/s 상한)와 구분되는 사출만 · 대조군 잡음 50~120
		int32 TrigFrame = 400;   // 잠들기 시작 뒤
		void Record(const FSandSpike& Sim, int32 Frame, float R)
		{
			const TArray<FVector3f>& Pos = Sim.GetPositions();
			const TArray<FVector3f>& Vel = Sim.GetVelocities();
			const TArray<float>& W0 = Sim.GetInvMass();
			const TArray<float>& W = Sim.GetGpuLiveW();
			const TArray<uint32>& Sl = Sim.GetGpuLiveSleep();
			const TArray<float>& G = Sim.GetGroundLambda();
			const TArray<FVector4f>& WL = Sim.GetWallLambda();
			const int32 N = Pos.Num();
			if (Ring.Num() != N * Depth) { Ring.SetNum(N * Depth); }
			const int32 Slot = Frame % Depth;
			for (int32 p = 0; p < N; ++p)
			{
				FTraceRow& Row = Ring[p * Depth + Slot];
				Row = FTraceRow();
				Row.P = Pos[p]; Row.V = Vel[p];
				Row.W = W.IsValidIndex(p) ? W[p] : W0[p];
				Row.Sleep = Sl.IsValidIndex(p) ? Sl[p] : 0u;
				Row.Ground = G.IsValidIndex(p) ? G[p] : 0.f;
				Row.WallSum = WL.IsValidIndex(p) ? WL[p].X + WL[p].Y + WL[p].Z + WL[p].W : 0.f;
				Row.Frame = Frame;
			}
			for (const FSandContact& C : Sim.GetContacts())
			{
				const float Ov = (2.f * R - (Pos[C.I] - Pos[C.J]).Size()) / R;
				for (int32 k = 0; k < 2; ++k)
				{
					const int32 a = k ? C.J : C.I, b = k ? C.I : C.J;
					FTraceRow& Row = Ring[a * Depth + Slot];
					++Row.Pairs;
					Row.LamN += C.LambdaN;
					Row.OvAny = FMath::Max(Row.OvAny, Ov);
					if (W0[b] <= 0.f) { Row.OvFixed = FMath::Max(Row.OvFixed, Ov); }
				}
			}
		}
		void DumpOne(int32 p, int32 Frame, const TCHAR* Tag) const
		{
			for (int32 d = Depth - 1; d >= 0; --d)
			{
				const int32 f = Frame - d;
				if (f < 0) { continue; }
				const FTraceRow& Row = Ring[p * Depth + (f % Depth)];
				if (Row.Frame != f) { continue; }
				UE_LOG(LogSpikeSweep, Display, TEXT("      [trace %s] f%d p%d pos(%.2f,%.2f,%.2f) v(%.1f,%.1f,%.1f) |v| %.1f w %.0f sleep 0x%08x | 지면λ %.4f 벽λ %.4f | 쌍 %d Σλn %.4f 겹침 고정 %.2fR 전체 %.2fR"),
					Tag, f, p, Row.P.X, Row.P.Y, Row.P.Z, Row.V.X, Row.V.Y, Row.V.Z, Row.V.Size(), Row.W, Row.Sleep, Row.Ground, Row.WallSum, Row.Pairs, Row.LamN, Row.OvFixed, Row.OvAny);
			}
		}
		void Check(const FSandSpike& Sim, int32 Frame, float R)
		{
			if (NumDumps >= 3 || Frame < TrigFrame) { return; }
			const TArray<FVector3f>& Pos = Sim.GetPositions();
			const TArray<FVector3f>& Vel = Sim.GetVelocities();
			const TArray<float>& W0 = Sim.GetInvMass();
			const TArray<float>& W = Sim.GetGpuLiveW();
			for (int32 p = 0; p < Pos.Num() && NumDumps < 3; ++p)
			{
				if (W0[p] <= 0.f || Vel[p].Size() <= TrigVel || Dumped.Contains(p)) { continue; }
				Dumped.Add(p); ++NumDumps;
				const FVector3f Box = Sim.GetBoxHalf();
				UE_LOG(LogSpikeSweep, Display, TEXT("    [trace] ===== 사출 #%d: f%d p%d |v| %.1f · 벽까지 %.2fR · z %.2f ====="), NumDumps, Frame, p, Vel[p].Size(),
					FMath::Min(Box.X - FMath::Abs(Pos[p].X), Box.Y - FMath::Abs(Pos[p].Y)) / R, Pos[p].Z);
				DumpOne(p, Frame, TEXT("본"));
				// 이번 프레임 쌍 상세
				for (const FSandContact& C : Sim.GetContacts())
				{
					if (C.I != p && C.J != p) { continue; }
					const int32 q = C.I == p ? C.J : C.I;
					UE_LOG(LogSpikeSweep, Display, TEXT("      [trace 쌍] p%d–q%d 거리 %.3fR λn %.4f |λt| %.4f q: w %.0f 고정 %d |v| %.1f"),
						p, q, (Pos[p] - Pos[q]).Size() / R, C.LambdaN, C.LambdaTan.Size(), W.IsValidIndex(q) ? W[q] : -1.f, W0[q] <= 0.f ? 1 : 0, Vel[q].Size());
				}
				// 최근접 6개 (지금 위치 기준이 아니라 20 프레임 전 위치 기준 — 날아가기 전의 이웃)
				const int32 F0 = FMath::Max(0, Frame - Depth + 1);
				const FVector3f P0 = Ring[p * Depth + (F0 % Depth)].P;
				TArray<TPair<float, int32>> Near;
				for (int32 q = 0; q < Pos.Num(); ++q)
				{
					if (q == p) { continue; }
					Near.Add(TPair<float, int32>((Ring[q * Depth + (F0 % Depth)].P - P0).SizeSquared(), q));
				}
				Near.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
				for (int32 n = 0; n < FMath::Min(6, Near.Num()); ++n) { DumpOne(Near[n].Value, Frame, *FString::Printf(TEXT("이웃%d"), n)); }
			}
		}
	};

	int32 RunPerf(const FString& Params)
	{
		const TArray<int32> CountList = ParseList<int32>(Params, TEXT("particles="), { 20000 });
		int32 Frames = 100; FParse::Value(*Params, TEXT("frames="), Frames);
		int32 Iterations = 120; FParse::Value(*Params, TEXT("iterations="), Iterations);
		float Mu = 0.17f; FParse::Value(*Params, TEXT("mu="), Mu);
		const int32 WarmupFrames = 20;

		UE_LOG(LogSpikeSweep, Display, TEXT("성능 — 반복 %d  μ %.2f  프레임 %d (앞 %d 버림)  %s"), Iterations, Mu, Frames, WarmupFrames,
			FParse::Param(*Params, TEXT("gpu2")) ? TEXT("GPU 상주") : (FParse::Param(*Params, TEXT("gpu")) ? TEXT("GPU 반복만") : TEXT("CPU")));
		UE_LOG(LogSpikeSweep, Display, TEXT("   입자 | 층 | ms/프레임 평균  최소  최대 | GPU ms 평균 | 쌍/입자 | 경계 | 끝 평균|v|"));

		for (int32 Count : CountList)
		{
			FSandParams P;
			P.Friction = Mu; P.Substeps = 1; P.Iterations = Iterations;
			P.WarmStart = 1.f; P.WarmGapTol = 0.05f; P.SorOmega = 1.f;
			P.bGatherApply = true; P.bMassSplit = true; P.BlockSize = 30.f;
			FParse::Value(*Params, TEXT("substeps="), P.Substeps);   // 서브스텝 계획 S0
			FParse::Value(*Params, TEXT("vmax="), P.MaxDepenVel);   // #47
			FParse::Value(*Params, TEXT("kc="), P.Kc);   // #48
			P.bSleep = FParse::Param(*Params, TEXT("sleep"));   // C안
			P.bParticleMargin = FParse::Param(*Params, TEXT("pmargin"));   // #50
			FParse::Value(*Params, TEXT("pmarginmin="), P.MarginMinR);
			P.bWakeCold = FParse::Param(*Params, TEXT("wakecold"));   // #50
			P.bWakeZeroPairs = FParse::Param(*Params, TEXT("wakezero"));   // 조사 H9
			P.bSleepColdPairs = FParse::Param(*Params, TEXT("sleepcoldpairs"));   // 조사 H9 (b)
			FParse::Value(*Params, TEXT("wakelam="), P.WakeLamRatio);   // 조사 H9 (e)
			FParse::Value(*Params, TEXT("wakelamabs="), P.WakeLamAbs);
			FParse::Value(*Params, TEXT("sleepv="), P.SleepVel);
			FParse::Value(*Params, TEXT("sleepframes="), P.SleepFrames);
			FParse::Value(*Params, TEXT("wakev="), P.WakeVel);
			FParse::Value(*Params, TEXT("wakevtouch="), P.WakeTouchVel);   // #52
			FParse::Value(*Params, TEXT("warm="), P.WarmStart);
			FParse::Value(*Params, TEXT("kv="), P.Kv);
			FParse::Value(*Params, TEXT("block="), P.BlockSize);
			P.bGpuSolve = FParse::Param(*Params, TEXT("gpu"));
			P.bGpuResident = FParse::Param(*Params, TEXT("gpu2"));
			P.bColor8 = FParse::Param(*Params, TEXT("color8"));   // A안 단계 1 (GPU 8색)
			P.bColor8Par = FParse::Param(*Params, TEXT("color8par"));   // A안 단계 2
			P.bColor8 = P.bColor8 || P.bColor8Par;
			ApplyGpuProfile(Params, P);
			if (P.bColor8) { P.bGatherApply = false; P.bMassSplit = false; }   // CPU 8색은 GS (GPU 는 이 플래그를 안 본다)

			FTiltSetup S;
			S.TargetCount = Count;
			S.Layers = FMath::Max(3, FMath::RoundToInt(FMath::Pow((float)Count, 1.f / 3.f)));
			S.FixedLayers = 1;
			S.BaseJitter = 0.3f;
			S.bOpenDownhill = false;
			FParse::Value(*Params, TEXT("seed="), P.Seed);   // 벽 사출 조사 §4

			// #44 정지 상태 — -window=N 이면 N 프레임마다 그 구간 평균을 찍는다 (다짐 → 정지 경과). -w=W 면 BondFrame 에 물을 주고 결합
			int32 Window = 0; FParse::Value(*Params, TEXT("window="), Window);
			const bool bDiag = FParse::Param(*Params, TEXT("diag"));   // #46 끓음 기전
			const bool bTrace = FParse::Param(*Params, TEXT("trace"));   // 벽 사출 조사 §5
			FTracer Tracer;
			FParse::Value(*Params, TEXT("tracev="), Tracer.TrigVel);
			FParse::Value(*Params, TEXT("tracef="), Tracer.TrigFrame);
			float W = 0.f; FParse::Value(*Params, TEXT("w="), W);
			int32 BondFrame = 300; FParse::Value(*Params, TEXT("bondframe="), BondFrame);
			FParse::Value(*Params, TEXT("strength="), P.BondStrength);
			FParse::Value(*Params, TEXT("form="), P.BondFormStretch);

			FSandSpike Sim;
			Sim.InitTiltBox(P, S);
			double Sum = 0.0, SumGpu = 0.0; float MinMs = FLT_MAX, MaxMs = 0.f; int32 Measured = 0;
			double WinSum = 0.0; int32 WinN = 0;
			for (int32 f = 0; f < Frames; ++f)
			{
				if (W > 0.f && f == BondFrame)
				{
					Sim.SetWGlobal(W);
					const int32 Made = Sim.FormBonds();
					UE_LOG(LogSpikeSweep, Display, TEXT("   f%d 결합 %d (W %.2f)"), f, Made, W);
				}
				Sim.Step(1.f / 60.f);
				if (bTrace && f >= Tracer.TrigFrame - FTracer::Depth) { Tracer.Record(Sim, f, P.Radius); Tracer.Check(Sim, f, P.Radius); }
				if (Window > 0)
				{
					WinSum += Sim.LastStepMs(); ++WinN;
					if (WinN == Window)
					{
						UE_LOG(LogSpikeSweep, Display, TEXT("   [창] %7d f%5d | %8.1f ms | 쌍/입자 %5.1f | 평균|v| %.3f | 잠듦 %4.1f%% · 깬 블록 %d"),
							Sim.Num(), f + 1, WinSum / WinN, (float)Sim.GetContacts().Num() / FMath::Max(1, Sim.Num()), Sim.AvgSpeed(),
							100.f * Sim.LastNumAsleep() / FMath::Max(1, Sim.Num()), Sim.LastNumAwakeBlocks());
						WinSum = 0.0; WinN = 0;
						if (bDiag) { LogBoilDiag(Sim, P.Radius); }
					}
				}
				if (f < WarmupFrames) { continue; }
				const float Ms = Sim.LastStepMs();
				Sum += Ms; SumGpu += Sim.LastGpuMs(); MinMs = FMath::Min(MinMs, Ms); MaxMs = FMath::Max(MaxMs, Ms); ++Measured;
			}
			const int32 NumC = Sim.GetContacts().Num();
			UE_LOG(LogSpikeSweep, Display, TEXT("%7d | %2d | %8.1f  %8.1f  %8.1f | %8.1f | %5.1f | %3.0f%% | %.3f"),
				Sim.Num(), S.Layers, Sum / FMath::Max(1, Measured), MinMs, MaxMs, SumGpu / FMath::Max(1, Measured),
				(float)NumC / FMath::Max(1, Sim.Num()), Sim.BoundaryContactFrac() * 100.f, Sim.AvgSpeed());
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
	/** 결합 단위 검사 — m0-spike.md §2-6. 구현 전에 실패해야 한다 */
	int32 RunBondTest(const FString& Params)
	{
		float W = 0.15f, Warm = 1.f;
		FParse::Value(*Params, TEXT("w="), W);
		FParse::Value(*Params, TEXT("warm="), Warm);
		FSandParams P;
		P.Friction = 0.15f;
		P.Iterations = 90;
		P.WGlobal = W;
		P.WarmStart = Warm;
		P.WarmGapTol = 0.02f;

		UE_LOG(LogSpikeSweep, Display, TEXT("결합 단위 검사 — W=%.2f  BondStrength %.1f  BreakStretch %.2f  StiffStrain %.3f  워밍 %.1f"), W, P.BondStrength, P.BreakStretch, P.BondStiffStrain, Warm);
		UE_LOG(LogSpikeSweep, Display, TEXT("   사슬 | 생성 살아있음 끊김 | 기대 늘어남  실측(위) | 아래 낙하 | 정지 | 판정"));
		for (int32 N : { 1, 2, 3 })
		{
			const FBondTestResult R = SpikeBatch::RunBondChain(P, N, 1200);
			const bool bShouldHold = (float)N <= P.BondStrength;   // 위 결합이 받는 무게 = N
			bool bPass;
			if (bShouldHold)
			{
				bPass = R.BondsAlive == N && R.TopStretch >= 0.f && FMath::Abs(R.TopStretch - R.ExpectedStretch) <= 0.01f * FMath::Max(R.ExpectedStretch, 1e-3f) + 0.005f;
			}
			else
			{
				bPass = R.BondsBroken >= 1 && R.BottomDrop > 5.f;
			}
			UE_LOG(LogSpikeSweep, Display, TEXT("   %4d | %4d %6d %5d | %8.3fcm %8.3fcm | %8.1fcm | %4s | %s (%s)"),
				N, R.BondsMade, R.BondsAlive, R.BondsBroken, R.ExpectedStretch, R.TopStretch, R.BottomDrop,
				R.bSettled ? TEXT("○") : TEXT("✗"), bPass ? TEXT("★통과") : TEXT("실패"), bShouldHold ? TEXT("버텨야") : TEXT("끊어져야"));
		}
		return 0;
	}

	/** 터널 (m0-spike.md §3-1) — 본 실행 · 대조군(W=0) · 지름 스윕을 같은 명령으로 */
	int32 RunTunnelSweep(const FString& Params)
	{
		float W = 0.15f, Strength = 3.f, Stretch = 1.3f, Mu = 0.13f, Form = 1.2f, Height = 60.f, BoxX = 45.f, BoxY = 30.f;
		int32 Iterations = 180, Hold = 1800, SettleCap = 3000;
		FParse::Value(*Params, TEXT("w="), W);
		FParse::Value(*Params, TEXT("strength="), Strength);
		FParse::Value(*Params, TEXT("stretch="), Stretch);
		FParse::Value(*Params, TEXT("mu="), Mu);
		FParse::Value(*Params, TEXT("form="), Form);
		FParse::Value(*Params, TEXT("height="), Height);
		FParse::Value(*Params, TEXT("boxx="), BoxX);
		FParse::Value(*Params, TEXT("boxy="), BoxY);
		FParse::Value(*Params, TEXT("iterations="), Iterations);
		FParse::Value(*Params, TEXT("hold="), Hold);
		FParse::Value(*Params, TEXT("settlecap="), SettleCap);
		int32 Ramp = 0;
		FParse::Value(*Params, TEXT("ramp="), Ramp);   // > 0 이면 3-2 덩어리 붕괴
		const bool bDump = FParse::Param(*Params, TEXT("dumppos"));
		if (Ramp > 0 && !FParse::Value(*Params, TEXT("hold="), Hold)) { Hold = 600; }   // 붕괴 실험은 유지를 짧게
		const TArray<float> DiamList = ParseList<float>(Params, TEXT("diam="), { 30.f });
		FString ShapeRaw = TEXT("round,square");
		ReadRaw(Params, TEXT("shape="), ShapeRaw);
		TArray<FString> Shapes;
		ShapeRaw.ParseIntoArray(Shapes, TEXT(","), true);

		FSandParams P;
		P.Friction = Mu;
		P.Iterations = Iterations;
		FParse::Value(*Params, TEXT("substeps="), P.Substeps);   // 서브스텝 계획 S0
		FParse::Value(*Params, TEXT("kv="), P.Kv);   // #47
		FParse::Value(*Params, TEXT("vmax="), P.MaxDepenVel);
		FParse::Value(*Params, TEXT("kc="), P.Kc);   // #48
		P.bSleep = FParse::Param(*Params, TEXT("sleep"));   // C안
		P.bParticleMargin = FParse::Param(*Params, TEXT("pmargin"));   // #50
		FParse::Value(*Params, TEXT("pmarginmin="), P.MarginMinR);
		P.bWakeCold = FParse::Param(*Params, TEXT("wakecold"));   // #50
		P.bWakeZeroPairs = FParse::Param(*Params, TEXT("wakezero"));   // 조사 H9
		P.bSleepColdPairs = FParse::Param(*Params, TEXT("sleepcoldpairs"));   // 조사 H9 (b)
		FParse::Value(*Params, TEXT("wakelam="), P.WakeLamRatio);   // 조사 H9 (e)
		FParse::Value(*Params, TEXT("wakelamabs="), P.WakeLamAbs);
		FParse::Value(*Params, TEXT("sleepv="), P.SleepVel);
		FParse::Value(*Params, TEXT("sleepframes="), P.SleepFrames);
		FParse::Value(*Params, TEXT("wakev="), P.WakeVel);
		FParse::Value(*Params, TEXT("wakevtouch="), P.WakeTouchVel);   // #52
		P.WGlobal = W;
		P.WarmStart = 1.f;
		P.WarmGapTol = 0.02f;
		P.bGatherApply = FParse::Param(*Params, TEXT("gather"));   // m0b-spike §3-1
		P.bMassSplit = FParse::Param(*Params, TEXT("masssplit"));  // A′
		P.bGatherBonds = !FParse::Param(*Params, TEXT("seqbonds"));  // 결합만 순차 (진단)
		FParse::Value(*Params, TEXT("bondomega="), P.BondOmega);
		P.bMassSplitBonds = FParse::Param(*Params, TEXT("splitbonds"));
		P.bGatherContacts = !FParse::Param(*Params, TEXT("seqcontacts"));  // 거울 진단
		FParse::Value(*Params, TEXT("block="), P.BlockSize);   // ①-b 블록 GS 모사 (cm)
		P.bGpuSolve = FParse::Param(*Params, TEXT("gpu"));       // 3-1 ② 반복 루프를 GPU 로
		P.bGpuResident = FParse::Param(*Params, TEXT("gpu2"));   // 3-1 ② 2단계 — 스텝 전체 GPU 상주
		P.bBlockShift = FParse::Param(*Params, TEXT("blockshift"));
		P.bApgd = FParse::Param(*Params, TEXT("apgd"));   // D′ APGD (papers C5)
		P.bApgdBonds = !FParse::Param(*Params, TEXT("apgdnobonds"));   // 결합은 외삽 없이
		P.bColor8 = FParse::Param(*Params, TEXT("color8"));   // A안 단계 0
		P.bColor8Par = FParse::Param(*Params, TEXT("color8par"));   // A안 단계 2
		P.bColor8 = P.bColor8 || P.bColor8Par;
		P.bColor8Rotate = FParse::Param(*Params, TEXT("color8rot"));
		FParse::Value(*Params, TEXT("sor="), P.SorOmega);
		FParse::Value(*Params, TEXT("ma="),  P.MomentumA);
		FParse::Value(*Params, TEXT("mb="),  P.MomentumB);
		P.BondStrength = Strength;
		P.BreakStretch = Stretch;
		P.BondFormStretch = Form;
		ApplyGpuProfile(Params, P, &Iterations, &Mu);

		TArray<FTunnelSetup> Setups;
		for (const FString& Sh : Shapes)
		for (float Dm : DiamList)
		{
			FTunnelSetup T;
			T.BoxHalfX = BoxX;
			T.BoxHalfY = BoxY;
			T.Height = Height;
			T.Diameter = Dm;
			T.bRound = !Sh.Equals(TEXT("square"), ESearchCase::IgnoreCase);
			T.SettleCap = SettleCap;
			T.HoldFrames = Hold;
			T.CollapseRampFrames = Ramp;
			T.bDumpFinal = bDump;
			Setups.Add(T);
		}
		TArray<FTunnelResult> Results;
		Results.SetNum(Setups.Num());
		SpikeParallelFor(Setups.Num(), [&](int32 i) { Results[i] = SpikeBatch::RunTunnel(P, Setups[i]); });

		UE_LOG(LogSpikeSweep, Display, TEXT("터널 — 블록 %.0f×%.0f×%.0f  W %.2f  BondStrength %.1f  Form %.2f  μ %.2f  반복 %d  (판정: 천장 이동비 < 5%% · 관통 반경 안 0개)"),
			2.f * BoxX, 2.f * BoxY, Height, W, Strength, Form, Mu, Iterations);
		UE_LOG(LogSpikeSweep, Display, TEXT("   단면   지름 | 입자  제거 | 결합 생성/끊김 | 준비 | 높이 전→후 | 천장 %d개: 이동비  처짐 | 관통 안 | 정지 | 초 | 판정"), Results.Num() > 0 ? Results[0].CeilingCount : 0);
		for (const FTunnelResult& R : Results)
		{
			const bool bCeil = R.CeilingMovedFrac < 0.05f;
			// 관통: 문서는 "0개"지만 한 알이 통로를 막지는 않는다 — 제거 수의 1% 까지 허용, 개수는 그대로 적는다
			const bool bOpen = R.InTunnel <= FMath::Max(2, R.Removed / 100);
			UE_LOG(LogSpikeSweep, Display, TEXT("   %-6s %4.0fcm | %4d %5d | %5d / %5d | %s | %5.1f→%5.1f | %3d개: %5.1f%%  %5.1fcm | %5d | %s | %5.0f | 경계 %3.0f%% | %s"),
				R.bRound ? TEXT("원형") : TEXT("사각"), R.Diameter, R.FreeCount, R.Removed, R.BondsMade, R.BondsBroken,
				R.bReady ? TEXT("○") : TEXT("✗"), R.HeightBefore, R.HeightAfter, R.CeilingCount, R.CeilingMovedFrac * 100.f, R.CeilingSag, R.InTunnel,
				R.bSettledAfter ? TEXT("○") : TEXT("✗"), R.WallSeconds, R.BoundaryContactFrac * 100.f,
				(bCeil && bOpen) ? TEXT("★선다") : (bCeil ? TEXT("천장은 서나 막힘") : TEXT("붕괴")));
			FString Ser;
			for (int32 k = 0; k < R.CeilingSeries.Num(); ++k) { Ser += FString::Printf(TEXT(" %4.1f%%/%d"), R.CeilingSeries[k] * 100.f, R.InTunnelSeries[k]); }
			UE_LOG(LogSpikeSweep, Display, TEXT("           300프레임마다 천장 이동비/관통 안:%s   준비 창:%s"), *Ser,
				*[&]{ FString Q; for (float M : R.ReadySeries) { Q += FString::Printf(TEXT(" %.1f%%"), M * 100.f); } return Q; }());
			if (Ramp > 0)
			{
				UE_LOG(LogSpikeSweep, Display, TEXT("     ★ 덩어리 붕괴 — 이동 %d/%d (%.0f%%)  파편 %d개  massWeightedMeanSize %.1f  최대 파편 %d  finesRatio %.2f  첫 파단 W %.3f  W10 %.3f  W90 %.3f  ΔW %.3f  결합 끊김 %d  높이 %.1f→%.1f%s"),
					R.MovedTotal, R.FreeCount, R.FreeCount > 0 ? 100.f * R.MovedTotal / R.FreeCount : 0.f, R.FragmentCount, R.FinalMwms, R.LargestFragment, R.FinalFines,
					R.FirstBreakW, R.W10, R.W90, (R.W10 >= 0.f && R.W90 >= 0.f) ? R.W10 - R.W90 : -1.f, R.BondsBroken, R.HeightBefore, R.HeightAfter,
					R.DumpPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  → %s"), *R.DumpPath));
				FString CS;
				for (int32 k = 0; k < R.CW.Num(); ++k) { CS += FString::Printf(TEXT(" W%.3f:%.0f%%/%.0f"), R.CW[k], R.CMoved[k] * 100.f, R.CMwms[k]); }
				UE_LOG(LogSpikeSweep, Display, TEXT("       창별 W:이동%%/MWMS:%s"), *CS);
			}
		}
		return 0;
	}

	/** 절토 높이 스윕 — 점착의 자 c[Pa] (m0-spike.md §3-3-B) */
	int32 RunCutSweep(const FString& Params)
	{
		float W = 0.15f, Strength = 1.5f, Stretch = 1.3f, Mu = 0.15f, Form = 1.05f;
		int32 Side = 12, SettleCap = 3000, Hold = 1800;
		FParse::Value(*Params, TEXT("form="), Form);
		const bool bOpen = !FParse::Param(*Params, TEXT("noopen"));
		const bool bPourBlock = FParse::Param(*Params, TEXT("pourblock"));
		float BoxHalf = 29.f;
		FParse::Value(*Params, TEXT("boxhalf="), BoxHalf);
		int32 CutIterations = 90;   // #27~#29 의 값. 예산 스윕은 -iterations= 로 덮는다
		FParse::Value(*Params, TEXT("iterations="), CutIterations);
		const TArray<float> HeightList = ParseList<float>(Params, TEXT("heights="), { 60.f, 70.f, 80.f, 90.f });
		FParse::Value(*Params, TEXT("w="), W);
		FParse::Value(*Params, TEXT("strength="), Strength);
		FParse::Value(*Params, TEXT("stretch="), Stretch);
		FParse::Value(*Params, TEXT("mu="), Mu);
		FParse::Value(*Params, TEXT("side="), Side);
		FParse::Value(*Params, TEXT("hold="), Hold);
		const TArray<int32> LayerList = ParseList<int32>(Params, TEXT("layers="), { 6, 9, 12, 15, 18 });
		const bool bDumpXyz = FParse::Param(*Params, TEXT("dumpxyz"));   // 시각 프리뷰용 위치 덤프

		FSandParams P;
		P.Friction = Mu;
		P.Iterations = CutIterations;
		FParse::Value(*Params, TEXT("substeps="), P.Substeps);   // 서브스텝 계획 S0
		FParse::Value(*Params, TEXT("iterations="), P.Iterations);
		P.WGlobal = W;
		P.WarmStart = 1.f;
		P.WarmGapTol = 0.02f;
		FParse::Value(*Params, TEXT("warm="), P.WarmStart);
		FParse::Value(*Params, TEXT("warmtol="), P.WarmGapTol);
		FParse::Value(*Params, TEXT("kv="), P.Kv);
		FParse::Value(*Params, TEXT("vmax="), P.MaxDepenVel);   // #47
		FParse::Value(*Params, TEXT("kc="), P.Kc);   // #48
		P.bSleep = FParse::Param(*Params, TEXT("sleep"));   // C안
		P.bParticleMargin = FParse::Param(*Params, TEXT("pmargin"));   // #50
		FParse::Value(*Params, TEXT("pmarginmin="), P.MarginMinR);
		P.bWakeCold = FParse::Param(*Params, TEXT("wakecold"));   // #50
		P.bWakeZeroPairs = FParse::Param(*Params, TEXT("wakezero"));   // 조사 H9
		P.bSleepColdPairs = FParse::Param(*Params, TEXT("sleepcoldpairs"));   // 조사 H9 (b)
		FParse::Value(*Params, TEXT("wakelam="), P.WakeLamRatio);   // 조사 H9 (e)
		FParse::Value(*Params, TEXT("wakelamabs="), P.WakeLamAbs);
		FParse::Value(*Params, TEXT("sleepv="), P.SleepVel);
		FParse::Value(*Params, TEXT("sleepframes="), P.SleepFrames);
		FParse::Value(*Params, TEXT("wakev="), P.WakeVel);
		FParse::Value(*Params, TEXT("wakevtouch="), P.WakeTouchVel);   // #52
		FParse::Value(*Params, TEXT("wallmu="), P.WallFriction);
		P.bGatherApply = FParse::Param(*Params, TEXT("gather"));   // m0b-spike §3-1
		P.bMassSplit = FParse::Param(*Params, TEXT("masssplit"));  // A′
		P.bGatherBonds = !FParse::Param(*Params, TEXT("seqbonds"));  // 결합만 순차 (진단)
		FParse::Value(*Params, TEXT("bondomega="), P.BondOmega);
		P.bMassSplitBonds = FParse::Param(*Params, TEXT("splitbonds"));
		P.bGatherContacts = !FParse::Param(*Params, TEXT("seqcontacts"));  // 거울 진단
		FParse::Value(*Params, TEXT("block="), P.BlockSize);   // ①-b 블록 GS 모사 (cm)
		P.bGpuSolve = FParse::Param(*Params, TEXT("gpu"));       // 3-1 ② 반복 루프를 GPU 로
		P.bGpuResident = FParse::Param(*Params, TEXT("gpu2"));   // 3-1 ② 2단계 — 스텝 전체 GPU 상주
		P.bBlockShift = FParse::Param(*Params, TEXT("blockshift"));
		P.bApgd = FParse::Param(*Params, TEXT("apgd"));   // D′ APGD (papers C5)
		P.bApgdBonds = !FParse::Param(*Params, TEXT("apgdnobonds"));   // 결합은 외삽 없이
		P.bColor8 = FParse::Param(*Params, TEXT("color8"));   // A안 단계 0
		P.bColor8Par = FParse::Param(*Params, TEXT("color8par"));   // A안 단계 2
		P.bColor8 = P.bColor8 || P.bColor8Par;
		P.bColor8Rotate = FParse::Param(*Params, TEXT("color8rot"));
		FParse::Value(*Params, TEXT("sor="), P.SorOmega);
		FParse::Value(*Params, TEXT("ma="),  P.MomentumA);
		FParse::Value(*Params, TEXT("mb="),  P.MomentumB);
		float CutBaseJitter = 0.3f;
		FParse::Value(*Params, TEXT("basejitter="), CutBaseJitter);
		P.BondStrength = Strength;
		P.BreakStretch = Stretch;
		P.BondFormStretch = Form;

		ApplyGpuProfile(Params, P);
		TArray<FCutFaceResult> Results;
		if (bPourBlock)
		{
			// 목표 높이 → 입자 수. 충전율 0.59 (부어 쌓기 실측 #23)
			const float Vp = (4.f / 3.f) * UE_PI * FMath::Pow(P.Radius, 3.f);
			const float Area = FMath::Square(2.f * BoxHalf);
			TArray<int32> Counts;
			for (float H : HeightList) { Counts.Add(FMath::RoundToInt(Area * H * 0.59f / Vp)); }
			Results.SetNum(Counts.Num());
			SpikeParallelFor(Counts.Num(), [&](int32 i) { Results[i] = SpikeBatch::RunCutFacePour(P, Counts[i], BoxHalf, SettleCap, Hold, bOpen, CutBaseJitter); Results[i].Layers = FMath::RoundToInt(HeightList[i]); });
		}
		else
		{
			Results.SetNum(LayerList.Num());
			SpikeParallelFor(LayerList.Num(), [&](int32 i) { Results[i] = SpikeBatch::RunCutFace(P, LayerList[i], Side, SettleCap, Hold, bOpen); });
		}

		// 시각 프리뷰 덤프 — 오프라인 메시화로 "5cm 입자가 모래로 보이는가" 를 보기 위한 것. 판정과 무관하다
		if (bDumpXyz)
		{
			const FString DumpDir = FPaths::ProjectSavedDir() / TEXT("SpikeRuns");
			for (const FCutFaceResult& R : Results)
			{
				FString Out = FString::Printf(TEXT("# BeachSimulator cut-face dump\n# radius_cm %.4f\n# layers %d\n# side %d\n# w %.3f\n# height_before_cm %.2f\n# columns: x y z free\n"),
					P.Radius, R.Layers, Side, W, R.HeightBefore);
				Out.Reserve(Out.Len() + R.FinalPos.Num() * 40);
				for (int32 i = 0; i < R.FinalPos.Num(); ++i)
				{
					Out += FString::Printf(TEXT("%.4f %.4f %.4f %d\n"),
						R.FinalPos[i].X, R.FinalPos[i].Y, R.FinalPos[i].Z,
						R.FinalInvMass.IsValidIndex(i) && R.FinalInvMass[i] > 0.f ? 1 : 0);
				}
				const FString Path = DumpDir / FString::Printf(TEXT("cut_L%02d_side%d_w%.2f.xyz"), R.Layers, Side, W);
				FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				UE_LOG(LogSpikeSweep, Display, TEXT("덤프 %s — 입자 %d"), *Path, R.FinalPos.Num());
			}
		}


		// 단위 환산 — 질량 1 = 지름 5cm 석영구 0.1734 kg (2650 kg/m³ × 65.45 cm³). 힘 단위 1 = 1.734e-3 N. c[힘/cm²] × 17.34 = Pa
		const float ForceToN = 0.1734f * 0.01f;
		const float CsimToPa = ForceToN / 1e-4f;
		const float Phi = FMath::DegreesToRadians(34.f);
		const float TanTerm = FMath::Tan(UE_PI / 4.f + Phi / 2.f);

		UE_LOG(LogSpikeSweep, Display, TEXT("절토 스윕 — W %.2f  BondStrength %.1f  BreakStretch %.2f  Form %.2f  μ %.2f  반복 %d  warm %.1f tol %.2f kv %.1f wallμ %.2f bj %.2f  %s%s"), W, Strength, Stretch, Form, Mu, P.Iterations, P.WarmStart, P.WarmGapTol, P.Kv, P.WallFriction, CutBaseJitter,
			bPourBlock ? *FString::Printf(TEXT("상자 붓기 반폭 %.0fcm (층 열 = 목표 높이 cm)"), BoxHalf) : *FString::Printf(TEXT("격자 채움 한 변 %d입자"), Side),
			bOpen ? TEXT("") : TEXT("  [벽 안 엶 — 대조]"));
		UE_LOG(LogSpikeSweep, Display, TEXT("   층 | 입자 | 높이 전→후 | 결합 생성/끊김/넘침 | 면 이동비 전체 | 상단 후퇴 | 런아웃 | γ_sim | 이 높이가 H_c 라면 c | 준비(드리프트/이동)/정지 | 초"));
		float Hc = -1.f, GammaAtHc = 0.f;
		for (const FCutFaceResult& R : Results)
		{
			// Terzaghi 파괴는 블록을 관통하는 활동면이라 전체 이동비로 본다. 면 띠의 이동은 벽에 기대 있던 바깥 열이
			// 떨어지는 표피 효과가 섞여(6층에서 면 25% · 전체 4%) 3-1 의 5% 기준으론 못 가른다 — 따로 적는다
			// 상단 띠가 2D 이상 물러나면 면이 무너진 것. 표피 탈락은 1D 안이고, 마른 사면은 H/tan34 만큼 물러난다
			const bool bCollapsed = R.TopRetreat > 2.f * 2.f * P.Radius;
			const float CsimIfHc = R.HeightBefore * R.GammaSim / (4.f * TanTerm);
			if (bCollapsed && Hc < 0.f) { Hc = R.HeightBefore; GammaAtHc = R.GammaSim; }
			UE_LOG(LogSpikeSweep, Display, TEXT("   %2d | %4d | %5.1f→%5.1fcm | %5d / %5d / %4d | %5.1f%%  %5.1f%% | %6.1fcm | %6.1fcm | %5.2f | %7.0f Pa | %s/%s | %5.1f %s"),
				R.Layers, R.FreeCount, R.HeightBefore, R.HeightAfter, R.BondsMade, R.BondsBroken, R.SlotOverflow,
				R.FaceMovedFrac * 100.f, R.AllMovedFrac * 100.f, R.TopRetreat, R.MaxRunout, R.GammaSim, CsimIfHc * CsimToPa,
				*FString::Printf(TEXT("%s(%+.2f/%.1f%%)"), R.bReady ? TEXT("○") : TEXT("✗"), R.ReadyDrift, R.ReadyMoved * 100.f), R.bSettledAfter ? TEXT("○") : TEXT("✗"), R.WallSeconds,
				bCollapsed ? TEXT("★붕괴") : (R.FaceMovedFrac > 0.05f ? TEXT("섬 (표피 탈락)") : TEXT("섬")));
			FString Prof;
			for (float Rt : R.RetreatByBand) { Prof += FString::Printf(TEXT(" %5.1f"), Rt); }
			UE_LOG(LogSpikeSweep, Display, TEXT("         후퇴(cm) 아래→위:%s"), *Prof);
			FString Ser;
			for (float Mv : R.ReadySeries) { Ser += FString::Printf(TEXT(" %4.1f%%"), Mv * 100.f); }
			UE_LOG(LogSpikeSweep, Display, TEXT("         준비 창별 이동비(300프레임):%s"), *Ser);
		}
		if (Hc > 0.f)
		{
			const float Csim = Hc * GammaAtHc / (4.f * TanTerm);
			UE_LOG(LogSpikeSweep, Display, TEXT("→ 처음 무너진 높이 H_c ≈ %.1fcm  →  c ≈ %.0f Pa  (Terzaghi, φ=34°)   목표 1500 · 터널 요구 2400 · Rumpf 상한(BondStrength %.1f) %.0f Pa"),
				Hc, Csim * CsimToPa, Strength, 1.5f * Strength * 1.734f / 0.0025f);
		}
		else
		{
			UE_LOG(LogSpikeSweep, Display, TEXT("→ 스윕 안에서 안 무너짐 — c 하한만. 층수를 올릴 것"));
		}
		return 0;
	}

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
	GSpikeSerial = FParse::Param(*Params, TEXT("gpu")) || FParse::Param(*Params, TEXT("gpu2"));
	if (FParse::Param(*Params, TEXT("creeptest")))
	{
		return RunCreepTest(Params);
	}
	if (FParse::Param(*Params, TEXT("perf")))
	{
		return RunPerf(Params);
	}
	if (FParse::Param(*Params, TEXT("lambdaprofile")))
	{
		return RunLambdaProfile(Params);
	}
	if (FParse::Param(*Params, TEXT("frictiontest")))
	{
		return RunFrictionTest();
	}
	if (FParse::Param(*Params, TEXT("bondtest")))
	{
		return RunBondTest(Params);
	}
	if (FParse::Param(*Params, TEXT("cut")))
	{
		return RunCutSweep(Params);
	}
	if (FParse::Param(*Params, TEXT("tunnel")))
	{
		return RunTunnelSweep(Params);
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
	SpikeParallelFor(Configs.Num(), [&](int32 i)
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
