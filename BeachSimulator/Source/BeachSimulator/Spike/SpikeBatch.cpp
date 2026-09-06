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
	return TEXT("mu,atanMu,rateDegPerSec,particles,onset,onsetDeg,error,packFrames,lastSlide,wallSeconds");
}

FString SpikeBatch::TiltRow(const FTiltRunResult& R)
{
	const float AtanMu = FMath::RadiansToDegrees(FMath::Atan(R.Friction));
	return FString::Printf(TEXT("%.2f,%.1f,%.2f,%d,%d,%.2f,%+.2f,%.0f,%.4f,%.1f"),
		R.Friction, AtanMu, R.RateDegPerSec, R.Particles,
		R.bOnset ? 1 : 0, R.OnsetDeg, R.OnsetDeg - AtanMu, R.PackFrames, R.LastSlide, R.WallSeconds);
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

FTiltRunResult SpikeBatch::RunTiltStatic(const FSandParams& Params, const FTiltSetup& Setup, float MaxDeg,
                                         int32 HoldFrames)
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

	// ★ 판정 = "움직임이 죽어가는가" — 유지 창의 앞 절반과 뒤 절반의 평균 속력 비교.
	//
	// Pouliquen 1999 의 θ_start 정의(흐름이 지속되는가)와 같은 성질이고, 임의 상수가 없다.
	//
	// 앞서 시도한 둘이 왜 실패했는지 (papers.md §A-R5 의 경고):
	//   ① 누적 변위 > 0.5r   임계 이하에서도 방향성 크리프가 안 멈춰 유지 시간이 길수록
	//                        낮은 각에서 걸렸다. 실측 20.1° → 17.6° → 9.2° (유지 1/4/16초)
	//   ② §2-2 그대로        60프레임 "연속" 조건이 스파이크 한 번에 부러진다.
	//                        구간 평균은 임계의 1/10인데도 거의 모든 각이 "정지 못 함"이 됐다
	//
	// 구간 평균끼리 비교하면 둘 다 피한다 — 크리프는 감쇠하므로 뒤가 작고,
	// 사태는 유지되거나 가속하므로 뒤가 크다. 변위는 진단용으로만 기록한다.
	auto IsStableAt = [&](float Deg, float& OutSlide) -> bool
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

		// 속도도 변위와 같은 집단(중앙 60%)에서 본다.
		// 전체 평균을 쓰면 닫힌 상자의 내리막 벽에 눌린 입자들이 섞여 안 죽는다 —
		// 실측에서 그 때문에 1°에서도 "유지된다"로 분류됐다
		const int32 Half = FMath::Max(1, HoldFrames / 2);
		double SumFirst = 0.0, SumSecond = 0.0;
		for (int32 f = 0; f < HoldFrames; ++f)
		{
			Sim.Step(BatchDt);

			double SumSpeed = 0.0;
			for (int32 i : Watch)
			{
				SumSpeed += Sim.GetVelocities()[i].Size();
			}
			const double MeanSpeed = Watch.Num() > 0 ? SumSpeed / Watch.Num() : 0.0;

			if (f < Half) { SumFirst  += MeanSpeed; }
			else          { SumSecond += MeanSpeed; }
		}
		const double MeanFirst  = SumFirst  / Half;
		const double MeanSecond = SumSecond / FMath::Max(1, HoldFrames - Half);
		const bool bDying = MeanSecond < MeanFirst;

		double SumSlide = 0.0;
		for (int32 k = 0; k < Watch.Num(); ++k)
		{
			SumSlide += Sim.GetPositions()[Watch[k]].X - StartX[k];
		}
		OutSlide = Watch.Num() > 0 ? (float)(SumSlide / Watch.Num()) : 0.f;

		return bDying;
	};

	// --- 이분 탐색 ---
	float Lo = 0.f;          // 버티는 것이 확인된 각
	float Hi = MaxDeg;       // 무너지는 것이 확인된 각
	float Slide = 0.f;
	if (IsStableAt(MaxDeg, Slide))
	{
		R.bOnset   = false;
		R.OnsetDeg = MaxDeg;
		R.WallSeconds = FPlatformTime::Seconds() - T0;
		return R;
	}

	for (int32 k = 0; k < TiltBisectSteps; ++k)
	{
		const float Mid = 0.5f * (Lo + Hi);
		if (IsStableAt(Mid, Slide))
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}

	R.bOnset    = true;
	R.OnsetDeg  = 0.5f * (Lo + Hi);
	R.LastSlide = Slide;   // 진단용. 판정에는 쓰지 않는다
	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}


void SpikeBatch::MeasureCreep(const FSandParams& Params, const FTiltSetup& Setup, float Deg,
                              int32 Seconds, TArray<float>& OutSlidePerSec, TArray<float>& OutSpeedPerSec)
{
	FSandSpike Sim;
	Sim.InitTiltBox(Params, Setup);

	while (!Sim.IsSettled() && Sim.FrameIndex() < 1800)
	{
		Sim.Step(BatchDt);
	}

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

	// 목표 각도까지 1초 램프 (본 절차와 동일)
	for (int32 f = 0; f < TiltRampFrames; ++f)
	{
		Sim.SetTiltDegrees(Deg * (f + 1) / (float)TiltRampFrames);
		Sim.Step(BatchDt);
	}

	TArray<float> StartX;
	for (int32 i : Watch)
	{
		StartX.Add(Sim.GetPositions()[i].X);
	}

	OutSlidePerSec.Reset();
	OutSpeedPerSec.Reset();
	for (int32 sec = 0; sec < Seconds; ++sec)
	{
		double SumSpeed = 0.0;
		for (int32 f = 0; f < 60; ++f)
		{
			Sim.Step(BatchDt);
			for (int32 i : Watch)
			{
				SumSpeed += Sim.GetVelocities()[i].Size();
			}
		}

		double SumSlide = 0.0;
		for (int32 k = 0; k < Watch.Num(); ++k)
		{
			SumSlide += Sim.GetPositions()[Watch[k]].X - StartX[k];
		}
		OutSlidePerSec.Add(Watch.Num() > 0 ? (float)(SumSlide / Watch.Num()) : 0.f);
		OutSpeedPerSec.Add(Watch.Num() > 0 ? (float)(SumSpeed / (60.0 * Watch.Num())) : 0.f);
	}
}
