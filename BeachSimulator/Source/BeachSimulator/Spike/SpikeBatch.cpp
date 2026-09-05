#include "SpikeBatch.h"

namespace
{
	constexpr float BatchDt = 1.f / 60.f;   // 실시간 뷰의 FixedDt 와 같아야 한다
	constexpr int32 CreepWindow = 60;       // 1초
}

FSpikeRunResult SpikeBatch::RunOne(const FSandParams& Params, const FColumnSetup& Column,
                                   int32 MaxFrames, bool bCapturePerFrame, int32 ReleaseDelayFrames)
{
	const double T0 = FPlatformTime::Seconds();

	FSpikeRunResult R;
	R.Friction   = Params.Friction;
	R.Relaxation = Params.Relaxation;
	R.Substeps   = Params.Substeps;
	R.Iterations = Params.Iterations;
	R.RunName    = FString::Printf(TEXT("aor_mu%.2f_w%.2f_ss%d_it%d"),
		Params.Friction, Params.Relaxation, Params.Substeps, Params.Iterations);

	FSandSpike Sim;
	Sim.Init(Params, Column);
	R.Particles = Sim.Num();

	FSpikeRecorder Rec;
	if (bCapturePerFrame)
	{
		Rec.Begin(R.RunName, Sim);
	}

	// --- 1단계: 벽 안에서 다지기. 정지하고 지연이 지나면 벽을 뺀다 ---
	int32 HeldFrames = 0;
	while (Sim.WallsActive() && Sim.FrameIndex() < MaxFrames)
	{
		Sim.Step(BatchDt);
		Rec.RecordFrame(Sim);

		HeldFrames = Sim.IsSettled() ? HeldFrames + 1 : 0;
		if (HeldFrames >= ReleaseDelayFrames)
		{
			Sim.ReleaseWalls();
			R.ReleaseFrame = Sim.FrameIndex();
			R.bReleased    = true;
		}
	}

	// --- 2단계: 붕괴. 다시 정지할 때까지 ---
	TArray<float> PeakHist;
	PeakHist.Reserve(CreepWindow + 1);
	while (!Sim.IsSettled() && Sim.FrameIndex() < MaxFrames)
	{
		Sim.Step(BatchDt);
		Rec.RecordFrame(Sim);

		PeakHist.Add(SpikeMeasure::PeakHeight(Sim.GetPositions()));
		if (PeakHist.Num() > CreepWindow + 1)
		{
			PeakHist.RemoveAt(0, EAllowShrinking::No);
		}
	}

	R.bSettled    = Sim.IsSettled();
	R.SettleFrame = Sim.FrameIndex();
	R.SettleTime  = Sim.SimTime();
	R.AvgSpeed    = Sim.AvgSpeed();
	R.MaxSpeed    = Sim.MaxSpeed();
	R.PeakHeight  = SpikeMeasure::PeakHeight(Sim.GetPositions());

	// 정지 직전 1초 동안 최고점이 얼마나 내려갔는가. 판정만 통과하고 흐르는 중인지 가른다
	if (PeakHist.Num() >= 2)
	{
		const float Elapsed = (PeakHist.Num() - 1) * BatchDt;
		R.CreepRate = (PeakHist[0] - PeakHist.Last()) / Elapsed;
	}

	R.Angle = SpikeMeasure::AngleOfRepose(Sim.GetPositions(), Params.Radius);
	R.Profile = SpikeMeasure::RadialProfile(Sim.GetPositions(), Params.Radius, R.ProfileBinWidth);

	if (bCapturePerFrame)
	{
		R.PerFrameCsv = Rec.BuildText(Sim, R.Angle);
	}

	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}

FString SpikeBatch::SummaryHeader()
{
	return TEXT("mu,omega,substeps,iterations,particles,")
	       TEXT("angleProfile,angleMode,angleMedian,angleMean,surface,sampled,")
	       TEXT("releaseFrame,released,settleFrame,settleTime,settled,valid,")
	       TEXT("peakHeight,creepCmPerSec,avgSpeed,maxSpeed,wallSeconds");
}

FString SpikeBatch::SummaryRow(const FSpikeRunResult& R)
{
	return FString::Printf(
		TEXT("%.2f,%.2f,%d,%d,%d,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%.2f,%d,%d,%.2f,%.3f,%.3f,%.3f,%.1f"),
		R.Friction, R.Relaxation, R.Substeps, R.Iterations, R.Particles,
		R.Angle.ProfileDeg, R.Angle.ModeDeg, R.Angle.MedianDeg, R.Angle.MeanDeg,
		R.Angle.SurfaceNum, R.Angle.SampledNum,
		R.ReleaseFrame, R.bReleased ? 1 : 0, R.SettleFrame, R.SettleTime, R.bSettled ? 1 : 0,
		(R.bReleased && R.bSettled) ? 1 : 0,
		R.PeakHeight, R.CreepRate, R.AvgSpeed, R.MaxSpeed, R.WallSeconds);
}


// ---------------------------------------------------------------------------

FTiltRunResult SpikeBatch::RunTilt(const FSandParams& Params, const FTiltSetup& Setup,
                                   float RateDegPerSec, float MaxDeg)
{
	const double T0 = FPlatformTime::Seconds();

	FTiltRunResult R;
	R.Friction      = Params.Friction;
	R.RateDegPerSec = RateDegPerSec;

	FSandSpike Sim;
	Sim.InitTiltBox(Params, Setup);
	R.Particles = Sim.Num();

	// --- 1단계: 수평에서 다진다 ---
	const int32 PackCap = 1800;
	while (!Sim.IsSettled() && Sim.FrameIndex() < PackCap)
	{
		Sim.Step(BatchDt);
	}
	R.PackFrames = (float)Sim.FrameIndex();

	// --- 2단계: 서서히 기울인다 ---
	// 개시 판정은 정지 판정과 같은 임계를 쓴다(§2-2). 정지의 반대가 곧 개시다.
	// 단 사면 방향이 열려 있어 가장자리는 항상 조금씩 흘러내리므로 중앙 구역만 본다
	// 판정은 속도가 아니라 실제 이동 거리로 한다.
	// 기울이는 동안엔 미세 재배치가 늘 있어서 속도 임계는 그걸 사태로 오인한다
	// (같은 이유로 기울기 속도 1°/s 와 4°/s 가 11° 차이났다).
	const float CenterHalfX = Sim.GetBoxHalf().X * 0.6f;
	const TArray<float>& W = Sim.GetInvMass();

	TArray<int32> Watch;
	for (int32 i = 0; i < W.Num(); ++i)
	{
		if (W[i] > 0.f && FMath::Abs(Sim.GetPositions()[i].X) < CenterHalfX)
		{
			Watch.Add(i);
		}
	}
	R.FreeCount = Watch.Num();

	TArray<FVector3f> Start;
	for (int32 i : Watch)
	{
		Start.Add(Sim.GetPositions()[i]);
	}

	const float Trigger = 0.25f * Params.Radius;

	float Deg = 0.f;
	while (Deg < MaxDeg)
	{
		Deg += RateDegPerSec * BatchDt;
		Sim.SetTiltDegrees(Deg);
		Sim.Step(BatchDt);

		// 사면 방향 평균 이동. 한 알이 튀는 것 말고 무리가 움직이는 것을 본다
		double SumSlide = 0.0;
		for (int32 k = 0; k < Watch.Num(); ++k)
		{
			SumSlide += Sim.GetPositions()[Watch[k]].X - Start[k].X;
		}
		const float MeanSlide = Watch.Num() > 0 ? (float)(SumSlide / Watch.Num()) : 0.f;

		if (MeanSlide > Trigger)
		{
			R.bOnset   = true;
			R.OnsetDeg = Deg;
			break;
		}
	}

	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}

FString SpikeBatch::TiltHeader()
{
	return TEXT("mu,atanMu,rateDegPerSec,particles,onset,onsetDeg,error,packFrames,wallSeconds");
}

FString SpikeBatch::TiltRow(const FTiltRunResult& R)
{
	const float AtanMu = FMath::RadiansToDegrees(FMath::Atan(R.Friction));
	return FString::Printf(TEXT("%.2f,%.1f,%.2f,%d,%d,%.2f,%+.2f,%.0f,%.1f"),
		R.Friction, AtanMu, R.RateDegPerSec, R.Particles,
		R.bOnset ? 1 : 0, R.OnsetDeg, R.OnsetDeg - AtanMu, R.PackFrames, R.WallSeconds);
}


FTiltRunResult SpikeBatch::RunTiltSingle(const FSandParams& Params, const FTiltSetup& Setup,
                                         float RateDegPerSec, float MaxDeg)
{
	const double T0 = FPlatformTime::Seconds();

	FTiltRunResult R;
	R.Friction      = Params.Friction;
	R.RateDegPerSec = RateDegPerSec;

	FSandSpike Sim;
	Sim.InitTiltBox(Params, Setup);
	R.Particles = Sim.Num();

	// 얹힌 입자가 자리를 잡을 때까지
	for (int32 f = 0; f < 120; ++f)
	{
		Sim.Step(BatchDt);
	}
	R.PackFrames = (float)Sim.FrameIndex();

	// 미끄러짐 판정 — 자유 입자의 사면 방향 누적 변위가 반경의 1/4을 넘는 순간.
	// 속도 임계는 기울이는 동안의 미세 재배치에 걸리므로, 실제로 이동했는가를 본다
	const TArray<float>& W = Sim.GetInvMass();
	TArray<int32> Free;
	for (int32 i = 0; i < W.Num(); ++i)
	{
		if (W[i] > 0.f) { Free.Add(i); }
	}
	R.FreeCount = Free.Num();

	TArray<FVector3f> Start;
	for (int32 i : Free)
	{
		Start.Add(Sim.GetPositions()[i]);
	}

	const float Trigger = 0.25f * Params.Radius;
	float Deg = 0.f;
	while (Deg < MaxDeg)
	{
		Deg += RateDegPerSec * BatchDt;
		Sim.SetTiltDegrees(Deg);
		Sim.Step(BatchDt);

		float MaxSlide = 0.f;
		for (int32 k = 0; k < Free.Num(); ++k)
		{
			MaxSlide = FMath::Max(MaxSlide, Sim.GetPositions()[Free[k]].X - Start[k].X);
		}
		if (MaxSlide > Trigger)
		{
			R.bOnset   = true;
			R.OnsetDeg = Deg;
			break;
		}
	}

	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}


// ---------------------------------------------------------------------------

namespace
{
	constexpr int32 TiltRampFrames = 60;    // 목표 각도까지 1초에 걸쳐 (관성 전이는 여기서 끝낸다)
	constexpr int32 TiltHoldFrames = 240;   // 붙잡고 4초 관찰
	constexpr int32 TiltBisectSteps = 8;    // 70° / 2^8 ≈ 0.3° 해상도
}

FTiltRunResult SpikeBatch::RunTiltStatic(const FSandParams& Params, const FTiltSetup& Setup, float MaxDeg)
{
	const double T0 = FPlatformTime::Seconds();

	FTiltRunResult R;
	R.Friction = Params.Friction;

	FSandSpike Sim;
	Sim.InitTiltBox(Params, Setup);
	R.Particles = Sim.Num();

	// --- 수평에서 다진다. 이 상태를 모든 시험이 공유한다 ---
	while (!Sim.IsSettled() && Sim.FrameIndex() < 1800)
	{
		Sim.Step(BatchDt);
	}
	R.PackFrames = (float)Sim.FrameIndex();

	FSandState Packed;
	Sim.SaveState(Packed);

	// 관찰 대상 — 중앙 구역의 자유 입자
	const float CenterHalfX = Sim.GetBoxHalf().X * 0.6f;
	const TArray<float>& W = Sim.GetInvMass();
	TArray<int32> Watch;
	for (int32 i = 0; i < W.Num(); ++i)
	{
		if (W[i] > 0.f && FMath::Abs(Packed.Pos[i].X) < CenterHalfX)
		{
			Watch.Add(i);
		}
	}
	R.FreeCount = Watch.Num();

	// 붙잡은 동안 사면 방향으로 반경의 절반 넘게 흐르면 무너진 것
	const float Trigger = 0.5f * Params.Radius;

	auto IsStableAt = [&](float Deg) -> bool
	{
		Sim.RestoreState(Packed);

		for (int32 f = 0; f < TiltRampFrames; ++f)
		{
			Sim.SetTiltDegrees(Deg * (f + 1) / (float)TiltRampFrames);
			Sim.Step(BatchDt);
		}

		TArray<float> StartX;
		StartX.Reserve(Watch.Num());
		for (int32 i : Watch)
		{
			StartX.Add(Sim.GetPositions()[i].X);
		}

		for (int32 f = 0; f < TiltHoldFrames; ++f)
		{
			Sim.Step(BatchDt);

			double SumSlide = 0.0;
			for (int32 k = 0; k < Watch.Num(); ++k)
			{
				SumSlide += Sim.GetPositions()[Watch[k]].X - StartX[k];
			}
			if (Watch.Num() > 0 && SumSlide / Watch.Num() > Trigger)
			{
				return false;
			}
		}
		return true;
	};

	// --- 이분 탐색 ---
	float Lo = 0.f;          // 버티는 것이 확인된 각
	float Hi = MaxDeg;       // 무너지는 것이 확인된 각
	if (IsStableAt(MaxDeg))
	{
		R.bOnset   = false;
		R.OnsetDeg = MaxDeg;
		R.WallSeconds = FPlatformTime::Seconds() - T0;
		return R;
	}

	for (int32 k = 0; k < TiltBisectSteps; ++k)
	{
		const float Mid = 0.5f * (Lo + Hi);
		if (IsStableAt(Mid))
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}

	R.bOnset   = true;
	R.OnsetDeg = 0.5f * (Lo + Hi);
	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}
