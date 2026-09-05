# WPO (World Position Offset)

> [← 구현 용어집](../glossary-impl.md) › [5. 표현과 렌더링](../glossary-impl.md#5-표현과-렌더링)

**좌표가 아니라 "얼마나 옮길지"** — 원래 정점 위치에 더하는 이동량이다.
Unity의 버텍스 셰이더 정점 이동, Shader Graph의 Vertex Position 입력과 같다.

```
평면 메시 정점 (x, y, 0)  +  WPO (0, 0, SampleHeight(x,y))  =  지형 (x, y, b)
```
CPU가 전혀 개입하지 않는다. 격자 시뮬이 `b`를 바꾸면 다음 프레임에 지형이 따라온다.

**한계 셋 — 전부 이 프로젝트에 걸린다**
1. **물리 엔진이 모른다.** 충돌은 여전히 원래 평면 기준 → 커서를 깊이 픽킹으로 하는 이유
2. **컬링·그림자에서 잘린다.** 바운드가 원래 메시 기준이라 `Bounds Scale`을 키워야 한다.
   안 하면 "카메라를 돌리면 지형이 사라지는" 증상이 나온다
3. **정점 밀도가 곧 해상도.** 512²면 26만 정점, 1024²면 100만 정점

| 기능 | 무엇을 옮기나 |
|---|---|
| **WPO** | 정점을 3D로 이동. 실루엣이 실제로 바뀜 |
| Pixel Depth Offset | 픽셀 깊이값만 조정. 형태는 그대로, 교차선만 자연스러워짐 |
| Normal 맵 | 아무것도 안 옮김. 빛 계산만 속임 |
| 테셀레이션 | 정점을 늘림 (옮기는 건 WPO가 함) |

---

**관련 문서** — [Virtual Heightfield Mesh (VHM)](virtual-heightfield-mesh.md) · [깊이 버퍼 픽킹 (depth picking)](depth-picking.md) · [하이트필드 (heightfield) ★](heightfield.md)
