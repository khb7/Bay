# 패스 (pass)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

한 스텝을 이루는 계산 단계 하나. 우리 격자는 스텝당 9패스.
**패스를 나누는 이유는 동기화** — 모든 셀이 이전 단계를 끝낸 뒤에 다음 단계로 가야 하는데,
GPU는 스레드 간 순서를 보장하지 않으므로 [디스패치](dispatch.md)를 끊어서 강제한다.

---

**관련 문서** — [GPU 리드백 (`FRHIGPUBufferReadback`)](gpu-readback.md) · [RDG (Render Dependency Graph)](rdg.md) · [radix sort](radix-sort.md) · [디스패치 (dispatch)](dispatch.md) · [배리어 (barrier)](barrier.md) · [컴퓨트 셰이더 (compute shader)](compute-shader.md) · [핑퐁 (ping-pong)](ping-pong.md)
