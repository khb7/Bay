#include "SandGridCpu.h"

namespace
{
	double MinMod3(double A, double B, double C)
	{
		if (A > 0.0 && B > 0.0 && C > 0.0) { return FMath::Min3(A, B, C); }
		if (A < 0.0 && B < 0.0 && C < 0.0) { return FMath::Max3(A, B, C); }
		return 0.0;
	}
}

void FSandGridCpu::Init(int32 InNX, int32 InNY, double InDx, double InDy)
{
	NX = InNX; NY = InNY; Dx = InDx; Dy = InDy;
	BCorner.Init(0.0, (NX + 1) * (NY + 1));
	W.Init(0.0, NX * NY); HU.Init(0.0, NX * NY); HV.Init(0.0, NX * NY);
}

double FSandGridCpu::BFaceX(int32 i, int32 j) const
{
	// 칸 (i, j) 의 오른쪽 경계 x_{i+½} — 꼭짓점 (i+1, j) · (i+1, j+1) 평균 (KP07 3.11)
	const int32 c = FMath::Clamp(i + 1, 0, NX);
	return 0.5 * (BCorner[j * (NX + 1) + c] + BCorner[(j + 1) * (NX + 1) + c]);
}

double FSandGridCpu::BFaceY(int32 i, int32 j) const
{
	const int32 r = FMath::Clamp(j + 1, 0, NY);
	return 0.5 * (BCorner[r * (NX + 1) + i] + BCorner[r * (NX + 1) + i + 1]);
}

double FSandGridCpu::BCell(int32 i, int32 j) const
{
	return 0.25 * (BFaceX(i, j) + BFaceX(i - 1, j) + BFaceY(i, j) + BFaceY(i, j - 1));
}

void FSandGridCpu::Desing(double h, double q, double& u) const
{
	// KP07 (2.17): u = √2·h·q / √(h⁴ + max(h⁴, ε)), ε = Δx⁴ — 단순 h/(h²+ε) 는 작은 h 에서 u 가 폭주한다
	const double E = Eps > 0.0 ? Eps : FMath::Max(FMath::Pow(Dx, 4.0), FMath::Pow(Dy, 4.0));
	const double H4 = h * h * h * h;
	u = UE_SQRT_2 * h * q / FMath::Sqrt(H4 + FMath::Max(H4, E));
}

double FSandGridCpu::ComputeRhs(const TArray<double>& Wi, const TArray<double>& HUi, const TArray<double>& HVi,
	TArray<double>& dW, TArray<double>& dHU, TArray<double>& dHV) const
{
	dW.Init(0.0, NX * NY); dHU.Init(0.0, NX * NY); dHV.Init(0.0, NX * NY);
	double AMax = 0.0;

	// 벽 경계: 바깥 칸 = 안쪽 거울 (수직 유량 부호 반대)
	auto Get = [&](int32 i, int32 j, double& w, double& qu, double& qv, double& b)
	{
		double Su = 1.0, Sv = 1.0;
		if (i < 0) { i = -1 - i; Su = -1.0; } else if (i >= NX) { i = 2 * NX - 1 - i; Su = -1.0; }
		if (j < 0) { j = -1 - j; Sv = -1.0; } else if (j >= NY) { j = 2 * NY - 1 - j; Sv = -1.0; }
		i = FMath::Clamp(i, 0, NX - 1); j = FMath::Clamp(j, 0, NY - 1);   // 폭이 1~2 칸인 방향 (1D 띠) 에서 거울이 한 번 더 넘어갈 때
		const int32 k = j * NX + i;
		w = Wi[k]; qu = HUi[k] * Su; qv = HVi[k] * Sv; b = BCell(i, j);
	};

	// 한 방향 처리 — Axis 0 = x (i 를 따라), 1 = y
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const int32 NA = Axis == 0 ? NX : NY, NB = Axis == 0 ? NY : NX;
		const double D = Axis == 0 ? Dx : Dy;
		for (int32 m = 0; m < NB; ++m)
		{
			// 칸 c 의 재구성: 경계 c−½ (West/South) 쪽 값과 c+½ (East/North) 쪽 값
			struct FRec { double wm, wp, qm, qp, rm, rp; };   // q = 진행 방향 유량, r = 가로 유량
			auto Cell = [&](int32 c, double& w, double& q, double& r, double& b)
			{
				double qu, qv;
				if (Axis == 0) { Get(c, m, w, qu, qv, b); q = qu; r = qv; }
				else           { Get(m, c, w, qu, qv, b); q = qv; r = qu; }
			};
			auto FaceB = [&](int32 c) { return Axis == 0 ? BFaceX(FMath::Clamp(c, -1, NX - 1), m) : BFaceY(m, FMath::Clamp(c, -1, NY - 1)); };   // c+½
			auto Rec = [&](int32 c) -> FRec
			{
				double w0, q0, r0, b0, w1, q1, r1, b1, w2, q2, r2, b2;
				Cell(c - 1, w0, q0, r0, b0); Cell(c, w1, q1, r1, b1); Cell(c + 1, w2, q2, r2, b2);
				auto Slope = [&](double a, double bb, double cc) { return MinMod3(Theta * (bb - a), 0.5 * (cc - a), Theta * (cc - bb)); };
				const double sw = Slope(w0, w1, w2), sq = Slope(q0, q1, q2), sr = Slope(r0, r1, r2);
				FRec R{ w1 - 0.5 * sw, w1 + 0.5 * sw, q1 - 0.5 * sq, q1 + 0.5 * sq, r1 - 0.5 * sr, r1 + 0.5 * sr };
				// KP07 (2.15)/(2.16): 경계 수면이 지형 아래면 그 경계에 맞추고 반대쪽을 질량 보존으로
				const double BE = FaceB(c), BW = FaceB(c - 1);
				if (R.wp < BE) { R.wp = BE; R.wm = 2.0 * w1 - BE; }
				else if (R.wm < BW) { R.wm = BW; R.wp = 2.0 * w1 - BW; }
				return R;
			};

			TArray<double> FW, FQ, FR;   // 경계 c+½ 유량, c = −1..NA−1
			FW.SetNumZeroed(NA + 1); FQ.SetNumZeroed(NA + 1); FR.SetNumZeroed(NA + 1);
			TArray<FRec> Recs; Recs.SetNum(NA + 2);
			for (int32 c = -1; c <= NA; ++c) { Recs[c + 1] = Rec(c); }
			for (int32 c = -1; c < NA; ++c)
			{
				const FRec& L = Recs[c + 1];
				const FRec& R = Recs[c + 2];
				const double Bf = FaceB(c);
				const double hL = FMath::Max(0.0, L.wp - Bf), hR = FMath::Max(0.0, R.wm - Bf);
				double uL, uR, vL, vR;
				Desing(hL, L.qp, uL); Desing(hR, R.qm, uR);
				Desing(hL, L.rp, vL); Desing(hR, R.rm, vR);
				const double qL = hL * uL, qR = hR * uR, rL = hL * vL, rR = hR * vR;   // 일관성 — 유량을 h·u 로 다시 (2.21)
				const double ap = FMath::Max3(uL + FMath::Sqrt(G * hL), uR + FMath::Sqrt(G * hR), 0.0);
				const double am = FMath::Min3(uL - FMath::Sqrt(G * hL), uR - FMath::Sqrt(G * hR), 0.0);
				AMax = FMath::Max(AMax, FMath::Max(ap, -am) / D);
				const int32 f = c + 1;
				if (ap - am < 1e-12) { continue; }
				const double Inv = 1.0 / (ap - am);
				const double F1L = qL, F1R = qR;
				const double F2L = qL * uL + 0.5 * G * hL * hL, F2R = qR * uR + 0.5 * G * hR * hR;
				const double F3L = rL * uL, F3R = rR * uR;
				FW[f] = (ap * F1L - am * F1R) * Inv + ap * am * Inv * (R.wm - L.wp);
				FQ[f] = (ap * F2L - am * F2R) * Inv + ap * am * Inv * (qR - qL);
				FR[f] = (ap * F3L - am * F3R) * Inv + ap * am * Inv * (rR - rL);
			}
			for (int32 c = 0; c < NA; ++c)
			{
				const int32 k = Axis == 0 ? (m * NX + c) : (c * NX + m);
				const FRec& Rc = Recs[c + 1];
				const double BE = FaceB(c), BW = FaceB(c - 1);
				// 경사 원천 (KP07 2.10 / 3.10): −g·h̄·(B_{c+½} − B_{c−½})/Δ, h̄ = 두 경계 수심 평균 — 정지 수면에서 유량 차와 정확히 상쇄
				const double Hbar = 0.5 * ((Rc.wp - BE) + (Rc.wm - BW));
				const double Src = -G * Hbar * (BE - BW) / D;
				dW[k] -= (FW[c + 1] - FW[c]) / D;
				if (Axis == 0) { dHU[k] += -(FQ[c + 1] - FQ[c]) / D + Src; dHV[k] -= (FR[c + 1] - FR[c]) / D; }
				else           { dHV[k] += -(FQ[c + 1] - FQ[c]) / D + Src; dHU[k] -= (FR[c + 1] - FR[c]) / D; }
			}
		}
	}
	return AMax;
}

void FSandGridCpu::FixDry(TArray<double>& Wi, TArray<double>& HUi, TArray<double>& HVi) const
{
	// 칸 평균에도 속도 특이점 제거 — 유량을 h·u(KP07 2.17) 로 다시. 아주 얕은 칸 (HDry 아래) 은 운동량 0
	// (KP07 은 재구성 점에서만 하지만, 얇은 막의 칸 평균 hu 가 남아 가짜 흐름이 자란다 — m3-log #1)
	if (!bFixDry) { return; }
	for (int32 j = 0; j < NY; ++j)
	{
		for (int32 i = 0; i < NX; ++i)
		{
			const int32 k = Idx(i, j);
			const double h = Wi[k] - BCell(i, j);
			if (h < HDry) { HUi[k] = 0.0; HVi[k] = 0.0; continue; }
			if (Manning > 0.0 && FrictionDt > 0.0)
			{
				// Manning 바닥 마찰, 암시적: q ← q / (1 + Δt·g·n²·|u| / h^(4/3)) — 얇은 막일수록 강하게 멈춘다
				const double U = FMath::Sqrt(HUi[k] * HUi[k] + HVi[k] * HVi[k]) / h;
				const double D = 1.0 + FrictionDt * G * Manning * Manning * U / FMath::Pow(h, 4.0 / 3.0);
				HUi[k] /= D; HVi[k] /= D;
			}
			if (!bDesingCells) { continue; }
			double u, v; Desing(h, HUi[k], u); Desing(h, HVi[k], v);
			HUi[k] = h * u; HVi[k] = h * v;
		}
	}
}

double FSandGridCpu::Step(double Dt)
{
	TArray<double> dW, dHU, dHV;
	const double A = ComputeRhs(W, HU, HV, dW, dHU, dHV);
	if (Dt <= 0.0) { Dt = A > 0.0 ? Cfl / A : 1e-3; }   // A = max(a/Δ) 를 두 방향 합으로 보지 않고 큰 쪽 — CFL 1/4 가 둘레를 덮는다
	const int32 N = NX * NY;
	TArray<double> W1 = W, HU1 = HU, HV1 = HV;
	for (int32 k = 0; k < N; ++k) { W1[k] += Dt * dW[k]; HU1[k] += Dt * dHU[k]; HV1[k] += Dt * dHV[k]; }
	FrictionDt = Dt; FixDry(W1, HU1, HV1);
	ComputeRhs(W1, HU1, HV1, dW, dHU, dHV);
	for (int32 k = 0; k < N; ++k)
	{
		W[k]  = 0.5 * (W[k]  + W1[k]  + Dt * dW[k]);
		HU[k] = 0.5 * (HU[k] + HU1[k] + Dt * dHU[k]);
		HV[k] = 0.5 * (HV[k] + HV1[k] + Dt * dHV[k]);
	}
	FixDry(W, HU, HV);
	return Dt;
}

double FSandGridCpu::TotalVolume() const
{
	double V = 0.0;
	for (int32 j = 0; j < NY; ++j) { for (int32 i = 0; i < NX; ++i) { V += H(i, j); } }
	return V * Dx * Dy;
}

double FSandGridCpu::MaxSpeed() const
{
	double M = 0.0;
	for (int32 j = 0; j < NY; ++j)
	{
		for (int32 i = 0; i < NX; ++i)
		{
			const int32 k = Idx(i, j);
			const double h = H(i, j);
			double u, v; Desing(h, HU[k], u); Desing(h, HV[k], v);
			M = FMath::Max(M, FMath::Sqrt(u * u + v * v));
		}
	}
	return M;
}
