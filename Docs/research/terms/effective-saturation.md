# 유효 포화도 Se / 흡입력 / 투수계수

> [← 용어집](../glossary.md) › [4. 증발 / 건조](../glossary.md#4-증발--건조)

```csharp
float Se      = (theta - theta_r) / (theta_s - theta_r);
float suction = P_entry * pow(Se, -1f / lambda);       // 모세관 흡입력
float K       = K_sat * pow(Se, 3f + 2f / lambda);     // 투수계수
```
**`K`가 `Se`의 3~5제곱** — 조금만 말라도 물 이동 능력이 폭락한다.
이게 2단계로 넘어가는 물리적 이유.
(van Genuchten 1980 / Brooks & Corey 1964)
