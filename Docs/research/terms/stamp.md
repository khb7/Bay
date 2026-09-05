# 스탬프 (stamp)

> [← 구현 용어집](../glossary-impl.md) › [6. 자원 관리](../glossary-impl.md#6-자원-관리)

브러시를 시뮬에 적용하는 단위. CPU는 위치·반경·세기·연산 종류만 넘기고,
**높이 판정 같은 건 GPU가 자기 셀에서 한다.**
```cpp
struct FToolStamp { FVector Center; float Radius; float Strength; EToolOp Op; };
```
