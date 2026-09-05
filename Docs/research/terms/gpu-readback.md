# GPU 리드백 (`FRHIGPUBufferReadback`)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

GPU 버퍼 내용을 CPU로 가져오는 **[비동기](sync-async.md) 헬퍼 클래스**.
[동기 / 비동기](sync-async.md)에서 말한 `SampleAsync`의 실제 구현체다.

## 왜 클래스가 따로 필요한가

GPU 메모리는 CPU가 직접 못 읽는다. 두 단계를 거쳐야 한다.

```
GPU 전용 메모리 ──복사──▶ 스테이징 버퍼 ──매핑──▶ CPU가 읽음
                          (CPU 접근 가능 메모리)
```

그리고 그 복사가 **언제 끝났는지** 알아야 한다. 이걸 알려주는 것이 **펜스(fence)** —
"GPU가 여기까지 처리했나"를 CPU가 확인할 수 있게 하는 동기화 객체.

`FRHIGPUBufferReadback`은 스테이징 버퍼와 펜스를 한 덩어리로 묶어 관리한다.

## 사용 패턴

```cpp
FRHIGPUBufferReadback Readback(TEXT("SandSample"));

// ── 렌더 스레드: 복사 명령을 큐에 넣는다 ──
Readback.EnqueueCopy(RHICmdList, SourceBuffer, NumBytes);

// ── 이후 프레임들: 준비됐나 확인 후 읽는다 ──
if (Readback.IsReady())                          // 펜스 확인 — 기다리지 않음
{
    const void* Data = Readback.Lock(NumBytes);  // CPU 주소 공간에 매핑
    FMemory::Memcpy(Cache.GetData(), Data, NumBytes);
    Readback.Unlock();
}
```

`IsReady()`가 핵심이다. **"됐으면 읽고 아니면 넘어간다"** 이지 기다리지 않는다.
텍스처용으로는 `FRHIGPUTextureReadback`이 같은 패턴으로 있다.

Unity의 `AsyncGPUReadback.Request()`는 이 전부(스테이징 버퍼·펜스·상태 관리)를
콜백 하나로 감싼 것. UE5는 좀 더 날것이라 직접 다뤄야 한다.

## 함정 셋

### ① `IsReady()` 없이 `Lock()` 하면 스톨
준비 안 된 상태에서 잠그면 CPU가 GPU를 기다린다 → [파이프라인 스톨](pipeline-stall.md).
비동기로 만든 의미가 사라지고 오히려 느려진다.

### ② 매 프레임 요청하려면 링 버퍼
객체 하나로는 "복사 중인데 또 복사 명령"이 된다. 2~3개를 순환시킨다.

```cpp
TStaticArray<FRHIGPUBufferReadback*, 3> Ring;
int32 WriteIdx = 0;

void Tick()
{
    Ring[WriteIdx]->EnqueueCopy(...);            // 지금 프레임 요청
    WriteIdx = (WriteIdx + 1) % 3;
    if (Ring[WriteIdx]->IsReady()) { /* 3프레임 전 요청 결과 */ }
}
```
하나는 복사 중, 하나는 대기 중, 하나는 읽는 중.

### ③ 크기를 작게 유지할 것
PCIe를 통과하는 전송이라 대역폭이 아깝다. **512² RGBA32F 전체는 4MB** —
매 프레임 가져오면 낭비다.

**이 프로젝트의 패턴 — 모으기 [패스](pass.md) + 작은 리드백**
```
① CPU가 관심 지점 목록을 GPU에 넘김       (커서 1점 + 활성 타일 중심 수십 점)
② 컴퓨트 셰이더가 그 지점들만 작은 버퍼에 모음  (64점 × 4채널 = 1 KB)
③ 그 1KB만 리드백
```
4MB가 아니라 1KB. 전송 시간이 사실상 0이 되고, [지연](latency.md)은 순수하게 프레임 대기분만 남는다.

## 어디에 쓰나

| 상황 | 읽는 값 | 왜 CPU가 알아야 하나 |
|---|---|---|
| 양동이로 물 뜨기 | `d` | 여기 물이 있나? 없으면 도구가 실패해야 함 |
| 타일 깨우기 판정 | `d`, `v` | 잠든 타일에 파도가 닿았나 → [sleep / wake](sleep-wake.md) |
| UI 피드백 | `W`, `depthToWater` | "이 자리 수분 62%" |
| 소리 · VFX | `v`, `d` | 파도가 부서지는 위치 |

**커서 위치는 여기 포함되지 않는다.** 지형·입자·물이 서로 다른 시스템이라
[깊이 버퍼 픽킹](depth-picking.md)으로 따로 처리한다.

---

**관련 문서** — [sleep / wake](sleep-wake.md) · [간접 드로우 (DrawIndirect)](draw-indirect.md) · [깊이 버퍼 픽킹 (depth picking)](depth-picking.md) · [동기 / 비동기 (sync / async)](sync-async.md) · [지연 (latency)](latency.md) · [파이프라인 스톨 (pipeline stall)](pipeline-stall.md) · [패스 (pass)](pass.md) · [헬퍼 클래스 / UE5 접두사](helper-class.md)
