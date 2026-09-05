# 렌더 스레드 / SceneProxy / MeshBatch

> [← 구현 용어집](../glossary-impl.md) › [5. 표현과 렌더링](../glossary-impl.md#5-표현과-렌더링)

GPU에서 만든 메시를 UE5에서 그리려면 넘어야 하는 첫 관문.

## 게임 스레드 / 렌더 스레드

UE5는 두 스레드가 따로 돌고, **렌더 스레드가 게임 스레드보다 1프레임 뒤에서** 일한다.

```cpp
// 게임 스레드 → 렌더 스레드로 넘기는 유일한 통로
ENQUEUE_RENDER_COMMAND(MyPass)(
    [Params](FRHICommandListImmediate& RHICmdList) { /* 여기가 렌더 스레드 */ });
```

**게임 스레드 객체를 렌더 스레드에서 직접 만지면 안 된다** — 그 사이에 삭제될 수 있다.
필요한 데이터를 복사해서 넘긴다.

[RDG](rdg.md)도 렌더 스레드 전용이라 같은 통로를 쓴다.

## Component ↔ SceneProxy

```
UPrimitiveComponent     게임 스레드. 사용자가 다루는 것
       │ CreateSceneProxy()
       ↓
FPrimitiveSceneProxy    렌더 스레드. 실제로 그리는 것
```

분리하는 이유가 위의 스레드 문제다. 게임 스레드가 액터를 지워도
렌더 스레드는 자기 프록시를 안전하게 계속 그린다.

GPU 생성 메시를 그리려면 커스텀 `FPrimitiveSceneProxy`를 만들게 된다.

## MeshBatch — 그리기 "요청"

프록시는 draw call을 직접 부르지 않고 **서술을 제출**한다.

```cpp
void GetDynamicMeshElements(...) {
    FMeshBatch& Mesh = Collector.AllocateMesh();
    Mesh.VertexFactory       = &MyVertexFactory;     // ← vertex-factory.md
    Mesh.MaterialRenderProxy = MaterialProxy;
    // 인덱스 버퍼, 프리미티브 타입 ...
}
```

엔진이 이 서술들을 모아 정렬·병합해서 실제 draw call로 바꾼다.

## 이 프로젝트에서

모래성을 GPU 생성 메시로 그릴 때 필요하다. 다만 **[3단계 경로](../../design/system-design.md)의
3단계에서만** — 1단계는 `UDynamicMeshComponent`를 쓰므로 이 지식이 전혀 필요 없다.

---

**관련 문서** — [RDG (Render Dependency Graph)](rdg.md)
