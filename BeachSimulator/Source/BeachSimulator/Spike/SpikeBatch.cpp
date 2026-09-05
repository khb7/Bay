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
	       TEXT("angleMode,angleMedian,angleMean,surface,sampled,")
	       TEXT("releaseFrame,released,settleFrame,settleTime,settled,valid,")
	       TEXT("peakHeight,creepCmPerSec,avgSpeed,maxSpeed,wallSeconds");
}

FString SpikeBatch::SummaryRow(const FSpikeRunResult& R)
{
	return FString::Printf(
		TEXT("%.2f,%.2f,%d,%d,%d,%.1f,%.1f,%.1f,%d,%d,%d,%d,%d,%.2f,%d,%d,%.2f,%.3f,%.3f,%.3f,%.1f"),
		R.Friction, R.Relaxation, R.Substeps, R.Iterations, R.Particles,
		R.Angle.ModeDeg, R.Angle.MedianDeg, R.Angle.MeanDeg, R.Angle.SurfaceNum, R.Angle.SampledNum,
		R.ReleaseFrame, R.bReleased ? 1 : 0, R.SettleFrame, R.SettleTime, R.bSettled ? 1 : 0,
		(R.bReleased && R.bSettled) ? 1 : 0,
		R.PeakHeight, R.CreepRate, R.AvgSpeed, R.MaxSpeed, R.WallSeconds);
}
