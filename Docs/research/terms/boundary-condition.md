# 경계 조건 (boundary condition)

> [← 구현 용어집](../glossary-impl.md) › [3. 격자 시뮬레이션](../glossary-impl.md#3-격자-시뮬레이션)

격자 가장자리에서 값을 어떻게 정할지. 우리는 **바다쪽 경계 셀의 수면 높이를 파형 함수로 강제**한다.
그러면 파이프 관성이 안쪽으로 전파되면서 uprush/backwash가 저절로 생긴다.
