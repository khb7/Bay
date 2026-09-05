# 열침식 (thermal erosion)

> [← 용어집](../glossary.md) › [2. 소성과 항복](../glossary.md#2-소성과-항복)
> [← 구현 용어집](../glossary-impl.md) › [3. 격자 시뮬레이션](../glossary-impl.md#3-격자-시뮬레이션)
>
> 물리 관점(glossary)과 구현 관점(glossary-impl)을 합친 문서.

[하이트필드](heightfield.md)에서의 [안식각](angle-of-repose.md). 이웃과의 경사가 talus angle을 넘으면 아래로 모래를 옮긴다.
```csharp
float talusAngle = lerp(34f, 90f, wetness);   // 젖으면 더 가파른 각도를 버팀
```
[연속체](continuum.md)의 "[포화도](saturation.md) → [점착력](cohesion.md) → [항복면](yield-surface.md) 이동"의 하이트필드 버전.

---

## 구현 관점 (UE5 / Niagara)

경사가 **안식각**을 넘으면 모래를 아래 셀로 옮기는 규칙.
```
if ((b − b_neighbor) / cellSize > tan(talusAngle)) 아래로_옮긴다();
talusAngle = lerp(34°, 90°, wetness);   // 젖으면 더 가파른 각을 버틴다
```
모래성이 하이트필드에서 "서는" 지점. 연속체 논문의 항복면 이동과 같은 아이디어를 1/1000 비용으로.

---

**관련 문서** — [Simulation Stages (Niagara)](simulation-stages.md) · [리턴 매핑 (return mapping)](return-mapping.md) · [안식각 (angle of repose)](angle-of-repose.md) · [연속체 (continuum)](continuum.md) · [점착력 (cohesion, c)](cohesion.md) · [포화도 (saturation)](saturation.md) · [하이트필드 (heightfield) ★](heightfield.md) · [항복면 (yield surface) ★](yield-surface.md)
