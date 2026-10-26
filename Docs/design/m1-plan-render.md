# M1 — 화면 패스 지원: 모션 벡터 · Lumen · 레이트레이싱 (2026-10-27)

> m1-result §6 의 "Lumen · 하드웨어 레이트레이싱 · 모션 벡터 — 확인 안 함" 을 확인한 결과와 할 일.
> 설계 전제: papers L-5 — GI · 반사는 Lumen 이 해 준다.

## 확인한 것

| 항목 | 지금 | 근거 |
|---|---|---|
| 모션 벡터 | **없음** — 무너지는 중(f125)에도 모래 영역 속도 0 (`ShowFlag.VisualizeMotionVectors`). 정점이 버퍼 안에서 움직여서 엔진은 정지 물체로 본다 → TSR 잔상 | `img/m1-mv-f125.png` |
| 프로젝트 GI · 반사 | **꺼짐** — `DefaultEngine.ini` 에 렌더 설정이 없어 엔진 기본값 (`r.GenerateMeshDistanceFields` 0, RT 0). 지금까지 화면은 Lumen 없이 찍혔다 | 로그 "Ray tracing is disabled … r.RayTracing=0" |
| 모래가 Lumen 장면에 들어가는가 | 소프트웨어 Lumen 은 정적 메시의 거리장으로 장면을 만든다 → 동적 GPU 메시는 **화면 공간 추적에서만** 보인다 (화면 밖 GI · 반사에서 빠짐). 하드웨어 RT 는 동적 형상을 받는다 (Niagara 리본 · 스켈레탈의 `GetDynamicRayTracingInstances`) | papers C15 |

## 할 일

| 단계 | 내용 | 검증 |
|---|---|---|
| **R1** 모션 벡터 ✓ #30 | 정점 속도 = 입자 속도의 셰퍼드 보간 (W2 와 같은 커널) → 정점 UV 채널 (UV0 = vx vy, UV1 = vz ·). 재질 WPO = PreviousFrameSwitch(0, −v·Δt) — 엔진 정점 팩토리 그대로. 프록시 `bAlwaysHasVelocity` | 무너질 때 모래에 모션 벡터가 나오는가 · 쌓인 뒤 0 인가 · CPU 판과 정점 속도 합 일치 |
| **R2** Lumen 켜고 보기 ✓ #31 — 받기만 된다, Lumen 장면엔 없음 | 명령줄로만 (`r.DynamicGlobalIlluminationMethod 1 · r.ReflectionMethod 1`) 화면 비교 — 프로젝트 설정은 사용자 결정 | 모래가 GI 를 받는가 · 바닥에 그늘 · 반사가 지는가 |
| R3 하드웨어 RT | 프로젝트 RT 를 켜야 하고 (전 셰이더 재컴파일) 동적 RT 형상 경로가 필요 — **사용자 결정 대기** (#31) | Lumen 개요에 모래가 보이는가 · BLAS 갱신 ms |

## 하지 않는 것
- 정점 대응 (프레임 사이 같은 정점) — Surface Nets 정점은 셀에 붙어 있어 대응이 없다. 그래서 위치 차분이 아니라 **입자 속도**로 준다.
