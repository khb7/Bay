// M3 T1 — 얕은 물 격자 GPU 판 (m3-plan.md · Shaders/Private/SandGrid.usf). CPU 기준판은 BeachSimulator/Grid/SandGridCpu.
// 칸 크기 · 영역은 바꿀 수 있게 (m3-plan: 칸 5cm, 영역은 T5 비용으로).

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"

struct FSandGridGpuInit
{
	int32 NX = 0, NY = 0;
	float Dx = 0.05f, Dy = 0.05f;
	float Gravity = 9.81f, Theta = 1.3f, Cfl = 0.25f, HDry = 1e-4f;
	float EpsDesing = 0.f;          // 0 이면 max(Dx, Dy)⁴
	bool  bWetDry = true;
	float Manning = 0.f;            // 바닥 마찰 n. 실험 유리 수조 0.01 (m3-log #4), 모래 0.02~0.03
	// T3 바다 쪽 경계 (+x) — 리만 불변량 입사 · 반사 흡수 (papers D-M3-5). 끄면 벽
	bool  bOpenEast = false;
	float SeaLevel = 0.f;
	float BoundaryDepth = 0.f;      // 경계 정지 수심
	TArray<FVector4f> Waves;        // (a, ω, φ, k_y) 성분 — 입사 η(y, t) = Σ a·cos(φ + k_y·y − ωt)
	bool  bPeriodicY = false;       // 해안선 방향 주기 경계 (끄면 벽)
	float IgGain = 0.f;             // T3b 장주기파: η_IG = IgGain·(A² − Σa²), A = 성분 포락선. 0 이면 끔
	bool  bDispersive = false;      // T3c Madsen & Sørensen 1992 Boussinesq (Celeris 방식). 줄 길이 ≤ 1024, 경계는 벽으로 (열린 · 주기 경계 근처 처리는 c4)
	float BreakStop = 0.3f;         // 쇄파 종료 ε — 켜진 칸은 ε < 이 값까지 NSW 유지 (Tonelli & Petti, papers D-M3-17). 0 이면 순간 판정
	float BreakSlope = 0.f;         // BreakMode 2 기울기 조건 tan φ_c (Kazolea & Ricchiuto 2018: φ_c 30° → 0.577). 0 이면 끔
	float EddyDelta = 1.2f;
	bool  bBreakRoller = false;     // 쇄파 칸 묶음 → roller 길이로 넓힘 (papers D-M3-22)
	float BreakTbrk = 12.f;         // BreakMode 3 (FUNWAVE 나이 · 번짐 와점성) 쇄파 지속 T_brk (s)         // BreakMode 2 Kennedy 와점성 δ_b (Kennedy et al. 2000 · FUNWAVE-TVD)
	int32 BreakMode = 1;            // 1: 시작 (∂η/∂t)/√(gh) > BreakRatio (SWASH · Kazolea, D-M3-18) · 0: |η|/h (Tonelli & Petti)
	float BreakRatio = 0.8f;        // 쇄파 전환 |η|/h (Tonelli & Petti 2009). 0 이면 끔
	int32 DispTaper = 8;            // T3c c4 열린 경계 쪽 분산 줄이는 칸 수 (d → 0)
	bool  bBoundIg = false;         // T3c c4 경계에 묶인 장주기파 (성분 쌍, LHS 1962 방향 일반화 — m3-plan c4)
	class FSandGridGpu* NestOuter = nullptr;   // T3c c4 한 방향 nesting: 동쪽 입사를 이 바깥 격자에서 (바깥을 같은 프레임에 먼저 Advance). y 길이는 같게
	float NestX = 0.f;              // 바깥 격자 좌표에서 이 격자 동쪽 경계 x (m)
	float Ks = 0.f;                 // White-Colebrook k_s (m, papers D-M3-15): > 0 이면 Manning · 구역 대신 영역 전체 한 식
	float ManningSwash = 0.f, FricX = 0.f;   // 구역 마찰 (papers D-M3-13/14): x < FricX (swash 구역) 칸은 ManningSwash (FricX 0 이면 안 씀)
	bool  bGround = false;          // T3c c6 지하수 · 침투 (m3-plan c6 — Dupuit Darcy + Green–Ampt + 침출). 대수층 바닥 = 최저 바닥 − 1 m
	float GwK = 5e-4f;              // 투수 계수 m/s (중사, papers D-M3-12)
	float GwPsi = 0.05f;            // 습윤 전선 흡인 m (Rawls 1983 모래)
	float GwNe = 0.3f;              // 유효 공극률
	// T4a 모래 (m3-plan T4, papers D-M3-23): MPM 소류사 + Exner (꼭짓점 바닥) + 사태. 서브스텝 끝 약결합
	bool  bMorph = false;
	float SedD50 = 2.5e-4f;         // m
	float SedS = 2.65f;             // 상대 밀도 (석영)
	float SedPor = 0.4f;            // 공극률 (XBeach por)
	float Morfac = 1.f;             // 지형 과장 배수 (XBeach morfac)
	bool  bSus = false;             // T4b 부유사 (papers D-M3-25)
	float SedD90 = 3e-4f;           // m — XBeach 예제 값 (근거 약함)
	float SusTsfac = 0.1f, SusTsmin = 0.5f, SusCmax = 0.1f;   // XBeach 기본
	float AvalDry = 1.f, AvalWet = 0.3f;   // 사태 임계 기울기 (XBeach dryslp · wetslp)
	TArray<float> GwInit;           // 처음 지하수면 NX·NY (비면 min(B, SeaLevel)) — 시험용
	TArray<float> BCorner;          // (NX+1)·(NY+1)
	TArray<float> W, HU, HV;        // NX·NY
};

class SANDGPU_API FSandGridGpu
{
public:
	FSandGridGpu();
	~FSandGridGpu();

	void Init(const FSandGridGpuInit& In);
	/** T6 도구: 다음 Advance / StepFixed 맨 앞에 반영. (X, Y) m · 반지름 m · 바닥 부피 m³ (+ 쌓기, − 파기) 또는 물 부피 m³ (+). 칸 합으로 정규화해 요청 부피가 격자에 정확히 들어간다 */
	void QueueBedStamp(float X, float Y, float Radius, float Volume);
	void QueueWaterStamp(float X, float Y, float Radius, float Volume);
	void SetFricX(float X) { Params.FricX = X; }   // swash 구역 경계 갱신 (FSwashZone)
	/** 고정 Δt 로 NumSteps 서브스텝 (CPU 대조용) */
	void StepFixed(float Dt, int32 NumSteps);
	/** 적응 Δt (CFL) 로 FrameTime 만큼, 최대 MaxSub 서브스텝. Δt 는 GPU 가 정한다 (기다리지 않음). 쓴 시간은 Download 의 SimTime */
	void Advance(float FrameTime, int32 MaxSub);
	/** 동기 내려받기 (시험용) */
	void Download(TArray<float>& OutW, TArray<float>& OutHU, TArray<float>& OutHV, float* OutSimTime = nullptr) const;
	/** 지하수 (η_gw, δ) 동기 내려받기 · 대수층 바닥 */
	void DownloadGround(TArray<FVector2f>& OutGw, float* OutZaq = nullptr) const;
	void DownloadNu(TArray<float>& OutNu) const;
	/** T5: 프레임마다 (w, h, b, 젖음) 를 쓸 RGBA32F 렌더 타깃 (UAV 가능, NX·NY). 렌더 스레드 자원을 넘긴다 */
	void SetExportTarget(class FTextureRenderTargetResource* Rt, float DryRatePerFrame);
	/** 비동기 수면 내려받기 — 요청하고 다음 Tick 들에서 Poll (기다리지 않음). swash 구역 · 진단용 */
	void RequestW();
	bool PollW(TArray<float>& OutW);
	float LastSimTime() const;
	void DownloadSus(TArray<float>& OutS) const;   // 칸 S = hC (m), 동기
	void DownloadBed(TArray<float>& OutBCorner, bool bStruct = false) const;   // 꼭짓점 바닥 (NX+1)·(NY+1), 동기. 물이 보는 유효 바닥 b + b_struct · bStruct 면 b_struct (M4 T2)
	void DownloadBreakDiag(TArray<FVector2f>& Out) const;
	void ResetBreakDiag();   // 칸별 (최대 |∇η|, 최대 η_t/√(gH)) — 시작부터 누적 (진단)   // 쇄파 와점성 ν (진단, D-M3-20)
	/** 마지막 Step 의 GPU 시간 (타임스탬프, ms) */
	void DownloadTime(float* OutSimTime) const;   // 시간 · 최대 파속만 (16 B) — 서브스텝 수 갱신용
	float LastGpuMs() const;   // 비동기 타임스탬프 (1~3 프레임 전 Run, ms)
	int32 GetLastSubsteps() const { return LastSubsteps; }

	/** M4 T1: 꼭짓점 바닥 버퍼 (m, (NX+1)·(NY+1)) — 입자 지면이 같은 GPU 버퍼를 읽는다. Init 뒤 고정 핸들 */
	TRefCountPtr<FRDGPooledBuffer> GetBedBuffer() const;
	/** M4 T2: 꼭짓점 b_struct 버퍼 (m) — 입자 지면은 Bed − Struct */
	TRefCountPtr<FRDGPooledBuffer> GetStructBuffer() const;
	/** M4 T2 장애물 마스크: 매 Advance 맨 앞에 입자 위치 (지난 프레임) → b_struct. Sim 은 SetStructSource(nullptr) 전까지 살아 있어야 한다.
	 *  Map: 입자 좌표 (cm) ↔ 격자 — 칸 (cm) · 꼭짓점 (0,0) 위치 · b = 0 높이 · 입자 반지름 */
	struct FStructMap { float CellCm = 5.f; FVector2f OriginCm = FVector2f::ZeroVector; float ZCm = 0.f; float RadiusCm = 2.5f; };
	void SetStructSource(const class FSandGpuSim* Sim, const FStructMap& Map);
	/** 시험용: 고정 위치 (xyz cm · w 역질량). 비우면 다음 Run 에 b_struct 가 0 으로 */
	void SetStructPositions(const TArray<FVector4f>& Pos, const FStructMap& Map);
	int32 GetNX() const { return Params.NX; }
	int32 GetNY() const { return Params.NY; }

private:
	void Run(bool bFixed, float DtOrFrame, int32 NumSub);
	void QueueStamp(float X, float Y, float Radius, float Volume, bool bWater);
	TArray<FVector4f> PendingStamps;
	const class FSandGpuSim* StructSim = nullptr;
	FStructMap StructMap;
	bool bStruct = false;          // 마스크 패스를 돈다 (위치를 비운 뒤 한 번 더 돌아 0 으로 만든다)
	TArray<FVector4f> StructUpload; bool bStructUpload = false;

	struct FState;
	FState* State = nullptr;
	FSandGridGpuInit Params;   // 배열은 Init 뒤 비운다
	float GpuMs = 0.f;
	float LastAMax = 0.f;      // 지난 Advance 의 최대 a/Δ (Download 때 받는다)
	int32 LastSubsteps = 0;
};
