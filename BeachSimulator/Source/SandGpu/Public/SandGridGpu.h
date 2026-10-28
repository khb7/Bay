// M3 T1 — 얕은 물 격자 GPU 판 (m3-plan.md · Shaders/Private/SandGrid.usf). CPU 기준판은 BeachSimulator/Grid/SandGridCpu.
// 칸 크기 · 영역은 바꿀 수 있게 (m3-plan: 칸 5cm, 영역은 T5 비용으로).

#pragma once

#include "CoreMinimal.h"

struct FSandGridGpuInit
{
	int32 NX = 0, NY = 0;
	float Dx = 0.05f, Dy = 0.05f;
	float Gravity = 9.81f, Theta = 1.3f, Cfl = 0.25f, HDry = 1e-4f;
	float EpsDesing = 0.f;          // 0 이면 max(Dx, Dy)⁴
	bool  bWetDry = true;
	float Manning = 0.f;            // 바닥 마찰 n. 실험 유리 수조 0.01 (m3-log #4), 모래 0.02~0.03
	TArray<float> BCorner;          // (NX+1)·(NY+1)
	TArray<float> W, HU, HV;        // NX·NY
};

class SANDGPU_API FSandGridGpu
{
public:
	FSandGridGpu();
	~FSandGridGpu();

	void Init(const FSandGridGpuInit& In);
	/** 고정 Δt 로 NumSteps 서브스텝 (CPU 대조용) */
	void StepFixed(float Dt, int32 NumSteps);
	/** 적응 Δt (CFL) 로 FrameTime 만큼, 최대 MaxSub 서브스텝. Δt 는 GPU 가 정한다 (기다리지 않음). 쓴 시간은 Download 의 SimTime */
	void Advance(float FrameTime, int32 MaxSub);
	/** 동기 내려받기 (시험용) */
	void Download(TArray<float>& OutW, TArray<float>& OutHU, TArray<float>& OutHV, float* OutSimTime = nullptr) const;
	/** 마지막 Step 의 GPU 시간 (타임스탬프, ms) */
	float LastGpuMs() const { return GpuMs; }

	int32 GetNX() const { return Params.NX; }
	int32 GetNY() const { return Params.NY; }

private:
	void Run(bool bFixed, float DtOrFrame, int32 NumSub);

	struct FState;
	FState* State = nullptr;
	FSandGridGpuInit Params;   // 배열은 Init 뒤 비운다
	float GpuMs = 0.f;
};
