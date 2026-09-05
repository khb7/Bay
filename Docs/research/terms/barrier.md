# 배리어 (barrier)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

GPU에게 **"여기서 멈춰라"** 라고 알리는 명령. GPU는 기본적으로 디스패치들을 겹쳐 실행하므로,
뒤 [패스](pass.md)가 앞 패스의 결과를 읽어야 할 때는 명시적으로 막아야 한다.

배리어 하나가 세 가지를 한다.
1. **실행 동기화** — 앞 [디스패치](dispatch.md)의 모든 스레드가 끝날 때까지 대기
2. **캐시 플러시/무효화** — 앞 패스가 쓴 값이 L2에만 있고 메모리에 안 내려갔을 수 있다
3. **[자원 상태 전이](resource-transition.md)(transition)** — 아래 참조

| 종류 | 언제 |
|---|---|
| **Transition** | 용도가 바뀔 때. UAV → SRV 등 |
| **UAV** | 용도는 그대로, 순서만 강제. 같은 버퍼를 연속으로 쓰는 컴퓨트 패스들 사이 |
| **Aliasing** | 두 자원이 같은 메모리를 공유할 때 |

**빠뜨리면** 값이 깨지는데 **항상 깨지지는 않는다** — GPU 부하와 스케줄링에 따라 간헐적이고
다른 GPU에서는 재현이 안 된다. **과하면** 매번 파이프라인을 비워(drain) 수십 μs씩 날린다.

---

**관련 문서** — [RDG (Render Dependency Graph)](rdg.md) · [디스패치 (dispatch)](dispatch.md) · [자원 상태 전이 (resource transition)](resource-transition.md) · [패스 (pass)](pass.md)
