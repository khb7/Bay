# 동기 / 비동기 (sync / async)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

- **동기**: 요청하고 결과가 올 때까지 기다린다 → 스톨
- **비동기**: 요청만 걸어두고 진행. 몇 프레임 뒤 결과를 받는다 → 스톨 없음, 대신 값이 낡음

[GPU 리드백](gpu-readback.md)은 **항상 비동기로** 한다. 우리 용례(수분 UI, 파도 도달 판정, 소리 트리거)는
33~50ms [지연](latency.md)을 전부 허용한다.

```cpp
// 실제 패턴 — 요청해두고, 도착한 최신값 캐시를 읽는다
Field->SampleAsync(WatchPoints, [this](auto Vals){ Cache = Vals; });
if (Cache.W > 0.5f) { ... }   // 항상 2프레임 낡은 값을 즉시 읽음
```

---

**관련 문서** — [GPU 리드백 (`FRHIGPUBufferReadback`)](gpu-readback.md) · [Render Target / SRV / UAV](render-target-srv-uav.md) · [지연 (latency)](latency.md)
