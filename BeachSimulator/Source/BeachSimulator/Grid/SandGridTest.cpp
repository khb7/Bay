// M3 T1 검증 — 커맨드릿 `-run=SpikeSweep -gridtest` (m3-plan T1 · T2 통과 기준).
//   ① 정지 수면: 울퉁불퉁한 바닥 + 섬 (마른 칸), 1000 스텝 뒤 최대 속력
//   ② 마른 경사로 흘러드는 댐 붕괴: 질량 보존 · 음수 수심 · NaN
//   ③ Synolakis 고립파 처오름 (1:19.85, H/d 0.0185 · 0.3): R/d 를 2.831·√cot β·(H/d)^1.25 와 비교

#include "SandGridCpu.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogSandGrid, Log, All);

namespace
{
	double GBreak = 0.8;
	struct FOpt { bool bFix = true, bDesing = false; double HDry = 1e-4, Eps = 0.0, N = 0.0; bool bWetDry = true, bDisp = false; } GOpt;
	void Apply(FSandGridCpu& S) { S.bFixDry = GOpt.bFix; S.bDesingCells = GOpt.bDesing; S.HDry = GOpt.HDry; S.Eps = GOpt.Eps; S.Manning = GOpt.N; S.bWetDry = GOpt.bWetDry; S.bDispersive = GOpt.bDisp; S.BreakRatio = GBreak; }

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

	// 이산 평형 수면: 수면 L 이 칸의 두 경계 지형 사이면 젖은 삼각형 넓이 (Bollermann 3.4) — 0.55 로 일괄 채우면 평형이 아니다
	double EqHbar(double L, double BW, double BE)
	{
		const double Lo = FMath::Min(BW, BE), Hi = FMath::Max(BW, BE);
		if (L >= Hi) { return L - 0.5 * (BW + BE); }
		if (L <= Lo) { return 0.0; }
		return (L - Lo) * (L - Lo) / (2.0 * (Hi - Lo));
	}

	void LakeIsland1D()
	{
		FSandGridCpu S; Apply(S);
		S.Init(200, 1, 0.05, 0.05);
		for (int32 j = 0; j <= 1; ++j)
		for (int32 i = 0; i <= S.NX; ++i)
		{
			const double x = i * S.Dx;
			S.BCorner[j * (S.NX + 1) + i] = 0.2 + 0.2 * FMath::Sin(0.9 * x) + 0.6 * FMath::Exp(-(x - 5.0) * (x - 5.0) / 0.8);   // 물 위로 나온 언덕 둘
		}
		const double L = 0.5;
		int32 Partial = 0;
		for (int32 i = 0; i < S.NX; ++i)
		{
			const double BW = S.BCorner[i], BE = S.BCorner[i + 1];
			const double Bc[4] = { BW, BE, S.BCorner[(S.NX + 1) + i], S.BCorner[(S.NX + 1) + i + 1] };
			S.W[i] = S.BCell(i, 0) + FSandGridCpu::CellVolume(Bc, L);   // 풀이와 같은 적분 — 이산 평형
			Partial += (L > FMath::Min(BW, BE) && L < FMath::Max(BW, BE)) ? 1 : 0;
		}
		const double V0 = S.TotalVolume();
		double T = 0.0;
		for (int32 n = 0; n < 20000; ++n) { T += S.Step(); }
		double Mh = 0.0;
		for (int32 i = 0; i < S.NX; ++i) { const double h = S.H(i, 0); if (h > 1e-6) { Mh = FMath::Max(Mh, FMath::Abs(S.HU[i])); } }
		UE_LOG(LogSandGrid, Display, TEXT("①b 1D 섬 정지 수면 (일부 잠긴 칸 %d): 20000 스텝 %.1fs · 최대 |hu| %.3e · 부피 변화 %.2e (Bollermann 표 2: ~1e-16, KP07 ~9e-5)"),
			Partial, T, Mh, (S.TotalVolume() - V0) / V0);
	}

	// 2D 해변 정지 수면: 바다 쪽(+x)으로 기울고 해안을 따라 굴곡 — 물가가 굽은 선. 초기 칸 평균 = 이선형 지형 위 max(0, L − B) 적분 (칸 안 16×16 표본)
	void LakeBeach2D()
	{
		FSandGridCpu S; Apply(S);
		S.Init(80, 60, 0.05, 0.05);
		for (int32 j = 0; j <= S.NY; ++j)
		for (int32 i = 0; i <= S.NX; ++i)
		{
			const double x = i * S.Dx, y = j * S.Dy;
			S.BCorner[j * (S.NX + 1) + i] = 0.6 - 0.15 * x + 0.08 * FMath::Sin(2.1 * y) + 0.03 * FMath::Cos(3.7 * x + 1.3 * y);
		}
		const double L = 0.3;
		int32 Partial = 0;
		for (int32 j = 0; j < S.NY; ++j)
		for (int32 i = 0; i < S.NX; ++i)
		{
			const double B00 = S.BCorner[j * (S.NX + 1) + i], B10 = S.BCorner[j * (S.NX + 1) + i + 1];
			const double B01 = S.BCorner[(j + 1) * (S.NX + 1) + i], B11 = S.BCorner[(j + 1) * (S.NX + 1) + i + 1];
			double V = 0.0;
			for (int32 b = 0; b < 8; ++b) for (int32 a = 0; a < 8; ++a)   // 풀이의 CellLevel 과 같은 표본 — 이산 평형
			{
				const double u = (a + 0.5) / 8.0, v = (b + 0.5) / 8.0;
				V += FMath::Max(0.0, L - ((1 - u) * (1 - v) * B00 + u * (1 - v) * B10 + (1 - u) * v * B01 + u * v * B11));
			}
			S.W[S.Idx(i, j)] = S.BCell(i, j) + V / 64.0;
			const double Lo = FMath::Min(FMath::Min(B00, B10), FMath::Min(B01, B11)), Hi = FMath::Max(FMath::Max(B00, B10), FMath::Max(B01, B11));
			Partial += (L > Lo && L < Hi) ? 1 : 0;
		}
		const double V0 = S.TotalVolume();
		double T = 0.0;
		for (int32 n = 1; n <= 20000; ++n)
		{
			T += S.Step();
			if (n == 100 || n == 1000 || n == 20000)
			{
				double M = 0; int32 Fast = 0;
				for (int32 j = 0; j < S.NY; ++j) for (int32 i = 0; i < S.NX; ++i)
				{
					const double h = S.H(i, j); if (h <= 1e-3) { continue; }
					const double v = FMath::Sqrt(FMath::Square(S.HU[S.Idx(i, j)]) + FMath::Square(S.HV[S.Idx(i, j)])) / h;
					M = FMath::Max(M, v); Fast += v > 0.01 ? 1 : 0;
				}
				UE_LOG(LogSandGrid, Display, TEXT("①c 2D 해변 정지 수면 (물가 칸 %d) n %5d (%.1fs): 수심 1mm↑ 최대 속력 %.3e m/s · 1cm/s 넘는 칸 %d · 부피 변화 %.2e"),
					Partial, n, T, M, Fast, (S.TotalVolume() - V0) / V0);
			}
		}
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

namespace
{
	// T2 — 쇄파 H/d 0.3 실험 수면 (Synolakis 1986/87, NOAA NCTR 벤치마크) 과 시간별 대조. 자료: Docs/research/data/synolakis_h03.csv
	void SynolakisBreakingVsLab(const FString& CsvPath, double Dx)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *CsvPath)) { UE_LOG(LogSandGrid, Error, TEXT("실험 자료를 못 읽음: %s"), *CsvPath); return; }
		struct FPt { double t, x, eta; };
		TArray<FPt> Lab;
		for (const FString& L : Lines)
		{
			if (L.StartsWith(TEXT("#")) || L.IsEmpty()) { continue; }
			TArray<FString> C; L.ParseIntoArray(C, TEXT(","));
			if (C.Num() == 3) { Lab.Add({ FCString::Atod(*C[0]), FCString::Atod(*C[1]), FCString::Atod(*C[2]) }); }
		}
		const double Hd = 0.3, Cot = 19.85, X0 = Cot, XL = -12.0, XR = 90.0;
		FSandGridCpu S; Apply(S);
		S.Init(FMath::RoundToInt((XR - XL) / Dx), 1, Dx, Dx);
		auto Bat = [&](double x) { return x >= X0 ? -1.0 : -x / Cot; };
		for (int32 j = 0; j <= 1; ++j) { for (int32 i = 0; i <= S.NX; ++i) { S.BCorner[j * (S.NX + 1) + i] = Bat(XL + i * Dx); } }
		const double Gam = FMath::Sqrt(0.75 * Hd);
		const double X1 = X0 + std::acosh(FMath::Sqrt(20.0)) / Gam;   // NOAA 벤치마크와 같은 시작 (Xs = X0 + arccosh√20 / γ)
		for (int32 i = 0; i < S.NX; ++i)
		{
			const double x = XL + (i + 0.5) * Dx;
			const double Sech = 1.0 / std::cosh(Gam * (x - X1));
			const double Eta = Hd * Sech * Sech;
			const double B = S.BCell(i, 0);
			S.W[i] = FMath::Max(Eta, B);
			const double h = S.W[i] - B;
			S.HU[i] = h > 0.0 ? -h * Eta * FMath::Sqrt(S.G) : 0.0;
		}
		const double Times[4] = { 15.0, 20.0, 25.0, 30.0 };
		double T = 0.0, Run = -1e9;
		for (double Tn : Times)
		{
			const double Target = Tn / FMath::Sqrt(S.G);   // 무차원 t·√(g/d), d = 1
			while (T < Target - 1e-9)
			{
				double Dt = -1.0;
				Dt = S.Step(FMath::Min(0.002, Target - T));   // CFL 보다 작게 (Δx 0.05 · a ~4 → 0.003)
				T += Dt;
				for (int32 i = 0; i < S.NX; ++i) { if (S.H(i, 0) > 1e-4 && S.BCell(i, 0) >= 0.0) { Run = FMath::Max(Run, S.W[i]); } }
			}
			double Err = 0.0; int32 Cnt = 0; double LabShore = 1e9, SimShore = 1e9;
			for (const FPt& P : Lab)
			{
				if (FMath::Abs(P.t - Tn) > 1e-6) { continue; }
				const double f = (P.x - XL) / Dx - 0.5;
				const int32 i0 = FMath::Clamp(FMath::FloorToInt(f), 0, S.NX - 2);
				const double a = FMath::Clamp(f - i0, 0.0, 1.0);
				const double Eta = (1 - a) * S.W[i0] + a * S.W[i0 + 1];
				Err += FMath::Abs(Eta - P.eta); ++Cnt;
				if (P.x < 0.0) { LabShore = FMath::Min(LabShore, P.x); }
			}
			for (int32 i = 0; i < S.NX; ++i) { if (S.H(i, 0) > 1e-3) { SimShore = FMath::Min(SimShore, XL + (i + 0.5) * Dx); break; } }
			UE_LOG(LogSandGrid, Display, TEXT("T2 쇄파 대조 t=%.0f: 실험 %d 점 평균 |Δη| %.4f d (= H 의 %.1f%%) · 젖은 앞 끝 계산 %.2f / 실험 %.2f d"),
				Tn, Cnt, Err / FMath::Max(1, Cnt), 100.0 * Err / FMath::Max(1, Cnt) / Hd, SimShore, LabShore);
		}
		UE_LOG(LogSandGrid, Display, TEXT("T2 쇄파 t ≤ 30 최대 처오름 R/d %.4f"), Run);
	}
}

namespace
{
	// T3c 분산 관계 — 벽 상자 안 정상파 η = a·cos(kx) (상자 = 2 파장), 경계 칸 수면의 영점 교차로 주기 → ω 를 Airy √(gk·tanh kd) 와 비교
	void StandingWave(double Kd, bool bDisp)
	{
		const double d = 1.0, k = Kd / d, Lam = UE_TWO_PI / k, Dx = Lam / 40.0;
		FSandGridCpu S; Apply(S); S.bDispersive = bDisp; S.SeaLevel = 0.0; S.bWetDry = true;
		const int32 NX = 80;
		S.Init(NX, 1, Dx, Dx);
		for (int32 j = 0; j <= 1; ++j) { for (int32 i = 0; i <= NX; ++i) { S.BCorner[j * (NX + 1) + i] = -d; } }
		for (int32 i = 0; i < NX; ++i) { S.W[i] = 0.002 * FMath::Cos(k * (i + 0.5) * Dx); }
		const double Om = FMath::Sqrt(9.81 * k * FMath::Tanh(Kd)), Tth = UE_TWO_PI / Om;
		double T = 0, Prev = S.W[0] , Tz0 = -1, Tz1 = -1; int32 Nz = 0;
		while (T < 6.5 * Tth)
		{
			T += S.Step(Tth / 400.0);
			const double E = S.W[0];
			if ((Prev > 0) != (E > 0)) { const double Tc = T - S.Dx * 0 ; if (Nz == 0) { Tz0 = Tc; } Tz1 = Tc; ++Nz; }
			Prev = E;
		}
		const double Tsim = Nz > 1 ? 2.0 * (Tz1 - Tz0) / (Nz - 1) : 0.0;
		const double Tnlsw = UE_TWO_PI / (k * FMath::Sqrt(9.81 * d));
		UE_LOG(LogSandGrid, Display, TEXT("c1 정상파 kd %.1f %s: 주기 %.4f s · Airy %.4f (비 %.3f) · 얕은 물 이론 %.4f"),
			Kd, bDisp ? TEXT("Boussinesq") : TEXT("얕은 물"), Tsim, Tth, Tsim / Tth, Tnlsw);
	}
}

int32 RunSandGridGpuTest(const FString& Params);

int32 RunSandGridTest(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("gridgpu"))) { return RunSandGridGpuTest(Params); }
	if (FParse::Param(*Params, TEXT("griddisp")))
	{
		GOpt = FOpt();
		for (double Kd : { 0.5, 1.0, 2.0, 3.0 }) { StandingWave(Kd, false); StandingWave(Kd, true); }
		return 0;
	}
	if (FParse::Param(*Params, TEXT("gridlab")))
	{
		FString Csv = FPaths::Combine(FPaths::ProjectDir(), TEXT("../Docs/research/data/synolakis_h03.csv"));
		FParse::Value(*Params, TEXT("griddata="), Csv);
		GOpt = FOpt();
		FParse::Value(*Params, TEXT("gridmanning="), GOpt.N);   // 실험 유리 수조 n = 0.01 (arXiv 2210.01499 · 1902.03021)
		GOpt.bDisp = FParse::Param(*Params, TEXT("gridbous"));   // T3c — Boussinesq 분산
		FParse::Value(*Params, TEXT("gridbrk="), GBreak);
		UE_LOG(LogSandGrid, Display, TEXT("T2 Manning n %.3f"), GOpt.N);
		SynolakisBreakingVsLab(Csv, 0.05);
		SynolakisBreakingVsLab(Csv, 0.025);
		return 0;
	}
	const double T0 = FPlatformTime::Seconds();
	GOpt.bFix = !FParse::Param(*Params, TEXT("gridnofix"));
	GOpt.bDesing = FParse::Param(*Params, TEXT("griddesing"));
	double Er = 0.0; if (FParse::Value(*Params, TEXT("gridepsr="), Er)) { GOpt.Eps = Er * Er * Er * Er; }   // ε^¼ (m)
	FParse::Value(*Params, TEXT("gridmanning="), GOpt.N);
	GOpt.bWetDry = !FParse::Param(*Params, TEXT("gridkp07"));   // 대조: KP07 그대로
	double Hd = 1e-4; if (FParse::Value(*Params, TEXT("gridhdry="), Hd)) { GOpt.HDry = Hd; }
	UE_LOG(LogSandGrid, Display, TEXT("격자 시험 — 마른 칸 운동량 0 %d · 칸 평균 특이점 제거 %d · HDry %.1e · ε^¼ %.1e · Manning %.3f · 젖음/마름 재구성 %d"), GOpt.bFix ? 1 : 0, GOpt.bDesing ? 1 : 0, GOpt.HDry, FMath::Pow(GOpt.Eps, 0.25), GOpt.N, GOpt.bWetDry ? 1 : 0);
	LakeAtRest(false);
	LakeIsland1D();
	LakeBeach2D();
	if (!FParse::Param(*Params, TEXT("grid1donly"))) { LakeAtRest(true); }
	DamBreakDrySlope();
	Synolakis(0.0185, 0.1);
	Synolakis(0.0185, 0.05);
	Synolakis(0.3, 0.05);
	UE_LOG(LogSandGrid, Display, TEXT("격자 시험 끝 — %.1fs"), FPlatformTime::Seconds() - T0);
	return 0;
}

// ------------------------------------------------------------------------------------------------
// M3 T1 GPU 판 대조 — `-gridtest -gridgpu`
#include "SandGridGpu.h"

namespace
{
	// 2D 해변 (LakeBeach2D 와 같은 지형) + 바다 쪽 물 더미 → 물가로 밀려 올라갔다 내려간다
	void BeachScene(FSandGridCpu& S, int32 NX, int32 NY, double Dx, bool bMound)
	{
		S.Init(NX, NY, Dx, Dx);
		for (int32 j = 0; j <= S.NY; ++j)
		for (int32 i = 0; i <= S.NX; ++i)
		{
			const double x = i * S.Dx, y = j * S.Dy;
			S.BCorner[j * (S.NX + 1) + i] = 0.6 - 0.15 * x + 0.08 * FMath::Sin(2.1 * y) + 0.03 * FMath::Cos(3.7 * x + 1.3 * y);
		}
		const double L = 0.3, Lx = NX * Dx, Ly = NY * Dx;
		for (int32 j = 0; j < S.NY; ++j)
		for (int32 i = 0; i < S.NX; ++i)
		{
			const double Bc[4] = { S.BCorner[j * (S.NX + 1) + i], S.BCorner[j * (S.NX + 1) + i + 1], S.BCorner[(j + 1) * (S.NX + 1) + i], S.BCorner[(j + 1) * (S.NX + 1) + i + 1] };
			double Lev = L;
			if (bMound)
			{
				const double x = (i + 0.5) * Dx, y = (j + 0.5) * Dx;
				Lev += 0.08 * FMath::Exp(-((x - 0.75 * Lx) * (x - 0.75 * Lx) + (y - 0.5 * Ly) * (y - 0.5 * Ly)) / (0.04 * Lx * Lx));
			}
			S.W[S.Idx(i, j)] = S.BCell(i, j) + FSandGridCpu::CellVolume(Bc, Lev);
		}
	}

	FSandGridGpuInit ToGpu(const FSandGridCpu& S)
	{
		FSandGridGpuInit G;
		G.NX = S.NX; G.NY = S.NY; G.Dx = (float)S.Dx; G.Dy = (float)S.Dy; G.HDry = (float)S.HDry; G.bWetDry = S.bWetDry; G.Manning = (float)S.Manning;
		G.SeaLevel = (float)S.SeaLevel; G.bDispersive = S.bDispersive; G.BreakRatio = (float)S.BreakRatio;
		for (double v : S.BCorner) { G.BCorner.Add((float)v); }
		for (int32 k = 0; k < S.W.Num(); ++k) { G.W.Add((float)S.W[k]); G.HU.Add((float)S.HU[k]); G.HV.Add((float)S.HV[k]); }
		return G;
	}

	void CompareDynamic(bool bDisp = false)
	{
		FSandGridCpu C; Apply(C);
		C.bDispersive = bDisp; C.SeaLevel = 0.3;   // 정지 수면 = BeachScene 의 L
		BeachScene(C, 80, 60, 0.05, true);
		FSandGridGpu G; G.Init(ToGpu(C));
		const double V0 = C.TotalVolume();
		const double Dt = 0.002;
		for (int32 Chunk = 1; Chunk <= 4; ++Chunk)
		{
			for (int32 n = 0; n < 250; ++n) { C.Step(Dt); }
			G.StepFixed((float)Dt, 250);
			TArray<float> W, HU, HV; G.Download(W, HU, HV);
			double DW = 0, DQ = 0, QMax = 0, VG = 0;
			for (int32 j = 0; j < C.NY; ++j) for (int32 i = 0; i < C.NX; ++i)
			{
				const int32 k = C.Idx(i, j);
				DW = FMath::Max(DW, FMath::Abs(C.W[k] - W[k]));
				DQ = FMath::Max(DQ, FMath::Max(FMath::Abs(C.HU[k] - HU[k]), FMath::Abs(C.HV[k] - HV[k])));
				QMax = FMath::Max(QMax, FMath::Max(FMath::Abs(C.HU[k]), FMath::Abs(C.HV[k])));
				VG += W[k] - C.BCell(i, j);
			}
			VG *= C.Dx * C.Dy;
			UE_LOG(LogSandGrid, Display, TEXT("G①%s 동적 대조 %.1fs: 최대 |Δw| %.2e m · 최대 |Δq| %.2e (|q| 최대 %.2e) · 부피 변화 CPU %.1e / GPU %.1e"),
				bDisp ? TEXT(" (분산)") : TEXT(""), Chunk * 250 * Dt, DW, DQ, QMax, (C.TotalVolume() - V0) / V0, (VG - V0) / V0);
		}
	}

	void GpuLake()
	{
		FSandGridCpu C; Apply(C);
		BeachScene(C, 80, 60, 0.05, false);
		FSandGridGpu G; G.Init(ToGpu(C));
		for (int32 n = 0; n < 20; ++n) { G.Advance(1.f / 60.f, 64); }   // 몇 프레임 데우기
		for (int32 n = 0; n < 600; ++n) { G.Advance(1.f / 60.f, 64); }
		TArray<float> W, HU, HV; float T = 0; G.Download(W, HU, HV, &T);
		double M = 0;
		for (int32 j = 0; j < C.NY; ++j) for (int32 i = 0; i < C.NX; ++i)
		{
			const int32 k = C.Idx(i, j); const double h = W[k] - C.BCell(i, j);
			if (h > 1e-3) { M = FMath::Max(M, FMath::Sqrt((double)HU[k] * HU[k] + (double)HV[k] * HV[k]) / h); }
		}
		UE_LOG(LogSandGrid, Display, TEXT("G② GPU 2D 해변 정지 수면 (float, 적응 Δt) %.1fs: 수심 1mm↑ 최대 속력 %.3e m/s"), T, M);
	}

	void GpuSynolakis(double Hd, double Dx)
	{
		const double Cot = 19.85, X0 = Cot, XL = -6.0, XR = 90.0;
		FSandGridCpu S; Apply(S);
		S.Init(FMath::RoundToInt((XR - XL) / Dx), 1, Dx, Dx);
		auto Bat = [&](double x) { return x >= X0 ? -1.0 : -x / Cot; };
		for (int32 j = 0; j <= 1; ++j) { for (int32 i = 0; i <= S.NX; ++i) { S.BCorner[j * (S.NX + 1) + i] = Bat(XL + i * Dx); } }
		const double Gam = FMath::Sqrt(0.75 * Hd);
		const double X1 = X0 + std::acosh(FMath::Sqrt(20.0)) / Gam;
		for (int32 i = 0; i < S.NX; ++i)
		{
			const double x = XL + (i + 0.5) * Dx;
			const double Sech = 1.0 / std::cosh(Gam * (x - X1));
			const double Eta = Hd * Sech * Sech;
			const double B = S.BCell(i, 0);
			S.W[i] = FMath::Max(Eta, B);
			const double h = S.W[i] - B;
			S.HU[i] = h > 0.0 ? -h * Eta * FMath::Sqrt(S.G) : 0.0;
		}
		FSandGridGpu G; G.Init(ToGpu(S));
		double Run = -1e9; float T = 0;
		TArray<float> W, HU, HV;
		while (T < 30.f)
		{
			G.Advance(1.f / 60.f, 256);
			G.Download(W, HU, HV, &T);
			for (int32 i = 0; i < S.NX; ++i) { const double h = W[i] - S.BCell(i, 0); if (h > 1e-4 && S.BCell(i, 0) >= 0.0) { Run = FMath::Max(Run, (double)W[i]); } }
		}
		const double Law = 2.831 * FMath::Sqrt(Cot) * FMath::Pow(Hd, 1.25);
		UE_LOG(LogSandGrid, Display, TEXT("G③ GPU Synolakis H/d %.4f · Δx %.3f: R/d %.4f · 법칙 %.4f · 비 %.3f"), Hd, Dx, Run, Law, Run / Law);
	}

	// c3 — GPU 분산 관계 (CPU StandingWave 와 같은 상자, float)
	void GpuStandingWave(double Kd)
	{
		const double d = 1.0, k = Kd / d, Lam = UE_TWO_PI / k, Dx = Lam / 40.0;
		FSandGridCpu S; Apply(S); S.bDispersive = true; S.SeaLevel = 0.0; S.bWetDry = true;
		const int32 NX = 80;
		S.Init(NX, 1, Dx, Dx);
		for (int32 j = 0; j <= 1; ++j) { for (int32 i = 0; i <= NX; ++i) { S.BCorner[j * (NX + 1) + i] = -d; } }
		for (int32 i = 0; i < NX; ++i) { S.W[i] = 0.002 * FMath::Cos(k * (i + 0.5) * Dx); }
		const double Om = FMath::Sqrt(9.81 * k * FMath::Tanh(Kd)), Tth = UE_TWO_PI / Om, Dt = Tth / 400.0;
		FSandGridGpu G; G.Init(ToGpu(S));
		double T = 0, Prev = S.W[0], Tz0 = -1, Tz1 = -1; int32 Nz = 0;
		TArray<float> W, HU, HV;
		while (T < 6.5 * Tth)
		{
			G.StepFixed((float)Dt, 4); T += 4 * Dt;   // 4 스텝마다 읽어 영점 교차 (주기 400 스텝 — 1% 분해)
			G.Download(W, HU, HV);
			const double E = W[0];
			if ((Prev > 0) != (E > 0)) { if (Nz == 0) { Tz0 = T; } Tz1 = T; ++Nz; }
			Prev = E;
		}
		const double Tsim = Nz > 1 ? 2.0 * (Tz1 - Tz0) / (Nz - 1) : 0.0;
		UE_LOG(LogSandGrid, Display, TEXT("c3 GPU 정상파 kd %.1f Boussinesq: 주기 %.4f s · Airy %.4f (비 %.3f)"), Kd, Tsim, Tth, Tsim / Tth);
	}

	// c4 주기 y 순환 삼중대각 (Sherman–Morrison): y 로 진행하는 정상파, NY = 2 파장 (80 칸, 2 의 거듭제곱 아님)
	void GpuPeriodicY(double Kd)
	{
		const double d = 1.0, k = Kd / d, Lam = UE_TWO_PI / k, Dy = Lam / 40.0;
		FSandGridCpu S; Apply(S); S.bDispersive = true; S.SeaLevel = 0.0; S.bWetDry = true;
		const int32 NX = 4, NY = 80;
		S.Init(NX, NY, Dy, Dy);
		for (int32 j = 0; j <= NY; ++j) { for (int32 i = 0; i <= NX; ++i) { S.BCorner[j * (NX + 1) + i] = -d; } }
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { S.W[S.Idx(i, j)] = 0.002 * FMath::Cos(k * (j + 0.5) * Dy + 0.3); }   // 위상을 틀어 감싸는 칸이 마디가 아니게
		const double Om = FMath::Sqrt(9.81 * k * FMath::Tanh(Kd)), Tth = UE_TWO_PI / Om, Dt = Tth / 400.0;
		FSandGridGpuInit In = ToGpu(S); In.bPeriodicY = true;
		FSandGridGpu G; G.Init(In);
		double T = 0, Prev = S.W[0], Tz0 = -1, Tz1 = -1, Asym = 0; int32 Nz = 0;
		TArray<float> W, HU, HV;
		while (T < 6.5 * Tth)
		{
			G.StepFixed((float)Dt, 4); T += 4 * Dt;
			G.Download(W, HU, HV);
			const double E = W[0];
			if ((Prev > 0) != (E > 0)) { if (Nz == 0) { Tz0 = T; } Tz1 = T; ++Nz; }
			Prev = E;
			for (int32 j = 0; j < NY / 2; ++j) { Asym = FMath::Max(Asym, (double)FMath::Abs(W[j * NX] - W[(j + NY / 2) * NX])); }   // 두 파장이 같아야 (주기 이음매 오차)
		}
		const double Tsim = Nz > 1 ? 2.0 * (Tz1 - Tz0) / (Nz - 1) : 0.0;
		UE_LOG(LogSandGrid, Display, TEXT("c4 GPU 주기 y 정상파 kd %.1f: 주기 %.4f s · Airy %.4f (비 %.3f) · 두 파장 차 최대 %.2e m (진폭 2e-3)"), Kd, Tsim, Tth, Tsim / Tth, Asym);
	}

	// c6 지하수 (m3-plan c6) — 평평 · 경사 바닥 격자 (닫힌 상자, 벽)
	FSandGridGpuInit GroundGrid(int32 NX, int32 NY, double Dx, TFunctionRef<double(double)> Bof)
	{
		FSandGridGpuInit In;
		In.NX = NX; In.NY = NY; In.Dx = In.Dy = (float)Dx; In.bGround = true;
		In.BCorner.SetNum((NX + 1) * (NY + 1));
		for (int32 j = 0; j <= NY; ++j) for (int32 i = 0; i <= NX; ++i) { In.BCorner[j * (NX + 1) + i] = (float)Bof(i * Dx); }
		In.W.SetNum(NX * NY); In.HU.Init(0.f, NX * NY); In.HV.Init(0.f, NX * NY);
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { In.W[j * NX + i] = (float)(0.5 * (Bof(i * Dx) + Bof((i + 1) * Dx))); }   // 마름 (수심 0)
		return In;
	}

	double GroundVolume(const FSandGridGpuInit& In, const FSandGridGpu& G, double Ne)
	{
		TArray<float> W, HU, HV; TArray<FVector2f> Gw; float Zaq = 0.f;
		G.Download(W, HU, HV); G.DownloadGround(Gw, &Zaq);
		double V = 0.0;
		for (int32 j = 0; j < In.NY; ++j)
		for (int32 i = 0; i < In.NX; ++i)
		{
			const int32 k = j * In.NX + i, c = j * (In.NX + 1) + i;
			const double Bm = 0.25 * (In.BCorner[c] + In.BCorner[c + 1] + In.BCorner[c + In.NX + 1] + In.BCorner[c + In.NX + 2]);
			V += (W[k] - Bm) + Ne * (Gw[k].X - Zaq);
		}
		return V * In.Dx * In.Dy;
	}

	void GpuGround()
	{
		// (1) 질량 보존 — 1:5 경사 (바다 쪽 물), 물가 위 마른 모래에 물 덩이 → 처오르고 스미고 지하수가 흐른다
		{
			const int32 NX = 120, NY = 8; const double Dx = 0.05;
			FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double x) { return 0.6 - 0.2 * x; });   // x 0 → +0.6, 6 m → −0.6
			In.Manning = 0.02f;
			for (int32 j = 0; j < NY; ++j)
			for (int32 i = 0; i < NX; ++i)
			{
				const double x = (i + 0.5) * Dx, B = 0.6 - 0.2 * x;
				float& W = In.W[j * NX + i];
				W = (float)FMath::Max(B, 0.0);
				if (x > 1.0 && x < 1.6) { W = (float)(B + 0.08); }
			}
			FSandGridGpu G; G.Init(In);
			const double V0 = GroundVolume(In, G, In.GwNe);
			double Vmax = 0.0;
			for (int32 n = 0; n < 40; ++n) { G.StepFixed(2e-3f, 250); Vmax = FMath::Max(Vmax, FMath::Abs(GroundVolume(In, G, In.GwNe) - V0)); }
			TArray<FVector2f> Gw; G.DownloadGround(Gw);
			UE_LOG(LogSandGrid, Display, TEXT("c6 ① 지하수 질량 보존 (닫힌 상자 20 s): 최대 |ΔV| %.3e m³ / 전체 %.4f m³ (상대 %.2e, 기준 < 1e-4) · 물가 위 x 1.3 m 지하수면 %.3f m (처음 0)"),
				Vmax, V0, Vmax / V0, Gw[4 * NX + 26].X);
		}
		// (2) 지하수면 계단 (지표수 없음) → 선형 확산 D = K·h/n_e, erf 해
		{
			const int32 NX = 200, NY = 4; const double Dx = 0.05, L = NX * Dx;
			FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 1.0; });   // 대수층 바닥 0
			In.GwInit.SetNum(NX * NY);
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { In.GwInit[j * NX + i] = (i + 0.5) * Dx < 0.5 * L ? 0.41f : 0.39f; }
			FSandGridGpu G; G.Init(In);
			const double Tend = 600.0;
			for (int32 n = 0; n < 60; ++n) { G.StepFixed(0.05f, 200); }
			TArray<FVector2f> Gw; G.DownloadGround(Gw);
			const double D = In.GwK * 0.4 / In.GwNe;
			double Err = 0.0;
			for (int32 i = 0; i < NX; ++i)
			{
				const double x = (i + 0.5) * Dx - 0.5 * L, An = 0.4 - 0.01 * std::erf(x / FMath::Sqrt(4.0 * D * Tend));
				Err = FMath::Max(Err, FMath::Abs(Gw[NX + i].X - An));
			}
			UE_LOG(LogSandGrid, Display, TEXT("c6 ② 지하수면 계단 확산 (D %.2e m²/s, 600 s, 퍼짐 %.2f m): 최대 오차 %.2f%% (계단 2 cm 대비, 기준 ±5%%)"), D, FMath::Sqrt(4.0 * D * Tend), 100.0 * Err / 0.02);
		}
		// (3) 마른 모래 위 웅덩이 10 cm → Green–Ampt 누적 침투 F(t) (같은 식 CPU 상미분)
		{
			const int32 NX = 8, NY = 8; const double Dx = 0.05;
			FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });   // 대수층 바닥 −1
			for (float& W : In.W) { W = 0.1f; }
			In.GwInit.Init(-0.9f, NX * NY);
			FSandGridGpu G; G.Init(In);
			const double K = In.GwK, Psi = In.GwPsi, Ne = In.GwNe;
			double F = 0.0, Tc = 0.0, ErrMax = 0.0;
			FString Line;
			TArray<float> W, HU, HV;
			for (int32 n = 1; n <= 6; ++n)
			{
				for (int32 m = 0; m < 40; ++m) { G.StepFixed(1e-3f, 500); }
				for (int32 s = 0; s < 200000; ++s) { const double h = 0.1 - F; if (h <= 0.0) { break; } F += FMath::Min(h, K * (1.0 + (h + Psi) / FMath::Max(F / Ne, 1e-3)) * 1e-4); }
				Tc += 20.0;
				G.Download(W, HU, HV);
				const double Fg = 0.1 - W[3 * NX + 3];
				ErrMax = FMath::Max(ErrMax, FMath::Abs(Fg - F) / 0.1);
				Line += FString::Printf(TEXT(" %.0fs %.2f/%.2f cm"), Tc, 100 * Fg, 100 * F);
			}
			UE_LOG(LogSandGrid, Display, TEXT("c6 ③ Green–Ampt 웅덩이 (GPU/CPU):%s · 최대 오차 %.1f%% (웅덩이 대비, 기준 ±5%%)"), *Line, 100 * ErrMax);
		}
	}

	void GpuMorph()
	{
		// T4a ③ 사태 (m3-plan T4): 기울기 2 의 삼각 둔덕 → 마름 1.0 · 젖음 0.3 까지 무너지고 Σb 보존
		for (const bool bWet : { false, true })
		{
			const int32 NX = 80, NY = 4; const double Dx = 0.05, Xc = 2.0;
			auto Bof = [&](double x) { return FMath::Max(0.0, 0.4 - 2.0 * FMath::Abs(x - Xc)); };
			FSandGridGpuInit In = GroundGrid(NX, NY, Dx, Bof);
			In.bGround = false; In.bMorph = true; In.bPeriodicY = true;
			if (bWet) { for (int32 k = 0; k < NX * NY; ++k) { In.W[k] = 0.6f; } }   // 고인 물 (흐름 거의 0 → 소류사 0, 사태만)
			FSandGridGpu G; G.Init(In);
			TArray<float> B0; G.DownloadBed(B0);
			G.StepFixed(2e-3f, 2000);
			TArray<float> B1; G.DownloadBed(B1);
			double S0 = 0, S1 = 0, Smax = 0;
			for (int32 j = 0; j < NY; ++j)
			for (int32 i = 0; i <= NX; ++i)
			{
				const int32 c = j * (NX + 1) + i; S0 += B0[c]; S1 += B1[c];
				if (i < NX) { Smax = FMath::Max(Smax, FMath::Abs((double)B1[c + 1] - B1[c]) / Dx); }
			}
			UE_LOG(LogSandGrid, Display, TEXT("T4 ③ 사태 (%s, 처음 기울기 2): 최대 기울기 %.3f (기준 ≤ %.2f + 2%%) · 꼭대기 %.3f → %.3f m · Σb 상대 변화 %.2e (기준 < 1e-3)"),
				bWet ? TEXT("젖음") : TEXT("마름"), Smax, bWet ? In.AvalWet : In.AvalDry, B0[NX / 2 + 0], B1[NX / 2], (S1 - S0) / FMath::Max(S0, 1e-9));
		}
	}

	void GpuSus()
	{
		// T4b ① 부유사 보존 (m3-plan T4b): 닫힌 영역 물 둑 붕괴 — (1−p)Σb + ΣhC 가 그대로
		const int32 NX = 160, NY = 4; const double Dx = 0.05;
		FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });
		In.bGround = false; In.bMorph = true; In.bSus = true; In.bPeriodicY = true;
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { In.W[j * NX + i] = i < NX / 2 ? 0.6f : 0.2f; }
		FSandGridGpu G; G.Init(In);
		TArray<float> B0, B1, Su; G.DownloadBed(B0);
		G.StepFixed(1e-3f, 3000);
		G.DownloadBed(B1); G.DownloadSus(Su);
		double Db = 0, Ab = 0, Sm = 0;
		for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i <= NX; ++i) { const int32 c = j * (NX + 1) + i; Db += (double)B1[c] - B0[c]; Ab += FMath::Abs((double)B1[c] - B0[c]); }
		for (float V : Su) { Sm += V; }
		const double Tot = (1.0 - In.SedPor) * Db + Sm;
		UE_LOG(LogSandGrid, Display, TEXT("T4b ① 부유사 보존: ΣhC %.3e · (1−p)ΣΔb %.3e · 합 %.3e · 비 (합 / (1−p)Σ|Δb|) %.2e (기준 < 1e-3)"), Sm, (1.0 - In.SedPor) * Db, Tot, Tot / FMath::Max((1.0 - In.SedPor) * Ab, 1e-12));
	}

	void GpuTool()
	{
		// T6 (m3-plan): 스탬프 부피가 요청과 같은가 · 파내고 되쌓으면 Σb 제자리 · 물 깊이 유지 · 물붓기 = h 증가. 물 0.2 m, 사태만 동작
		const int32 NX = 160, NY = 64; const double Dx = 0.05;
		FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });
		In.bGround = false; In.bMorph = true; In.bPeriodicY = true;
		for (float& V : In.W) { V = 0.2f; }
		FSandGridGpu G; G.Init(In);
		auto Measure = [&](double& Sb, double& Sh, double& Bmin)
		{
			TArray<float> Bc, W, Hu, Hv; G.DownloadBed(Bc); G.Download(W, Hu, Hv);
			Sb = 0; Sh = 0; Bmin = 1e9;
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i <= NX; ++i) { const float B = Bc[j * (NX + 1) + i]; Sb += B; Bmin = FMath::Min<double>(Bmin, B); }
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i)
			{
				const int32 c = j * (NX + 1) + i;
				Sh += W[j * NX + i] - 0.25 * ((double)Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2]);
			}
			Sb *= Dx * Dx; Sh *= Dx * Dx;
		};
		double B0, H0, M, B1, H1, M1, B2, H2, M2, B3, H3, M3;
		Measure(B0, H0, M);
		const float Vd = 2e-3f;   // 2 L
		G.QueueBedStamp(4.f, 1.6f, 0.2f, -Vd); G.StepFixed(1e-3f, 1); Measure(B1, H1, M1);
		G.QueueBedStamp(4.f, 1.6f, 0.2f, Vd); G.StepFixed(1e-3f, 1); Measure(B2, H2, M2);
		G.QueueWaterStamp(4.f, 1.6f, 0.2f, Vd); G.StepFixed(1e-3f, 1); Measure(B3, H3, M3);
		UE_LOG(LogSandGrid, Display, TEXT("T6 파기: ΔΣb %.6f L (요청 -2) · 최저 b %.4f m · Σh 변화 %.3e L"), (B1 - B0) * 1e3, M1, (H1 - H0) * 1e3);
		UE_LOG(LogSandGrid, Display, TEXT("T6 되쌓기: Σb 원래와 차 %.3e L (기준 < 2e-3 = 0.1%%) · Σh 변화 %.3e L"), (B2 - B0) * 1e3, (H2 - H0) * 1e3);
		UE_LOG(LogSandGrid, Display, TEXT("T6 물붓기: ΔΣh %.6f L (요청 2) · Σb 변화 %.3e L"), (H3 - H2) * 1e3, (B3 - B2) * 1e3);
		G.StepFixed(1e-3f, 3000); double B4, H4, M4; Measure(B4, H4, M4);
		UE_LOG(LogSandGrid, Display, TEXT("T6 3 s 뒤: Σb 차 %.3e L · Σh 차 %.3e L · 유한 %d"), (B4 - B3) * 1e3, (H4 - H3) * 1e3, FMath::IsFinite(B4 + H4) ? 1 : 0);
	}

	void GpuLoad()
	{
		// M4 T4 ① (m4-plan): 벽 하중 F = ρ g h ∇b_struct ΔA. ① 한쪽 물 0.2 m → ΣFx = ½ρgh²·폭 ② 양쪽 같은 물 → ΣFx ≈ 0
		// ③ Lobovský 외 (2014) 댐 붕괴 (H 0.3 · L 0.6 · 벽까지 1.0 m) — 벽 앞 수심 · 바닥 정수압 · 단위 폭 힘의 시간 이력
		const double Dx = 0.05; const int32 NY = 16; const float Wd = NY * Dx;
		FSandGridGpu::FStructMap Map;
		auto Wall = [&](TArray<FVector4f>& P, float X0, float X1, float Hgt)
		{
			for (float x = X0 * 100.f + 2.5f; x < X1 * 100.f; x += 5.f) for (int32 c = 0; c < NY; ++c) for (float z = 2.5f; z < Hgt * 100.f; z += 5.f) { P.Add(FVector4f(x, 2.5f + 5.f * c, z, 1.f)); }
		};
		auto SumF = [&](FSandGridGpu& G, int32 NX, float XA, float XB)
		{
			TArray<FVector2f> L; G.DownloadLoad(L); FVector2d S(0, 0);
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const float X = (i + 0.5f) * Dx; if (X >= XA && X <= XB) { S += FVector2d(L[j * NX + i]); } }
			return S;
		};
		for (int32 Case = 0; Case < 2; ++Case)
		{
			const int32 NX = 120;
			FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });
			In.bGround = false; In.bMorph = false; In.bPeriodicY = true;
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const float X = (i + 0.5f) * Dx; In.W[j * NX + i] = (X < 3.f || Case == 1) ? 0.2f : 0.f; }
			FSandGridGpu G; G.Init(In);
			TArray<FVector4f> P; Wall(P, 3.0f, 3.3f, 0.4f); G.SetStructPositions(P, Map);
			for (int32 n = 0; n < 10; ++n) { G.StepFixed(1e-3f, 100); }
			const FVector2d F = SumF(G, NX, 2.5f, 3.8f), FL = SumF(G, NX, 2.5f, 3.15f), FR = SumF(G, NX, 3.15f, 3.8f);
			const double Ex = 0.5 * 1000.0 * 9.81 * 0.2 * 0.2 * Wd;
			UE_LOG(LogSandGrid, Display, TEXT("T4 %s 1 s: ΣFx %.2f N (앞면 %.2f · 뒷면 %.2f) · ΣFy %.2e N · 기대 앞면 ½ρgh²·폭 = %.2f N → 비 %.3f"),
				Case == 0 ? TEXT("① 한쪽 물") : TEXT("② 양쪽 물"), F.X, FL.X, FR.X, F.Y, Ex, FL.X / Ex);
		}
		{
			const int32 NX = 64; const float H = 0.3f, XG = 0.5f, XW = 1.5f;   // 저수 0.5–1.1 (가운데 벽 x < 0.5) · 수문 x 1.1 · 하류 벽 x 2.1 (= 수문에서 1.0 m)
			FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });
			In.bGround = false; In.bMorph = false; In.bPeriodicY = true;
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const float X = (i + 0.5f) * Dx; In.W[j * NX + i] = (X > XG && X < XG + 0.6f) ? H : 0.f; }
			FSandGridGpu G; G.Init(In);
			// 하류 벽 앞면 = XG + 0.6 + 1.0 = 2.1 m
			TArray<FVector4f> P; Wall(P, 0.2f, XG, 0.65f); Wall(P, XG + 0.6f + XW - 0.5f, XG + 0.6f + XW - 0.2f, 0.65f);
			G.SetStructPositions(P, Map);
			G.StepFixed(1e-3f, 1);
			float HMax = 0.f, FMax = 0.f, THit = -1.f, TH = 0.f, TF = 0.f;
			const float Ts = FMath::Sqrt(9.81f / H);
			FString Hist;
			for (int32 n = 1; n <= 300; ++n)
			{
				G.StepFixed(1e-3f, 10);
				const float T = n * 0.01f;
				TArray<float> Bc, W, Hu, Hv; G.DownloadBed(Bc); G.Download(W, Hu, Hv);
				const int32 i = (int32)(2.025f / Dx), c = i;   // 벽 앞 칸 (j = 0) — 벽 걸친 칸 (2.05–2.10) 바로 앞
				const float h = W[i] - 0.25f * (Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2]);
				const float Fw = (float)SumF(G, NX, 1.9f, 2.3f).X / Wd;   // 단위 폭 (N/m)
				if (THit < 0.f && h > 0.003f) { THit = T; }
				if (h > HMax) { HMax = h; TH = T; }
				if (Fw > FMax) { FMax = Fw; TF = T; }
				if (n % 25 == 0) { Hist += FString::Printf(TEXT(" t*%.1f:h%.3f/F%.0f(heq %.3f)"), T * Ts, h, Fw, FMath::Sqrt(FMath::Max(0.f, 2.f * Fw / 9810.f))); }
			}
			UE_LOG(LogSandGrid, Display, TEXT("T4 ③ Lobovský H 0.3: 도착 t* %.2f · 벽 앞 최대 h %.3f m (t* %.2f) → 바닥 정수압 %.1f mb · z 15/30/80 mm %.1f/%.1f/%.1f mb · 최대 단위 폭 힘 %.0f N/m (t* %.2f, ½ρg h²max = %.0f · 같은 힘의 정수압 깊이 %.3f m → 바닥 %.1f mb)"),
				THit * Ts, HMax, TH * Ts, 1000.f * 9.81f * HMax / 100.f, 1000.f * 9.81f * (HMax - 0.015f) / 100.f, 1000.f * 9.81f * (HMax - 0.03f) / 100.f, 1000.f * 9.81f * (HMax - 0.08f) / 100.f, FMax, TF * Ts, 0.5f * 1000.f * 9.81f * HMax * HMax, FMath::Sqrt(2.f * FMax / 9810.f), 9810.f * FMath::Sqrt(2.f * FMax / 9810.f) / 100.f);
			UE_LOG(LogSandGrid, Display, TEXT("T4 ③ 이력:%s"), *Hist);
		}
	}

	void GpuStruct()
	{
		// M4 T2 (m4-plan): 고정 입자 → b_struct. 물 0.2 m 고인 평평한 바닥에 벽 (30×30×40 cm) 과 터널 블록 (40×40×40 cm, 가운데 20 cm 폭 · 밑 5–20 cm 비움).
		// ① b_struct 값 (벽 0.40 · 터널 0.05) ② 물 부피 보존 (세울 때 · 3 s · 치울 때) ③ 모래 b 는 그대로 (사태가 성을 깎지 않는다) ④ 벽 위 칸 마름 · 터널 칸 물 있음
		const int32 NX = 160, NY = 64; const double Dx = 0.05;
		FSandGridGpuInit In = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });
		In.bGround = false; In.bMorph = true; In.bPeriodicY = true;
		for (float& V : In.W) { V = 0.2f; }
		FSandGridGpu G; G.Init(In);
		TArray<FVector4f> Pos;
		for (int32 a = 0; a < 6; ++a) for (int32 c = 0; c < 6; ++c) for (int32 k = 0; k < 8; ++k) { Pos.Add(FVector4f(300.f + 2.5f + 5.f * a, 140.f + 2.5f + 5.f * c, 2.5f + 5.f * k, 1.f)); }
		for (int32 a = 0; a < 8; ++a) for (int32 c = 0; c < 8; ++c) for (int32 k = 0; k < 8; ++k)
		{
			if (c >= 2 && c <= 5 && k >= 1 && k <= 3) { continue; }   // 터널 (x 방향으로 뚫림)
			Pos.Add(FVector4f(500.f + 2.5f + 5.f * a, 140.f + 2.5f + 5.f * c, 2.5f + 5.f * k, 1.f));
		}
		FSandGridGpu::FStructMap Map;
		auto Measure = [&](double& Sb, double& Sh, double& Ss, float& Smax, float& DbMax)
		{
			TArray<float> Bc, Sc, W, Hu, Hv; G.DownloadBed(Bc); G.DownloadBed(Sc, true); G.Download(W, Hu, Hv);
			Sb = 0; Sh = 0; Ss = 0; Smax = 0; DbMax = 0;
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i <= NX; ++i) { const int32 c = j * (NX + 1) + i; Sb += Bc[c] - Sc[c]; Ss += Sc[c]; Smax = FMath::Max(Smax, Sc[c]); DbMax = FMath::Max(DbMax, FMath::Abs(Bc[c] - Sc[c])); }
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i)
			{
				const int32 c = j * (NX + 1) + i;
				Sh += W[j * NX + i] - 0.25 * ((double)Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2]);
			}
			Sb *= Dx * Dx; Sh *= Dx * Dx; Ss *= Dx * Dx;
		};
		auto CellH = [&](float X, float Y)
		{
			TArray<float> Bc, W, Hu, Hv; G.DownloadBed(Bc); G.Download(W, Hu, Hv);
			const int32 i = (int32)(X / Dx), j = (int32)(Y / Dx), c = j * (NX + 1) + i;
			return W[j * NX + i] - 0.25f * (Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2]);
		};
		double B0, H0, S0, B1, H1, S1, B2, H2, S2, B3, H3, S3; float Mx, Db;
		Measure(B0, H0, S0, Mx, Db);
		G.SetStructPositions(Pos, Map); G.StepFixed(1e-3f, 1); Measure(B1, H1, S1, Mx, Db);
		TArray<float> Sc; G.DownloadBed(Sc, true);
		auto Sat = [&](float X, float Y) { return Sc[FMath::RoundToInt(Y / Dx) * (NX + 1) + FMath::RoundToInt(X / Dx)]; };
		UE_LOG(LogSandGrid, Display, TEXT("T2 ① b_struct: 최대 %.3f m · 벽 가운데 %.3f (기대 0.40) · 터널 가운데 %.3f (기대 0.05) · 터널 벽 %.3f (기대 0.40) · 블록 밖 %.3f"),
			Mx, Sat(3.15f, 1.55f), Sat(5.20f, 1.60f), Sat(5.20f, 1.45f), Sat(4.0f, 1.55f));
		UE_LOG(LogSandGrid, Display, TEXT("T2 ② 세움: Σh 변화 %.3e L · Σb 변화 %.3e L · Σb_struct %.3f L"), (H1 - H0) * 1e3, (B1 - B0) * 1e3, S1 * 1e3);
		for (int32 n = 0; n < 30; ++n) { G.StepFixed(1e-3f, 100); }   // 매 Run 마다 마스크 패스 (30 번)
		Measure(B2, H2, S2, Mx, Db);
		UE_LOG(LogSandGrid, Display, TEXT("T2 ③ 3 s 뒤: Σh 차 %.3e L · Σb 차 %.3e L · 최대 |b| %.2e m (사태가 성을 깎으면 커진다) · Σb_struct 차 %.3e L · 벽 위 칸 h %.4f m · 터널 칸 h %.4f m · 밖 h %.4f m"),
			(H2 - H1) * 1e3, (B2 - B1) * 1e3, Db, (S2 - S1) * 1e3, CellH(3.12f, 1.52f), CellH(5.22f, 1.62f), CellH(4.0f, 1.0f));
		G.SetStructPositions(TArray<FVector4f>(), Map); G.StepFixed(1e-3f, 1); Measure(B3, H3, S3, Mx, Db);
		UE_LOG(LogSandGrid, Display, TEXT("T2 ④ 치움: Σb_struct %.3e L · Σh 변화 %.3e L · Σb 변화 %.3e L · 유한 %d"), S3 * 1e3, (H3 - H2) * 1e3, (B3 - B2) * 1e3, FMath::IsFinite(H3 + B3) ? 1 : 0);

		// ⑤ 흐름: 댐 붕괴 (x < 2 m 에 물 0.3 m, 나머지 마름) 가 x = 3–3.4 m 에 선 블록 (40×40×40 cm) 을 만난다. 블록 없음 · 벽 · 터널 (가운데 20 cm 폭, 밑 5–20 cm 빈) 셋을 비교.
		// 1.5 s 뒤 블록 위 칸 · 터널 칸 · 블록 바로 뒤 칸 · 옆 칸의 물 깊이, 전체 물 부피 (닫힌 상자)
		for (int32 Case = 0; Case < 3; ++Case)
		{
			FSandGridGpuInit D = GroundGrid(NX, NY, Dx, [](double) { return 0.0; });
			D.bGround = false; D.bMorph = false; D.bPeriodicY = true;
			for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { D.W[j * NX + i] = (i + 0.5) * Dx < 2.0 ? 0.3f : 0.f; }
			FSandGridGpu Gd; Gd.Init(D);
			TArray<FVector4f> Pb;
			if (Case > 0)
			{
				for (int32 a = 0; a < 8; ++a) for (int32 c = 0; c < 8; ++c) for (int32 k = 0; k < 8; ++k)
				{
					if (Case == 2 && c >= 2 && c <= 5 && k >= 1 && k <= 3) { continue; }
					Pb.Add(FVector4f(300.f + 2.5f + 5.f * a, 140.f + 2.5f + 5.f * c, 2.5f + 5.f * k, 1.f));
				}
				Gd.SetStructPositions(Pb, Map);
			}
			auto Vol = [&]()
			{
				TArray<float> Bc, W, Hu, Hv; Gd.DownloadBed(Bc); Gd.Download(W, Hu, Hv);
				double V = 0; for (int32 j = 0; j < NY; ++j) for (int32 i = 0; i < NX; ++i) { const int32 c = j * (NX + 1) + i; V += FMath::Max(0.0, W[j * NX + i] - 0.25 * ((double)Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2])); }
				return V * Dx * Dx;
			};
			Gd.StepFixed(1e-3f, 1);
			const double V0 = Vol();
			for (int32 n = 0; n < 15; ++n) { Gd.StepFixed(1e-3f, 100); }
			const double V1 = Vol();
			TArray<float> Bc, W, Hu, Hv; Gd.DownloadBed(Bc); Gd.Download(W, Hu, Hv);
			auto H = [&](float X, float Y) { const int32 i = (int32)(X / Dx), j = (int32)(Y / Dx), c = j * (NX + 1) + i; return W[j * NX + i] - 0.25f * (Bc[c] + Bc[c + 1] + Bc[c + NX + 1] + Bc[c + NX + 2]); };
			auto Qx = [&](float X, float Y) { const int32 i = (int32)(X / Dx), j = (int32)(Y / Dx); return Hu[j * NX + i]; };
			UE_LOG(LogSandGrid, Display, TEXT("T2 ⑤ %s 1.5 s: 블록 위 h %.3f m · 터널 h %.3f m (qx %.4f) · 바로 뒤 (x 3.5) h %.3f · 옆 (y 1.2) h %.3f · 앞 (x 2.9) h %.3f · 물 부피 차 %.3e L / %.1f L"),
				Case == 0 ? TEXT("없음") : Case == 1 ? TEXT("벽  ") : TEXT("터널"), H(3.22f, 1.42f), H(3.22f, 1.62f), Qx(3.22f, 1.62f), H(3.52f, 1.62f), H(3.22f, 1.22f), H(2.92f, 1.62f), (V1 - V0) * 1e3, V0 * 1e3);
		}
	}

	void GpuCost(int32 Size, bool bDisp = false)
	{
		FSandGridCpu C; Apply(C);   // 지형 · 초기값만 (CPU 로 풀지 않는다)
		C.bDispersive = bDisp; C.SeaLevel = 0.0;
		C.Init(Size, Size, 0.05, 0.05);
		const double Lx = Size * 0.05;
		for (int32 j = 0; j <= Size; ++j) for (int32 i = 0; i <= Size; ++i) { C.BCorner[j * (Size + 1) + i] = 1.0 - 2.0 * (i * 0.05) / Lx + 0.05 * FMath::Sin(0.7 * j * 0.05); }   // 2m 내려가는 해변
		for (int32 j = 0; j < Size; ++j) for (int32 i = 0; i < Size; ++i) { C.W[C.Idx(i, j)] = FMath::Max(0.0, C.BCell(i, j)); }   // 수면 0 (물가 칸은 대충 — 비용만)
		FSandGridGpu G; G.Init(ToGpu(C));
		G.StepFixed(0.002f, 4);   // 셰이더 데우기
		G.StepFixed(0.002f, 20);
		UE_LOG(LogSandGrid, Display, TEXT("G④%s GPU 비용 %d² (칸 5cm, %.0fm): 서브스텝 20 회 %.2f ms → 1 회 %.3f ms"), bDisp ? TEXT(" (분산)") : TEXT(""), Size, Lx, G.LastGpuMs(), G.LastGpuMs() / 20.f);
	}
}

int32 RunSandGridGpuTest(const FString& Params)
{
	GOpt = FOpt();
	FParse::Value(*Params, TEXT("gridmanning="), GOpt.N);
	if (FParse::Param(*Params, TEXT("gridground"))) { GpuGround(); return 0; }
	if (FParse::Param(*Params, TEXT("gridtool"))) { GpuTool(); return 0; }
	if (FParse::Param(*Params, TEXT("gridstruct"))) { GpuStruct(); return 0; }
	if (FParse::Param(*Params, TEXT("gridload"))) { GpuLoad(); return 0; }
	if (FParse::Param(*Params, TEXT("gridmorph"))) { GpuMorph(); GpuSus(); return 0; }
	if (FParse::Param(*Params, TEXT("gridbous")))
	{
		CompareDynamic(true);
		for (double Kd : { 0.5, 1.0, 2.0, 3.0 }) { GpuStandingWave(Kd); }
		for (double Kd : { 0.5, 2.0 }) { GpuPeriodicY(Kd); }
		GpuCost(512, false); GpuCost(512, true); GpuCost(1024, false); GpuCost(1024, true);
		return 0;
	}
	CompareDynamic();
	GpuLake();
	GpuSynolakis(0.0185, 0.05);
	GpuSynolakis(0.3, 0.05);
	GpuCost(512);
	GpuCost(1024);
	return 0;
}
