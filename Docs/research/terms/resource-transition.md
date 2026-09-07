# 자원 상태 전이 (resource transition)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

텍스처는 **용도에 따라 GPU 내부의 실제 저장 방식이 다르다.**

| 용도 | 내부 표현 |
|---|---|
| 렌더 타겟 | 압축 저장 (delta color compression) |
| SRV (셰이더 읽기) | 읽기에 최적화된 배치 |
| UAV (컴퓨트 쓰기) | 압축 불가, 임의 접근 가능한 선형 배치 |
| 깊이 버퍼 | HiZ 압축 |

같은 메모리 주소인데 바이트 배치가 다르므로, 용도를 바꾸려면 실제로 재배치나 압축 해제가
일어날 수 있다. 그 작업이 전이이고, 전이 [배리어](barrier.md)가 그것을 지시한다.

---

**관련 문서** — [Render Target / SRV / UAV](render-target-srv-uav.md) · [배리어 (barrier)](barrier.md)
