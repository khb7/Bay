#include "SandSpike.h"

#include "Math/RandomStream.h"

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
	AllocateBuffers();
}

void FSandSpike::InitTiltBox(const FSandParams& InParams, const FTiltSetup& InTilt)
{
	Params = InParams;
	Tilt   = InTilt;

	bBoxScene = true;
	SpawnTiltBox();
	AllocateBuffers();
}

void FSandSpike::SaveState(FSandState& Out) const
{
	Out.Pos     = Pos;
	Out.Vel     = Vel;
	Out.TiltDeg = TiltDeg;
}

void FSandSpike::RestoreState(const FSandState& In)
{
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

void FSandSpike::AllocateBuffers()
{
	// 입자 수의 2배 이상인 2의 거듭제곱. 충돌은 거리 검사가 걸러내므로 정확도가 아니라 비용 문제다
	TableSize = 1;
	while (TableSize < Pos.Num() * 2)
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
	WallLambdaN.SetNumZeroed(Pos.Num());

	bWallsActive  = true;
	Frame         = 0;
	Time          = 0.f;
	SettledFrames = 0;
	LastMaxSpeed  = 0.f;
	TiltDeg       = 0.f;
	GravityDir    = FVector3f(0.f, 0.f, -1.f);
}

void FSandSpike::SpawnTiltBox()
{
	const float R2 = Params.Radius;
	const float S  = 2.f * R2;

	// 층수를 고정하고 한 변을 역산한다
	const int32 PerLayer = FMath::Max(1, Tilt.TargetCount / FMath::Max(1, Tilt.Layers));
	const int32 Side     = FMath::Max(2, FMath::RoundToInt(FMath::Sqrt((float)PerLayer)));

	BoxHalf = FVector3f(Side * S * 0.5f, Side * S * 0.5f, 0.f);

	FRandomStream Rng(Params.Seed);
	const float Jitter = 0.05f * R2;

	// 고정층(거친 바닥)을 먼저 넣어 인덱스 앞쪽에 모은다
	Pos.Reset();
	InvMass.Reset();
	for (int32 iz = 0; iz < Tilt.Layers; ++iz)
	{
		const bool bFixed = iz < Tilt.FixedLayers;
		for (int32 iy = 0; iy < Side; ++iy)
		for (int32 ix = 0; ix < Side; ++ix)
		{
			const float X = (ix - (Side - 1) * 0.5f) * S;
			const float Y = (iy - (Side - 1) * 0.5f) * S;
			const float J = bFixed ? 0.f : Jitter;   // 바닥은 흔들지 않는다
			Pos.Add(FVector3f(
				X + Rng.FRandRange(-J, J),
				Y + Rng.FRandRange(-J, J),
				R2 + iz * S + Rng.FRandRange(-J, J)));
			InvMass.Add(bFixed ? 0.f : 1.f);
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

	Pos.Reset();
	InvMass.Reset();
	for (int32 iz = 0; iz < FMath::CeilToInt((H - R2) / S); ++iz)
	{
		// 바닥 몇 층은 고정해 거칠게 만든다. 매끄러운 지면 위에서는 밑동이 미끄러져 각이 낮아진다
		const bool bFixed = iz < Column.FixedLayers;
		const float J = bFixed ? 0.f : Jitter;
		for (int32 iy = -Lat; iy <= Lat; ++iy)
		{
			for (int32 ix = -Lat; ix <= Lat; ++ix)
			{
				const float X = ix * S;
				const float Y = iy * S;
				if (X * X + Y * Y > FillR * FillR)
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

	// 여유 = 2 × (한 프레임에 움직일 수 있는 거리). 반경의 [0.1, 2]배로 묶는다
	const float Margin = FMath::Clamp(SafetyMultiplier * LastMaxSpeed * Dt,
		0.1f * Params.Radius, 2.f * Params.Radius);
	const float Range  = D + Margin;
	const float Range2 = Range * Range;

	BuildHash(Range);

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
					Contacts.Add({ i, j, 0.f, 0.f });
				}
			}
		}
	}
}

void FSandSpike::PredictPositions(float SubDt)
{
	const FVector3f G = GravityDir * FMath::Abs(Params.Gravity);
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

	const float Beta = (Params.MomentumA > 0.f && MaxIter > 1)
		? FMath::Min(Params.MomentumA * FMath::Pow((float)Iter / (float)MaxIter, Params.MomentumB), 1.f)
		: 0.f;

	for (FSandContact& C : Contacts)
	{
		const int32 i = C.I;
		const int32 j = C.J;
		const float Wi = InvMass[i];
		const float Wj = InvMass[j];
		const float WSum = Wi + Wj;
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
			continue;
		}

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
		const float MaxTan = Params.Friction * C.LambdaN;
		const float TanLen = NewTan.Size();
		if (TanLen > MaxTan)
		{
			NewTan = (TanLen > UE_KINDA_SMALL_NUMBER) ? NewTan * (MaxTan / TanLen) : FVector3f::ZeroVector;
		}
		C.LambdaTan = NewTan;
		DLambdaTan  = NewTan - OldTan;
		C.PrevDLambdaTan = DLambdaTan;

		// --- 적용: v 와 x 를 같은 임펄스로 ---
		const FVector3f Impulse = N * DLambdaN + DLambdaTan;
		Vel[i] += Impulse * Wi;
		Vel[j] -= Impulse * Wj;
		Pos[i] += Impulse * (Wi * StepDt);
		Pos[j] -= Impulse * (Wj * StepDt);
	}
}

void FSandSpike::SolveWallsSP(float StepDt)
{
	if (!bWallsActive)
	{
		return;
	}

	const float InvH = 1.f / StepDt;

	// 마찰 없는 단방향 제약. 형틀은 물성이 아니라 비계다
	auto Push = [&](int32 i, const FVector3f& N, float Gap)
	{
		const float Wi = InvMass[i];
		if (Wi <= 0.f)
		{
			return;
		}
		if (Gap > 0.f && WallLambdaN[i] <= 0.f)
		{
			WallLambdaN[i] = 0.f;
			return;
		}

		const float ResN = Params.Kv * FVector3f::DotProduct(Vel[i], N) + (Params.Kc * InvH) * Gap;
		float DLambdaN = -(Params.SorOmega / Wi) * ResN;

		const float OldN = WallLambdaN[i];
		WallLambdaN[i] = FMath::Max(0.f, WallLambdaN[i] + DLambdaN);
		DLambdaN = WallLambdaN[i] - OldN;

		Vel[i] += N * (DLambdaN * Wi);
		Pos[i] += N * (DLambdaN * Wi * StepDt);
	};

	if (bBoxScene)
	{
		const float LX = BoxHalf.X - Params.Radius;
		const float LY = BoxHalf.Y - Params.Radius;
		for (int32 i = 0; i < Pos.Num(); ++i)
		{
			if (!Tilt.bOpenDownhill)
			{
				Push(i, FVector3f( 1.f, 0.f, 0.f), Pos[i].X + LX);
				Push(i, FVector3f(-1.f, 0.f, 0.f), LX - Pos[i].X);
			}
			Push(i, FVector3f(0.f,  1.f, 0.f), Pos[i].Y + LY);
			Push(i, FVector3f(0.f, -1.f, 0.f), LY - Pos[i].Y);
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
		Push(i, N, Limit - Rad);
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
				Pos[i].X = FMath::Clamp(Pos[i].X, -LX, LX);
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
	LastAvgSpeed = Free > 0 ? (float)(SumSpeed / Free) : 0.f;
	LastMaxSpeed = MaxSpeed;

	// §2-2 — 평균 |v| < ε·√(g·d) 가 60프레임 연속.
	// dt 가 들어가지 않으므로 시간 조각을 바꿔도 판정선이 안 움직인다
	const float Threshold = Params.SettleEpsilon *
		FMath::Sqrt(FMath::Abs(Params.Gravity) * 2.f * Params.Radius);
	SettledFrames = (LastAvgSpeed < Threshold) ? SettledFrames + 1 : 0;
}

void FSandSpike::Step(float Dt)
{
	const double T0 = FPlatformTime::Seconds();

	BuildContacts(Dt);   // ★ 프레임당 1회 (논문 §3.5)

	const float SubDt = Dt / (float)Params.Substeps;
	for (int32 s = 0; s < Params.Substeps; ++s)
	{
		// ★ λn 과 λt 는 같은 범위에서 누적하고 그 안에서 클램프한다 (Catto 2005 / C4 §6).
		//
		// 범위를 프레임으로 두면 λn 은 서브스텝마다 무조건 쌓이는데 λt 는 실제로
		// 미끄러질 때만 쌓여서, 서브스텝·반복을 늘릴수록 예산 μ·λn 만 부풀었다
		// (실측: ss 5→20 에서 15.2° → 무한, 반복 1→16 에서 8° → 무한).
		// 스텝 하나를 범위로 잡고 그 안에서 반복을 돌리면 양쪽이 같은 횟수로 쌓인다.
		for (FSandContact& C : Contacts)
		{
			C.LambdaN = 0.f;
			C.LambdaT = 0.f;
		}
		FMemory::Memzero(GroundLambdaN.GetData(), GroundLambdaN.Num() * sizeof(float));
		FMemory::Memzero(GroundLambdaT.GetData(), GroundLambdaT.Num() * sizeof(float));
		FMemory::Memzero(WallLambdaN.GetData(), WallLambdaN.Num() * sizeof(float));

		PredictPositions(SubDt);

		if (Params.bSequentialPositions)
		{
			for (FSandContact& C : Contacts)
			{
				C.LambdaTan      = FVector3f::ZeroVector;
				C.PrevDLambdaN   = 0.f;
				C.PrevDLambdaTan = FVector3f::ZeroVector;
			}

			for (int32 it = 0; it < Params.Iterations; ++it)
			{
				SolveContactsSP(SubDt, it, Params.Iterations);
				SolveGroundSP(SubDt);
				SolveWallsSP(SubDt);
			}
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

	UpdateSettleState(Dt);

	++Frame;
	Time  += Dt;
	StepMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
}
