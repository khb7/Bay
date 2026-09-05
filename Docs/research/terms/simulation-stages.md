# Simulation Stages (Niagara)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

Niagara의 `Particle Update` 하나를 **임의 개수의 커스텀 단계로 쪼개는** 기능.
단계마다 무엇을 순회할지, 몇 번 반복할지를 따로 정한다.

| 설정 | 의미 |
|---|---|
| **Iteration Source** | 무엇을 순회하나 — `Particles` 또는 **`Data Interface`(Grid2D/3D)** |
| **Num Iterations** | 이 단계를 몇 번 반복할지 ([열침식](thermal-erosion.md)·[XPBD](xpbd.md) 반복에 유용) |
| Execute Behavior | 항상 / 리셋 시에만 / 조건부 |

`Iteration Source`를 `Grid2D`로 두면 **셀 하나당 스레드 하나**가 되어,
파티클 시스템이 아니라 그냥 [컴퓨트 셰이더](compute-shader.md)가 된다. 우리 9패스가 곧 스테이지 9개다.

**자동으로 해주는 것**: [핑퐁](ping-pong.md) 스왑 / [디스패치](dispatch.md) 크기 계산 / RT export / 에디터 실시간 편집.

**함정 다섯**
1. 그래프 노드로 다 짜면 지옥이다. **Custom HLSL 노드**에 코드를 덩어리로 넣을 것
2. **중단점이 없다.** Niagara Debugger의 값 시각화만 가능
3. GPU 시뮬 전용 (`Sim Target = GPUComputeSim`)
4. **상태가 날아간다.** 리컴파일·스트리밍·컬링에서 리셋 → 누적되는 것을 여기 두지 말 것
5. **고정 dt를 직접 강제**해야 한다. 프레임레이트에 따라 침식량이 달라진다

---

**관련 문서** — [XPBD](xpbd.md) · [디스패치 (dispatch)](dispatch.md) · [열침식 (thermal erosion)](thermal-erosion.md) · [컴퓨트 셰이더 (compute shader)](compute-shader.md) · [핑퐁 (ping-pong)](ping-pong.md)
