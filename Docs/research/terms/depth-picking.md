# 깊이 버퍼 픽킹 (depth picking)

> [← 구현 용어집](../glossary-impl.md) › [5. 표현과 렌더링](../glossary-impl.md#5-표현과-렌더링)

커서 픽셀 하나의 깊이값을 읽어 월드 좌표로 되돌리는 것.
지형·입자·물이 이미 다 그려져 있으므로 **셋을 구분할 필요 없이** 커서 위치가 나온다.
[WPO](wpo.md) 지형에 CPU 충돌체가 없는 문제도 이걸로 해결된다.

---

**관련 문서** — [GPU 리드백 (`FRHIGPUBufferReadback`)](gpu-readback.md) · [WPO (World Position Offset)](wpo.md)
