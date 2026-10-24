#include "SandSpike.h"

#include "Math/RandomStream.h"
#include "SandGpuSolver.h"

namespace
{
	// 공간 해시 키 — Teschner 2003
	FORCEINLINE int32 HashCell(int32 X, int32 Y, int32 Z, int32 TableSize)
	{
		const uint32 H = (uint32)(X * 73856093) ^ (uint32)(Y * 19349663) ^ (uint32)(Z * 83492791);
		return (int32)(H & (uint32)(TableSize - 1));
	}

	// 브로드페이즈 여유. 논문의 k·Δt·v_body (k=2)를 입자에 맞춘 것.
	// 서브스텝 동안 새로 닿을 수 있는 쌍까지 미리 잡아둔다
	constexpr float SafetyMultiplier = 2.f;
}

void FSandSpike::Init(const FSandParams& InParams, const FColumnSetup& InColumn)
{
	Params = InParams;
	Column = InColumn;

	bBoxScene = false;
	SpawnColumn();
	ResizeBuffers();
	ResetRunState();
}

void FSandSpike::InitTiltBox(const FSandParams& InParams, const FTiltSetup& InTilt)
{
	Params = InParams;
	Tilt   = InTilt;

	bBoxScene = true;
	SpawnTiltBox();
	ResizeBuffers();
	ResetRunState();
}

void FSandSpike::SaveState(FSandState& Out) const
{
	SyncFromGpu();
	Out.Pos     = Pos;
	Out.Vel     = Vel;
	Out.TiltDeg = TiltDeg;
}

void FSandSpike::RestoreState(const FSandState& In)
{
	bGpuDirty = true;
	bGpuStale = false;   // 복원이 GPU 쪽 결과를 덮는다
	bGpuPairsValid = false;
	Pos     = In.Pos;
	Vel     = In.Vel;
	PrevPos = In.Pos;
	SetTiltDegrees(In.TiltDeg);
	SettledFrames = 0;
	LastMaxSpeed  = 0.f;
}

void FSandSpike::SetTiltDegrees(float Deg)
{
	TiltDeg = Deg;
	const float R = FMath::DegreesToRadians(Deg);
	// 상자를 돌리는 대신 중력을 돌린다. 사면 아래 방향은 +X
	GravityDir = FVector3f(FMath::Sin(R), 0.f, -FMath::Cos(R));
}

void FSandSpike::ResizeBuffers()
{
	bGpuDirty = true;         // 입자 수가 바뀌는 길은 전부 여기를 지난다
	bGpuPairsValid = false;   // AddParticles 만 덧붙임이라 되살린다
	// 입자 수의 2배 이상인 2의 거듭제곱. 충돌은 거리 검사가 걸러내므로 정확도가 아니라 비용 문제다
	TableSize = 1;
	while (TableSize < FMath::Max(1, Pos.Num()) * 2)
	{
		TableSize <<= 1;
	}
	CellStart.SetNumZeroed(TableSize + 1);
	SortedIdx.SetNumZeroed(Pos.Num());
	Delta.SetNumZeroed(Pos.Num());
	DeltaCount.SetNumZeroed(Pos.Num());
	DeltaScale.SetNumZeroed(Pos.Num());
	GroundLambdaN.SetNumZeroed(Pos.Num());
	GroundLambdaT.SetNumZeroed(Pos.Num());
	WallLambda.SetNumZeroed(Pos.Num());
}

void FSandSpike::ResetRunState()
{
	bWallsActive  = true;
	Frame         = 0;
	Time          = 0.f;
	SettledFrames = 0;
	LastMaxSpeed  = 0.f;
	TiltDeg       = 0.f;
	GravityDir    = FVector3f(0.f, 0.f, -1.f);
}

void FSandSpike::InitPour(const FSandParams& InParams, const FPourSetup& InPour)
{
	Params = InParams;
	Pour   = InPour;

	bBoxScene    = false;
	bWallsActive = false;   // 부어 쌓기에는 형틀이 없다
	WallRadius   = 0.f;

	SpawnPourBase();
	ResizeBuffers();
	ResetRunState();
	if (Pour.BoxHalf > 0.f)
	{
		// 상자 안에 붓기 — 벽은 SP 규약(SolveWallsSP 의 상자 분기). 절토는 나중에 OpenFaceXPlus 로 한 면만 연다
		bBoxScene = true;
		BoxHalf = FVector3f(Pour.BoxHalf, Pour.BoxHalfY > 0.f ? Pour.BoxHalfY : Pour.BoxHalf, 0.f);
		Tilt.bOpenDownhill = false;
		Tilt.bOpenXPlus = false;
		bWallsActive = true;
	}
	else
	{
		bWallsActive = false;   // ResetRunState 가 켜므로 다시 끈다
	}
}

void FSandSpike::SpawnPourBase()
{
	const float R2 = Params.Radius;
	const float S  = 2.f * R2;

	// 예상 퇴적 반경에서 역산 — 거친 바닥은 더미보다 넓어야 한다 (#19)
	float BaseR = Pour.BaseRadius;
	if (BaseR <= 0.f)
	{
		// 부피 = N·(4/3πr³)/충전율,  원뿔 r³ = 3V/(π·tan34°)
		const float V = Pour.TargetCount * (4.f / 3.f) * UE_PI * R2 * R2 * R2 / 0.64f;
		const float ConeR = FMath::Pow(3.f * V / (UE_PI * 0.6745f), 1.f / 3.f);
		BaseR = ConeR * 1.2f;
	}

	const bool bBox = Pour.BoxHalf > 0.f;
	if (bBox) { BaseR = FMath::Max(Pour.BoxHalf, Pour.BoxHalfY) + S; }   // 상자를 벽 너머까지 덮는다 (정사각 바닥으로 넉넉히)
	const int32 Lat = FMath::CeilToInt(BaseR / S);
	FRandomStream Rng(Params.Seed ^ 0x9E3779B9u);
	const float J = Pour.BaseJitter * R2;
	Pos.Reset();
	InvMass.Reset();
	for (int32 iz = 0; iz < FMath::Max(1, Pour.BaseLayers); ++iz)
	{
		for (int32 iy = -Lat; iy <= Lat; ++iy)
		for (int32 ix = -Lat; ix <= Lat; ++ix)
		{
			const float X = ix * S;
			const float Y = iy * S;
			if (bBox ? (FMath::Abs(X) > BaseR || FMath::Abs(Y) > BaseR) : (X * X + Y * Y > BaseR * BaseR))
			{
				continue;
			}
			Pos.Add(FVector3f(X + Rng.FRandRange(-J, J), Y + Rng.FRandRange(-J, J), R2 + iz * S + Rng.FRandRange(-J, J)));
			InvMass.Add(0.f);   // 전부 고정
		}
	}

	Vel.SetNumZeroed(Pos.Num());
	PrevPos = Pos;
}

void FSandSpike::InitCustom(const FSandParams& InParams, const TArray<FVector3f>& InPos, const TArray<float>& InInvMass)
{
	Params = InParams;
	bBoxScene = false;
	WallRadius = 0.f;

	Pos = InPos;
	InvMass = InInvMass;
	Vel.SetNumZeroed(Pos.Num());
	PrevPos = Pos;
	Bonds.Reset();
	BondsBrokenTotal = 0;
	BondSlotOverflow = 0;

	ResizeBuffers();
	ResetRunState();
	bWallsActive = false;
}

float FSandSpike::CohesionOfW(float W) const
{
	if (W <= Params.WDry || W >= Params.WSat) { return 0.f; }
	if (W < Params.WOptStart) { return (W - Params.WDry) / FMath::Max(Params.WOptStart - Params.WDry, UE_KINDA_SMALL_NUMBER); }
	if (W <= Params.WOptEnd)  { return 1.f; }
	return (Params.WSat - W) / FMath::Max(Params.WSat - Params.WOptEnd, UE_KINDA_SMALL_NUMBER);
}

float FSandSpike::BondCompliance() const
{
	const float Coh = CohesionOfW(Params.WGlobal);
	if (Coh <= 0.f) { return 0.f; }
	// 변형률 BondStiffStrain 에서 힘이 BondStrength·m·g 에 닿는 강성. 그 위는 SolveBondsSP 의 λ 클램프가 고원을 만든다. m = 1
	const float D = 2.f * Params.Radius;
	const float K = Params.BondStrength * FMath::Abs(Params.Gravity) / FMath::Max(Params.BondStiffStrain * D, UE_KINDA_SMALL_NUMBER);
	return 1.f / (K * Coh);
}

int32 FSandSpike::FormBonds()
{
	if (CohesionOfW(Params.WGlobal) <= 0.f) { return 0; }
	SyncFromGpu();
	bGpuDirty = true;

	const float D = 2.f * Params.Radius;
	const float MaxDist = D * Params.BondFormStretch;

	// 이미 있는 결합은 다시 만들지 않는다
	TSet<uint64> Existing;
	for (const FSandBond& B : Bonds) { Existing.Add(((uint64)FMath::Min(B.I, B.J) << 32) | (uint64)FMath::Max(B.I, B.J)); }

	TArray<int32> SlotUsed;
	SlotUsed.SetNumZeroed(Pos.Num());
	for (const FSandBond& B : Bonds) { ++SlotUsed[B.I]; ++SlotUsed[B.J]; }

	// 후보를 전부 모아 가까운 순으로 — 슬롯이 모자랄 때 어느 쌍이 남는지가 결정적이어야 한다.
	// O(N²) 는 도구 조작 시점 한 번이라 감당한다 (5000개 = 1.2×10⁷ 거리 검사)
	struct FCand { int32 I, J; float Dist; };
	TArray<FCand> Cands;
	for (int32 i = 0; i < Pos.Num(); ++i)
	for (int32 j = i + 1; j < Pos.Num(); ++j)
	{
		if (InvMass[i] <= 0.f && InvMass[j] <= 0.f) { continue; }
		const float Dist = FVector3f::Dist(Pos[i], Pos[j]);
		if (Dist < MaxDist && !Existing.Contains(((uint64)i << 32) | (uint64)j)) { Cands.Add({ i, j, Dist }); }
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.Dist < B.Dist || (A.Dist == B.Dist && (A.I < B.I || (A.I == B.I && A.J < B.J))); });

	int32 Made = 0;
	for (const FCand& C : Cands)
	{
		if (SlotUsed[C.I] >= Params.BondSlots || SlotUsed[C.J] >= Params.BondSlots) { ++BondSlotOverflow; continue; }
		// 휴지 길이 하한 = D. 살짝 침투한 쌍에 현재 거리를 주면 결합은 D 아래로 당기고 접촉은 D 로 밀어
		// 매 반복 싸운다 — 실측: 절토 12층에서 벽을 열자 높이 46→81cm 로 솟음. 인장만 받는 결합은 D 에서 시작해야 한다
		Bonds.Add({ C.I, C.J, FMath::Max(C.Dist, D), 0.f });
		++SlotUsed[C.I]; ++SlotUsed[C.J]; ++Made;
	}
	return Made;
}

void FSandSpike::SolveBondsSP(float StepDt)
{
	// XPBD (Macklin 2016) 컴플라이언스 제약을 SP 규약(임펄스 하나로 v·x 함께)으로.
	//   C = |x_i − x_j| − d_rest,   Δλ = −(C + α̃λ) / (w_i + w_j + α̃),   α̃ = α/h²
	//   Δx_i = +Δλ·w_i·N,  Δx_j = −Δλ·w_j·N,  Δv = Δx / h
	// 인장만 — λ ≤ 0. 압축은 접촉 제약의 일이고, 모세관 다리는 당기기만 한다 (Lian 1993).
	if (Bonds.Num() == 0)
	{
		return;
	}
	const float Alpha = BondCompliance();
	if (Alpha <= 0.f)
	{
		return;   // cohesion(W) = 0 — BreakBonds 가 목록을 비운다
	}
	const float AlphaTilde = Alpha / (StepDt * StepDt);
	// 고원 — 힘 ≤ BondStrength·m·g·cohesion(W). λ 는 위치형 승수라 힘 = λ/h²
	const float LambdaMax = Params.BondStrength * FMath::Abs(Params.Gravity) * CohesionOfW(Params.WGlobal) * StepDt * StepDt;

	const bool bGatherHere = Params.bGatherApply && Params.bGatherBonds;   // 결합만 순차로 두는 진단 (bGatherBonds=false)
	if (bGatherHere)
	{
		FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(FVector3f));
		if (Params.bMassSplit) { CountConstraintsPerParticle(); }
	}
	const bool bSplit = bGatherHere && Params.bMassSplit && Params.bMassSplitBonds;
	const float OmegaB = bGatherHere ? Params.BondOmega : 1.f;
	const bool bApgdHere = bGatherHere && Params.bApgd && Params.bApgdBonds && Params.BlockSize <= 0.f;

	for (FSandBond& B : Bonds)
	{
		const float Wi = InvMass[B.I];
		const float Wj = InvMass[B.J];
		const float WSum = bSplit
			? Wi * FMath::Max(1, DeltaCount[B.I]) + Wj * FMath::Max(1, DeltaCount[B.J])
			: Wi + Wj;
		if (WSum <= 0.f)
		{
			continue;
		}
		const FVector3f Diff = Pos[B.I] - Pos[B.J];
		const float Dist = Diff.Size();
		if (Dist < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector3f N = Diff / Dist;
		const float C = Dist - B.RestLen;

		float DLambda = -OmegaB * (C + AlphaTilde * B.Lambda) / (WSum + AlphaTilde);
		const float NewLambda = FMath::Clamp(B.Lambda + DLambda, -LambdaMax, 0.f);   // 인장만 · 고원
		if (bApgdHere)
		{
			// γ = Π(y − g/L), y' = γ + β(γ − γ_prev). 상태엔 y' − y 를 적용한다
			const float DGamma = NewLambda - B.Gamma;
			ApgdRestartAcc += (double)((C + AlphaTilde * B.Lambda) / StepDt) * DGamma;
			const float YNew = NewLambda + ApgdBeta * DGamma;
			B.Gamma = NewLambda;
			DLambda = YNew - B.Lambda;
			B.Lambda = YNew;
		}
		else
		{
			DLambda = NewLambda - B.Lambda;
			B.Lambda = NewLambda;
			B.Gamma = NewLambda;   // 순차 결합(진단)과 섞여도 정리가 낡은 γ 로 되돌리지 않게
		}
		if (DLambda == 0.f)
		{
			continue;
		}

		const FVector3f Corr = N * DLambda;
		if (bGatherHere)
		{
			Delta[B.I] += Corr * (Wi / StepDt);   // Δv 단위로 모아 패스 2 에서 함께
			Delta[B.J] -= Corr * (Wj / StepDt);
			continue;
		}
		Pos[B.I] += Corr * Wi;
		Pos[B.J] -= Corr * Wj;
		Vel[B.I] += Corr * (Wi / StepDt);
		Vel[B.J] -= Corr * (Wj / StepDt);
	}

	if (bGatherHere)
	{
		ApplyGatheredVelocityDeltas(StepDt);
	}
}

void FSandSpike::BreakBonds()
{
	if (Bonds.Num() == 0)
	{
		return;
	}
	// 거리 > d_rest·breakStretch (Lian 1993 파단 거리) 또는 말라서 cohesion(W) = 0.
	// 지운 결합은 돌아오지 않는다 — 되붙이는 것은 FormBonds(도구)만 한다.
	const bool bDry = CohesionOfW(Params.WGlobal) <= 0.f;
	for (int32 b = Bonds.Num() - 1; b >= 0; --b)
	{
		const FSandBond& B = Bonds[b];
		const float Dist = FVector3f::Dist(Pos[B.I], Pos[B.J]);
		if (bDry || Dist > B.RestLen * Params.BreakStretch)
		{
			Bonds.RemoveAtSwap(b);
			++BondsBrokenTotal;
		}
	}
}

int32 FSandSpike::RemoveParticles(TFunctionRef<bool(int32)> ShouldRemove)
{
	SyncFromGpu();
	TArray<int32> NewIndex;
	NewIndex.SetNum(Pos.Num());
	int32 Kept = 0;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		NewIndex[i] = (InvMass[i] > 0.f && ShouldRemove(i)) ? -1 : Kept++;
	}
	const int32 Removed = Pos.Num() - Kept;
	if (Removed == 0)
	{
		return 0;
	}

	auto Compact = [&](auto& Arr)
	{
		int32 w = 0;
		for (int32 i = 0; i < Arr.Num(); ++i) { if (NewIndex[i] >= 0) { Arr[w++] = Arr[i]; } }
		Arr.SetNum(w);
	};
	Compact(Pos); Compact(Vel); Compact(PrevPos); Compact(InvMass);
	Compact(GroundLambdaN); Compact(GroundLambdaT); Compact(WallLambda);

	for (int32 b = Bonds.Num() - 1; b >= 0; --b)
	{
		const int32 NI = NewIndex[Bonds[b].I], NJ = NewIndex[Bonds[b].J];
		if (NI < 0 || NJ < 0) { Bonds.RemoveAtSwap(b); continue; }
		Bonds[b].I = NI; Bonds[b].J = NJ;
	}

	Contacts.Reset();
	WarmLambda.Reset();
	ResizeBuffers();
	SettledFrames = 0;
	return Removed;
}

void FSandSpike::AddParticles(const TArray<FVector3f>& NewPos)
{
	SyncFromGpu();
	const bool bPairsWereValid = bGpuPairsValid;
	for (const FVector3f& P : NewPos)
	{
		Pos.Add(P);
		Vel.Add(FVector3f::ZeroVector);
		PrevPos.Add(P);
		InvMass.Add(1.f);
	}
	ResizeBuffers();
	bGpuPairsValid = bPairsWereValid;   // 덧붙이기만 했다 — GPU 쌍 목록(워밍)은 그대로 쓴다
}

void FSandSpike::SpawnTiltBox()
{
	const float R2 = Params.Radius;
	const float S  = 2.f * R2;

	// 층수를 고정하고 한 변을 역산한다
	const int32 PerLayer = FMath::Max(1, Tilt.TargetCount / FMath::Max(1, Tilt.Layers));
	const int32 Side     = FMath::Max(2, FMath::RoundToInt(FMath::Sqrt((float)PerLayer)));

	FRandomStream Rng(Params.Seed);
	const float Jitter = Tilt.FreeJitter * R2;
	const float BaseJ  = Tilt.BaseJitter * R2;

	// 자유 격자 간격은 D + 2J — 흔들어도 초기 겹침이 없어야 한다.
	// 간격 D 에 0.3R 을 흔들면 최대 1.5cm 침투로 시작하고, 솔버가 그걸 밀어내며 블록이 부푼다
	// (실측: 5층이 25cm 가 아니라 37cm, 충전율 0.35). 부어 쌓기가 무사했던 것은 바닥만 흔들어서다.
	const float SF = S + 2.f * Jitter;
	BoxHalf = FVector3f(Side * SF * 0.5f, Side * SF * 0.5f, 0.f);

	Pos.Reset();
	InvMass.Reset();

	// 고정층(거친 바닥) — 간격 D 로 상자를 덮는다. 겹침은 고정이라 무해. 인덱스 앞쪽에 모은다
	const int32 BaseN = FMath::CeilToInt(Side * SF / S);
	for (int32 iz = 0; iz < Tilt.FixedLayers; ++iz)
	{
		for (int32 iy = 0; iy < BaseN; ++iy)
		for (int32 ix = 0; ix < BaseN; ++ix)
		{
			const float X = (ix - (BaseN - 1) * 0.5f) * S;
			const float Y = (iy - (BaseN - 1) * 0.5f) * S;
			Pos.Add(FVector3f(
				X + Rng.FRandRange(-BaseJ, BaseJ),
				Y + Rng.FRandRange(-BaseJ, BaseJ),
				R2 + iz * S + Rng.FRandRange(-BaseJ, BaseJ)));   // 격자 바닥은 거칠기가 아니라 템플릿이다 (#25)
			InvMass.Add(0.f);
		}
	}

	// 자유층 — 간격 SF. 첫 층은 바닥 윗면 + 흔들림 여유만큼 띄운다
	const float FreeZ0 = R2 + Tilt.FixedLayers * S + Jitter + BaseJ;
	for (int32 iz = Tilt.FixedLayers; iz < Tilt.Layers; ++iz)
	{
		for (int32 iy = 0; iy < Side; ++iy)
		for (int32 ix = 0; ix < Side; ++ix)
		{
			const float X = (ix - (Side - 1) * 0.5f) * SF;
			const float Y = (iy - (Side - 1) * 0.5f) * SF;
			Pos.Add(FVector3f(
				X + Rng.FRandRange(-Jitter, Jitter),
				Y + Rng.FRandRange(-Jitter, Jitter),
				FreeZ0 + (iz - Tilt.FixedLayers) * SF + Rng.FRandRange(-Jitter, Jitter)));
			InvMass.Add(1.f);
		}
	}

	Vel.SetNumZeroed(Pos.Num());
	PrevPos = Pos;
}

void FSandSpike::SpawnColumn()
{
	const float R2 = Params.Radius;
	const float S  = 2.f * R2;   // 격자 간격 = 접촉 거리. 처음부터 서로 닿아 있다

	float R = Column.Radius;
	float H = Column.Height;
	if (R <= 0.f)
	{
		// 목표 개수와 종횡비에서 역산. 단순 입방 충전 기준 (N = πR²H / S³, H = a·R)
		const float A = FMath::Max(0.1f, Column.AspectRatio);
		R = FMath::Pow((float)Column.TargetCount * S * S * S / (UE_PI * A), 1.f / 3.f);
		H = A * R;
	}
	WallRadius     = R;
	Column.Radius  = R;   // 역산한 값을 되돌려 놓는다. 로그·측정이 이걸 본다
	Column.Height  = H;

	const float FillR = R - R2;
	const int32 Lat   = FMath::CeilToInt(FillR / S);

	FRandomStream Rng(Params.Seed);
	const float Jitter = 0.05f * R2;   // 격자 충전의 인공적인 대칭만 깬다

	// 거친 바닥은 퇴적물보다 넓게 깐다 — 좁으면 바깥쪽만 매끄러운 지면 위에 놓인다
	const float BaseR  = FillR * FMath::Max(1.f, Column.BaseRadiusScale);
	const int32 BaseLat = FMath::CeilToInt(BaseR / S);

	Pos.Reset();
	InvMass.Reset();
	for (int32 iz = 0; iz < FMath::CeilToInt((H - R2) / S); ++iz)
	{
		// 바닥 몇 층은 고정해 거칠게 만든다. 매끄러운 지면 위에서는 밑동이 미끄러져 각이 낮아진다
		const bool bFixed = iz < Column.FixedLayers;
		const float J = bFixed ? 0.f : Jitter;
		const float LimitR = bFixed ? BaseR : FillR;
		const int32 L      = bFixed ? BaseLat : Lat;
		for (int32 iy = -L; iy <= L; ++iy)
		{
			for (int32 ix = -L; ix <= L; ++ix)
			{
				const float X = ix * S;
				const float Y = iy * S;
				if (X * X + Y * Y > LimitR * LimitR)
				{
					continue;
				}
				Pos.Add(FVector3f(
					X + Rng.FRandRange(-J, J),
					Y + Rng.FRandRange(-J, J),
					R2 + iz * S + Rng.FRandRange(-J, J)));
				InvMass.Add(bFixed ? 0.f : 1.f);
			}
		}
	}

	WallLiftZ = 0.f;
	Vel.SetNumZeroed(Pos.Num());
	PrevPos = Pos;
}

int32 FSandSpike::CellKey(const FVector3f& P) const
{
	return HashCell(
		FMath::FloorToInt(P.X / CellSize),
		FMath::FloorToInt(P.Y / CellSize),
		FMath::FloorToInt(P.Z / CellSize),
		TableSize);
}

void FSandSpike::BuildHash(float CellSizeIn)
{
	CellSize = CellSizeIn;
	const int32 N = Pos.Num();

	// 계수 정렬 — 셀당 상한이 없다. 최밀 충전에서도 잘리지 않는다
	FMemory::Memzero(CellStart.GetData(), CellStart.Num() * sizeof(int32));
	for (int32 i = 0; i < N; ++i)
	{
		++CellStart[CellKey(Pos[i])];
	}
	int32 Sum = 0;
	for (int32 c = 0; c <= TableSize; ++c)
	{
		const int32 Count = (c < TableSize) ? CellStart[c] : 0;
		CellStart[c] = Sum;
		Sum += Count;
	}
	TArray<int32> Cursor = CellStart;
	for (int32 i = 0; i < N; ++i)
	{
		SortedIdx[Cursor[CellKey(Pos[i])]++] = i;
	}
}

void FSandSpike::BuildContacts(float Dt)
{
	const float D = 2.f * Params.Radius;

	// 여유 = 2 × (한 프레임에 움직일 수 있는 거리).
	//
	// 상한을 2r 로 묶어두면 빠른 입자가 여유를 넘어 이동해 **접촉 탐색을 통째로 통과한다** —
	// 2000 cm/s 면 한 프레임에 33cm 를 가는데 여유는 5cm 였다. 아무것도 안 닿은 채 날아간다.
	// 서브스텝을 늘려도 접촉 목록은 프레임당 한 번만 만들므로 소용이 없다.
	// 상한은 비용을 위한 것이므로 넉넉히 두되(8r), 하한만 유지한다.
	const float Margin = FMath::Clamp(SafetyMultiplier * LastMaxSpeed * Dt,
		0.1f * Params.Radius, 8.f * Params.Radius);
	const float Range  = D + Margin;
	const float Range2 = Range * Range;

	BuildHash(Range);

	// 워밍 스타트 — 목록을 다시 만들기 전에 살아 있는 λ 를 입자쌍 키로 보관한다
	WarmLambda.Reset();
	if (Params.WarmStart > 0.f && Params.bSequentialPositions)
	{
		for (const FSandContact& C : Contacts)
		{
			if (C.LambdaN > 0.f)
			{
				const uint64 Key = ((uint64)FMath::Min(C.I, C.J) << 32) | (uint64)FMath::Max(C.I, C.J);
				WarmLambda.Add(Key, TPair<float, FVector3f>(C.LambdaN, C.LambdaTan));
			}
		}
	}

	Contacts.Reset();

	int32 Keys[27];
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		const int32 CX = FMath::FloorToInt(Pos[i].X / CellSize);
		const int32 CY = FMath::FloorToInt(Pos[i].Y / CellSize);
		const int32 CZ = FMath::FloorToInt(Pos[i].Z / CellSize);

		// 27개 이웃 셀. 해시 충돌로 같은 버킷을 두 번 도는 것만 막는다
		int32 NumKeys = 0;
		for (int32 dz = -1; dz <= 1; ++dz)
		for (int32 dy = -1; dy <= 1; ++dy)
		for (int32 dx = -1; dx <= 1; ++dx)
		{
			const int32 K = HashCell(CX + dx, CY + dy, CZ + dz, TableSize);
			bool bSeen = false;
			for (int32 k = 0; k < NumKeys; ++k)
			{
				if (Keys[k] == K) { bSeen = true; break; }
			}
			if (!bSeen) { Keys[NumKeys++] = K; }
		}

		for (int32 k = 0; k < NumKeys; ++k)
		{
			for (int32 s = CellStart[Keys[k]]; s < CellStart[Keys[k] + 1]; ++s)
			{
				const int32 j = SortedIdx[s];
				if (j <= i)
				{
					continue;   // 쌍마다 한 번만
				}
				if (FVector3f::DistSquared(Pos[i], Pos[j]) < Range2)
				{
					FSandContact NewC{ i, j, 0.f, 0.f };
					if (WarmLambda.Num() > 0)
					{
						const uint64 Key = ((uint64)FMath::Min(i, j) << 32) | (uint64)FMath::Max(i, j);
						if (const TPair<float, FVector3f>* W = WarmLambda.Find(Key))
						{
							NewC.LambdaN   = W->Key;
							NewC.LambdaTan = W->Value;
						}
					}
					Contacts.Add(NewC);
				}
			}
		}
	}
}

void FSandSpike::PredictPositions(float SubDt)
{
	const FVector3f G = GravityDir * (FMath::Abs(Params.Gravity) * GravityScale);
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		PrevPos[i] = Pos[i];
		if (InvMass[i] <= 0.f)
		{
			continue;   // 고정 입자
		}
		Vel[i] += G * SubDt;
		Pos[i] += Vel[i] * SubDt;
	}
}

void FSandSpike::SolveContacts()
{
	const float D = 2.f * Params.Radius;

	FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(FVector3f));
	FMemory::Memzero(DeltaCount.GetData(), DeltaCount.Num() * sizeof(int32));

	for (FSandContact& C : Contacts)
	{
		const int32 i = C.I;
		const int32 j = C.J;

		const FVector3f Diff = Pos[i] - Pos[j];
		const float Dist2 = Diff.SizeSquared();
		if (Dist2 >= D * D || Dist2 < UE_KINDA_SMALL_NUMBER)
		{
			continue;   // 이번 서브스텝에는 안 닿는다
		}

		const float Dist  = FMath::Sqrt(Dist2);
		const float Depth = D - Dist;          // 논문의 d
		const FVector3f N = Diff / Dist;       // 항상 최신 방향을 쓴다 (논문 §3.4)

		// 법선 — α = 0 이므로 Δλ = d / (w_i + w_j). 보정은 각자의 w 비율로
		const float WSum = InvMass[i] + InvMass[j];
		if (WSum <= 0.f)
		{
			continue;   // 둘 다 고정
		}
		const float DLambdaN = Depth / WSum;
		C.PendingN = DLambdaN;
		C.PendingT = 0.f;

		FVector3f Corr = N * DLambdaN;

		// 정지마찰 — 접선 상대변위를 통째로 지운다. 단 λt < μs·λn 일 때만 (논문 §3.5)
		// 게이트는 이번 서브스텝의 법선 기여까지 포함해서 본다.
		// 빼면 프레임 첫 서브스텝에서 λn = 0 이라 마찰이 아예 안 걸린다
		if (Params.bPositionFriction && C.LambdaT < Params.Friction * (C.LambdaN + DLambdaN))
		{
			const FVector3f Dp = (Pos[i] - PrevPos[i]) - (Pos[j] - PrevPos[j]);
			const FVector3f Dpt = Dp - N * FVector3f::DotProduct(Dp, N);
			const float TLen = Dpt.Size();
			if (TLen > UE_KINDA_SMALL_NUMBER)
			{
				C.PendingT = TLen / WSum;
				Corr -= Dpt / WSum;
			}
		}

		if (Params.bGaussSeidel)
		{
			// 즉시 적용 — 나누지 않으므로 장부가 그대로 맞는다
			Pos[i] += Corr * InvMass[i];
			Pos[j] -= Corr * InvMass[j];
			C.LambdaN += C.PendingN;
			C.LambdaT += C.PendingT;
			C.PendingN = 0.f;
			C.PendingT = 0.f;
		}
		else
		{
			Delta[i] += Corr * InvMass[i];
			Delta[j] -= Corr * InvMass[j];
			++DeltaCount[i];
			++DeltaCount[j];
		}
	}
}

void FSandSpike::SolveContactsSP(float StepDt, int32 Iter, int32 MaxIter)
{
	// Frâncu 2014 Algorithm 1.
	//   r  = kv·nu + (kc/h)·delta       속도 오차와 위치 오차를 섞은 잔차
	//   dLambda = -(omega/d)·r - beta·dLambda^{l-1}      (38) 뒤 항이 heavy-ball 모멘텀
	//   v += W J^T dLambda,  x += h·W J^T dLambda        ★ 같은 임펄스로 둘을 함께
	const float D    = 2.f * Params.Radius;
	const float InvH = 1.f / StepDt;

	const float Beta = (Params.MomentumA > 0.f && MaxIter > 1 && !Params.bApgd)
		? FMath::Min(Params.MomentumA * FMath::Pow((float)Iter / (float)MaxIter, Params.MomentumB), 1.f)
		: 0.f;

	// gather: 이 패스 동안 v·x 를 건드리지 않고 Δv 를 모았다가 끝에 한 번 적용한다 (m0b-spike §3-1 갈래 A)
	// 블록 모드(①-b): 블록 안 접촉은 즉시(순차), 경계 접촉만 gather
	const bool bBlock     = Params.bGatherApply && Params.bGatherContacts && Params.BlockSize > 0.f;
	const bool bGatherAll = Params.bGatherApply && Params.bGatherContacts && !bBlock;   // 거울 진단: bGatherContacts=false 면 접촉은 순차
	const bool bAnyGather = bGatherAll || bBlock;
	if (bAnyGather)
	{
		FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(FVector3f));
		if (bBlock) { AssignBlocks((Params.bBlockShift && (Iter & 1)) ? 0.5f * Params.BlockSize : 0.f); }
		if (Params.bMassSplit) { CountConstraintsPerParticle(bBlock); }
	}
	const bool bSplit = bAnyGather && Params.bMassSplit;
	const bool bApgdHere = bGatherAll && Params.bApgd;
	int32 BoundaryN = 0, ActiveN = 0;

	for (FSandContact& C : Contacts)
	{
		const int32 i = C.I;
		const int32 j = C.J;
		const float Wi = InvMass[i];
		const float Wj = InvMass[j];
		// 이 접촉을 모아서 적용하나(gather) — 전부 gather 이거나, 블록 모드에서 경계를 넘는 쌍
		const bool bGatherThis = bGatherAll || (bBlock && BlockId[i] != BlockId[j]);
		// 질량 분할: 유효 질량은 n 배 가벼운 부분체로(역질량 n·w), 적용은 전체 w 로 (Tonge 2012). gather 되는 쌍에만
		const float WSum = (bSplit && bGatherThis)
			? Wi * FMath::Max(1, DeltaCount[i]) + Wj * FMath::Max(1, DeltaCount[j])
			: Wi + Wj;
		if (WSum <= 0.f)
		{
			continue;
		}

		const FVector3f Diff = Pos[i] - Pos[j];
		const float Dist = Diff.Size();
		if (Dist < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector3f N = Diff / Dist;
		const float Gap = Dist - D;            // 음수면 침투

		// 단방향 제약 — 떨어져 있고 임펄스도 없으면 놓아준다
		if (Gap > 0.f && C.LambdaN <= 0.f)
		{
			C.LambdaN = 0.f;
			C.LambdaTan = FVector3f::ZeroVector;
			C.GammaN = 0.f;
			C.GammaTan = FVector3f::ZeroVector;
			continue;
		}
		++ActiveN; if (bBlock && BlockId[i] != BlockId[j]) { ++BoundaryN; }

		const FVector3f RelV = Vel[i] - Vel[j];

		// --- 법선 ---
		const float NuN  = FVector3f::DotProduct(RelV, N);
		const float ResN = Params.Kv * NuN + (Params.Kc * InvH) * Gap;
		// (38) 의 −β·Δλ 에서 Δλ 는 직전 반복의 "실제 변화량"이고, 논문 표기의 부호가
		// 우리 것과 반대다. 그대로 빼면 직전 진행을 거슬러 수렴이 나빠진다(실측 a=2에서 13°→2.9°).
		// heavy-ball 이 되려면 같은 방향으로 더해야 한다
		float DLambdaN = -(Params.SorOmega / WSum) * ResN + Beta * C.PrevDLambdaN;

		const float OldN = C.LambdaN;
		C.LambdaN = FMath::Max(0.f, C.LambdaN + DLambdaN);   // 누적 후 클램프 (Catto 2005)
		const float GammaNNew = C.LambdaN;                    // APGD: 투영된 해. 원뿔 반경도 이것으로
		if (bApgdHere)
		{
			const float DGammaN = GammaNNew - C.GammaN;
			ApgdRestartAcc += (double)ResN * DGammaN;
			C.LambdaN = GammaNNew + ApgdBeta * DGammaN;       // y — 음수일 수 있다(외삽)
			C.GammaN = GammaNNew;
		}
		else
		{
			C.GammaN = GammaNNew;
		}
		DLambdaN = C.LambdaN - OldN;
		C.PrevDLambdaN = DLambdaN;

		// --- 접선 ---
		// 위치 오차는 스텝 시작 이후의 접선 상대변위. 그것이 정지마찰의 실체다
		const FVector3f Drift  = (Pos[i] - PrevPos[i]) - (Pos[j] - PrevPos[j]);
		const FVector3f DriftT = Drift - N * FVector3f::DotProduct(Drift, N);
		const FVector3f NuT    = RelV - N * FVector3f::DotProduct(RelV, N);

		const FVector3f ResT = NuT * Params.Kv + DriftT * (Params.Kc * InvH);
		FVector3f DLambdaTan = ResT * (-(Params.SorOmega / WSum)) + C.PrevDLambdaTan * Beta;

		const FVector3f OldTan = C.LambdaTan;
		FVector3f NewTan = C.LambdaTan + DLambdaTan;

		// 원뿔 투영 — |lambda_t| <= mu·lambda_n
		const float MaxTan = Params.Friction * GammaNNew;
		const float TanLen = NewTan.Size();
		if (TanLen > MaxTan)
		{
			NewTan = (TanLen > UE_KINDA_SMALL_NUMBER) ? NewTan * (MaxTan / TanLen) : FVector3f::ZeroVector;
		}
		if (bApgdHere)
		{
			const FVector3f DGammaT = NewTan - C.GammaTan;
			ApgdRestartAcc += (double)FVector3f::DotProduct(ResT, DGammaT);
			C.GammaTan = NewTan;
			NewTan += DGammaT * ApgdBeta;
		}
		else
		{
			C.GammaTan = NewTan;
		}
		C.LambdaTan = NewTan;
		DLambdaTan  = NewTan - OldTan;
		C.PrevDLambdaTan = DLambdaTan;

		// --- 적용: v 와 x 를 같은 임펄스로 ---
		const FVector3f Impulse = N * DLambdaN + DLambdaTan;
		if (bGatherThis)
		{
			Delta[i] += Impulse * Wi;      // Δv 단위. x 는 h 배 — 패스 2 에서
			Delta[j] -= Impulse * Wj;
			continue;
		}
		Vel[i] += Impulse * Wi;
		Vel[j] -= Impulse * Wj;
		Pos[i] += Impulse * (Wi * StepDt);
		Pos[j] -= Impulse * (Wj * StepDt);
	}

	if (bAnyGather)
	{
		ApplyGatheredVelocityDeltas(StepDt);
	}
	if (bBlock && Iter == 0) { LastBoundaryFrac = ActiveN > 0 ? (float)BoundaryN / ActiveN : 0.f; }
}

void FSandSpike::CountConstraintsPerParticle(bool bBoundaryOnly)
{
	// 활성 접촉(닿았거나 λ 를 들고 있는 쌍) + 결합. 지면·벽은 입자 하나짜리라 과보정이 없어 세지 않는다.
	// 블록 모드(bBoundaryOnly)면 gather 되는 것은 경계 접촉뿐이므로 그것만 센다 — 안 그러면 경계 접촉이 과소완화된다
	FMemory::Memzero(DeltaCount.GetData(), DeltaCount.Num() * sizeof(int32));
	const float D = 2.f * Params.Radius;
	for (const FSandContact& C : Contacts)
	{
		const float Gap = FVector3f::Dist(Pos[C.I], Pos[C.J]) - D;
		if (Gap > 0.f && C.LambdaN <= 0.f) { continue; }
		if (bBoundaryOnly && BlockId[C.I] == BlockId[C.J]) { continue; }
		++DeltaCount[C.I]; ++DeltaCount[C.J];
	}
	if (!bBoundaryOnly) { for (const FSandBond& B : Bonds) { ++DeltaCount[B.I]; ++DeltaCount[B.J]; } }
}

void FSandSpike::OrderContactsColor8()
{
	// 블록 = 워크그룹, 색 = (bx,by,bz) 의 홀짝 3비트. 같은 색 블록은 입자를 공유하지 않으므로 GPU 에선 동시에 돈다.
	// CPU 는 순차라 이 순서가 곧 GPU 결과다 (블록 안 배치도 서로 입자를 안 겹치므로 순서 무관)
	const float SavedB = Params.BlockSize;
	if (Params.BlockSize <= 0.f) { Params.BlockSize = 30.f; }
	AssignBlocks();
	Params.BlockSize = SavedB;

	const int32 NC = Contacts.Num();
	TArray<int64> BlockOf; BlockOf.SetNumUninitialized(NC);
	TArray<int32> Idx; Idx.SetNumUninitialized(NC);
	for (int32 c = 0; c < NC; ++c)
	{
		BlockOf[c] = BlockId[FMath::Min(Contacts[c].I, Contacts[c].J)];   // 소유 블록
		Idx[c] = c;
	}
	Idx.Sort([&BlockOf](int32 A, int32 B) { return BlockOf[A] != BlockOf[B] ? BlockOf[A] < BlockOf[B] : A < B; });

	// 블록 안 greedy 배치 — 입자별 사용 배치 비트마스크
	TArray<uint8> Batch; Batch.SetNumZeroed(NC);
	TArray<uint64> Mask; Mask.SetNumZeroed(Pos.Num());
	TArray<int32> Touched;
	int32 MaxBatch = 0; int64 SumBatches = 0; int32 NumBlocks = 0;
	for (int32 k = 0; k < NC; )
	{
		const int64 Bk = BlockOf[Idx[k]];
		int32 BlockMax = 0;
		for (; k < NC && BlockOf[Idx[k]] == Bk; ++k)
		{
			const FSandContact& C = Contacts[Idx[k]];
			const uint64 Used = Mask[C.I] | Mask[C.J];
			const int32 B = (Used == ~0ull) ? 63 : (int32)FMath::CountTrailingZeros64(~Used);
			Batch[Idx[k]] = (uint8)B;
			const uint64 Bit = 1ull << B;
			if (Mask[C.I] == 0) { Touched.Add(C.I); }
			if (Mask[C.J] == 0) { Touched.Add(C.J); }
			Mask[C.I] |= Bit; Mask[C.J] |= Bit;
			BlockMax = FMath::Max(BlockMax, B + 1);
		}
		for (int32 t : Touched) { Mask[t] = 0; }
		Touched.Reset();
		MaxBatch = FMath::Max(MaxBatch, BlockMax);
		SumBatches += BlockMax; ++NumBlocks;
	}
	LastMaxBatch = MaxBatch;
	LastAvgBatches = NumBlocks > 0 ? (float)SumBatches / NumBlocks : 0.f;

	const int32 Rot = Params.bColor8Rotate ? (Frame & 7) : 0;
	auto ColorOf = [Rot](int64 Id) { return ((int32)(((Id >> 42) & 1) | (((Id >> 21) & 1) << 1) | ((Id & 1) << 2)) + Rot) & 7; };
	Idx.Sort([&](int32 A, int32 B)
	{
		const int32 CA = ColorOf(BlockOf[A]), CB = ColorOf(BlockOf[B]);
		if (CA != CB) { return CA < CB; }
		if (BlockOf[A] != BlockOf[B]) { return BlockOf[A] < BlockOf[B]; }
		if (Batch[A] != Batch[B]) { return Batch[A] < Batch[B]; }
		return A < B;
	});
	TArray<FSandContact> Sorted; Sorted.Reserve(NC);
	for (int32 c : Idx) { Sorted.Add(Contacts[c]); }
	Contacts = MoveTemp(Sorted);
}

void FSandSpike::AssignBlocks(float Offset)
{
	// 위치를 BlockSize 격자로 양자화한 정확한 키. 이웃 셀끼리만 비교하므로 21비트 좌표면 충분하다
	BlockId.SetNum(Pos.Num(), EAllowShrinking::No);
	const float InvB = 1.f / Params.BlockSize;
	const int64 Bias = 1048576;   // 2^20
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		const int64 bx = (int64)FMath::FloorToInt((Pos[i].X + Offset) * InvB) + Bias;
		const int64 by = (int64)FMath::FloorToInt((Pos[i].Y + Offset) * InvB) + Bias;
		const int64 bz = (int64)FMath::FloorToInt((Pos[i].Z + Offset) * InvB) + Bias;
		BlockId[i] = (bx * 4398046511104LL) + (by * 2097152LL) + bz;   // bx<<42 | by<<21 | bz
	}
}

void FSandSpike::SolveIterationsGpu(float StepDt)
{
	// 3-1 ② — 반복 루프를 통째로 GPU 에. 여기서 하는 일은 분류와 포장뿐이다:
	//   접촉을 블록 안(블록 순으로 모음) / 경계로 가르고, 경계 접촉과 결합의 입자별 인접(CSR)을 만든다.
	// 블록은 서브스텝 동안 고정이다 (CPU ①-b 는 반복마다 다시 매겼다 — GPU 는 프레임당 한 번 정렬하므로 이쪽이 실제 형태).
	checkf(SandGpu::IsAvailable(), TEXT("-gpu 는 RHI 가 필요하다. 커맨드릿이면 -AllowCommandletRendering 을 붙일 것"));
	checkf(Params.MomentumA == 0.f && !Params.bMassSplitBonds, TEXT("GPU 경로는 모멘텀·결합 질량 분할을 지원하지 않는다 (#36 에서 둘 다 껐다)"));

	const double T0 = FPlatformTime::Seconds();
	const int32 N = Pos.Num();
	const float SavedBlock = Params.BlockSize;
	if (Params.BlockSize <= 0.f) { Params.BlockSize = 30.f; }
	AssignBlocks();
	Params.BlockSize = SavedBlock;

	static FSandGpuSolveData D;   // 배열 재할당을 피한다. 게임 스레드 전용
	D.Iterations = Params.Iterations;
	D.Pos.SetNumUninitialized(N);
	D.Vel.SetNumUninitialized(N);
	D.PrevPos.SetNumUninitialized(N);
	D.WallLambda.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		D.Pos[i]     = FVector4f(Pos[i], InvMass[i]);
		D.Vel[i]     = FVector4f(Vel[i], 0.f);
		D.PrevPos[i] = FVector4f(PrevPos[i], 0.f);
		D.WallLambda[i] = WallLambda[i];
	}
	D.GroundLambda = GroundLambdaN;

	// --- 접촉: 블록 안은 블록별로 모으고(목록 순서 유지), 경계는 뒤에 ---
	const int32 NumC = Contacts.Num();
	TMap<int64, int32> BlockIndex;
	TArray<int32> BlockOf;          // 접촉 → 블록 번호 (-1 = 경계)
	TArray<int32> BlockCount;
	BlockOf.SetNumUninitialized(NumC);
	for (int32 k = 0; k < NumC; ++k)
	{
		const FSandContact& C = Contacts[k];
		if (BlockId[C.I] != BlockId[C.J]) { BlockOf[k] = -1; continue; }
		int32& B = BlockIndex.FindOrAdd(BlockId[C.I], -1);
		if (B < 0) { B = BlockCount.Add(0); }
		BlockOf[k] = B;
		++BlockCount[B];
	}
	const int32 NumBlocks = BlockCount.Num();
	D.BlockRange.SetNumUninitialized(NumBlocks + 1);
	int32 NumInternal = 0;
	for (int32 b = 0; b < NumBlocks; ++b)
	{
		D.BlockRange[b] = NumInternal;
		NumInternal += BlockCount[b];
		BlockCount[b] = D.BlockRange[b];   // 이제부터 채움 커서
	}
	D.BlockRange[NumBlocks] = NumInternal;
	D.NumInternal = NumInternal;

	TArray<int32> GpuSlot;          // 접촉 → GPU 버퍼 자리
	GpuSlot.SetNumUninitialized(NumC);
	D.ContactIJ.SetNumUninitialized(NumC * 2);
	D.ContactLambda.SetNumUninitialized(NumC);
	D.BoundaryAdjOffset.SetNumUninitialized(N + 1);
	FMemory::Memzero(D.BoundaryAdjOffset.GetData(), (N + 1) * sizeof(uint32));
	int32 BoundaryCursor = NumInternal;
	for (int32 k = 0; k < NumC; ++k)
	{
		const FSandContact& C = Contacts[k];
		const int32 Slot = BlockOf[k] >= 0 ? BlockCount[BlockOf[k]]++ : BoundaryCursor++;
		GpuSlot[k] = Slot;
		D.ContactIJ[2 * Slot]     = C.I;
		D.ContactIJ[2 * Slot + 1] = C.J;
		D.ContactLambda[Slot]     = FVector4f(C.LambdaTan, C.LambdaN);
		if (BlockOf[k] < 0) { ++D.BoundaryAdjOffset[C.I + 1]; ++D.BoundaryAdjOffset[C.J + 1]; }
	}
	const int32 NumBoundary = NumC - NumInternal;
	LastBoundaryFrac = NumC > 0 ? (float)NumBoundary / NumC : 0.f;

	// 입자별 인접 (CSR). 원소 = ((Base + 순번) << 1) | (입자가 j 쪽이면 1). Offset 에는 입자별 개수가 [i+1] 에 들어 있다
	auto FillAdj = [N](TArray<uint32>& Offset, TArray<uint32>& Adj, int32 Count, int32 Base, TFunctionRef<void(int32, int32&, int32&)> PairOf)
	{
		for (int32 i = 0; i < N; ++i) { Offset[i + 1] += Offset[i]; }
		Adj.SetNumUninitialized(Offset[N]);
		TArray<uint32> Cursor(Offset.GetData(), N);
		for (int32 c = 0; c < Count; ++c)
		{
			int32 I, J;
			PairOf(c, I, J);
			Adj[Cursor[I]++] = ((uint32)(Base + c) << 1);
			Adj[Cursor[J]++] = ((uint32)(Base + c) << 1) | 1u;
		}
	};
	FillAdj(D.BoundaryAdjOffset, D.BoundaryAdj, NumBoundary, NumInternal, [&](int32 c, int32& I, int32& J)
	{
		I = D.ContactIJ[2 * (NumInternal + c)];
		J = D.ContactIJ[2 * (NumInternal + c) + 1];
	});

	// --- 결합 ---
	const float Alpha = BondCompliance();
	D.bBondsActive = Bonds.Num() > 0 && Alpha > 0.f;
	D.BondAdjOffset.SetNumUninitialized(N + 1);
	FMemory::Memzero(D.BondAdjOffset.GetData(), (N + 1) * sizeof(uint32));
	D.BondIJ.Reset();
	D.BondData.Reset();
	D.BondAdj.Reset();
	if (D.bBondsActive)
	{
		const int32 NumB = Bonds.Num();
		D.BondIJ.SetNumUninitialized(NumB * 2);
		D.BondData.SetNumUninitialized(NumB);
		for (int32 b = 0; b < NumB; ++b)
		{
			D.BondIJ[2 * b]     = Bonds[b].I;
			D.BondIJ[2 * b + 1] = Bonds[b].J;
			D.BondData[b]       = FVector4f(Bonds[b].RestLen, Bonds[b].Lambda, 0.f, 0.f);
			++D.BondAdjOffset[Bonds[b].I + 1];
			++D.BondAdjOffset[Bonds[b].J + 1];
		}
		FillAdj(D.BondAdjOffset, D.BondAdj, NumB, 0, [&](int32 b, int32& I, int32& J) { I = Bonds[b].I; J = Bonds[b].J; });
		D.BondAlphaTilde = Alpha / (StepDt * StepDt);
		D.BondLambdaMax  = Params.BondStrength * FMath::Abs(Params.Gravity) * CohesionOfW(Params.WGlobal) * StepDt * StepDt;
		D.BondOmega      = Params.BondOmega;
	}

	D.Radius   = Params.Radius;
	D.StepDt   = StepDt;
	D.Kv       = Params.Kv;
	D.Kc       = Params.Kc;
	D.SorOmega = Params.SorOmega;
	D.Friction = Params.Friction;
	D.bMassSplit = Params.bMassSplit;

	D.WallFriction = Params.WallFriction;
	BuildWallParams(D.WallMode, D.WallActiveMask, D.WallLX, D.WallLY, D.WallLimit, D.WallLiftZ);

	const double T1 = FPlatformTime::Seconds();
	SandGpu::Solve(D);
	const double T2 = FPlatformTime::Seconds();

	// --- 내려받기 ---
	for (int32 i = 0; i < N; ++i)
	{
		Pos[i] = FVector3f(D.Pos[i]);
		Vel[i] = FVector3f(D.Vel[i]);
		WallLambda[i] = D.WallLambda[i];
	}
	GroundLambdaN = D.GroundLambda;
	for (int32 k = 0; k < NumC; ++k)
	{
		const FVector4f& L = D.ContactLambda[GpuSlot[k]];
		Contacts[k].LambdaN   = L.W;
		Contacts[k].LambdaTan = FVector3f(L);
	}
	if (D.bBondsActive)
	{
		for (int32 b = 0; b < Bonds.Num(); ++b) { Bonds[b].Lambda = D.BondData[b].Y; }
	}

	GpuMs     += (float)((T2 - T1) * 1000.0);
	GpuPrepMs += (float)((T1 - T0 + FPlatformTime::Seconds() - T2) * 1000.0);
}

void FSandSpike::BuildWallParams(int32& Mode, uint32& Mask, float& LX, float& LY, float& Limit, float& LiftZ) const
{
	Mode = 0; Mask = 0; LX = LY = Limit = LiftZ = 0.f;
	if (bWallsActive && bBoxScene)
	{
		Mode = 1;
		LX = BoxHalf.X - Params.Radius;
		LY = BoxHalf.Y - Params.Radius;
		Mask = 4u | 8u;
		if (!Tilt.bOpenDownhill) { Mask |= 1u; if (!Tilt.bOpenXPlus) { Mask |= 2u; } }
	}
	else if (bWallsActive)
	{
		Mode  = 2;
		Limit = WallRadius - Params.Radius;
		LiftZ = WallLiftZ;
	}
}

void FSandSpike::BuildBondUpload(TArray<uint32>& IJ, TArray<FVector4f>& Data, TArray<uint32>& AdjOffset, TArray<uint32>& Adj) const
{
	const int32 N = Pos.Num();
	const int32 NumB = Bonds.Num();
	IJ.SetNumUninitialized(NumB * 2);
	Data.SetNumUninitialized(NumB);
	AdjOffset.SetNumZeroed(N + 1);
	for (int32 b = 0; b < NumB; ++b)
	{
		IJ[2 * b]     = Bonds[b].I;
		IJ[2 * b + 1] = Bonds[b].J;
		Data[b]       = FVector4f(Bonds[b].RestLen, Bonds[b].Lambda, 0.f, 0.f);
		++AdjOffset[Bonds[b].I + 1];
		++AdjOffset[Bonds[b].J + 1];
	}
	for (int32 i = 0; i < N; ++i) { AdjOffset[i + 1] += AdjOffset[i]; }
	Adj.SetNumUninitialized(AdjOffset[N]);
	TArray<uint32> Cursor(AdjOffset.GetData(), N);
	for (int32 b = 0; b < NumB; ++b)
	{
		Adj[Cursor[Bonds[b].I]++] = ((uint32)b << 1);
		Adj[Cursor[Bonds[b].J]++] = ((uint32)b << 1) | 1u;
	}
}

void FSandSpike::SyncToGpu()
{
	if (!GpuSim) { GpuSim = MakeUnique<FSandGpuSim>(); }
	FSandGpuUpload U;
	const int32 N = Pos.Num();
	U.Pos.SetNumUninitialized(N);
	U.Vel.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		U.Pos[i] = FVector4f(Pos[i], InvMass[i]);
		U.Vel[i] = FVector4f(Vel[i], 0.f);
	}
	U.GroundLambda = GroundLambdaN;
	U.WallLambda   = WallLambda;
	BuildBondUpload(U.BondIJ, U.BondData, U.BondAdjOffset, U.BondAdj);
	GpuSim->Upload(U, bGpuPairsValid);
	GpuBondsUploaded = Bonds;
	bGpuDirty = false;
	bGpuStale = false;
	if (!bGpuPairsValid) { Contacts.Reset(); }   // 제거·재배치 뒤엔 접촉 λ 를 버린다 — 한 프레임 차가운 시작 (RemoveParticles 의 CPU 규약과 같다)
	bGpuPairsValid = true;
	WarmLambda.Reset();
}

void FSandSpike::SyncFromGpu() const
{
	if (!bGpuStale || !GpuSim) { return; }
	FSandSpike* Self = const_cast<FSandSpike*>(this);
	FSandGpuDownload D;
	GpuSim->Download(D);
	const int32 N = Pos.Num();
	check(D.Pos.Num() == N);
	for (int32 i = 0; i < N; ++i)
	{
		Self->Pos[i] = FVector3f(D.Pos[i]);
		Self->Vel[i] = FVector3f(D.Vel[i]);
	}
	Self->GroundLambdaN = D.GroundLambda;
	Self->WallLambda    = D.WallLambda;

	const int32 NumC = D.PairLambda.Num();
	Self->Contacts.SetNum(NumC);
	for (int32 k = 0; k < NumC; ++k)
	{
		FSandContact& C = Self->Contacts[k];
		C = FSandContact{};
		C.I = (int32)D.PairIJ[2 * k];
		C.J = (int32)D.PairIJ[2 * k + 1];
		C.LambdaN   = D.PairLambda[k].W;
		C.LambdaTan = FVector3f(D.PairLambda[k]);
	}

	// 결합 — GPU 는 죽은 결합을 표시만 한다(x < 0). CPU 목록은 올린 순서(GpuBondsUploaded)에서 산 것만 추린다.
	// GPU 쪽 번호는 올린 순서 그대로라 다시 올릴 필요가 없다
	check(D.BondData.Num() == GpuBondsUploaded.Num());
	Self->Bonds.Reset(GpuBondsUploaded.Num());
	for (int32 b = 0; b < GpuBondsUploaded.Num(); ++b)
	{
		if (D.BondData[b].X < 0.f) { continue; }
		FSandBond B = GpuBondsUploaded[b];
		B.Lambda = D.BondData[b].Y;
		Self->Bonds.Add(B);
	}

	Self->bGpuStale = false;
}

void FSandSpike::StepGpuResident(float Dt)
{
	checkf(SandGpu::IsAvailable(), TEXT("-gpu2 는 RHI 가 필요하다. 커맨드릿이면 -AllowCommandletRendering 을 붙일 것"));
	checkf(Params.Substeps == 1 && Params.bSequentialPositions && Params.MomentumA == 0.f && !Params.bMassSplitBonds,
		TEXT("GPU 상주 경로는 서브스텝 1 · SP · 모멘텀 0 · 결합 질량 분할 없음 만 지원한다"));

	if (bGpuDirty) { SyncToGpu(); }

	// 탐색 반경 — BuildContacts 와 같은 식이되 상한이 8R 이 아니라 2R 이다. 쌍 수는 반경의 세제곱이라 8R(지름 5배)이면
	// 입자당 300쌍이 되어 GPU 버퍼가 못 받는다. 2R 은 한 프레임에 반경 두 개(150 cm/s)까지의 이동을 놓치지 않는다 — 붓기 낙하(≈100 cm/s)는 안이다
	const float D = 2.f * Params.Radius;
	const float Margin = FMath::Clamp(2.f * LastMaxSpeed * Dt, 0.1f * Params.Radius, 2.f * Params.Radius);

	FSandGpuFrameParams P;
	P.Iterations  = Params.Iterations;
	P.Radius      = Params.Radius;
	P.StepDt      = Dt;
	P.SearchRange = D + Margin;
	P.BlockSize   = Params.BlockSize > 0.f ? Params.BlockSize : (Params.bColor8 ? 15.f : 30.f);   // A안: 15cm 확정 (#40)
	P.bColor8     = Params.bColor8 || Params.bColor8Par;
	P.bColor8Par  = Params.bColor8Par;
	P.FrameIndex  = Params.bColor8Rotate || Params.bColor8 ? Frame : 0;   // GPU 색 모드는 회전 기본 (#40 R5)
	P.WarmStart   = Params.WarmStart;
	P.WarmGapTol  = Params.WarmGapTol;
	P.bWarmTangent = Params.bWarmTangent;
	P.Kv = Params.Kv; P.Kc = Params.Kc; P.SorOmega = Params.SorOmega; P.Friction = Params.Friction;
	P.MaxDepenVel = Params.MaxDepenVel;
	P.bMassSplit  = Params.bMassSplit;
	P.Gravity     = GravityDir * (FMath::Abs(Params.Gravity) * GravityScale);
	const float Alpha = BondCompliance();
	P.bBondsActive   = Bonds.Num() > 0 && Alpha > 0.f;
	P.bBondsDry      = CohesionOfW(Params.WGlobal) <= 0.f;
	P.BondAlphaTilde = Alpha > 0.f ? Alpha / (Dt * Dt) : 0.f;
	P.BondLambdaMax  = Params.BondStrength * FMath::Abs(Params.Gravity) * CohesionOfW(Params.WGlobal) * Dt * Dt;
	P.BondOmega      = Params.BondOmega;
	P.BreakStretch   = Params.BreakStretch;
	P.VelocityDamping = VelocityDamping;
	P.WallFriction   = Params.WallFriction;
	BuildWallParams(P.WallMode, P.WallActiveMask, P.WallLX, P.WallLY, P.WallLimit, P.WallLiftZ);

	FSandGpuStats S;
	GpuSim->Step(P, S);
	checkf(S.PairOverflow == 0, TEXT("GPU 쌍 버퍼 넘침 (%d) — MaxPairs 를 올릴 것"), S.PairOverflow);
	if (Params.bColor8Par && Frame % 600 == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[color8par] f%d 블록 %d · 배치 최대 %d · 평균 %.1f · 해시 넘침 %d"), Frame, S.NumBatchBlocks, S.MaxBatch,
			S.NumBatchBlocks > 0 ? (float)S.SumBatch / S.NumBatchBlocks : 0.f, S.BatchOverflow);
	}

	BondsBrokenTotal += S.BondsBroken;
	LastBoundaryFrac = S.NumPairs > 0 ? (float)S.NumBoundary / S.NumPairs : 0.f;
	GpuMs = (float)S.GpuMs;
	ApplyFrameStats(S.SumSpeed, S.MaxSpeed, S.FreeCount, S.SumLambda);
	bGpuStale = true;
}

void FSandSpike::ApplyGatheredVelocityDeltas(float StepDt)
{
	// 패스 2 — 입자마다 한 번. Delta 는 Δv 단위, x 는 같은 임펄스의 h 배 (SP 규약 유지)
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (InvMass[i] <= 0.f) { continue; }
		Vel[i] += Delta[i];
		Pos[i] += Delta[i] * StepDt;
	}
}

void FSandSpike::ApgdSnapToGamma(float StepDt)
{
	// 외삽된 y 를 투영된 γ 로 되돌린다 — 차이를 gather 임펄스로 (v·x 함께). 재시작과 반복 끝에서
	FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(FVector3f));
	for (FSandContact& C : Contacts)
	{
		const float DN = C.GammaN - C.LambdaN;
		const FVector3f DT = C.GammaTan - C.LambdaTan;
		C.LambdaN = C.GammaN;
		C.LambdaTan = C.GammaTan;
		if (DN == 0.f && DT.IsZero()) { continue; }
		const FVector3f Diff = Pos[C.I] - Pos[C.J];
		const float Dist = Diff.Size();
		if (Dist < UE_KINDA_SMALL_NUMBER) { continue; }
		const FVector3f Impulse = (Diff / Dist) * DN + DT;
		Delta[C.I] += Impulse * InvMass[C.I];
		Delta[C.J] -= Impulse * InvMass[C.J];
	}
	for (FSandBond& B : Bonds)
	{
		const float DL = B.Gamma - B.Lambda;
		B.Lambda = B.Gamma;
		if (DL == 0.f) { continue; }
		const FVector3f Diff = Pos[B.I] - Pos[B.J];
		const float Dist = Diff.Size();
		if (Dist < UE_KINDA_SMALL_NUMBER) { continue; }
		const FVector3f Corr = (Diff / Dist) * (DL / StepDt);
		Delta[B.I] += Corr * InvMass[B.I];
		Delta[B.J] -= Corr * InvMass[B.J];
	}
	ApplyGatheredVelocityDeltas(StepDt);
}

void FSandSpike::SolveWallsSP(float StepDt)
{
	if (!bWallsActive)
	{
		return;
	}

	const float InvH = 1.f / StepDt;

	// 마찰 없는 단방향 제약. 형틀은 물성이 아니라 비계다
	auto Push = [&](int32 i, int32 K, const FVector3f& N, float Gap)
	{
		const float Wi = InvMass[i];
		if (Wi <= 0.f)
		{
			return;
		}
		float& WallLambdaN_i = WallLambda[i][K];
		if (Gap > 0.f && WallLambdaN_i <= 0.f)
		{
			WallLambdaN_i = 0.f;
			return;
		}

		const float ResN = Params.Kv * FVector3f::DotProduct(Vel[i], N) + (Params.Kc * InvH) * Gap;
		float DLambdaN = -(Params.SorOmega / Wi) * ResN;

		const float OldN = WallLambdaN_i;
		WallLambdaN_i = FMath::Max(0.f, WallLambdaN_i + DLambdaN);
		DLambdaN = WallLambdaN_i - OldN;

		Vel[i] += N * (DLambdaN * Wi);
		Pos[i] += N * (DLambdaN * Wi * StepDt);

		// 벽 마찰 — 지면(SolveGroundSP)과 같은 구조. 반복당 μ·λn 클램프
		if (Params.WallFriction > 0.f && WallLambdaN_i > 0.f)
		{
			FVector3f DriftT = Pos[i] - PrevPos[i];
			DriftT -= N * FVector3f::DotProduct(DriftT, N);
			FVector3f NuT = Vel[i];
			NuT -= N * FVector3f::DotProduct(NuT, N);
			FVector3f DLambdaTan = (NuT * Params.Kv + DriftT * (Params.Kc * InvH)) * (-(Params.SorOmega / Wi));
			const float MaxTan = Params.WallFriction * WallLambdaN_i;
			const float L = DLambdaTan.Size();
			if (L > MaxTan) { DLambdaTan = (L > UE_KINDA_SMALL_NUMBER) ? DLambdaTan * (MaxTan / L) : FVector3f::ZeroVector; }
			Vel[i] += DLambdaTan * Wi;
			Pos[i] += DLambdaTan * (Wi * StepDt);
		}
	};

	if (bBoxScene)
	{
		const float LX = BoxHalf.X - Params.Radius;
		const float LY = BoxHalf.Y - Params.Radius;
		for (int32 i = 0; i < Pos.Num(); ++i)
		{
			if (!Tilt.bOpenDownhill)
			{
				Push(i, 0, FVector3f( 1.f, 0.f, 0.f), Pos[i].X + LX);
				if (!Tilt.bOpenXPlus)
				{
					Push(i, 1, FVector3f(-1.f, 0.f, 0.f), LX - Pos[i].X);
				}
			}
			Push(i, 2, FVector3f(0.f,  1.f, 0.f), Pos[i].Y + LY);
			Push(i, 3, FVector3f(0.f, -1.f, 0.f), LY - Pos[i].Y);
		}
		return;
	}

	// 원통 — 들어올린 높이 아래는 벽이 없다
	const float Limit = WallRadius - Params.Radius;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (Pos[i].Z < WallLiftZ)
		{
			continue;
		}
		const float Rad2 = Pos[i].X * Pos[i].X + Pos[i].Y * Pos[i].Y;
		if (Rad2 < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const float Rad = FMath::Sqrt(Rad2);
		const FVector3f N(-Pos[i].X / Rad, -Pos[i].Y / Rad, 0.f);   // 안쪽을 향한다
		Push(i, 0, N, Limit - Rad);
	}
}

void FSandSpike::SolveGroundSP(float StepDt)
{
	// 지면은 무한 질량. w_world = 0 이므로 d = w_i
	const float R2   = Params.Radius;
	const float InvH = 1.f / StepDt;
	const FVector3f N(0.f, 0.f, 1.f);

	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		const float Wi = InvMass[i];
		if (Wi <= 0.f)
		{
			continue;
		}
		const float Gap = Pos[i].Z - R2;
		if (Gap > 0.f && GroundLambdaN[i] <= 0.f)
		{
			GroundLambdaN[i] = 0.f;
			continue;
		}

		const float ResN = Params.Kv * Vel[i].Z + (Params.Kc * InvH) * Gap;
		float DLambdaN = -(Params.SorOmega / Wi) * ResN;

		const float OldN = GroundLambdaN[i];
		GroundLambdaN[i] = FMath::Max(0.f, GroundLambdaN[i] + DLambdaN);
		DLambdaN = GroundLambdaN[i] - OldN;

		FVector3f DriftT = Pos[i] - PrevPos[i];
		DriftT.Z = 0.f;
		FVector3f NuT = Vel[i];
		NuT.Z = 0.f;

		FVector3f DLambdaTan = (NuT * Params.Kv + DriftT * (Params.Kc * InvH)) * (-(Params.SorOmega / Wi));

		const float MaxTan = Params.Friction * GroundLambdaN[i];
		const float L = DLambdaTan.Size();
		if (L > MaxTan)
		{
			DLambdaTan = (L > UE_KINDA_SMALL_NUMBER) ? DLambdaTan * (MaxTan / L) : FVector3f::ZeroVector;
		}

		const FVector3f Impulse = N * DLambdaN + DLambdaTan;
		Vel[i] += Impulse * Wi;
		Pos[i] += Impulse * (Wi * StepDt);
	}
}

void FSandSpike::ApplyDeltas()
{
	const float Omega = Params.Relaxation;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		DeltaScale[i] = 0.f;
		if (DeltaCount[i] > 0 && InvMass[i] > 0.f)
		{
			DeltaScale[i] = Omega / (float)DeltaCount[i];
			Pos[i] += Delta[i] * DeltaScale[i];
		}
	}
}

void FSandSpike::CommitLambdas()
{
	// 실제로 적용된 비율만큼만 장부에 올린다.
	// 고정 입자는 안 움직이므로 자유 입자 쪽 비율이 그대로 유효 적용률이 된다
	for (FSandContact& C : Contacts)
	{
		const float Wi = InvMass[C.I];
		const float Wj = InvMass[C.J];
		const float WSum = Wi + Wj;
		if (WSum <= 0.f)
		{
			continue;
		}
		const float Applied = (Wi * DeltaScale[C.I] + Wj * DeltaScale[C.J]) / WSum;
		C.LambdaN += C.PendingN * Applied;
		C.LambdaT += C.PendingT * Applied;
		C.PendingN = 0.f;
		C.PendingT = 0.f;
	}
}

void FSandSpike::SolveWalls()
{
	if (!bWallsActive)
	{
		return;
	}

	if (bBoxScene)
	{
		// 경사판 = 수로. 양옆(Y)만 막고 사면 방향(X)과 위는 열어둔다
		const float LX = BoxHalf.X - Params.Radius;
		const float LY = BoxHalf.Y - Params.Radius;
		for (int32 i = 0; i < Pos.Num(); ++i)
		{
			if (InvMass[i] <= 0.f)
			{
				continue;
			}
			if (!Tilt.bOpenDownhill)
			{
				Pos[i].X = FMath::Clamp(Pos[i].X, -LX, Tilt.bOpenXPlus ? FLT_MAX : LX);
			}
			Pos[i].Y = FMath::Clamp(Pos[i].Y, -LY, LY);
		}
		return;
	}

	// 형틀은 물성이 아니라 비계다. 마찰 없이 반지름만 묶는다.
	// 들어올린 높이 아래는 벽이 없다 — 거기서부터 모래가 흘러나온다
	const float Limit = WallRadius - Params.Radius;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (InvMass[i] <= 0.f || Pos[i].Z < WallLiftZ)
		{
			continue;
		}
		const float Rad2 = Pos[i].X * Pos[i].X + Pos[i].Y * Pos[i].Y;
		if (Rad2 <= Limit * Limit || Rad2 < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const float Rad = FMath::Sqrt(Rad2);
		const float Sc  = Limit / Rad;
		Pos[i].X *= Sc;
		Pos[i].Y *= Sc;
	}
}

void FSandSpike::SolveGround()
{
	// 지면도 접촉이다. 무한 질량이라 w = 0 → Δλ = d (절반이 아니라 전부)
	const float R2 = Params.Radius;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (InvMass[i] <= 0.f || Pos[i].Z >= R2)
		{
			continue;
		}
		const float Depth = R2 - Pos[i].Z;
		GroundLambdaN[i] += Depth;
		Pos[i].Z = R2;

		if (GroundLambdaT[i] < Params.Friction * GroundLambdaN[i])
		{
			FVector3f Dpt = Pos[i] - PrevPos[i];
			Dpt.Z = 0.f;
			const float TLen = Dpt.Size();
			if (TLen > UE_KINDA_SMALL_NUMBER)
			{
				GroundLambdaT[i] += TLen;
				Pos[i] -= Dpt;
			}
		}
	}
}

void FSandSpike::UpdateVelocities(float SubDt)
{
	const float Inv = 1.f / SubDt;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		Vel[i] = InvMass[i] > 0.f ? (Pos[i] - PrevPos[i]) * Inv : FVector3f::ZeroVector;
	}
}

void FSandSpike::ApplyDynamicFriction(float SubDt)
{
	// Δv = −v_t/|v_t| · min(h·μd·|f_n|, |v_t|),  f_n = λn/h²  (논문 §3.6 식 30)
	// min 이 "보정이 속도 자체를 못 넘는다"를 보장해서 무조건 안정이다
	const float InvH = 1.f / SubDt;
	const float D    = 2.f * Params.Radius;

	for (const FSandContact& C : Contacts)
	{
		if (C.LambdaN <= 0.f)
		{
			continue;
		}
		const int32 i = C.I;
		const int32 j = C.J;

		const FVector3f Diff = Pos[i] - Pos[j];
		const float Dist = Diff.Size();
		if (Dist < UE_KINDA_SMALL_NUMBER || Dist >= D)
		{
			continue;
		}
		const FVector3f N = Diff / Dist;

		const FVector3f RelV = Vel[i] - Vel[j];
		const FVector3f Vt   = RelV - N * FVector3f::DotProduct(RelV, N);
		const float VtLen = Vt.Size();
		if (VtLen < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// h·μ·f_n = h·μ·(λn/h²) = μ·λn/h
		const float Budget = Params.Friction * C.LambdaN * InvH;
		const FVector3f DV = Vt * (-FMath::Min(Budget, VtLen) / VtLen);

		const float WSum = InvMass[i] + InvMass[j];
		if (WSum <= 0.f)
		{
			continue;
		}
		Vel[i] += DV * (InvMass[i] / WSum);
		Vel[j] -= DV * (InvMass[j] / WSum);
	}

	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (GroundLambdaN[i] <= 0.f || InvMass[i] <= 0.f)
		{
			continue;
		}
		FVector3f Vt = Vel[i];
		Vt.Z = 0.f;
		const float VtLen = Vt.Size();
		if (VtLen < UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const float Budget = Params.Friction * GroundLambdaN[i] * InvH;
		Vel[i] -= Vt * (FMath::Min(Budget, VtLen) / VtLen);   // 지면은 무한 질량이라 전부 입자에
	}
}

int32 FSandSpike::ContactCountOf(int32 i) const
{
	SyncFromGpu();
	int32 N = 0;
	for (const FSandContact& C : Contacts)
	{
		if (C.I == i || C.J == i) { ++N; }
	}
	return N;
}

FString FSandSpike::DescribeParticle(int32 i) const
{
	SyncFromGpu();
	const float D = 2.f * Params.Radius;
	FString S = FString::Printf(TEXT("#%d p(%.2f %.2f %.2f) prev(%.2f %.2f %.2f) v(%.1f %.1f %.1f) |v| %.1f gλ %.3f 접촉:"),
		i, Pos[i].X, Pos[i].Y, Pos[i].Z, PrevPos[i].X, PrevPos[i].Y, PrevPos[i].Z,
		Vel[i].X, Vel[i].Y, Vel[i].Z, Vel[i].Size(), GroundLambdaN.IsValidIndex(i) ? GroundLambdaN[i] : 0.f);
	int32 Shown = 0;
	for (const FSandContact& C : Contacts)
	{
		if (C.I != i && C.J != i) { continue; }
		const int32 j = (C.I == i) ? C.J : C.I;
		const float Gap = (Pos[i] - Pos[j]).Size() - D;
		if (Shown < 12)
		{
			S += FString::Printf(TEXT(" [%d%s gap %.2f λn %.2f λt %.2f |vj| %.1f]"), j, InvMass[j] <= 0.f ? TEXT("고정") : TEXT(""), Gap, C.LambdaN, C.LambdaTan.Size(), Vel[j].Size());
		}
		++Shown;
	}
	S += FString::Printf(TEXT(" (총 %d)"), Shown);
	return S;
}

void FSandSpike::UpdateSettleState(float Dt)
{
	(void)Dt;   // 판정이 더 이상 dt 에 의존하지 않는다

	double SumSpeed = 0.0;
	float  MaxSpeed = 0.f;
	int32  Free = 0;
	for (int32 i = 0; i < Vel.Num(); ++i)
	{
		if (InvMass[i] <= 0.f)
		{
			continue;   // 고정 입자는 평균을 희석시킬 뿐이다
		}
		const float S = Vel[i].Size();
		SumSpeed += S;
		MaxSpeed  = FMath::Max(MaxSpeed, S);
		++Free;
	}
	// 불평형력 비 — 남은 운동량(= 이번 스텝에 못 푼 잔차) 을 접촉이 나른 임펄스로 나눈다. 둘 다 질량·속도 단위
	double SumLambda = 0.0;
	for (const FSandContact& C : Contacts) { SumLambda += FMath::Abs(C.LambdaN); }
	for (int32 i = 0; i < GroundLambdaN.Num(); ++i) { SumLambda += FMath::Abs(GroundLambdaN[i]); }

	ApplyFrameStats(SumSpeed, MaxSpeed, Free, SumLambda);
}

void FSandSpike::ApplyFrameStats(double SumSpeed, float MaxSpeed, int32 Free, double SumLambda)
{
	LastAvgSpeed = Free > 0 ? (float)(SumSpeed / Free) : 0.f;
	LastMaxSpeed = MaxSpeed;
	LastUnbalanced = SumLambda > 0.0 ? (float)(SumSpeed / SumLambda) : 0.f;   // m = 1

	// §2-2 — 평균 |v| < ε·√(g·d) 가 60프레임 연속.
	// dt 가 들어가지 않으므로 시간 조각을 바꿔도 판정선이 안 움직인다
	const float Threshold = Params.SettleEpsilon *
		FMath::Sqrt(FMath::Abs(Params.Gravity) * 2.f * Params.Radius);
	SettledFrames = (LastAvgSpeed < Threshold) ? SettledFrames + 1 : 0;
}

void FSandSpike::Step(float Dt)
{
	const double T0 = FPlatformTime::Seconds();
	GpuMs = GpuPrepMs = 0.f;

	if (Params.bGpuResident)
	{
		StepGpuResident(Dt);
		++Frame;
		Time  += Dt;
		StepMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
		return;
	}

	BuildContacts(Dt);   // ★ 프레임당 1회 (논문 §3.5)
	if (Params.bColor8 && !Params.bGatherApply && !Params.bGpuSolve)
	{
		OrderContactsColor8();
		if (Frame % 600 == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[color8] f%d 접촉 %d · 블록 안 배치 최대 %d · 평균 %.1f"), Frame, Contacts.Num(), LastMaxBatch, LastAvgBatches);
		}
	}
	ApgdRestarts = 0;

	const float SubDt = Dt / (float)Params.Substeps;
	for (int32 s = 0; s < Params.Substeps; ++s)
	{
		// ★ λn 과 λt 는 같은 범위에서 누적하고 그 안에서 클램프한다 (Catto 2005 / C4 §6).
		//
		// 범위를 프레임으로 두면 λn 은 서브스텝마다 무조건 쌓이는데 λt 는 실제로
		// 미끄러질 때만 쌓여서, 서브스텝·반복을 늘릴수록 예산 μ·λn 만 부풀었다
		// (실측: ss 5→20 에서 15.2° → 무한, 반복 1→16 에서 8° → 무한).
		// 스텝 하나를 범위로 잡고 그 안에서 반복을 돌리면 양쪽이 같은 횟수로 쌓인다.
		const bool bWarm = Params.WarmStart > 0.f && Params.bSequentialPositions;
		if (bWarm)
		{
			// 직전 λ 를 속도 임펄스로 먼저 적용한다. 위치는 아래 PredictPositions 가 v·h 로 옮기므로
			// 정지 접촉이면 중력 이동과 상쇄되어 드리프트가 거의 0 — 반복은 잔차만 고친다.
			for (FSandContact& C : Contacts)
			{
				C.LambdaN   *= Params.WarmStart;
				C.LambdaTan *= Params.WarmStart;
				C.LambdaT = 0.f;
				if (C.LambdaN <= 0.f) { C.LambdaN = 0.f; C.LambdaTan = FVector3f::ZeroVector; continue; }
				const FVector3f Diff = Pos[C.I] - Pos[C.J];
				const float Dist = Diff.Size();
				if (Dist < UE_KINDA_SMALL_NUMBER) { C.LambdaN = 0.f; C.LambdaTan = FVector3f::ZeroVector; continue; }
				// ★ 닿아 있는 쌍만 워밍한다. 살짝 떨어진 쌍의 λn 은 잔차(Kc/h·gap)가 작아 반복 안에 안 풀리고
				// 거리를 둔 반발 버팀대로 남는다 — 마찰과 무관한 전단 강도. 실측: μ=0 에서도 39~45° (#23)
				if (Dist > 2.f * Params.Radius + Params.WarmGapTol * Params.Radius) { C.LambdaN = 0.f; C.LambdaTan = FVector3f::ZeroVector; continue; }
				const FVector3f N = Diff / Dist;
				// 접선은 월드 벡터로 들고 있었다. 두 입자의 배치가 바뀌면 옛 접선에 법선 성분이 생기므로
				// 현재 접평면에 재투영하고, 원뿔도 지금의 λn 으로 다시 자른다
				FVector3f Tan = Params.bWarmTangent ? (C.LambdaTan - N * FVector3f::DotProduct(C.LambdaTan, N)) : FVector3f::ZeroVector;
				const float MaxTan = Params.Friction * C.LambdaN;
				const float TanLen = Tan.Size();
				if (TanLen > MaxTan) { Tan = (TanLen > UE_KINDA_SMALL_NUMBER) ? Tan * (MaxTan / TanLen) : FVector3f::ZeroVector; }
				C.LambdaTan = Tan;
				const FVector3f Impulse = N * C.LambdaN + Tan;
				Vel[C.I] += Impulse * InvMass[C.I];
				Vel[C.J] -= Impulse * InvMass[C.J];
			}
			for (int32 i = 0; i < Pos.Num(); ++i)
			{
				GroundLambdaN[i] = (Pos[i].Z <= Params.Radius) ? FMath::Max(0.f, GroundLambdaN[i] * Params.WarmStart) : 0.f;
				Vel[i].Z += GroundLambdaN[i] * InvMass[i];
			}
			FMemory::Memzero(GroundLambdaT.GetData(), GroundLambdaT.Num() * sizeof(float));

			// ★ 벽도 워밍한다. 벽만 매 서브스텝 0 에서 시작하면 벽에 눌린 층이 횡압 지지를 매번 다시 쌓아야 하고,
			// 그 잔차가 #24 의 크리프와 같은 기제로 벽 층을 흔든다 — 상자에 부은 60cm 블록이 창마다 50% 순환한 원인 (#29).
			// #18 에서 벽만 SP 규약에서 빠졌던 것과 같은 실수를 워밍에서 되풀이했다
			if (bWallsActive && bBoxScene)
			{
				const float LX = BoxHalf.X - Params.Radius;
				const float LY = BoxHalf.Y - Params.Radius;
				const float Tol = Params.WarmGapTol * Params.Radius;
				const FVector3f Ns[4] = { FVector3f(1.f, 0.f, 0.f), FVector3f(-1.f, 0.f, 0.f), FVector3f(0.f, 1.f, 0.f), FVector3f(0.f, -1.f, 0.f) };
				const bool bActive[4] = { !Tilt.bOpenDownhill, !Tilt.bOpenDownhill && !Tilt.bOpenXPlus, true, true };
				for (int32 i = 0; i < Pos.Num(); ++i)
				{
					if (InvMass[i] <= 0.f) { WallLambda[i] = FVector4f::Zero(); continue; }
					const float Gaps[4] = { Pos[i].X + LX, LX - Pos[i].X, Pos[i].Y + LY, LY - Pos[i].Y };
					for (int32 K = 0; K < 4; ++K)
					{
						const float L = WallLambda[i][K] * Params.WarmStart;
						if (!bActive[K] || Gaps[K] > Tol || L <= 0.f) { WallLambda[i][K] = 0.f; continue; }
						WallLambda[i][K] = L;
						Vel[i] += Ns[K] * (L * InvMass[i]);
					}
				}
			}
			else
			{
				FMemory::Memzero(WallLambda.GetData(), WallLambda.Num() * sizeof(FVector4f));
			}
		}
		else
		{
			for (FSandContact& C : Contacts)
			{
				C.LambdaN = 0.f;
				C.LambdaT = 0.f;
				C.LambdaTan = FVector3f::ZeroVector;
			}
			FMemory::Memzero(GroundLambdaN.GetData(), GroundLambdaN.Num() * sizeof(float));
			FMemory::Memzero(GroundLambdaT.GetData(), GroundLambdaT.Num() * sizeof(float));
			FMemory::Memzero(WallLambda.GetData(), WallLambda.Num() * sizeof(FVector4f));
		}

		// 결합 λ — 워밍이면 직전 값을 속도 임펄스로 먼저, 아니면 0. 접촉과 같은 규약 (#24)
		for (FSandBond& B : Bonds)
		{
			if (!bWarm) { B.Lambda = 0.f; continue; }
			B.Lambda *= Params.WarmStart;
			if (B.Lambda == 0.f) { continue; }
			const FVector3f Diff = Pos[B.I] - Pos[B.J];
			const float Dist = Diff.Size();
			if (Dist < UE_KINDA_SMALL_NUMBER) { B.Lambda = 0.f; continue; }
			const FVector3f Corr = (Diff / Dist) * (B.Lambda / SubDt);   // Δx = λ·w·∇C 를 속도로
			Vel[B.I] += Corr * InvMass[B.I];
			Vel[B.J] -= Corr * InvMass[B.J];
		}

		PredictPositions(SubDt);

		if (Params.bSequentialPositions)
		{
			for (FSandContact& C : Contacts)
			{
				C.PrevDLambdaN   = 0.f;
				C.PrevDLambdaTan = FVector3f::ZeroVector;
			}

			const bool bApgd = Params.bApgd && Params.bGatherApply && Params.bGatherContacts && Params.BlockSize <= 0.f;
			float ApgdTheta = 1.f;
			if (bApgd)
			{
				for (FSandContact& C : Contacts) { C.GammaN = C.LambdaN; C.GammaTan = C.LambdaTan; }
				for (FSandBond& B : Bonds) { B.Gamma = B.Lambda; }
			}
			const bool bTrace = (Frame == DebugTraceFrame && DebugTraceIndex >= 0 && DebugTraceIndex < Pos.Num());
			if (bTrace)
			{
				UE_LOG(LogTemp, Display, TEXT("[trace] f%d ss%d 예측 후  %s"), Frame, s, *DescribeParticle(DebugTraceIndex));
			}
			if (Params.bGpuSolve)
			{
				SolveIterationsGpu(SubDt);
			}
			else for (int32 it = 0; it < Params.Iterations; ++it)
			{
				if (bApgd)
				{
					// Nesterov: θ' = (−θ² + θ√(θ²+4))/2, β = θ(1−θ)/(θ²+θ')  (Heyn 2013 Alg. APGD)
					const float ThetaNew = 0.5f * (-ApgdTheta * ApgdTheta + ApgdTheta * FMath::Sqrt(ApgdTheta * ApgdTheta + 4.f));
					ApgdBeta = ApgdTheta * (1.f - ApgdTheta) / (ApgdTheta * ApgdTheta + ThetaNew);
					ApgdTheta = ThetaNew;
					ApgdRestartAcc = 0.0;
				}
				SolveContactsSP(SubDt, it, Params.Iterations);
				SolveBondsSP(SubDt);
				if (bApgd && ApgdRestartAcc > 0.0)
				{
					// 적응 재시작 (O'Donoghue–Candès) — 모멘텀이 오르막이면 y←γ, θ←1
					ApgdSnapToGamma(SubDt);
					ApgdTheta = 1.f;
					++ApgdRestarts;
				}
				SolveGroundSP(SubDt);
				SolveWallsSP(SubDt);
				if (bTrace)
				{
					const int32 k = DebugTraceIndex;
					UE_LOG(LogTemp, Display, TEXT("[trace] f%d ss%d it%3d  |v| %9.2f  v(%8.2f %8.2f %8.2f)  z %7.3f  gλ %8.3f"),
						Frame, s, it, Vel[k].Size(), Vel[k].X, Vel[k].Y, Vel[k].Z, Pos[k].Z, GroundLambdaN[k]);
				}
			}
			if (bApgd && !Params.bGpuSolve)
			{
				ApgdSnapToGamma(SubDt);   // 상태·λ 를 실현 가능한 γ 로 — 워밍도 이것을 물려받는다
			}
			if (bTrace)
			{
				UE_LOG(LogTemp, Display, TEXT("[trace] f%d ss%d 반복 후  %s"), Frame, s, *DescribeParticle(DebugTraceIndex));
			}
			BreakBonds();   // 반복이 끝난 자리에서 판정 — 도중에 지우면 뒤 반복이 다른 목록을 본다
			// ★ 속도 역산 없음 — v 는 솔버가 직접 냈다
		}
		else
		{
			for (int32 it = 0; it < Params.Iterations; ++it)
			{
				SolveContacts();
				if (!Params.bGaussSeidel)
				{
					ApplyDeltas();     // 입자마다 한 번 적용
					CommitLambdas();   // 적용된 만큼만 λ 에 기록
				}
				SolveWalls();
				SolveGround();
			}

			UpdateVelocities(SubDt);
			ApplyDynamicFriction(SubDt);   // 위치 풀이가 끝난 뒤 속도 패스
		}
	}

	if (VelocityDamping > 0.f)
	{
		const float K = 1.f - VelocityDamping;
		for (int32 i = 0; i < Vel.Num(); ++i) { if (InvMass[i] > 0.f) { Vel[i] *= K; } }
	}

	UpdateSettleState(Dt);

	++Frame;
	Time  += Dt;
	StepMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
}
