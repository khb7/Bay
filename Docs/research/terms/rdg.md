# RDG (Render Dependency Graph)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

렌더 [패스](pass.md)를 즉시 실행하지 않고 **그래프로 먼저 선언한 뒤 엔진이 최적화해 실행**하는 UE5 시스템.

```cpp
FRDGBuilder Graph(RHICmdList);
auto* P = Graph.AllocParameters<FErosionCS::FParameters>();
P->Src = GridA;                     // ← 읽는다고 선언
P->Dst = Graph.CreateUAV(Temp);     // ← 쓴다고 선언
FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("Erosion"), CS, P, Groups);
Graph.Execute();                    // ← 여기서 처음 실제 실행
```

**의존성을 손으로 적지 않는다.** 파라미터 구조체의 읽기/쓰기 선언이 곧 그래프다.
`Execute()`에서 하는 일: 의존 관계 도출 → [배리어](barrier.md) 자동 삽입 → 죽은 패스 제거 →
메모리 별칭(생명이 겹치지 않는 임시 자원끼리 공유) → 커맨드 기록.

`RDG_EVENT_NAME`으로 붙인 이름이 **RenderDoc과 GPU 프로파일러에 그대로 나타난다.**
Niagara에 없는 것이고, 입자 계층을 커스텀 컴퓨트로 뺀 주된 이유다.

**함정 ①** 렌더 스레드 전용. 게임 스레드에서는 `ENQUEUE_RENDER_COMMAND`로 넘긴다.
**함정 ②** 그래프는 프레임 단위로 사라진다. 프레임을 넘어 사는 자원(입자 위치·속도)은
`TRefCountPtr<FRDGPooledBuffer>`로 보관했다가 매 프레임 `RegisterExternalBuffer`로 등록한다.

---

**관련 문서** — [렌더 스레드 / SceneProxy / MeshBatch](render-thread-proxy.md) · [배리어 (barrier)](barrier.md) · [패스 (pass)](pass.md)
