# uniform / 상수 버퍼

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

모든 스레드가 **똑같이 읽는 값**. 태양 방향, 바람 세기, 조석 위상 같은 것.
셀마다 다른 값은 텍스처로, 전체가 공유하는 값은 uniform으로.
