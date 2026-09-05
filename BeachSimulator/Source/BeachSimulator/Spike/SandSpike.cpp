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
}

void FSandSpike::Init(const FSandParams& InParams, const FColumnSetup& InColumn)
{
	Params = InParams;
	Column = InColumn;

	CellSize = 2.f * Params.Radius;

	SpawnColumn();

	// 입자 수의 2배 이상인 2의 거듭제곱. 충돌은 거리 검사가 걸러내므로 정확도가 아니라 비용 문제다.
	TableSize = 1;
	while (TableSize < Pos.Num() * 2)
	{
		TableSize <<= 1;
	}
	CellStart.SetNumZeroed(TableSize + 1);
	SortedIdx.SetNumZeroed(Pos.Num());
	Delta.SetNumZeroed(Pos.Num());
	ContactCount.SetNumZeroed(Pos.Num());

	bWallsActive  = true;
	Frame         = 0;
	Time          = 0.f;
	SettledFrames = 0;
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

void FSandSpike::BuildHash()
{
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
	const float D  = 2.f * Params.Radius;
	const float D2 = D * D;

	FMemory::Memzero(Delta.GetData(), Delta.Num() * sizeof(FVector3f));
	FMemory::Memzero(ContactCount.GetData(), ContactCount.Num() * sizeof(int32));

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

				const FVector3f Diff = Pos[i] - Pos[j];
				const float Dist2 = Diff.SizeSquared();
				if (Dist2 >= D2 || Dist2 < UE_KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const float Dist  = FMath::Sqrt(Dist2);
				const float Depth = D - Dist;               // 침투량 = -C
				const FVector3f N = Diff / Dist;

				// 접촉 컴플라이언스 0 (강체). 질량이 같으므로 절반씩 나눈다
				FVector3f Corr = N * (0.5f * Depth);

				// 마찰 — Macklin 2014. 서브스텝 동안의 접선 상대변위를 μ·Depth 까지만 되돌린다.
				// 스냅샷 위치를 보므로 방금 계산한 법선 보정이 여기 섞이지 않는다
				const FVector3f Rel = (Pos[i] - PrevPos[i]) - (Pos[j] - PrevPos[j]);
				FVector3f Tang      = Rel - N * FVector3f::DotProduct(Rel, N);
				const float TLen    = Tang.Size();
				if (TLen > UE_KINDA_SMALL_NUMBER)
				{
					const float Slide = FMath::Min(TLen, Params.Friction * Depth);
					Corr -= Tang * (0.5f * Slide / TLen);
				}

				Delta[i] += Corr;
				Delta[j] -= Corr;
				++ContactCount[i];
				++ContactCount[j];
			}
		}
	}
}

void FSandSpike::ApplyDeltas()
{
	const float Omega = Params.Relaxation;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (ContactCount[i] > 0)
		{
			Pos[i] += Delta[i] * (Omega / (float)ContactCount[i]);
		}
	}
}

void FSandSpike::SolveWalls()
{
	if (!bWallsActive)
	{
		return;
	}

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
	const float R2 = Params.Radius;
	for (int32 i = 0; i < Pos.Num(); ++i)
	{
		if (Pos[i].Z >= R2)
		{
			continue;
		}
		const float Depth = R2 - Pos[i].Z;
		Pos[i].Z = R2;

		// 바닥 마찰. 입자간 마찰과 같은 규칙을 쓴다
		FVector3f Tang = Pos[i] - PrevPos[i];
		Tang.Z = 0.f;
		const float TLen = Tang.Size();
		if (TLen > UE_KINDA_SMALL_NUMBER)
		{
			const float Slide = FMath::Min(TLen, Params.Friction * Depth);
			Pos[i] -= Tang * (Slide / TLen);
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

	const float SubDt = Dt / (float)Params.Substeps;
	for (int32 s = 0; s < Params.Substeps; ++s)
	{
		PredictPositions(SubDt);
		BuildHash();
		for (int32 it = 0; it < Params.Iterations; ++it)
		{
			SolveContacts();   // 제안만 쌓는다 (Pos 읽기 전용)
			ApplyDeltas();     // 입자마다 한 번 적용
			SolveWalls();      // 경계는 입자당 하나뿐이라 경합이 없다. 바로 투영
			SolveGround();
		}
		UpdateVelocities(SubDt);
	}

	UpdateSettleState(Dt);

	++Frame;
	Time  += Dt;
	StepMs = (float)((FPlatformTime::Seconds() - T0) * 1000.0);
}
