// 배치 하네스 — 같은 코어를 최대 속도로 N회전시켜 스윕한다 (m0-spike.md §1 · §6).
// ★ 이 하네스는 버리지 않는다. M0-b에서 GPU 결과를 CPU와 대조하는 데 쓴다.

#pragma once

#include "CoreMinimal.h"
#include "SandSpike.h"
#include "SpikeMeasure.h"

/** 실행 하나의 결과. 요약 CSV 한 줄이 된다. */
struct FSpikeRunResult
{
	FString RunName;

	// 입력
	float Friction   = 0.f;
	float Relaxation = 0.f;
	int32 Substeps   = 0;
	int32 Iterations = 0;
	int32 Particles  = 0;

	// 각도
	FAngleStats Angle;

	// 거동
	int32 ReleaseFrame = 0;
	bool  bReleased    = false;   // false면 다지기 단계에서 상한에 걸린 것 — 각도는 원기둥 옆면이다
	int32 SettleFrame  = 0;
	float SettleTime   = 0.f;
	bool  bSettled     = false;   // false면 프레임 상한에 걸린 것 — 각도를 믿으면 안 된다
	float PeakHeight   = 0.f;
	float AvgSpeed     = 0.f;
	float MaxSpeed     = 0.f;

	/** 정지 직전 60프레임의 최고점 하강 속도(cm/s). 크면 "정지"가 아니라 흐르는 중이다. */
	float CreepRate = 0.f;

	double WallSeconds = 0.0;

	/** 프레임별 CSV 본문. 저장은 주 스레드가 한다 (워커에서 파일을 쓰지 않는다). */
	FString PerFrameCsv;
};

namespace SpikeBatch
{
	/**
	 * 충전 → 정지 → (지연) → 벽 제거 → 정지 → 각 측정.
	 *
	 * ReleaseDelayFrames 는 실시간 뷰의 AutoReleaseDelay 와 같아야 한다.
	 * 다르면 궤적이 갈려 앵커 셀이 재현되지 않는다 (실행 #2에서 겪음).
	 */
	FSpikeRunResult RunOne(const FSandParams& Params, const FColumnSetup& Column,
	                       int32 MaxFrames, bool bCapturePerFrame, int32 ReleaseDelayFrames = 30);

	FString SummaryHeader();
	FString SummaryRow(const FSpikeRunResult& R);
}
