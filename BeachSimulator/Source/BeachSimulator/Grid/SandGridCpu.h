// M3 T1 — 얕은 물 풀이 CPU 기준판 (m3-plan.md · papers D-M3-1).
// Kurganov & Petrova 2007 central-upwind: 정지 수면 유지(well-balanced) · 수심 ≥ 0 · 젖음/마름.
// GPU 판의 정답지 — 같은 식을 단순하게. 단위는 미터 · 초 (시험이 무차원 d = 1 을 쓰기 쉽게).
//
// 상태 (칸 중심): w = h + B (수면 높이), hu, hv. 지형 B 는 꼭짓점 값 (NX+1)×(NY+1), 칸 경계 B 는 두 꼭짓점 평균,
// 칸 중심 B 는 네 경계 평균 (KP07 2.2 절). 경계 = 벽 (반사).

#pragma once

#include "CoreMinimal.h"

class FSandGridCpu
{
public:
	int32 NX = 0, NY = 0;
	double Dx = 1.0, Dy = 1.0;
	double G = 9.81;
	double Theta = 1.3;          // minmod 기울기 제한 (1 = 가장 확산적, 2 = 가장 덜)
	double Cfl = 0.25;           // KP07 정리 3.1 — 2D 양수성
	double Eps = 0.0;            // 속도 특이점 제거 ε (0 이면 max(Dx⁴, Dy⁴))
	bool   bFixDry = true;       // 칸 평균 유량도 특이점 제거 · HDry 아래 운동량 0
	double HDry = 1e-4;          // m
	bool   bWetDry = true;       // Bollermann 2013 젖음/마름 재구성 + 배수 시간 (끄면 KP07 그대로)
	double Manning = 0.0;        // 바닥 마찰 n (s/m^⅓). 모래 0.02~0.03
	bool   bDesingCells = false;  // 칸 평균에도 (2.17) — 섬은 안정되지만 처오름 혀를 ε^¼ (= Δx) 아래에서 감속시킨다 (m3-log #1)

	TArray<double> BCorner;      // (NX+1)·(NY+1)
	TArray<double> W, HU, HV;    // NX·NY

	void Init(int32 InNX, int32 InNY, double InDx, double InDy);
	double BCell(int32 i, int32 j) const;
	double H(int32 i, int32 j) const { return W[Idx(i, j)] - BCell(i, j); }

	/** 한 스텝 (SSP-RK2). Dt ≤ 0 이면 CFL 로 정한다. 쓴 Dt 를 돌려준다 */
	double Step(double Dt = -1.0);
	double TotalVolume() const;
	double MaxSpeed() const;

	int32 Idx(int32 i, int32 j) const { return j * NX + i; }
	static double CellLevel(const double Bc[4], double Hbar, double Lo, double Hi);   // 물가 칸 수면 — 부피 역산
	static double CellVolume(const double Bc[4], double L);   // 이선형 칸 위 max(0, L − B) 평균 (8×8 표본)

private:
	mutable double FrictionDt = 0.0;   // 이번 단계 Δt (마찰은 SSP 단계마다 암시적으로)
	double ComputeRhs(const TArray<double>& Wi, const TArray<double>& HUi, const TArray<double>& HVi,
		TArray<double>& dW, TArray<double>& dHU, TArray<double>& dHV, double& InOutDt) const;
	double BFaceX(int32 i, int32 j) const;   // 칸 (i, j) 의 오른쪽 경계 (i+½) — i = −1..NX−1
	double BFaceY(int32 i, int32 j) const;   // 위쪽 경계 (j+½)
	void Desing(double h, double q, double& u) const;
	void FixDry(TArray<double>& Wi, TArray<double>& HUi, TArray<double>& HVi) const;
};
