# 핑퐁 (ping-pong)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

텍스처 두 장을 번갈아 쓰는 기법. 같은 텍스처를 읽으면서 동시에 쓰면
이웃 셀이 갱신 전 값인지 후 값인지 알 수 없다(경쟁 상태).
→ A에서 읽어 B에 쓰고, 다음 [패스](pass.md)는 B에서 읽어 A에 쓴다.

---

**관련 문서** — [Render Target / SRV / UAV](render-target-srv-uav.md) · [Simulation Stages (Niagara)](simulation-stages.md) · [패스 (pass)](pass.md)
