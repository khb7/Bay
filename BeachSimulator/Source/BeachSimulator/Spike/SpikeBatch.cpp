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


// ---------------------------------------------------------------------------

FConeLiftResult SpikeBatch::RunConeLift(const FSandParams& Params, const FColumnSetup& Column,
                                        float StepDiameters, int32 MaxFramesPerStep)
{
	const double T0 = FPlatformTime::Seconds();

	FConeLiftResult R;
	R.Friction      = Params.Friction;
	R.Iterations    = Params.Iterations;
	R.StepDiameters = StepDiameters;

	FSandSpike Sim;
	Sim.Init(Params, Column);
	R.Particles     = Sim.Num();
	R.InitialHeight = SpikeMeasure::PeakHeight(Sim.GetPositions());

	// --- ① 원통 안에서 다진다. 부풀면 그 뒤는 볼 필요가 없다 (실행 #12) ---
	while (!Sim.IsSettled() && Sim.FrameIndex() < 3000)
	{
		Sim.Step(BatchDt);
	}
	R.PackFrames   = (float)Sim.FrameIndex();
	R.PackedHeight = SpikeMeasure::PeakHeight(Sim.GetPositions());
	// 부풀지 않았고 + 실제로 멈췄어야 한다. 상한에 걸린 것은 "다졌다"가 아니다
	R.bPackStable  = (R.PackedHeight <= R.InitialHeight * 1.05f) && Sim.IsSettled();
	if (!R.bPackStable)
	{
		R.WallSeconds = FPlatformTime::Seconds() - T0;
		return R;   // 터진 기둥 위에서 각을 재지 않는다
	}

	// --- ② 계단식으로 들어올리고 매 단계 정지를 기다린다 ---
	const TArray<FVector3f> StartPos = Sim.GetPositions();
	const float MovedThresh2 = FMath::Square(2.f * Params.Radius);

	const float Step = StepDiameters * 2.f * Params.Radius;
	const float Top  = Sim.GetColumn().Height;

	for (float Lift = Step; Lift <= Top + Step; Lift += Step)
	{
		Sim.SetWallLift(Lift);
		++R.Steps;

		FConeLiftStep Log;
		Log.LiftZ = Lift;

		int32 f = 0;
		for (; f < MaxFramesPerStep && !Sim.IsSettled(); ++f)
		{
			Sim.Step(BatchDt);
			Log.MaxSpeed = FMath::Max(Log.MaxSpeed, Sim.MaxSpeed());
		}
		Log.SettleFrames = f;
		Log.bTimedOut    = (f >= MaxFramesPerStep);
		Log.EndSpeed     = Sim.AvgSpeed();
		if (Log.bTimedOut)
		{
			++R.TimedOutSteps;   // 준정적이 깨졌다는 신호
		}

		const TArray<FVector3f>& P = Sim.GetPositions();
		Log.PeakHeight = SpikeMeasure::PeakHeight(P);
		for (int32 i = 0; i < P.Num(); ++i)
		{
			if (FVector3f::DistSquared(P[i], StartPos[i]) > MovedThresh2)
			{
				++Log.MovedCount;
			}
		}
		float BinW = 0.f;
		Log.DepositRadius = SpikeMeasure::RadialProfile(P, Params.Radius, BinW).Num() * BinW;

		R.StepLog.Add(Log);
	}

	// --- ③ 벽을 완전히 뺀 뒤 최종 정지 ---
	Sim.ReleaseWalls();
	for (int32 f = 0; f < MaxFramesPerStep * 4 && !Sim.IsSettled(); ++f)
	{
		Sim.Step(BatchDt);
	}

	R.Angle      = SpikeMeasure::AngleOfRepose(Sim.GetPositions(), Params.Radius);
	R.ConeHeight = SpikeMeasure::PeakHeight(Sim.GetPositions());

	R.FinalProfile = SpikeMeasure::RadialProfile(Sim.GetPositions(), Params.Radius, R.ProfileBinWidth);
	R.ConeRadius   = R.FinalProfile.Num() * R.ProfileBinWidth;

	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}


// ---------------------------------------------------------------------------

FCutFaceResult SpikeBatch::RunCutFace(const FSandParams& Params, int32 Layers, int32 SidePerLayer, int32 SettleCap, int32 HoldFrames, bool bOpen)
{
	const double T0 = FPlatformTime::Seconds();
	FCutFaceResult R;
	R.Layers = Layers;

	FTiltSetup Setup;
	Setup.Layers = Layers;
	Setup.FixedLayers = 1;
	Setup.TargetCount = SidePerLayer * SidePerLayer * Layers;
	Setup.bOpenDownhill = false;   // 닫힌 상자에서 다진다
	Setup.BaseJitter = 0.3f;
	Setup.FreeJitter = 0.3f;

	// ① 마른 채로 다진다 — 결합은 다진 뒤 '도구 조작' 시점에 만든다
	FSandParams Dry = Params;
	Dry.WGlobal = 0.f;
	FSandSpike Sim;
	Sim.InitTiltBox(Dry, Setup);
	int32 f = 0;
	for (; f < SettleCap && !Sim.IsSettled(); ++f) { Sim.Step(BatchDt); }
	R.bSettledBefore = Sim.IsSettled();

	const TArray<FVector3f>& P0 = Sim.GetPositions();
	const TArray<float>& W = Sim.GetInvMass();
	const float D = 2.f * Params.Radius;
	const float BaseTop = 2.f * Params.Radius;   // 고정층 윗면
	const float LX = Sim.GetBoxHalf().X - Params.Radius;   // +X 벽선 (입자 중심)
	TArray<FVector3f> Before = P0;
	// 높이는 최고점이 아니라 98 백분위 — 튄 입자 하나가 블록 높이를 대표하면 γ 가 절반으로 나온다 (#23 의 단면 교훈)
	auto RobustTop = [&](const TArray<FVector3f>& Q)
	{
		TArray<float> Z;
		for (int32 i = 0; i < Q.Num(); ++i) { if (W[i] > 0.f) { Z.Add(Q[i].Z); } }
		if (Z.Num() == 0) { return 0.f; }
		Z.Sort();
		return Z[FMath::Clamp((int32)(Z.Num() * 0.98f), 0, Z.Num() - 1)];
	};
	for (int32 i = 0; i < P0.Num(); ++i) { if (W[i] > 0.f) { ++R.FreeCount; } }
	R.HeightBefore = RobustTop(P0) + Params.Radius - BaseTop;
	const float Area = FMath::Square(2.f * Sim.GetBoxHalf().X);
	R.GammaSim = R.FreeCount * FMath::Abs(Params.Gravity) / FMath::Max(Area * R.HeightBefore, 1.f);

	// ② 물을 준다 → 결합 → +X 벽 제거
	Sim.SetWGlobal(Params.WGlobal);
	R.BondsMade = Sim.FormBonds();
	R.SlotOverflow = Sim.BondOverflow();
	if (bOpen) { Sim.OpenFaceXPlus(); }   // 대조: 안 열고 같은 시간을 돌려 상자 자체가 부푸는지 본다

	int32 h = 0;
	for (; h < HoldFrames && !(h > 120 && Sim.IsSettled()); ++h) { Sim.Step(BatchDt); }
	R.bSettledAfter = Sim.IsSettled();
	R.BondsBroken = Sim.BondsBroken();

	// ③ 판정 — 절토면 띠(벽선에서 2D 안)의 입자 중 D 이상 움직인 비율. 3-1 의 '변위 > 2r' 과 같은 자
	const TArray<FVector3f>& P1 = Sim.GetPositions();
	int32 MovedFace = 0, MovedAll = 0;
	const float TopAfter = RobustTop(P1);
	for (int32 i = 0; i < P1.Num(); ++i)
	{
		if (W[i] <= 0.f) { continue; }
		const float Disp = FVector3f::Dist(P1[i], Before[i]);
		const bool bMoved = Disp > D;
		if (bMoved) { ++MovedAll; }
		if (Before[i].X > LX - 2.f * D) { ++R.FaceCount; if (bMoved) { ++MovedFace; } }
		R.MaxRunout = FMath::Max(R.MaxRunout, P1[i].X - LX);
	}
	// 후퇴 프로파일 — 띠별로 남아 있는 면의 위치. 이동비는 표피 탈락과 활동면을 못 가르지만 후퇴 깊이는 가른다
	{
		const int32 Bands = FMath::Max(1, FMath::CeilToInt(R.HeightBefore / D));
		TArray<TArray<float>> Xs;
		Xs.SetNum(Bands);
		for (int32 i = 0; i < P1.Num(); ++i)
		{
			if (W[i] <= 0.f) { continue; }
			const int32 b = FMath::Clamp((int32)((P1[i].Z - BaseTop) / D), 0, Bands - 1);
			Xs[b].Add(P1[i].X);
		}
		R.RetreatByBand.SetNumZeroed(Bands);
		for (int32 b = 0; b < Bands; ++b)
		{
			if (Xs[b].Num() == 0) { R.RetreatByBand[b] = R.HeightBefore; continue; }
			Xs[b].Sort();
			R.RetreatByBand[b] = LX - Xs[b][FMath::Clamp((int32)(Xs[b].Num() * 0.95f), 0, Xs[b].Num() - 1)];
		}
		const int32 TopBand = FMath::Max(0, Bands - 2);   // 표면 1D 는 표피라 뺀다
		R.TopRetreat = R.RetreatByBand[TopBand];
	}
	R.FaceMovedFrac = R.FaceCount > 0 ? (float)MovedFace / R.FaceCount : 0.f;
	R.AllMovedFrac  = R.FreeCount > 0 ? (float)MovedAll / R.FreeCount : 0.f;
	R.HeightAfter = TopAfter + Params.Radius - BaseTop;
	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}

FBondTestResult SpikeBatch::RunBondChain(const FSandParams& Params, int32 Chain, int32 Frames)
{
	FBondTestResult R;
	R.Chain = Chain;

	const float D = 2.f * Params.Radius;
	const float TopZ = 100.f;
	TArray<FVector3f> P;
	TArray<float> W;
	P.Add(FVector3f(0.f, 0.f, TopZ)); W.Add(0.f);   // 고정
	for (int32 k = 1; k <= Chain; ++k) { P.Add(FVector3f(0.f, 0.f, TopZ - k * D)); W.Add(1.f); }

	FSandSpike Sim;
	Sim.InitCustom(Params, P, W);
	R.BondsMade = Sim.FormBonds();
	R.ExpectedStretch = FMath::Abs(Params.Gravity) * Sim.BondCompliance();   // m = 1

	for (int32 f = 0; f < Frames && !Sim.IsSettled(); ++f) { Sim.Step(BatchDt); }
	R.bSettled = Sim.IsSettled();
	R.BondsAlive = Sim.GetBonds().Num();
	R.BondsBroken = Sim.BondsBroken();

	R.TopStretch = -1.f;
	for (const FSandBond& B : Sim.GetBonds())
	{
		if (B.I == 0 || B.J == 0) { R.TopStretch = FVector3f::Dist(Sim.GetPositions()[B.I], Sim.GetPositions()[B.J]) - B.RestLen; }
	}
	R.BottomDrop = (TopZ - Chain * D) - Sim.GetPositions().Last().Z;
	return R;
}

FPourResult SpikeBatch::RunPour(const FSandParams& Params, const FPourSetup& Setup, int32 SettleCap)
{
	const double T0 = FPlatformTime::Seconds();

	FPourResult R;
	R.Friction   = Params.Friction;
	R.Iterations = Params.Iterations;
	R.DropHeightDiameters = Setup.DropHeightDiameters;
	R.RatePerFrame = (float)Setup.PerDrop / FMath::Max(1, Setup.IntervalFrames);

	FSandSpike Sim;
	Sim.InitPour(Params, Setup);
	R.BaseCount = Sim.Num();
	Sim.DebugTraceFrame = Setup.DebugTraceFrame;
	Sim.DebugTraceIndex = Setup.DebugTraceIndex;

	// 진단 — 입자별 생성 프레임 (바닥은 -1)
	TArray<int32> SpawnFrame;
	SpawnFrame.Init(-1, Sim.Num());
	const float ImpactSpeed = FMath::Sqrt(2.f * FMath::Abs(Params.Gravity) * Setup.DropHeightDiameters * 2.f * Params.Radius);
	const float EventThreshold = Setup.EventSpeedFactor > 0.f ? Setup.EventSpeedFactor * ImpactSpeed : FLT_MAX;
	int32 EventCount = 0;
	float EventRecord = 0.f;

	const float D = 2.f * Params.Radius;
	FRandomStream Rng(Params.Seed);

	// --- ① 한 알씩 놓는다 ---
	int32 Poured = 0;
	int32 Frame  = 0;
	int32 LastPlacedFrame = 0;
	while (Poured < Setup.TargetCount)
	{
		// 정체 상한 — 낙하 높이가 낮고 산포가 좁으면 1.05D 여유를 영원히 못 찾는다 (#23: 무한 루프)
		if (Frame - LastPlacedFrame > 600)
		{
			UE_LOG(LogTemp, Warning, TEXT("부어 쌓기 정체: %d프레임 동안 자리를 못 찾음 (놓은 %d/%d, 낙하 %.2fd 산포 %.1fd)"),
				Frame - LastPlacedFrame, Poured, Setup.TargetCount, Setup.DropHeightDiameters, Setup.SpawnRadiusDiameters);
			break;
		}
		if (Frame % FMath::Max(1, Setup.IntervalFrames) == 0)
		{
			// 놓는 높이는 **축 근처 더미의 높이** 기준이다.
			//
			// 전체 최고점을 쓰면 튀어 오른 입자 하나가 최고점이 되어 다음 입자가 더 높이서
			// 떨어지고, 더 세게 충돌해 더 튄다 — 한 번 튀면 스스로 증폭한다(실측 4149 cm/s).
			//
			// ★ 그리고 **정지한 입자만** 센다. 공중에 있는 입자(방금 놓은 것)를 포함하면
			// 2프레임 뒤 0.5cm 떨어진 그 입자 위 1지름에 다음 것이 놓여, 놓을 때마다
			// 4.5cm 씩 올라가는 사슬이 된다 — 실측: 677개에 높이 244cm, 자유낙하 33프레임(#23).
			const float SpawnR = Setup.SpawnRadiusDiameters * D;
			const float NearR2 = FMath::Square(SpawnR + 2.f * D);
			const float RestSpeed = 0.1f * FMath::Sqrt(FMath::Abs(Params.Gravity) * D);   // ≈ 7 cm/s
			const TArray<FVector3f>& Vs = Sim.GetVelocities();
			float Apex = 0.f;
			for (int32 i = 0; i < Sim.Num(); ++i)
			{
				const FVector3f& P = Sim.GetPositions()[i];
				if (P.X * P.X + P.Y * P.Y < NearR2 && Vs[i].Size() < RestSpeed)
				{
					Apex = FMath::Max(Apex, P.Z);
				}
			}
			const float Z = Apex + Setup.DropHeightDiameters * D;

			// ★ 자리가 비었는지 확인하고 놓는다.
			//
			// 겹친 채로 생성하면 솔버가 그 침투를 한 스텝에 해소하려 하고, 그 보정이
			// 그대로 속도가 되어 날아간다 — 실측 최대속력 4149 cm/s (자유낙하의 24배).
			const float MinDist2 = FMath::Square(D * 1.05f);
			const TArray<FVector3f>& Existing = Sim.GetPositions();

			TArray<FVector3f> Batch;
			for (int32 k = 0; k < Setup.PerDrop && Poured < Setup.TargetCount; ++k)
			{
				bool bPlaced = false;
				for (int32 Try = 0; Try < 16 && !bPlaced; ++Try)
				{
					// 원판 위 균일 분포. 한 점이면 격자 정렬이 생긴다
					const float Ang = Rng.FRandRange(0.f, 2.f * UE_PI);
					const float Rad = SpawnR * FMath::Sqrt(Rng.FRand());
					const FVector3f Cand(Rad * FMath::Cos(Ang), Rad * FMath::Sin(Ang), Z);

					bool bClear = true;
					for (const FVector3f& E : Existing)
					{
						if (FVector3f::DistSquared(Cand, E) < MinDist2) { bClear = false; break; }
					}
					if (bClear)
					{
						for (const FVector3f& B : Batch)
						{
							if (FVector3f::DistSquared(Cand, B) < MinDist2) { bClear = false; break; }
						}
					}
					if (bClear)
					{
						Batch.Add(Cand);
						++Poured;
						bPlaced = true;
					}
				}
				// 16번 시도해도 자리가 없으면 이번 프레임은 건너뛴다. 다음 프레임에 다시
			}
			if (Batch.Num() > 0)
			{
				Sim.AddParticles(Batch);
				for (int32 k = 0; k < Batch.Num(); ++k) { SpawnFrame.Add(Frame); }
				LastPlacedFrame = Frame;
			}
		}

		Sim.Step(BatchDt);
		R.MaxSpeedDuringPour = FMath::Max(R.MaxSpeedDuringPour, Sim.MaxSpeed());

		// 진단 — 임계를 넘는 새 기록이 나올 때마다 그 입자를 찍는다
		if (Sim.MaxSpeed() > EventThreshold && Sim.MaxSpeed() > EventRecord * 1.2f)
		{
			EventRecord = Sim.MaxSpeed();
			const TArray<FVector3f>& V = Sim.GetVelocities();
			int32 Best = -1; float BestS = 0.f;
			for (int32 i = 0; i < V.Num(); ++i)
			{
				const float S = V[i].Size();
				if (S > BestS) { BestS = S; Best = i; }
			}
			if (Best >= 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[event %d] 프레임 %d  |v| %.1f (충돌 %.0f 의 %.1f배)  나이 %d프레임  입자수 %d  %s"),
					EventCount, Frame, BestS, ImpactSpeed, BestS / ImpactSpeed,
					SpawnFrame.IsValidIndex(Best) && SpawnFrame[Best] >= 0 ? Frame - SpawnFrame[Best] : -1,
					Sim.Num(), *Sim.DescribeParticle(Best));
			}
			++EventCount;
			if (Setup.EventStopCount > 0 && EventCount >= Setup.EventStopCount)
			{
				++Frame;
				break;
			}
		}
		++Frame;
	}
	R.Poured     = Poured;
	R.PourFrames = (float)Frame;

	// --- ② 다 붓고 정지까지 ---
	int32 f = 0;
	for (; f < SettleCap && !Sim.IsSettled(); ++f)
	{
		Sim.Step(BatchDt);
	}
	R.SettleFrames = (float)f;
	R.bSettled     = Sim.IsSettled();

	R.Angle      = SpikeMeasure::AngleOfRepose(Sim.GetPositions(), Params.Radius);
	R.ConeHeight = SpikeMeasure::PeakHeight(Sim.GetPositions());
	R.FinalProfile = SpikeMeasure::RadialProfile(Sim.GetPositions(), Params.Radius, R.ProfileBinWidth);
	R.ConeRadius = R.FinalProfile.Num() * R.ProfileBinWidth;

	// --- ③ 유지 — 정지 판정 뒤에도 퍼지는가 (크리프) ---
	if (Setup.HoldFrames > 0)
	{
		double SumAvg = 0.0, SumUnb = 0.0;
		for (int32 h = 0; h < Setup.HoldFrames; ++h)
		{
			Sim.Step(BatchDt);
			SumAvg += Sim.AvgSpeed();
			SumUnb += Sim.UnbalancedRatio();
		}
		R.CreepAvgSpeed = (float)(SumAvg / Setup.HoldFrames);
		R.UnbalancedRatio = (float)(SumUnb / Setup.HoldFrames);
		{
			// 하중 검산 — 고정 바닥과 지면이 받는 수직 임펄스가 자유 입자 무게와 맞는가
			const TArray<FVector3f>& P = Sim.GetPositions();
			const TArray<float>& W = Sim.GetInvMass();
			double SumZ = 0.0; int32 NFree = 0;
			for (const FSandContact& C : Sim.GetContacts())
			{
				const bool bFi = W[C.I] <= 0.f, bFj = W[C.J] <= 0.f;
				if (bFi == bFj || C.LambdaN <= 0.f) { continue; }
				const FVector3f N = (P[C.I] - P[C.J]).GetSafeNormal();   // I 쪽을 향함
				SumZ += C.LambdaN * FMath::Abs(N.Z);
			}
			for (int32 i = 0; i < W.Num(); ++i) { if (W[i] > 0.f) { ++NFree; } }
			const float SubDt = BatchDt / FMath::Max(1, Params.Substeps);
			R.BaseLoadRatio = NFree > 0 ? (float)(SumZ / (NFree * FMath::Abs(Params.Gravity) * SubDt)) : 0.f;
		}
		R.AngleAfterHold = SpikeMeasure::AngleOfRepose(Sim.GetPositions(), Params.Radius);
		R.ConeHeightAfterHold = SpikeMeasure::PeakHeight(Sim.GetPositions());
	}

	R.FinalPos     = Sim.GetPositions();
	R.FinalInvMass = Sim.GetInvMass();

	R.WallSeconds = FPlatformTime::Seconds() - T0;
	return R;
}
