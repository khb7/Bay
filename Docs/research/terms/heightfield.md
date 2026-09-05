# 하이트필드 (heightfield) ★

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)
> [← 구현 용어집](../glossary-impl.md) › [3. 격자 시뮬레이션](../glossary-impl.md#3-격자-시뮬레이션)
>
> 물리 관점(glossary)과 구현 관점(glossary-impl)을 합친 문서.

**2D 격자의 각 칸이 높이값 하나만 들고 있는 지형 표현.** `지형 = h(x, z)`
Unity Terrain / UE Landscape가 바로 이것.

```
  위에서 본 격자              옆에서 본 단면
  ┌──┬──┬──┬──┐                    ▄▄
  │12│15│18│16│                 ▄▄█████▄
  ├──┼──┼──┼──┤              ▄▄██████████▄▄
  │10│14│19│17│    →       ▄████████████████
  ├──┼──┼──┼──┤
  │ 8│11│15│14│            각 칸의 숫자만큼 솟아오름
  └──┴──┴──┴──┘
```

**차원 하나를 버려서 얻는 이득**

| | 하이트필드 | 복셀(3D) |
|---|---|---|
| 데이터 | 1024² float = **4 MB** | 1024³ float = **4 GB** |
| 충돌 조회 | `h[x][z]` → **O(1)** | 레이마칭 필요 |
| GPU | **그냥 텍스처** | 3D 텍스처, 무거움 |

**우리 프로젝트에 맞는 이유** — D절(Mei 2007) 시뮬이 전부 이 위에서 돈다.
같은 격자에 `b`(지형) `d`(물) `s`(뜬모래) `W`(수분)를 채널로 얹으면 되고,
침식은 `b ± amount` 한 줄, [열침식](thermal-erosion.md)은 이웃 경사 비교 한 줄. **결과가 곧 렌더링 입력.**

**★ 결정적 한계 — 한 (x,z)에 높이가 하나뿐**
```
   표현 가능              표현 불가
     ▄████▄               ▄███████   ← 처마/오버행
   ▄████████▄            ██▀
  ████████████           ██▄▄▄▄
                         같은 x에 높이 2개 필요
```
**오버행·아치·동굴·터널 불가. 모래성 터널을 못 뚫는다.**
벽이 통째로 넘어지는 것도 못 하고 위에서부터 스르륵 녹는다.

| 표현 | 데이터 | 오버행 | 비용 | 예 |
|---|---|---|---|---|
| **하이트필드** | 2D 스칼라 | ✗ | 매우 쌈 | Unity Terrain, UE Landscape |
| 레이어드 하이트필드 | 2D × N층 | 제한적 | 쌈 | 지질 층 (Šťava 2008) |
| 복셀 / [SDF](sdf.md) | 3D 스칼라 | ✓ | 비쌈 (해상도³) | Minecraft, Dual Contouring |
| 입자 | 위치 리스트 | ✓ | 중간 | [PBD](pbd.md), [MPM](mpm.md) |
| 메시 | 정점+면 | ✓ | [변형](strain.md)이 어려움 | 일반 3D 모델 |

→ **우리 설계의 [A]/[B] 분리가 정확히 이 한계 때문이다.**
해변 전체는 하이트필드(오버행 불필요, 압도적으로 쌈), 모래성 국소만 입자.

**언리얼**: Landscape, [Virtual Heightfield Mesh](virtual-heightfield-mesh.md), 또는 테셀레이션 평면 + [WPO](wpo.md).
시뮬은 Niagara Grid2D → [Render Target](render-target-srv-uav.md) → 머티리얼에서 샘플.

---

## 구현 관점 (UE5 / Niagara)

지형을 "셀마다 높이 하나"로 표현. 2.5D.
- 장점: 싸고, 이웃이 명확하고, 텍스처로 그대로 렌더링됨
- 한계: **오버행·터널·아치가 원리적으로 불가능**. 이 프로젝트가 입자 계층을 따로 두는 이유

---

**관련 문서** — [MPM (Material Point Method)](mpm.md) · [MPM 계보](mpm-lineage.md) · [PBD (Position Based Dynamics)](pbd.md) · [Render Target / SRV / UAV](render-target-srv-uav.md) · [SDF (Signed Distance Field)](sdf.md) · [Virtual Heightfield Mesh (VHM)](virtual-heightfield-mesh.md) · [WPO (World Position Offset)](wpo.md) · [가상 파이프 (virtual pipes)](virtual-pipes.md) · [변형 (strain, ε)](strain.md) · [열침식 (thermal erosion)](thermal-erosion.md) · [우리 프로젝트에서 강성은 거의 안 중요하다](stiffness-in-our-project.md) · [항복면 (yield surface) ★](yield-surface.md)
