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
	float LastSlide   = 0.f;     // 마지막 시행의 사면 방향 평균 변위 (진단용)
	double WallSeconds = 0.0;
};

/**
 * 계단 하나의 기록.
 *
 * 계단식의 전제는 "매 단계 완전히 멈춘다"이고, 그게 깨지면 연속 상승과 같아진다.
 * 어느 계단이 왜 안 멈췄는지는 총계로는 알 수 없어서 단계별로 남긴다.
 */
struct FConeLiftStep
{
	float LiftZ         = 0.f;   // 이 단계에서 벽을 올린 높이
	int32 SettleFrames  = 0;     // 정지까지 걸린 프레임 (상한이면 초과)
	bool  bTimedOut     = false;
	float MaxSpeed      = 0.f;   // 그 단계에서 가장 빨랐던 순간 — 사태의 크기
	float EndSpeed      = 0.f;   // 끝 시점 평균 — 임계에 얼마나 가까웠나
	float PeakHeight    = 0.f;
	float DepositRadius = 0.f;
	int32 MovedCount    = 0;     // 초기 위치에서 2r 넘게 움직인 누적 수
};

/** 원통 들어올리기 결과 (m0-spike.md §3-0-D — 기각, 대조용). */
struct FConeLiftResult
{
	float Friction   = 0.f;
	int32 Iterations = 0;
	int32 Particles  = 0;
	float StepDiameters = 0.f;

	// ★ 선행 조건 — 다지기 후 기둥이 부풀지 않았는가 (실행 #12)
	float InitialHeight = 0.f;
	float PackedHeight  = 0.f;
	bool  bPackStable   = false;
	float PackFrames    = 0.f;

	int32 Steps        = 0;
	int32 TimedOutSteps = 0;   // 한 단계 안에 정지 못 한 횟수. 0이 아니면 준정적이 깨진 것
	TArray<FConeLiftStep> StepLog;

	// 최종 단면 — 반경 구간별 최고점. 두 측정이 어긋날 때 모양을 직접 본다
	TArray<float> FinalProfile;
	float ProfileBinWidth = 0.f;

	FAngleStats Angle;         // 프로파일·법선 두 방식이 여기 들어 있다
	float ConeHeight = 0.f;
	float ConeRadius = 0.f;
	double WallSeconds = 0.0;
};

/** 부어 쌓기 결과 (m0-spike.md §3-0-A). */
struct FPourResult
{
	float Friction   = 0.f;
	int32 Iterations = 0;
	int32 Poured     = 0;      // 실제로 놓인 입자 수
	int32 BaseCount  = 0;      // 고정 바닥
	float DropHeightDiameters = 0.f;
	float RatePerFrame = 0.f;

	bool  bSettled   = false;  // 다 붓고 정지했는가
	float PourFrames = 0.f;
	float SettleFrames = 0.f;

	FAngleStats Angle;
	float ConeHeight = 0.f;
	float ConeRadius = 0.f;
	float MaxSpeedDuringPour = 0.f;   // 준정적이면 작아야 한다

	// 유지 후 재측정 (HoldFrames > 0)
	FAngleStats AngleAfterHold;
	float ConeHeightAfterHold = 0.f;
	float CreepAvgSpeed = 0.f;        // 유지 구간 평균 |v| 의 평균
	/** 바닥(고정 입자·지면)이 받는 수직 임펄스 합 / 자유 입자 총 무게 임펄스. 1 이 정답 — λn 인플레이션 계기 */
	float BaseLoadRatio = 0.f;

	TArray<float> FinalProfile;
	float ProfileBinWidth = 0.f;
	double WallSeconds = 0.0;

	/** 최종 위치 (오프라인 분석용). 고정 바닥은 InvMass 0 */
	TArray<FVector3f> FinalPos;
	TArray<float>     FinalInvMass;
};

/** 결합 단위 검사 결과 (m0-spike.md §2-6) */
struct FBondTestResult
{
	int32 Chain = 0;             // 매달린 자유 입자 수
	int32 BondsMade = 0;
	int32 BondsAlive = 0;
	int32 BondsBroken = 0;
	float ExpectedStretch = 0.f; // 결합 하나가 무게 1개를 받을 때 m·g·α
	float TopStretch = 0.f;      // 맨 위 결합의 실제 늘어남 (끊어졌으면 -1)
	float BottomDrop = 0.f;      // 맨 아래 입자가 시작보다 얼마나 내려갔나
	bool  bSettled = false;
};

/** 절토 — 점착의 자 (m0-spike.md §3-3-B). 한 면 지지 제거 후 높이별 붕괴 여부 */
struct FCutFaceResult
{
	int32 Layers = 0;
	int32 FreeCount = 0;
	float HeightBefore = 0.f;     // 다진 뒤 자유 입자 최고점 (바닥 윗면 기준)
	float HeightAfter = 0.f;
	int32 BondsMade = 0;
	int32 BondsBroken = 0;
	int32 SlotOverflow = 0;
	int32 FaceCount = 0;          // 절토면 띠(+X 쪽 2D) 입자 수
	float FaceMovedFrac = 0.f;    // 그중 변위 > D 비율 — 3-1 판정과 같은 자
	float AllMovedFrac = 0.f;
	float MaxRunout = 0.f;        // 벽선 밖으로 나간 최대 거리 (cm)
	float GammaSim = 0.f;         // 무게/부피 (시뮬 단위: 질량1·cm)
	bool  bSettledBefore = false;
	bool  bSettledAfter = false;
	double WallSeconds = 0.0;
};

namespace SpikeBatch
{
	/** 닫힌 상자에 Layers 층을 채워 다지고 → FormBonds → +X 벽 제거 → HoldFrames 뒤 절토면이 남았는가 */
	FCutFaceResult RunCutFace(const FSandParams& Params, int32 Layers, int32 SidePerLayer, int32 SettleCap, int32 HoldFrames);

	/** 고정 입자 아래 N 개를 세로로 결합해 매단다. N=1 은 mgα 만큼 늘어나 서야 하고, N≥2 는 위 결합이 BondStrength 를 넘어 끊어져야 한다 */
	FBondTestResult RunBondChain(const FSandParams& Params, int32 Chain, int32 Frames);

	/**
	 * 부어 쌓기 — θr 측정 (m0-spike.md §3-0-A).
	 *
	 * 더미를 한 알씩 키운다. 불안정 상태가 존재한 적이 없으므로 준정적이 저절로 성립한다.
	 * 남는 축은 낙하 높이와 공급 속도 둘뿐이고, 둘 다 무관성 검사로 확인한다.
	 */
	FPourResult RunPour(const FSandParams& Params, const FPourSetup& Setup, int32 SettleCap);

	/**
	 * 원통 들어올리기 — θr 측정 (m0-spike.md §3-0-D — #20 기각, 대조용).
	 *
	 * 계단식으로 올리고 매 단계 정지를 기다린다. 연속으로 올리면 "얼마나 천천히"가
	 * 새 변수가 되고, 경사판에서 그 변수가 11° 차이를 만든 전례가 있다.
	 */
	FConeLiftResult RunConeLift(const FSandParams& Params, const FColumnSetup& Column,
	                            float StepDiameters, int32 MaxFramesPerStep);

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
	FTiltRunResult RunTiltStatic(const FSandParams& Params, const FTiltSetup& Setup, float MaxDeg,
	                             int32 HoldFrames = 240);

	/**
	 * ★ 크리프 계측 — 임계 이하 각도에서 붙잡고 시간에 따른 변위와 속도를 본다.
	 *
	 * 변위가 시간에 선형으로 쌓이고 속도가 안 죽으면 크리프이고,
	 * 그러면 "N초 누적 변위" 판정은 사태가 아니라 크리프를 재고 있는 것이다.
	 */
	void MeasureCreep(const FSandParams& Params, const FTiltSetup& Setup, float Deg,
	                  int32 Seconds, TArray<float>& OutSlidePerSec, TArray<float>& OutSpeedPerSec);

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
