// M3 T1 검증 — 커맨드릿 `-run=SpikeSweep -gridtest` (m3-plan T1 · T2 통과 기준).
//   ① 정지 수면: 울퉁불퉁한 바닥 + 섬 (마른 칸), 1000 스텝 뒤 최대 속력
//   ② 마른 경사로 흘러드는 댐 붕괴: 질량 보존 · 음수 수심 · NaN
//   ③ Synolakis 고립파 처오름 (1:19.85, H/d 0.0185 · 0.3): R/d 를 2.831·√cot β·(H/d)^1.25 와 비교

#include "SandGridCpu.h"

#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogSandGrid, Log, All);

namespace
{
	struct FOpt { bool bFix = true, bDesing = false; double HDry = 1e-4, Eps = 0.0, N = 0.0; } GOpt;
	void Apply(FSandGridCpu& S) { S.bFixDry = GOpt.bFix; S.bDesingCells = GOpt.bDesing; S.HDry = GOpt.HDry; S.Eps = GOpt.Eps; S.Manning = GOpt.N; }

	void LakeAtRest(bool bIsland)
	{
		FSandGridCpu S; Apply(S);
		S.Init(100, 50, 0.1, 0.1);
		for (int32 j = 0; j <= S.NY; ++j)
		for (int32 i = 0; i <= S.NX; ++i)
		{
			const double x = i * S.Dx, y = j * S.Dy;
			double B = 0.25 * (1.0 + FMath::Sin(1.7 * x) * FMath::Cos(2.3 * y));
			if (bIsland) B += 0.7 * FMath::Exp(-((x - 6.0) * (x - 6.0) + (y - 2.5) * (y - 2.5)) / 0.5);   // 물 위로 나온 섬
			S.BCorner[j * (S.NX + 1) + i] = B;
		}
		for (int32 j = 0; j < S.NY; ++j) { for (int32 i = 0; i < S.NX; ++i) { S.W[S.Idx(i, j)] = FMath::Max(0.55, S.BCell(i, j)); } }
		const double V0 = S.TotalVolume();
		int32 Dry = 0; for (int32 j = 0; j < S.NY; ++j) { for (int32 i = 0; i < S.NX; ++i) { Dry += S.H(i, j) <= 0.0 ? 1 : 0; } }
		double T = 0.0;
		for (int32 n = 1; n <= 20000; ++n)
		{
			T += bIsland ? S.Step() : S.Step(0.004);   // 섬: CFL Δt (고정 Δt 는 가짜 속도가 커지면 CFL 을 넘는다)
			if (bIsland && (n == 10 || n == 100 || n == 1000 || n == 5000 || n == 20000))
			{
				// 가장 빠른 칸이 어디인지 — 젖음/마름 경계인가
				double M = 0; int32 Mi = 0, Mj = 0;
				int32 Fast = 0;
				for (int32 j = 0; j < S.NY; ++j) for (int32 i = 0; i < S.NX; ++i)
				{
					const double h = S.H(i, j); if (h <= 1e-3) { continue; }   // 1mm 넘는 물만 (얇은 막의 u 는 의미가 없다)
					Fast += FMath::Sqrt(FMath::Square(S.HU[S.Idx(i, j)]) + FMath::Square(S.HV[S.Idx(i, j)])) / h > 0.01 ? 1 : 0;
					const double v = FMath::Sqrt(FMath::Square(S.HU[S.Idx(i, j)]) + FMath::Square(S.HV[S.Idx(i, j)])) / h;
					if (v > M) { M = v; Mi = i; Mj = j; }
				}
				UE_LOG(LogSandGrid, Display, TEXT("   섬 n %5d (%.1fs): 수심 1mm↑ 최대 속력 %.3e m/s @ (%d,%d) 수심 %.2e · 1cm/s 넘는 칸 %d"), n, T, M, Mi, Mj, S.H(Mi, Mj), Fast);
			}
			if (!bIsland && n == 1000) { break; }
		}
		UE_LOG(LogSandGrid, Display, TEXT("① 정지 수면 (섬 %d · 마른 칸 %d): 1000 스텝 %.2fs 뒤 최대 속력 %.3e m/s · 부피 변화 %.2e (기준 |u| < 1e-6)"),
			bIsland ? 1 : 0, Dry, T, S.MaxSpeed(), (S.TotalVolume() - V0) / V0);
	}

	void DamBreakDrySlope()
	{
		FSandGridCpu S; Apply(S);
		S.Init(400, 1, 0.05, 0.05);
		for (int32 j = 0; j <= 1; ++j) { for (int32 i = 0; i <= S.NX; ++i) { S.BCorner[j * (S.NX + 1) + i] = 0.05 * i * S.Dx; } }
		for (int32 i = 0; i < S.NX; ++i) { const double B = S.BCell(i, 0); S.W[i] = i * S.Dx < 5.0 ? FMath::Max(1.0, B) : B; }
		const double V0 = S.TotalVolume();
		double T = 0.0, MinH = 1e9, Front = 0.0;
		bool bNan = false;
		for (int32 n = 0; n < 4000; ++n)
		{
			T += S.Step();
			for (int32 i = 0; i < S.NX; ++i)
			{
				const double h = S.H(i, 0);
				bNan |= !FMath::IsFinite(h) || !FMath::IsFinite(S.HU[i]);
				MinH = FMath::Min(MinH, h);
				if (h > 1e-3) { Front = FMath::Max(Front, (i + 0.5) * S.Dx); }
			}
		}
		UE_LOG(LogSandGrid, Display, TEXT("② 마른 경사 댐 붕괴: %.2fs · 부피 변화 %.2e · 최소 수심 %.2e · 물 앞 끝 %.2f m · NaN %s"),
			T, (S.TotalVolume() - V0) / V0, MinH, Front, bNan ? TEXT("있음") : TEXT("없음"));
	}

	void Synolakis(double Hd, double Dx)
	{
		// 무차원 d = 1. x 는 바다 쪽 + : 해안선 x = 0, 경사 B = −x/cotβ (0 ≤ x ≤ X0), 그 밖 평평한 바닥 −1. 땅(x < 0)은 같은 경사로 올라간다
		const double Cot = 19.85, X0 = Cot, XL = -6.0, XR = 90.0;
		FSandGridCpu S; Apply(S);
		S.Init(FMath::RoundToInt((XR - XL) / Dx), 1, Dx, Dx);
		auto Bat = [&](double x) { return x >= X0 ? -1.0 : -x / Cot; };
		for (int32 j = 0; j <= 1; ++j) { for (int32 i = 0; i <= S.NX; ++i) { S.BCorner[j * (S.NX + 1) + i] = Bat(XL + i * Dx); } }
		const double Gam = FMath::Sqrt(0.75 * Hd);
		const double X1 = X0 + std::acosh(FMath::Sqrt(20.0)) / Gam;   // Synolakis 1987: 파봉이 경사 발치에서 반 파장 떨어져 시작
		for (int32 i = 0; i < S.NX; ++i)
		{
			const double x = XL + (i + 0.5) * Dx;
			const double Sech = 1.0 / std::cosh(Gam * (x - X1));
			const double Eta = Hd * Sech * Sech;
			const double B = S.BCell(i, 0);
			S.W[i] = FMath::Max(Eta, B);
			const double h = S.W[i] - B;
			S.HU[i] = h > 0.0 ? -h * Eta * FMath::Sqrt(S.G) : 0.0;   // 해안 쪽 (−x) 으로 u = −η·√(g/d)
		}
		const double V0 = S.TotalVolume();
		double T = 0.0, Run = -1e9;
		bool bNan = false;
		while (T < 30.0 && !bNan)   // 30s (t√(g/d) ≈ 94) — 파가 38 d 를 와서 처오르고 되돌아갈 때까지
		{
			T += S.Step();
			for (int32 i = 0; i < S.NX; ++i)
			{
				const double h = S.H(i, 0);
				bNan |= !FMath::IsFinite(h);
				if (h > 1e-4 && S.BCell(i, 0) >= 0.0) { Run = FMath::Max(Run, S.W[i]); }   // 처음 마른 땅 위 젖은 칸의 수면 = 처오름
			}
		}
		const double Law = 2.831 * FMath::Sqrt(Cot) * FMath::Pow(Hd, 1.25);
		UE_LOG(LogSandGrid, Display, TEXT("③ Synolakis H/d %.4f · Δx %.3f: 최대 처오름 R/d %.4f · 법칙(비쇄파) %.4f · 비 %.3f · 부피 변화 %.2e · NaN %s"),
			Hd, Dx, Run, Law, Run / Law, (S.TotalVolume() - V0) / V0, bNan ? TEXT("있음") : TEXT("없음"));
	}
}

int32 RunSandGridTest(const FString& Params)
{
	const double T0 = FPlatformTime::Seconds();
	GOpt.bFix = !FParse::Param(*Params, TEXT("gridnofix"));
	GOpt.bDesing = FParse::Param(*Params, TEXT("griddesing"));
	double Er = 0.0; if (FParse::Value(*Params, TEXT("gridepsr="), Er)) { GOpt.Eps = Er * Er * Er * Er; }   // ε^¼ (m)
	FParse::Value(*Params, TEXT("gridmanning="), GOpt.N);
	double Hd = 1e-4; if (FParse::Value(*Params, TEXT("gridhdry="), Hd)) { GOpt.HDry = Hd; }
	UE_LOG(LogSandGrid, Display, TEXT("격자 시험 — 마른 칸 운동량 0 %d · 칸 평균 특이점 제거 %d · HDry %.1e · ε^¼ %.1e · Manning %.3f"), GOpt.bFix ? 1 : 0, GOpt.bDesing ? 1 : 0, GOpt.HDry, FMath::Pow(GOpt.Eps, 0.25), GOpt.N);
	LakeAtRest(false);
	LakeAtRest(true);
	DamBreakDrySlope();
	Synolakis(0.0185, 0.1);
	Synolakis(0.0185, 0.05);
	Synolakis(0.3, 0.05);
	UE_LOG(LogSandGrid, Display, TEXT("격자 시험 끝 — %.1fs"), FPlatformTime::Seconds() - T0);
	return 0;
}
