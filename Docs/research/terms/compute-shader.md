# 컴퓨트 셰이더 (compute shader)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

렌더링과 무관하게 **GPU에서 임의 계산**을 돌리는 셰이더. 픽셀을 그리지 않고 버퍼/텍스처를 읽고 쓴다.
격자 시뮬의 [패스](pass.md) 하나하나가 컴퓨트 셰이더 한 번이다.

---

**관련 문서** — [Simulation Stages (Niagara)](simulation-stages.md) · [디스패치 (dispatch)](dispatch.md) · [패스 (pass)](pass.md)
