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

	SpawnColumn();

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
	GroundLambdaN.SetNumZeroed(Pos.Num());
	GroundLambdaT.SetNumZeroed(Pos.Num());

	bWallsActive  = true;
	Frame         = 0;
	Time          = 0.f;
	SettledFrames = 0;
	LastMaxSpeed  = 0.f;
}

void FSandSpike::SpawnColumn()
{
	const float R2 = Params.Radius;
	const float S  = 2.f * R2;   // 격자 간격 = 접촉 거리. 처음부터 서로 닿아 있다

	float R = Column.Radius;
	float H = Column.Height;
	if (R <= 0.f)
	{
		// H/R = 1 에서 목표 개수를 담는 반지름. 단순 입방 충전 기준 (N = πR²H / S³)
		R = FMath::Pow((float)Column.TargetCount * S * S * S / UE_PI, 1.f / 3.f);
		H = R;
	}
	WallRadius     = R;
	Column.Radius  = R;   // 역산한 값을 되돌려 놓는다. 로그·측정이 이걸 본다
	Column.Height  = H;

	const float FillR = R - R2;
	const int32 Lat   = FMath::CeilToInt(FillR / S);

	FRandomStream Rng(Params.Seed);
	const float Jitter = 0.05f * R2;   // 격자 충전의 인공적인 대칭만 깬다

	Pos.Reset();
	for (int32 iz = 0; iz < FMath::CeilToInt((H - R2) / S); ++iz)
	{
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
					X + Rng.FRandRange(-Jitter, Jitter),
					Y + Rng.FRandRange(-Jitter, Jitter),
					R2 + iz * S + Rng.FRandRange(-Jitter, Jitter)));
			}
		}
	}

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
	const FVector3f G(0.f, 0.f, Params.Gravity);
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		PrevPos[i] = Pos[i];
		Vel[i]    += G * SubDt;
		Pos[i]    += Vel[i] * SubDt;
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

		// 법선 — α = 0, w_i = w_j = 1 이므로 Δλ = d/2, 보정은 절반씩
		const float DLambdaN = 0.5f * Depth;
		C.LambdaN += DLambdaN;

		FVector3f Corr = N * DLambdaN;

		// 정지마찰 — 접선 상대변위를 통째로 지운다. 단 λt < μs·λn 일 때만 (논문 §3.5)
		if (C.LambdaT < Params.Friction * C.LambdaN)
		{
			const FVector3f Dp = (Pos[i] - PrevPos[i]) - (Pos[j] - PrevPos[j]);
			const FVector3f Dpt = Dp - N * FVector3f::DotProduct(Dp, N);
			const float TLen = Dpt.Size();
			if (TLen > UE_KINDA_SMALL_NUMBER)
			{
				const float DLambdaT = 0.5f * TLen;
				C.LambdaT += DLambdaT;
				Corr -= Dpt * 0.5f;
			}
		}

		Delta[i] += Corr;
		Delta[j] -= Corr;
		++DeltaCount[i];
		++DeltaCount[j];
	}
}

void FSandSpike::ApplyDeltas()
{
	const float Omega = Params.Relaxation;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (DeltaCount[i] > 0)
		{
			Pos[i] += Delta[i] * (Omega / (float)DeltaCount[i]);
		}
	}
}

void FSandSpike::SolveWalls()
{
	if (!bWallsActive)
	{
		return;
	}

	// 형틀은 물성이 아니라 비계다. 마찰 없이 반지름만 묶는다
	const float Limit = WallRadius - Params.Radius;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
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
		if (Pos[i].Z >= R2)
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
		Vel[i] = (Pos[i] - PrevPos[i]) * Inv;
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

		// w_i = w_j = 1 → 임펄스를 절반씩
		Vel[i] += DV * 0.5f;
		Vel[j] -= DV * 0.5f;
	}

	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (GroundLambdaN[i] <= 0.f)
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
	double SumSpeed = 0.0;
	float  MaxSpeed = 0.f;
	for (const FVector3f& V : Vel)
	{
		const float S = V.Size();
		SumSpeed += S;
		MaxSpeed  = FMath::Max(MaxSpeed, S);
	}
	LastAvgSpeed = Vel.Num() > 0 ? (float)(SumSpeed / Vel.Num()) : 0.f;
	LastMaxSpeed = MaxSpeed;

	// §2-2 — 평균 |v| < 0.01 × (r/dt) 가 60프레임 연속. 반경으로 정규화해 해상도에 무관하다
	const float Threshold = 0.01f * (Params.Radius / Dt);
	SettledFrames = (LastAvgSpeed < Threshold) ? SettledFrames + 1 : 0;
}

void FSandSpike::Step(float Dt)
{
	const double T0 = FPlatformTime::Seconds();

	BuildContacts(Dt);   // ★ 프레임당 1회 (논문 §3.5)

	const float SubDt = Dt / (float)Params.Substeps;
	// λ 는 접촉이 만들어질 때 0에서 시작해 서브스텝들에 걸쳐 누적된다.
	//
	// 논문 §3.5의 "At each substep ... we initialize λn and λt with zero"를
	// 서브스텝마다 리셋으로 읽으면 게이트 λt < μs·λn 이 첫 적용에서 항상 참이 되어
	// (λt = 0) μ 가 검사에 참여하지 못한다 — 실측으로 μ 0.3~1.0 에서 각이 평평했다.
	// 누적으로 읽어야 λt 가 μs·λn 을 넘는 시점이 생기고 μ 가 그 시점을 정한다.
	for (FSandContact& C : Contacts)
	{
		C.LambdaN = 0.f;
		C.LambdaT = 0.f;
	}
	FMemory::Memzero(GroundLambdaN.GetData(), GroundLambdaN.Num() * sizeof(float));
	FMemory::Memzero(GroundLambdaT.GetData(), GroundLambdaT.Num() * sizeof(float));

	for (int32 s = 0; s < Params.Substeps; ++s)
	{
		PredictPositions(SubDt);

		for (int32 it = 0; it < Params.Iterations; ++it)
		{
			SolveContacts();   // 제안만 쌓는다 (Pos 읽기 전용)
			ApplyDeltas();     // 입자마다 한 번 적용
			SolveWalls();
			SolveGround();
		}

		UpdateVelocities(SubDt);
		ApplyDynamicFriction(SubDt);   // 위치 풀이가 끝난 뒤 속도 패스
	}

	UpdateSettleState(Dt);

	++Frame;
	Time  += Dt;
	StepMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
}
