# 소성 = 클램프 ★

> [← 용어집](../glossary.md) › [2. 소성과 항복](../glossary.md#2-소성과-항복)

가장 중요한 직관.
```csharp
elasticStrain += delta;

if (허용범위를_벗어남(elasticStrain))
{
    var clamped  = Clamp(elasticStrain);
    var 잘라낸양 = elasticStrain - clamped;

    elasticStrain  = clamped;      // 탄성은 한계까지만
    plasticStrain += 잘라낸양;      // 나머지는 영구 변형
}
```
`Vector3.ClampMagnitude`와 구조가 같다.

| 게임 코드 | [연속체](continuum.md) 역학 |
|---|---|
| `Clamp()` | **[리턴 매핑](return-mapping.md) (return mapping)** |
| 허용 범위의 경계 | **[항복면](yield-surface.md) (yield surface)** |
| 클램프가 작동함 | **항복했다 (yield)** |
| 잘라내고 남긴 기록 | 소성 변형 `F_P` |

---

**관련 문서** — [리턴 매핑 (return mapping)](return-mapping.md) · [연속체 (continuum)](continuum.md) · [항복면 (yield surface) ★](yield-surface.md)
