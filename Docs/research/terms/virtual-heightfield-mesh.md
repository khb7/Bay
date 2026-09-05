# Virtual Heightfield Mesh (VHM)

> [← 구현 용어집](../glossary-impl.md) › [5. 표현과 렌더링](../glossary-impl.md#5-표현과-렌더링)

Runtime Virtual Texture(RVT)에 담긴 높이맵을 읽어 GPU가 화면 적응 테셀레이션으로 그려주는 기능.
수 km 지형용으로 설계됐다.

> ⚠ **이 프로젝트에는 안 맞을 가능성이 높다.** RVT는 **캐시 기반**이고 정적 지형을 전제한다.
> 우리는 매 프레임 격자 전체가 바뀌므로 캐시가 계속 무효화되어 이득이 사라진다.
> 게다가 RVT 높이 채널은 보통 16비트라 우리 32F 요구와 충돌한다.
> 60m × 60m에 정점 100만 개는 요즘 GPU에 가벼우므로, **단순 평면 + [WPO](wpo.md)로 충분하다.**
> LOD가 정말 필요해지면 geometry clipmap(카메라 중심 동심 링) 쪽이 동적 높이맵에 맞는다.

---

**관련 문서** — [WPO (World Position Offset)](wpo.md) · [하이트필드 (heightfield) ★](heightfield.md)
