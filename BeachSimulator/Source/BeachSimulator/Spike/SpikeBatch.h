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

	/** 반경 구간별 최고점 (진단용). 빈 값은 -1. */
	TArray<float> Profile;
	float ProfileBinWidth = 0.f;

	/** 프레임별 CSV 본문. 저장은 주 스레드가 한다 (워커에서 파일을 쓰지 않는다). */
	FString PerFrameCsv;
};

/** 경사판 실행 하나. */
struct FTiltRunResult
{
	float Friction    = 0.f;
	float RateDegPerSec = 0.f;
	int32 Particles   = 0;
	int32 FreeCount   = 0;

	bool  bOnset      = false;   // false면 상한까지 안 무너진 것
	float OnsetDeg    = 0.f;     // ★ 사태 개시각 = 정적 안식각
	float PackFrames  = 0.f;     // 수평에서 다지는 데 걸린 프레임
	double WallSeconds = 0.0;
};

namespace SpikeBatch
{
	/**
	 * 경사판 (tilting box) — Al-Hashemi 2018 의 표준 측정법 중 하나.
	 *
	 * 수평에서 다진 뒤 중력을 RateDegPerSec 으로 서서히 기울이고,
	 * 정지 판정(§2-2)이 깨지는 각을 잡는다. 퇴적물 모양을 해석할 필요가 없다.
	 *
	 * Rate 를 바꿔가며 같은 값이 나와야 준정적(quasi-static)이다 — 그 확인이 이 함수의 절반이다.
	 */
	FTiltRunResult RunTilt(const FSandParams& Params, const FTiltSetup& Setup,
	                       float RateDegPerSec, float MaxDeg);

	/**
	 * ★ 정적 경사판 — 각도마다 붙잡고 "여기서 정지하는가"를 묻는다.
	 *
	 * 기울이는 중에 재면 기울임 자체가 만드는 재배치를 사태로 오인한다
	 * (속도 판정은 기울기 속도에 11° 휘둘렸고, 변위 판정은 3°에서 걸렸다).
	 * 각도를 고정하고 묻기 때문에 기울기 속도라는 변수가 없다. 이분 탐색으로 8회면 끝난다.
	 */
	FTiltRunResult RunTiltStatic(const FSandParams& Params, const FTiltSetup& Setup, float MaxDeg);

	/** 자유 입자가 몇 개 없을 때. 중앙 구역 평균 대신 자유 입자 전체를 본다. */
	FTiltRunResult RunTiltSingle(const FSandParams& Params, const FTiltSetup& Setup,
	                             float RateDegPerSec, float MaxDeg);

	FString TiltHeader();
	FString TiltRow(const FTiltRunResult& R);

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
