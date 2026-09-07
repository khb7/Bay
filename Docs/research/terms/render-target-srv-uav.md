# Render Target / SRV / UAV

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

`UTextureRenderTarget2D`는 **GPU가 쓸 수 있는 텍스처**. Unity의 `RenderTexture`와 같다.
(`enableRandomWrite = true` ↔ UAV 플래그)

| | `UTexture2D` | `UTextureRenderTarget2D` |
|---|---|---|
| 출처 | 디스크 이미지 | **런타임 생성** |
| GPU | 읽기만 | **읽기 + 쓰기** |
| CPU 접근 | 원본 있음 | **없음** ([비동기](sync-async.md) 리드백 필요) |

## SRV / UAV — 텍스처의 종류가 아니라 바인딩 방식

**같은 텍스처를 셰이더에 "어떤 권한으로" 연결할지를 정하는 꼬리표다.**

| | 풀네임 | 권한 |
|---|---|---|
| **SRV** | Shader Resource **View** | **읽기 전용** |
| **UAV** | **Unordered** Access **View** | **읽기 + 쓰기** |

`View`가 핵심이다 — 메모리는 하나인데 **들여다보는 창**을 다르게 만드는 것이다.
한 텍스처에 대해 SRV도 UAV도 만들 수 있다. 다만 동시에 바인딩은 못 한다(아래).

```hlsl
Texture2D<float4>   Src;   // SRV — 읽기만
RWTexture2D<float4> Dst;   // UAV — 쓰기 가능
```
HLSL에서 **`RW` 접두사가 붙은 타입이 곧 UAV**다.

### SRV — 읽기만 한다고 약속해서 얻는 것

GPU가 공격적으로 최적화한다. 텍스처 캐시에 태우고, 여러 코어가 같은 데이터를 마음껏
공유하고, 필터링(bilinear)·밉맵 같은 **텍스처 유닛 전용 하드웨어**를 쓸 수 있다.

### UAV — "Unordered"가 경고다

수천 스레드가 동시에 `Dst[uv] = value`를 실행하는데 **누가 먼저 쓸지 보장이 없다.**
자기 픽셀에만 쓰면 안전하지만, 여러 스레드가 같은 주소를 건드리면 결과가 비결정적이 된다.

```hlsl
Counter[0] += 1;                 // ✗ 읽고-더하고-쓰는 사이에 딴 스레드가 끼어든다
InterlockedAdd(Counter[0], 1);   // ✓ 원자적 연산
```

Unity의 `RWStructuredBuffer`·`InterlockedAdd`와 같은 것이고,
Burst에서 `NativeArray` 병렬 쓰기에 조심하던 것과 정확히 같은 종류의 문제다.

### 왜 동시에 못 붙이나 → [핑퐁](ping-pong.md)

읽는 쪽이 캐시된 옛 값을 볼지 방금 쓰인 값을 볼지 하드웨어가 보장할 수 없다.
**GPU 캐시는 CPU처럼 코어 간 코히런시를 유지하지 않는다.**
그래서 텍스처 두 장을 두고 매 패스마다 역할을 바꾼다.

```
패스 N    A(SRV, 읽기) → B(UAV, 쓰기)
패스 N+1  B(SRV, 읽기) → A(UAV, 쓰기)
```

[system-design](../../design/system-design.md)의 "메모리 100MB(핑퐁 포함)"가 이 두 배다.

## UE ↔ Unity

| | Unity | UE5 |
|---|---|---|
| 텍스처 생성 | `RenderTexture` | `UTextureRenderTarget2D` |
| UAV 켜기 | `enableRandomWrite = true` | `TexCreate_UAV` 플래그 |
| 전환 처리 | 드라이버가 알아서 | **[RDG](rdg.md)가 [자원 상태 전이](resource-transition.md)를 자동 삽입** |

SRV↔UAV 전환에는 배리어(barrier)라는 GPU 동기화 명령이 필요하다 —
"앞 패스의 쓰기가 다 끝날 때까지 기다려라". **빼먹으면 조용히 틀린 결과가 나오고
GPU마다 증상이 다르다.** RDG는 패스 간 의존성 그래프를 보고 이걸 자동으로 넣어준다.
M0-b에서 RDG를 넘어야 하는 이유이자, 넘고 나면 얻는 것.

## 포맷 정밀도 (높이 60m 범위 기준 오차)

| 포맷 | 오차 | 판정 |
|---|---|---|
| RGBA8 | 23 cm | 못 씀 |
| RGBA16F | 약 6 cm | 부족 (입자 지름이 5cm) |
| **RGBA32F** | 무시 가능 | **이걸 쓴다.** 512² 한 장 = 4MB |

---

**관련 문서** — [RDG (Render Dependency Graph)](rdg.md) · [동기 / 비동기 (sync / async)](sync-async.md) · [자원 상태 전이 (resource transition)](resource-transition.md) · [핑퐁 (ping-pong)](ping-pong.md) · [하이트필드 (heightfield) ★](heightfield.md)
