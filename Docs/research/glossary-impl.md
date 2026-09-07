# 구현 용어집

물리 용어는 [glossary.md](glossary.md), 상수는 [params.md](params.md) 참조.
여기는 **엔진 / GPU / 시뮬레이션 구현** 쪽 용어만 모은다.

> 기준 엔진은 UE5지만, 개념은 대부분 엔진 무관하다.
> Unity 경험이 있다면 §1 대응표를 먼저 보는 게 빠르다.

---

> **용어 하나에 문서 하나.** 항목을 누르면 [terms/](terms/) 아래 개별 문서로 간다.

## 목차

**[1. UE5 ↔ Unity 대응](#1-ue5--unity-대응)**  
[1-1. Subsystem — 수명으로 고르는 싱글턴](terms/subsystem.md) · [헬퍼 클래스 / UE5 접두사](terms/helper-class.md)

**[2. GPU 실행 모델](#2-gpu-실행-모델)**  
[컴퓨트 셰이더 (compute shader)](terms/compute-shader.md) · [디스패치 (dispatch)](terms/dispatch.md) · [패스 (pass)](terms/pass.md) ·
[Simulation Stages (Niagara)](terms/simulation-stages.md) · [Custom HLSL 노드 (Niagara)](terms/custom-hlsl.md) · [핑퐁 (ping-pong)](terms/ping-pong.md) · [Render Target / SRV / UAV](terms/render-target-srv-uav.md) ·
[채널 패킹 (channel packing)](terms/channel-packing.md) · [uniform / 상수 버퍼](terms/uniform-buffer.md) · [CFL 조건 (Courant–Friedrichs–Lewy)](terms/cfl-condition.md) ·
[서브스텝 (substep)](terms/substep.md) · [파이프라인 스톨 (pipeline stall)](terms/pipeline-stall.md) · [동기 / 비동기 (sync / async)](terms/sync-async.md) ·
[지연 (latency)](terms/latency.md) · [배리어 (barrier)](terms/barrier.md) · [자원 상태 전이 (resource transition)](terms/resource-transition.md) ·
[RDG (Render Dependency Graph)](terms/rdg.md) · [GPU 리드백 (`FRHIGPUBufferReadback`)](terms/gpu-readback.md)

**[3. 격자 시뮬레이션](#3-격자-시뮬레이션)**  
[하이트필드 (heightfield)](terms/heightfield.md) · [레이어드 하이트필드 (layered heightfield)](terms/layered-heightfield.md) · [가상 파이프 (virtual pipes)](terms/virtual-pipes.md) · [반라그랑주 이류 (semi-Lagrangian advection)](terms/semi-lagrangian-advection.md) ·
[열침식 (thermal erosion)](terms/thermal-erosion.md) · [수력 침식 (hydraulic erosion)](terms/hydraulic-erosion.md) · [경계 조건 (boundary condition)](terms/boundary-condition.md)

**[4. 입자 시뮬레이션](#4-입자-시뮬레이션)**  
[PBD (Position Based Dynamics)](terms/pbd.md) · [XPBD (eXtended PBD)](terms/xpbd.md) · [제약 (constraint)](terms/constraint.md) · [쿨롱 마찰 (Coulomb friction)](terms/coulomb-friction.md) ·
[컴플라이언스 (compliance)](terms/compliance.md) · [라그랑주 승수 (Lagrange multiplier, λ)](terms/lagrange-multiplier.md) · [반복 (iteration) / 완화 (relaxation)](terms/iteration-relaxation.md) · [Jacobi vs Gauss-Seidel](terms/gauss-seidel-jacobi-sor.md) ·
[이웃 탐색 (neighbor search)](terms/neighbor-search.md) · [Neighbor Grid3D (Niagara) — 균일 격자](terms/neighbor-grid3d.md) ·
[공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택](terms/spatial-hash.md) · [radix sort](terms/radix-sort.md) · [prefix sum (scan)](terms/prefix-sum.md) ·
[리턴 매핑 (return mapping)](terms/return-mapping.md)

**[5. 표현과 렌더링](#5-표현과-렌더링)**  
[SDF (Signed Distance Field)](terms/sdf.md) · [표면 재구성 (surface reconstruction)](terms/surface-reconstruction.md) ·
[Marching Cubes / Surface Nets](terms/marching-cubes.md) · [WPO (World Position Offset)](terms/wpo.md) · [Virtual Heightfield Mesh (VHM)](terms/virtual-heightfield-mesh.md) ·
[깊이 버퍼 픽킹 (depth picking)](terms/depth-picking.md) · [플로우맵 (flow map)](terms/flow-map.md) ·
[렌더 스레드 / SceneProxy / MeshBatch](terms/render-thread-proxy.md) · [Vertex Factory](terms/vertex-factory.md) · [간접 드로우 (DrawIndirect)](terms/draw-indirect.md)

**[6. 자원 관리](#6-자원-관리)**  
[sleep / wake](terms/sleep-wake.md) · [동결 (freeze)](terms/freeze.md) · [예산 (budget)](terms/budget.md) · [스탬프 (stamp)](terms/stamp.md) ·
[결정론 (determinism)](terms/determinism.md) · [jitter](terms/jitter.md)

---

## 1. UE5 ↔ Unity 대응

| UE5 | Unity | 비고 |
|---|---|---|
| `AActor` | `GameObject` | 씬에 놓이는 것 |
| `UActorComponent` | `Component` (MonoBehaviour) | |
| `UWorldSubsystem` | 싱글턴 매니저 / `ScriptableObject` 매니저 | 월드 수명과 함께 자동 생성·소멸 |
| `UDataAsset` | `ScriptableObject` | 순수 데이터 애셋 |
| **Niagara** | **VFX Graph** | GPU 파티클 / 시뮬 시스템 |
| Niagara `Grid2D` | `RenderTexture` (RWTexture2D) | 격자 상태 저장소 |
| Niagara `Simulation Stages` | `ComputeShader.Dispatch()` 순차 호출 | 한 스텝 안의 패스 나누기 |
| Niagara `Neighbor Grid3D` | 직접 짠 공간 해시 | 입자 이웃 탐색 가속 |
| `UTextureRenderTarget2D` | `RenderTexture` | |
| Niagara `Custom HLSL` 노드 | 컴퓨트 셰이더 코드 직접 작성 | 노드로 다 그리지 말 것 (§2 함정) |
| **RDG** (Render Dependency Graph) | **SRP RenderGraph** (URP14+/HDRP) | `CommandBuffer`는 이보다 한 단계 낮음 |
| **WPO** (World Position Offset) | 버텍스 셰이더 정점 이동 | 머티리얼에서 메시를 변형 |
| `FRHIGPUBufferReadback` | `AsyncGPUReadback.Request()` | 비동기 GPU→CPU 읽기 |
| Blueprint | Visual Scripting / 인스펙터 | |

| 용어 | |
|---|---|
| **[1-1. Subsystem — 수명으로 고르는 싱글턴](terms/subsystem.md)** | Unity에서 `static Instance` + `DontDestroyOnLoad`로 하던 것을 엔진이 대신 관리한다. |
| **[헬퍼 클래스 / UE5 접두사](terms/helper-class.md)** | 작업을 대신 해주려고 존재하는 보조 클래스. `U`/`A`/`F`/`T` 접두사로 종류가 드러난다. |

## 2. GPU 실행 모델

| 용어 | |
|---|---|
| **[컴퓨트 셰이더 (compute shader)](terms/compute-shader.md)** | 렌더링과 무관하게 GPU에서 임의 계산을 돌리는 셰이더. |
| **[디스패치 (dispatch)](terms/dispatch.md)** | 컴퓨트 셰이더를 실행시키는 호출. |
| **[패스 (pass)](terms/pass.md)** | 한 스텝을 이루는 계산 단계 하나. |
| **[Simulation Stages (Niagara)](terms/simulation-stages.md)** | Niagara의 `Particle Update` 하나를 임의 개수의 커스텀 단계로 쪼개는 기능. |
| **[Custom HLSL 노드 (Niagara)](terms/custom-hlsl.md)** | 그래프 안에 HLSL을 직접 써넣는 노드. 코드가 `.uasset` 바이너리에 살아 diff가 안 되므로 `.ush`로 빼는 게 낫다. |
| **[핑퐁 (ping-pong)](terms/ping-pong.md)** | 텍스처 두 장을 번갈아 쓰는 기법. |
| **[Render Target / SRV / UAV](terms/render-target-srv-uav.md)** | `UTextureRenderTarget2D`는 GPU가 쓸 수 있는 텍스처. |
| **[채널 패킹 (channel packing)](terms/channel-packing.md)** | RGBA 4채널 텍스처 한 장에 서로 다른 물리량 4개를 욱여넣는 것. |
| **[uniform / 상수 버퍼](terms/uniform-buffer.md)** | 모든 스레드가 똑같이 읽는 값. |
| **[CFL 조건 (Courant–Friedrichs–Lewy)](terms/cfl-condition.md)** | 한 스텝 동안 정보가 셀 하나를 넘어가면 안 된다. |
| **[서브스텝 (substep)](terms/substep.md)** | 한 프레임을 여러 개의 작은 `dt`로 쪼개 도는 것. |
| **[파이프라인 스톨 (pipeline stall)](terms/pipeline-stall.md)** | CPU가 GPU를 기다리며 멈추는 것. |
| **[동기 / 비동기 (sync / async)](terms/sync-async.md)** | 동기: 요청하고 결과가 올 때까지 기다린다 → 스톨 비동기: 요청만 걸어두고 진행. |
| **[지연 (latency)](terms/latency.md)** | 값이 준비되기까지 걸리는 시간. |
| **[배리어 (barrier)](terms/barrier.md)** | GPU에게 "여기서 멈춰라" 라고 알리는 명령. |
| **[자원 상태 전이 (resource transition)](terms/resource-transition.md)** | 텍스처는 용도에 따라 GPU 내부의 실제 저장 방식이 다르다. |
| **[RDG (Render Dependency Graph)](terms/rdg.md)** | 렌더 패스를 즉시 실행하지 않고 그래프로 먼저 선언한 뒤 엔진이 최적화해 실행하는 UE5 시스템. |
| **[GPU 리드백 (`FRHIGPUBufferReadback`)](terms/gpu-readback.md)** | GPU 버퍼를 CPU로 가져오는 비동기 헬퍼. 스테이징 버퍼 + 펜스를 묶어 관리한다. |

## 3. 격자 시뮬레이션

| 용어 | |
|---|---|
| **[하이트필드 (heightfield)](terms/heightfield.md)** | *2D 격자의 각 칸이 높이값 하나만 들고 있는 지형 표현. |
| **[레이어드 하이트필드 (layered heightfield)](terms/layered-heightfield.md)** | 컬럼마다 높이 하나 대신 구간 리스트를 저장해 오버행을 값싸게 표현. 다만 붕괴는 여전히 컬럼 간 질량 이동이다. |
| **[가상 파이프 (virtual pipes)](terms/virtual-pipes.md)** | 인접한 두 셀이 파이프로 연결됐다고 보는 하이트필드 물 흐름 모델. |
| **[반라그랑주 이류 (semi-Lagrangian advection)](terms/semi-lagrangian-advection.md)** | `s_new(x) = s_old(x − v·dt)` — 속도를 거슬러 올라가 값을 가져옴. |
| **[열침식 (thermal erosion)](terms/thermal-erosion.md)** | 하이트필드에서의 안식각. |
| **[수력 침식 (hydraulic erosion)](terms/hydraulic-erosion.md)** | 물의 운반 능력 `C`와 현재 실린 모래 `s`를 비교해 깎거나 쌓는 것. |
| **[경계 조건 (boundary condition)](terms/boundary-condition.md)** | 격자 가장자리에서 값을 어떻게 정할지. |

## 4. 입자 시뮬레이션

| 용어 | |
|---|---|
| **[PBD (Position Based Dynamics)](terms/pbd.md)** | 힘을 계산하지 않고 위치를 직접 옮겨 제약을 만족시킨다. |
| **[XPBD (eXtended PBD)](terms/xpbd.md)** | PBD의 개량. 강성을 물리 단위로 지정 가능. |
| **[제약 (constraint)](terms/constraint.md)** | 입자들이 지켜야 할 조건. |
| **[쿨롱 마찰 (Coulomb friction)](terms/coulomb-friction.md)** | 마찰력의 한계가 수직항력에 비례한다. 등호가 아니라 부등호라 클램프로 구현된다. |
| **[컴플라이언스 (compliance)](terms/compliance.md)** | 강성의 역수 `α = 1/k`. |
| **[라그랑주 승수 (Lagrange multiplier, λ)](terms/lagrange-multiplier.md)** | 제약을 지키게 하려면 얼마나 세게 밀어야 하는가 — 그 세기. 곧 제약력이라 마찰·파단 판정에 읽어 쓴다. |
| **[반복 (iteration) / 완화 (relaxation)](terms/iteration-relaxation.md)** | 제약을 한 번 풀면 다른 제약이 깨지므로 여러 번 반복해 수렴시킨다. |
| **[Jacobi vs Gauss-Seidel](terms/gauss-seidel-jacobi-sor.md)** | 제약이 여러 개인데 서로 얽혀 있어 한 번에 다 만족시킬 수 없다 → 반복해서 오차를 줄인다. |
| **[이웃 탐색 (neighbor search)](terms/neighbor-search.md)** | 모든 입자 쌍을 검사하면 O(N²) — 20만 개면 400억 쌍이라 불가능하다. |
| **[Neighbor Grid3D (Niagara) — 균일 격자](terms/neighbor-grid3d.md)** | Niagara가 제공하는 이웃 탐색 구조. |
| **[공간 해시 (spatial hash) — 정렬 기반, 이 프로젝트의 선택](terms/spatial-hash.md)** | 자료구조를 만드는 게 아니라 입자 배열을 셀 순서로 정렬한다. |
| **[radix sort](terms/radix-sort.md)** | 비교하지 않는 정렬. 자릿수별로 나눠 담기를 반복한다. |
| **[prefix sum (scan)](terms/prefix-sum.md)** | `[3, 1, 4, 1]` → `[0, 3, 4, 8]`. |
| **[리턴 매핑 (return mapping)](terms/return-mapping.md)** | "허용 영역을 벗어나면 경계로 끌어당기고, 잘라낸 만큼을 영구 변형으로 기록"하는 소성 처리. |

## 5. 표현과 렌더링

| 용어 | |
|---|---|
| **[SDF (Signed Distance Field)](terms/sdf.md)** | 공간의 각 점에서 표면까지의 거리를 담은 필드. |
| **[표면 재구성 (surface reconstruction)](terms/surface-reconstruction.md)** | 입자 덩어리를 연속된 표면으로 그리는 것. |
| **[Marching Cubes / Surface Nets](terms/marching-cubes.md)** | SDF나 복셀 데이터를 삼각형 메시로 바꾸는 알고리즘. |
| **[WPO (World Position Offset)](terms/wpo.md)** | *좌표가 아니라 "얼마나 옮길지" — 원래 정점 위치에 더하는 이동량이다. |
| **[Virtual Heightfield Mesh (VHM)](terms/virtual-heightfield-mesh.md)** | Runtime Virtual Texture(RVT)에 담긴 높이맵을 읽어 GPU가 화면 적응 테셀레이션으로 그려주는 기능. |
| **[깊이 버퍼 픽킹 (depth picking)](terms/depth-picking.md)** | 커서 픽셀 하나의 깊이값을 읽어 월드 좌표로 되돌리는 것. |
| **[플로우맵 (flow map)](terms/flow-map.md)** | 속도장을 텍스처로 저장해 머티리얼에서 UV를 흐르게 만드는 것. |
| **[렌더 스레드 / SceneProxy / MeshBatch](terms/render-thread-proxy.md)** | 게임 스레드와 렌더 스레드는 분리되어 있고, 그리기는 SceneProxy가 맡는다. |
| **[Vertex Factory](terms/vertex-factory.md)** | 정점 데이터가 셰이더에 어떻게 전달되는지 기술하는 것. 커스텀 속성을 실으려면 필요. |
| **[간접 드로우 (DrawIndirect)](terms/draw-indirect.md)** | 그릴 개수를 CPU가 모를 때. 개수를 GPU 버퍼에 써두고 드로우가 그걸 읽는다. |

## 6. 자원 관리

| 용어 | |
|---|---|
| **[sleep / wake](terms/sleep-wake.md)** | 움직임이 멈춘 시뮬 대상을 계산에서 빼는 것. |
| **[동결 (freeze)](terms/freeze.md)** | 잠든 것을 한 단계 더 줄여 입자를 해제하고 정적 메시로 굳히는 것. |
| **[예산 (budget)](terms/budget.md)** | 동시에 유지할 수 있는 입자·타일 수의 상한. |
| **[스탬프 (stamp)](terms/stamp.md)** | 브러시를 시뮬에 적용하는 단위. |
| **[결정론 (determinism)](terms/determinism.md)** | 같은 입력이면 항상 같은 결과가 나오는 성질. |
| **[jitter](terms/jitter.md)** | 값을 의도적으로 조금씩 흔드는 것. |

## 7. 아직 안 나온 것 (설계 진행하며 추가)

- [ ] 입자↔격자 전환의 구체적 알고리즘 용어
- [ ] 표면 재구성의 구체 기법 (화면공간 vs 메시화) 비교
- [ ] 저장 포맷
- [ ] 풍성 침식(aeolian) 구현 용어
