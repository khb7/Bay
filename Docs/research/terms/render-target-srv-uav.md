# Render Target / SRV / UAV

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

`UTextureRenderTarget2D`는 **GPU가 쓸 수 있는 텍스처**. Unity의 `RenderTexture`와 같다.
(`enableRandomWrite = true` ↔ UAV 플래그)

| | `UTexture2D` | `UTextureRenderTarget2D` |
|---|---|---|
| 출처 | 디스크 이미지 | **런타임 생성** |
| GPU | 읽기만 | **읽기 + 쓰기** |
| CPU 접근 | 원본 있음 | **없음** ([비동기](sync-async.md) 리드백 필요) |

셰이더에서 보이는 타입이 바인딩 방식으로 갈린다.
```hlsl
Texture2D<float4>   Src;   // SRV — 읽기 전용
RWTexture2D<float4> Dst;   // UAV — 쓰기 가능
```
**같은 텍스처를 SRV와 UAV로 동시에 바인딩할 수 없다** → [핑퐁](ping-pong.md)이 필요한 이유.

**포맷 정밀도** (높이 60m 범위 기준 오차)

| 포맷 | 오차 | 판정 |
|---|---|---|
| RGBA8 | 23 cm | 못 씀 |
| RGBA16F | 약 6 cm | 부족 (입자 지름이 5cm) |
| **RGBA32F** | 무시 가능 | **이걸 쓴다.** 512² 한 장 = 4MB |

---

**관련 문서** — [동기 / 비동기 (sync / async)](sync-async.md) · [핑퐁 (ping-pong)](ping-pong.md) · [하이트필드 (heightfield) ★](heightfield.md)
