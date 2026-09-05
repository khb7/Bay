// 계측 — m0-spike.md §2-5(CSV) · §3-0 판정(안식각).
// 스파이크 코드는 버려도 되지만 이 하네스는 남는다. M0-b에서 GPU 결과를 CPU와 대조하는 데 쓴다.

#pragma once

#include "CoreMinimal.h"

class FSandSpike;

/** 안식각 측정 결과. 히스토그램 최빈값이 판정값이다. */
struct FAngleStats
{
	float ModeDeg     = 0.f;   // ★ 판정값 — 통과 기준 34° ± 3°
	float MedianDeg   = 0.f;
	float MeanDeg     = 0.f;
	int32 SurfaceNum  = 0;     // 표면으로 분류된 입자 수
	int32 SampledNum  = 0;     // 그중 바닥·꼭대기 제외 후 실제로 각을 잰 수
	TArray<int32> Histogram;   // 1° 간격, 0~90
};

namespace SpikeMeasure
{
	/**
	 * 안식각 (§3-0 판정).
	 *
	 * atan(H/R)은 쓰지 않는다 — 바닥 마찰과 관성 확산에 오염된다.
	 * 표면 입자만 뽑아 지역 법선의 경사각을 재고, 그 히스토그램의 최빈값을 쓴다.
	 */
	FAngleStats AngleOfRepose(const TArray<FVector3f>& Pos, float Radius);

	float PeakHeight(const TArray<FVector3f>& Pos);
	float ComHeight(const TArray<FVector3f>& Pos);
}

/**
 * 프레임별 CSV + 정지 시점 요약.
 *
 * 같은 입력 → 같은 CSV 바이트. 이것을 회귀 테스트로 쓴다 (§2-3).
 */
class FSpikeRecorder
{
public:
	/** RunName 은 파일명이 된다. Saved/SpikeRuns/<RunName>.csv */
	void Begin(const FString& RunName, const FSandSpike& Sim);

	/** 매 프레임. 벽이 빠지는 순간을 감지해 변위 기준 위치를 스냅샷한다. */
	void RecordFrame(const FSandSpike& Sim);

	/** 정지 시점. 각도 측정까지 붙여 파일로 내보낸다. */
	void Finish(const FSandSpike& Sim, const FAngleStats& Angle);

	bool IsOpen()     const { return bOpen; }
	bool IsFinished() const { return bFinished; }
	const FString& GetFilePath() const { return FilePath; }

private:
	FString FilePath;
	FString Header;
	TArray<FString> Rows;

	TArray<FVector3f> ReleasePos;   // 벽 제거 시점 위치. movedParticles 기준
	bool bHaveReleasePos = false;
	bool bOpen           = false;
	bool bFinished       = false;
};
