# 파이프라인 스톨 (pipeline stall)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

CPU가 GPU를 기다리며 멈추는 것. GPU는 항상 CPU보다 1~3프레임 뒤처져 일하므로,
"지금 당장 GPU 값 내놔"라고 하면 밀린 작업이 다 끝날 때까지 CPU가 논다. 5~15ms 손실.

---

**관련 문서** — [GPU 리드백 (`FRHIGPUBufferReadback`)](gpu-readback.md)
