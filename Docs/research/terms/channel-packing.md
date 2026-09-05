# 채널 패킹 (channel packing)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

RGBA 4채널 텍스처 한 장에 서로 다른 물리량 4개를 욱여넣는 것.
```
RT_A (RGBA32F)  b(지형)  d(물깊이)  s(뜬모래)  W(수분)
```
텍스처 개수와 샘플링 횟수를 줄인다.
