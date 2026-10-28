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

double FSandGridCpu::CellLevel(const double Bc[4], double Hbar, double Lo, double Hi)
{
	// 이선형 칸 (꼭짓점 Bc: 00 10 01 11) 위 물 부피 / 넓이 = Hbar 가 되는 수면 L — 이분법. 부피는 L 에 대해 단조 증가.
	// 부피는 칸 안 8×8 표본으로 적분 (CPU 기준판 — GPU 판은 닫힌 꼴이나 표를 쓸 것)
	double A = Lo, B = Hi;
	for (int32 n = 0; n < 40; ++n) { const double M = 0.5 * (A + B); (CellVolume(Bc, M) < Hbar ? A : B) = M; }
	return 0.5 * (A + B);
}

double FSandGridCpu::CellVolume(const double Bc[4], double L)
{
	double V = 0.0;
	for (int32 b = 0; b < 8; ++b) for (int32 a = 0; a < 8; ++a)
	{
		const double u = (a + 0.5) / 8.0, v = (b + 0.5) / 8.0;
		V += FMath::Max(0.0, L - ((1 - u) * (1 - v) * Bc[0] + u * (1 - v) * Bc[1] + (1 - u) * v * Bc[2] + u * v * Bc[3]));
	}
	return V / 64.0;
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
	TArray<double>& dW, TArray<double>& dHU, TArray<double>& dHV, double& InOutDt) const
{
	const int32 N = NX * NY;
	dW.Init(0.0, N); dHU.Init(0.0, N); dHV.Init(0.0, N);
	double AMax = 0.0;

	// 벽 경계: 바깥 칸 = 안쪽 거울 (수직 유량 부호 반대)
	auto Get = [&](int32 i, int32 j, double& w, double& qu, double& qv)
	{
		double Su = 1.0, Sv = 1.0;
		if (i < 0) { i = -1 - i; Su = -1.0; } else if (i >= NX) { i = 2 * NX - 1 - i; Su = -1.0; }
		if (j < 0) { j = -1 - j; Sv = -1.0; } else if (j >= NY) { j = 2 * NY - 1 - j; Sv = -1.0; }
		i = FMath::Clamp(i, 0, NX - 1); j = FMath::Clamp(j, 0, NY - 1);   // 폭이 1~2 칸인 방향 (1D 띠) 에서 거울이 한 번 더 넘어갈 때
		const int32 k = j * NX + i;
		w = Wi[k]; qu = HUi[k] * Su; qv = HVi[k] * Sv;
	};

	// 경계 유량 저장 — 배수 시간(Bollermann 4.7)은 네 경계를 다 봐야 해서 쌓기 전에 모은다
	// x 경계: (NX+1)·NY, 칸 (i, j) 의 왼쪽 경계 = j·(NX+1) + i.  y 경계: NX·(NY+1), 칸 (i, j) 의 아래 경계 = j·NX + i
	struct FFace { double H1 = 0, H2a = 0, H2g = 0, H3 = 0; };
	TArray<FFace> FX, FY;
	FX.SetNum((NX + 1) * NY); FY.SetNum(NX * (NY + 1));

	// 칸마다 수면 하나 (m3-log #2): 잠긴 칸은 w̄, 물가 칸(일부만 잠김)은 이선형 지형 위 물 부피 = h̄ 가 되는 수면 L 을 역산.
	// 두 축이 같은 L 을 쓴다 — 축마다 1D 삼각형(Bollermann 3.5)으로 따로 추정하면 2D 에서 서로 어긋나 가짜 흐름이 남는다.
	// 기울기 계산도 L 로 (Bollermann Remark 3.1: 물가 칸에서 w̄ 는 수면이 아니다)
	TArray<double> Lc; Lc.SetNumUninitialized(N);
	TArray<uint8> Part; Part.Init(0, N);
	for (int32 j = 0; j < NY; ++j)
	for (int32 i = 0; i < NX; ++i)
	{
		const int32 k = Idx(i, j);
		Lc[k] = Wi[k];
		if (!bWetDry) { continue; }
		const double Bc[4] = { BCorner[j * (NX + 1) + i], BCorner[j * (NX + 1) + i + 1], BCorner[(j + 1) * (NX + 1) + i], BCorner[(j + 1) * (NX + 1) + i + 1] };
		const double Hi = FMath::Max(FMath::Max(Bc[0], Bc[1]), FMath::Max(Bc[2], Bc[3]));
		const double Lo = FMath::Min(FMath::Min(Bc[0], Bc[1]), FMath::Min(Bc[2], Bc[3]));
		const double Hbar = Wi[k] - BCell(i, j);
		const double VFull = Hi - BCell(i, j);   // 수면이 가장 높은 꼭짓점에 닿을 때의 h̄
		if (Hbar >= VFull || Hi - Lo < 1e-12) { continue; }   // 잠긴 칸
		Part[k] = 1;
		Lc[k] = CellLevel(Bc, FMath::Max(0.0, Hbar), Lo, Hi);
	}

	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const int32 NA = Axis == 0 ? NX : NY, NB = Axis == 0 ? NY : NX;
		const double D = Axis == 0 ? Dx : Dy;
		for (int32 m = 0; m < NB; ++m)
		{
			struct FRec { double wm, wp, qm, qp, rm, rp, wbar, bm, bp; bool bPartial; };   // q = 진행 방향 유량, r = 가로 유량, bm/bp = 양 경계 지형
			auto Cell = [&](int32 c, double& w, double& q, double& r)
			{
				double qu, qv;
				if (Axis == 0) { Get(c, m, w, qu, qv); q = qu; r = qv; }
				else           { Get(m, c, w, qu, qv); q = qv; r = qu; }
			};
			auto CellK = [&](int32 c)   // 거울 경계를 거친 칸 번호
			{
				int32 i = Axis == 0 ? c : m, j = Axis == 0 ? m : c;
				if (i < 0) { i = -1 - i; } else if (i >= NX) { i = 2 * NX - 1 - i; }
				if (j < 0) { j = -1 - j; } else if (j >= NY) { j = 2 * NY - 1 - j; }
				return Idx(FMath::Clamp(i, 0, NX - 1), FMath::Clamp(j, 0, NY - 1));
			};
			auto FaceB = [&](int32 c) { return Axis == 0 ? BFaceX(FMath::Clamp(c, -1, NX - 1), m) : BFaceY(m, FMath::Clamp(c, -1, NY - 1)); };   // c+½
			TArray<FRec> Recs; Recs.SetNum(NA + 2);
			for (int32 c = -1; c <= NA; ++c)
			{
				double w0, q0, r0, w1, q1, r1, w2, q2, r2;
				Cell(c - 1, w0, q0, r0); Cell(c, w1, q1, r1); Cell(c + 1, w2, q2, r2);
				const int32 k0 = CellK(c - 1), k1 = CellK(c), k2 = CellK(c + 1);
				const double l0 = Lc[k0], l1 = Lc[k1], l2 = Lc[k2];   // 기울기는 수면 L 로
				auto Slope = [&](double a, double bb, double cc) { return MinMod3(Theta * (bb - a), 0.5 * (cc - a), Theta * (cc - bb)); };
				const double sw = Slope(l0, l1, l2), sq = Slope(q0, q1, q2), sr = Slope(r0, r1, r2);
				const double BE = FaceB(c), BW = FaceB(c - 1);
				FRec R{ w1 - 0.5 * sw, w1 + 0.5 * sw, q1 - 0.5 * sq, q1 + 0.5 * sq, r1 - 0.5 * sr, r1 + 0.5 * sr, w1, BW, BE, Part[k1] != 0 };
				if (R.bPartial)
				{
					// 물가 칸: 수면은 평평한 L (마른 쪽 경계는 지형 높이 — 수심 0). 정지 수면이면 이웃 잠긴 칸의 경계 수면과 같다
					R.wm = FMath::Max(l1, BW); R.wp = FMath::Max(l1, BE);
				}
				else
				{
					// 잠긴 칸 — KP07 (2.15)/(2.16)
					if (R.wp < BE) { R.wp = BE; R.wm = 2.0 * w1 - BE; }
					else if (R.wm < BW) { R.wm = BW; R.wp = 2.0 * w1 - BW; }
				}
				Recs[c + 1] = R;
			}
			for (int32 c = -1; c < NA; ++c)
			{
				const FRec& L = Recs[c + 1];
				const FRec& R = Recs[c + 2];
				const double Bf = FaceB(c);
				const double hL = FMath::Max(0.0, L.wp - Bf), hR = FMath::Max(0.0, R.wm - Bf);
				double uL = 0, uR = 0, vL = 0, vR = 0;
				if (hL > 0.0) { Desing(hL, L.qp, uL); Desing(hL, L.rp, vL); }
				if (hR > 0.0) { Desing(hR, R.qm, uR); Desing(hR, R.rm, vR); }
				const double qL = hL * uL, qR = hR * uR, rL = hL * vL, rR = hR * vR;   // 일관성 — 유량을 h·u 로 다시 (KP07 2.21). 마른 쪽 u = 0
				const double ap = FMath::Max3(uL + FMath::Sqrt(G * hL), uR + FMath::Sqrt(G * hR), 0.0);
				const double am = FMath::Min3(uL - FMath::Sqrt(G * hL), uR - FMath::Sqrt(G * hR), 0.0);
				AMax = FMath::Max(AMax, FMath::Max(ap, -am) / D);
				FFace F;
				if (ap - am > 1e-12)
				{
					const double Inv = 1.0 / (ap - am), Dif = ap * am * Inv;
					F.H1  = (ap * qL - am * qR) * Inv + Dif * (hR - hL);                                 // (h+B)⁺ − (h+B)⁻, 같은 경계 B
					F.H2a = (ap * qL * uL - am * qR * uR) * Inv;                                         // 이류 — 배수 시간으로 자른다
					F.H2g = (ap * 0.5 * G * hL * hL - am * 0.5 * G * hR * hR) * Inv + Dif * (qR - qL);   // 중력 — 원천과 상쇄, 자르지 않는다 (4.11)
					F.H3  = (ap * rL * uL - am * rR * uR) * Inv + Dif * (rR - rL);
				}
				if (Axis == 0) { FX[m * (NX + 1) + (c + 1)] = F; } else { FY[(c + 1) * NX + m] = F; }
			}
			// 경사 원천 (Bollermann 2.7): −g·h̄_칸·(B_{c+½} − B_{c−½})/Δ — 칸 평균 수심. 일부만 잠긴 칸에서도 중력 유량과 정확히 상쇄된다
			for (int32 c = 0; c < NA; ++c)
			{
				const FRec& Rc = Recs[c + 1];
				const int32 k = Axis == 0 ? (m * NX + c) : (c * NX + m);
				// 물가 칸은 경계 수심 압력 차 g/2·(h_E² − h_W²)/Δ (정수압 재구성 꼴, Audusse 2004) — 평평한 L 이면 중력 유량과 정확히 상쇄.
				// 1D 에서는 Bollermann (2.7) 의 −g·h̄·ΔB/Δ 와 같다 (삼각형 h̄ = h_E²/2ΔB)
				const double Hbar = Rc.wbar - 0.5 * (Rc.bm + Rc.bp);
				const double hE = Rc.wp - Rc.bp, hW = Rc.wm - Rc.bm;
				const double Src = Rc.bPartial ? 0.5 * G * (hE * hE - hW * hW) / D : -G * Hbar * (Rc.bp - Rc.bm) / D;
				if (Axis == 0) { dHU[k] += Src; } else { dHV[k] += Src; }
			}
		}
	}

	if (InOutDt <= 0.0) { InOutDt = AMax > 0.0 ? Cfl / AMax : 1e-3; }
	const double Dt = InOutDt;

	// 배수 시간 (Bollermann 4.7–4.9): 칸의 물이 나가는 유량으로 다 빠지는 시간. 경계의 실효 Δt = min(Δt, 바람 위 칸의 배수 시간)
	TArray<double> Drain; Drain.Init(Dt, N);
	if (bWetDry)
	{
		for (int32 j = 0; j < NY; ++j)
		for (int32 i = 0; i < NX; ++i)
		{
			const double Out = FMath::Max(0.0, FX[j * (NX + 1) + i + 1].H1) / Dx + FMath::Max(0.0, -FX[j * (NX + 1) + i].H1) / Dx
			                 + FMath::Max(0.0, FY[(j + 1) * NX + i].H1) / Dy + FMath::Max(0.0, -FY[j * NX + i].H1) / Dy;
			const double h = Wi[Idx(i, j)] - BCell(i, j);
			if (Out > 0.0) { Drain[Idx(i, j)] = FMath::Min(Dt, FMath::Max(0.0, h) / Out); }
		}
	}
	auto Scale = [&](double H1, int32 kLeft, int32 kRight)   // 바람 위 칸 (H1 ≥ 0 이면 왼쪽). 벽 바깥(−1)이면 자르지 않는다
	{
		const int32 Up = H1 >= 0.0 ? kLeft : kRight;
		return Up >= 0 ? Drain[Up] / Dt : 1.0;
	};
	for (int32 j = 0; j < NY; ++j)
	for (int32 i = 0; i < NX; ++i)
	{
		const int32 k = Idx(i, j);
		const FFace& XL = FX[j * (NX + 1) + i];     const FFace& XR = FX[j * (NX + 1) + i + 1];
		const FFace& YL = FY[j * NX + i];           const FFace& YR = FY[(j + 1) * NX + i];
		const double sXL = Scale(XL.H1, i > 0 ? k - 1 : -1, k), sXR = Scale(XR.H1, k, i + 1 < NX ? k + 1 : -1);
		const double sYL = Scale(YL.H1, j > 0 ? k - NX : -1, k), sYR = Scale(YR.H1, k, j + 1 < NY ? k + NX : -1);
		dW[k]  -= (sXR * XR.H1 - sXL * XL.H1) / Dx + (sYR * YR.H1 - sYL * YL.H1) / Dy;
		dHU[k] -= (sXR * XR.H2a - sXL * XL.H2a + XR.H2g - XL.H2g) / Dx + (sYR * YR.H3 - sYL * YL.H3) / Dy;
		dHV[k] -= (sYR * YR.H2a - sYL * YL.H2a + YR.H2g - YL.H2g) / Dy + (sXR * XR.H3 - sXL * XL.H3) / Dx;
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
	ComputeRhs(W, HU, HV, dW, dHU, dHV, Dt);   // Dt ≤ 0 이면 CFL 로 정해 돌려준다 — 두 단계가 같은 Δt (배수 시간도 이 Δt 기준)
	const int32 N = NX * NY;
	TArray<double> W1 = W, HU1 = HU, HV1 = HV;
	for (int32 k = 0; k < N; ++k) { W1[k] += Dt * dW[k]; HU1[k] += Dt * dHU[k]; HV1[k] += Dt * dHV[k]; }
	FrictionDt = Dt; FixDry(W1, HU1, HV1);
	ComputeRhs(W1, HU1, HV1, dW, dHU, dHV, Dt);
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
