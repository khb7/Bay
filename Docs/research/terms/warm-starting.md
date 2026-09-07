# 워밍 스타트 (warm starting)

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

**직전 프레임에서 구한 [λ](lagrange-multiplier.md)를 이번 프레임의 초기값으로 쓰는 것.**
0에서 시작하지 않는다.

```csharp
// 차가운 시작 — 매 프레임 처음부터
lambda = 0;

// 워밍 스타트 — 직전 프레임 값에서 이어서
lambda = prevLambda[contactKey];
ApplyImpulse(lambda);              // ★ 먼저 그만큼 밀어놓고 시작한다
```

## 왜 이득인가

[반복](iteration-relaxation.md)이 하는 일은 **"이 접촉이 위에서 오는 무게를 느끼게 하는 것"** 이다
([papers.md](../papers.md) §C3). 깊은 더미에서 [Jacobi](gauss-seidel-jacobi-sor.md)는 정보가 반복당 한 칸씩만
내려가므로 이 과정이 비싸다.

그런데 **정지한 더미의 답은 프레임이 바뀌어도 거의 같다.** 어제 구한 `λ`가 오늘의
정답에 이미 가까우니, 그 상태에서 시작하면 반복 몇 회를 통째로 되돌려 받는다.
= 수렴을 빠르게 하는 게 아니라 **수렴한 상태를 프레임 간에 보존**하는 것.

게임 물리의 표준 관행이다 — sequential impulse 계열(Box2D·Bullet·PhysX)이 전부 쓴다.
**Catto (2005), "Iterative Dynamics with Temporal Coherence"** 가 정본.

## ★ 이 프로젝트에서의 위치 — shock propagation의 대안

[papers.md](../papers.md) §C3 결론의 3번이 이것이다.

| | 하는 일 | 부작용 |
|---|---|---|
| **shock propagation** | 아래층을 무한 질량으로 굳혀 수렴을 강제 종료 | `λ`가 하중을 못 받은 상태면 **그 상태를 그대로 굳힌다.** 무게를 모르는 채 안 가라앉기만 함 |
| **워밍 스타트** | 무게를 느낀 상태를 **보존** | 논문이 걱정한 그 부작용이 없다 |

**shock은 버리고 굳히는 쪽, 워밍은 들고 가는 쪽이다.** 우리는 μ가 [안식각](angle-of-repose.md)에
정직하게 반영되는지를 재야 하므로 후자가 맞다.

> ⚠ **[Jacobi](gauss-seidel-jacobi-sor.md) 계열에선 기대치를 낮출 것.** Frâncu & Moldoveanu (2014) §7이
> *"도움은 되지만 Gauss-Seidel에서만큼 극적이지는 않다"* 고 명시한다. GS는 한 스윕 안에서
> 좋은 초기값이 뒤 제약으로 전파되는데 Jacobi는 그 경로가 없다.

## 전제 — 접촉이 프레임을 건너 식별돼야 한다

`prevLambda[contactKey]`가 성립하려면 **같은 접촉임을 알아봐야 한다**(persistent contact).
강체는 접촉점 매칭이 까다롭지만 우리는 쉽다 — 입자쌍이 곧 키다.

```
key = (min(i,j), max(i,j))        입자 ID 자체가 안정적인 식별자
```

다만 20만 입자에서 이 표를 프레임마다 들고 다니려면 [정렬 기반 공간 해시](spatial-hash.md)의
이웃 리스트에 `λ` 슬롯을 얹는 형태가 된다. **[결정론](determinism.md)에 직접 걸린다** —
이웃 순서가 프레임마다 흔들리면 워밍 값도 흔들린다.

## 주의

- **끊어진 접촉의 `λ`는 반드시 버린다.** 남겨두면 없는 힘이 계속 작용해 [크리프](creep.md)처럼 보인다
- [sleep/wake](sleep-wake.md)와 궁합이 좋다. 잠든 더미는 `λ`가 수렴한 상태라 깨울 때 그대로 재사용
- 충격이 들어온 프레임에는 옛 `λ`가 오히려 틀린 초기값이다. 보통 그래도 이득이지만
  **교정 실험에서 이상한 첫 프레임이 보이면 여기를 의심할 것**

---

**관련 문서** — [Gauss-Seidel / Jacobi / SOR ★](gauss-seidel-jacobi-sor.md) · [sleep / wake](sleep-wake.md) · [결정론 (determinism)](determinism.md) · [공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택](spatial-hash.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [반복 (iteration) / 완화 (relaxation)](iteration-relaxation.md) · [안식각 (angle of repose)](angle-of-repose.md) · [크리프 (creep)](creep.md)
