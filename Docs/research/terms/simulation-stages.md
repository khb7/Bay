# Simulation Stages (Niagara)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

Niagara의 `Particle Update` 하나를 **임의 개수의 커스텀 단계로 쪼개는** 기능.
단계마다 무엇을 순회할지, 몇 번 반복할지를 따로 정한다.

## 먼저 — Particle Update가 무엇인가

Niagara 이미터는 스크립트 4개로 구성되고, `Particle Update`는 그중
**매 틱마다 살아 있는 입자 전부에 대해 도는** 것이다.

```
Emitter Spawn    이미터 생성 시 1회
Emitter Update   매 틱 1회            ← 스폰 개수 결정
Particle Spawn   입자가 태어날 때 1회   ← 초기값
Particle Update  매 틱 × 입자 수       ← ★
```

GPU 시뮬이면 **[디스패치](dispatch.md) 한 번, 입자 하나당 스레드 하나**다.
즉 이 스크립트는 컴파일되어 [컴퓨트 셰이더](compute-shader.md)가 된다.

### 모듈 스택 = 위에서 아래로 실행되는 함수 목록

```
Particle Update
 ├ Gravity Force              → Particles.PhysicsForce += g * Mass
 ├ Drag                       → Particles.PhysicsForce += -k*v
 ├ Solve Forces and Velocity  ← 실제 적분이 여기서 일어난다
 └ Custom HLSL                ← 덩어리 코드는 여기
```

각 모듈은 **속성(attribute)을 읽고 쓰는 함수**다. 네임스페이스로 구분된다 —
`Particles.*`(입자마다 하나) · `Emitter.*` · `System.*` · `User.*`(외부 주입) · `Engine.DeltaTime`.

힘 모듈들은 `PhysicsForce`에 **누적만** 하고, `Solve Forces and Velocity`가 한 번에 적분한다.
그 안은 [준암시적 오일러](time-integration.md) 두 줄이다.

```
v += (PhysicsForce / Mass) * dt;   x += v * dt;
```

**모듈 순서가 곧 실행 순서다.** 힘 모듈을 Solve 아래에 두면 그 힘은 다음 프레임에나 반영된다.

## ★ 근본 제약 두 개 — [B]가 Niagara가 아닌 이유

**1. 입자는 자기 속성만 쓸 수 있다.**
다른 입자를 **읽는 것**은 Data Interface로 되지만(Neighbor Grid3D, Particle Attribute Reader),
**쓰는 것은 불가능하다.** 스레드는 자기 슬롯에만 쓴다. 그런데 [XPBD](xpbd.md) 제약 투영은
정의상 쌍의 양쪽을 동시에 민다.

```csharp
p1 += w1 * dLambda * n;
p2 -= w2 * dLambda * n;     // ← Niagara에서 이 줄이 불가능
```

우회로는 각 입자가 자기가 속한 [제약](constraint.md)을 모아 자기 몫만 계산하는 것인데,
그게 곧 [Jacobi](gauss-seidel-jacobi-sor.md)이고 Gauss-Seidel보다 수렴이 훨씬 느리다.

**2. 속성 버퍼가 이중 버퍼다.**
읽는 값은 **전 프레임 끝 상태**, 쓰기는 새 버퍼로 간다 — 입자 단위 [핑퐁](ping-pong.md)이다.
같은 스테이지 안에서 이웃의 갱신값을 볼 방법이 없으니 **Jacobi가 선택이 아니라 구조로 강제된다.**

```
격자 [A]  → Niagara       셀당 스레드. Jacobi가 원래 맞는 방식이라 제약이 손해가 아님
구조물 [B] → 커스텀 컴퓨트  XPBD 제약 투영이 제약 1과 정면충돌
```

**[분업 결정](../../sessions/02-design.md)의 실질적 근거가 여기다.**

## 스테이지 설정

| 설정 | 의미 |
|---|---|
| **Iteration Source** | 무엇을 순회하나 — `Particles` 또는 **`Data Interface`(Grid2D/3D)** |
| **Num Iterations** | 이 단계를 몇 번 반복할지 ([열침식](thermal-erosion.md)·[XPBD](xpbd.md) 반복에 유용) |
| Execute Behavior | 항상 / 리셋 시에만 / 조건부 |

```
Particle Update  =  Iteration Source가 Particles, Num Iterations가 1인 스테이지 하나
```

Simulation Stages는 그 둘을 열어준 **일반화**다.
`Iteration Source`를 `Grid2D`로 두면 스레드가 셀에 걸려 **입자가 0개여도 돈다.**
그 순간 파티클 시스템이 아니라 그냥 컴퓨트 셰이더고, **우리 9패스가 곧 스테이지 9개다.**

**자동으로 해주는 것**: [핑퐁](ping-pong.md) 스왑 / [디스패치](dispatch.md) 크기 계산 / RT export / 에디터 실시간 편집.

## 함정 다섯

1. 그래프 노드로 다 짜면 지옥이다. **[Custom HLSL 노드](custom-hlsl.md)**에 코드를 덩어리로 넣을 것
2. **중단점이 없다.** Niagara Debugger의 값 시각화만 가능
3. GPU 시뮬 전용 (`Sim Target = GPUComputeSim`)
4. **상태가 날아간다.** 리컴파일·스트리밍·컬링에서 리셋 → 누적되는 것을 여기 두지 말 것
5. **고정 dt를 직접 강제**해야 한다. 프레임레이트에 따라 침식량이 달라진다

---

**관련 문서** — [Custom HLSL 노드 (Niagara)](custom-hlsl.md) · [Gauss-Seidel / Jacobi / SOR ★](gauss-seidel-jacobi-sor.md) · [XPBD](xpbd.md) · [디스패치 (dispatch)](dispatch.md) · [시간 적분 (time integration) — 적분기의 종류](time-integration.md) · [열침식 (thermal erosion)](thermal-erosion.md) · [제약 (constraint)](constraint.md) · [컴퓨트 셰이더 (compute shader)](compute-shader.md) · [핑퐁 (ping-pong)](ping-pong.md)
