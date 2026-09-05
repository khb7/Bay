# prefix sum (scan)

> [← 구현 용어집](../glossary-impl.md) › [4. 입자 시뮬레이션](../glossary-impl.md#4-입자-시뮬레이션)

`[3, 1, 4, 1]` → `[0, 3, 4, 8]`. 각 원소 앞의 누적합.
[radix sort](radix-sort.md)에서 "각 통이 어디서 시작하는지"를 구하는 데 쓰인다. GPU 병렬 스캔은 표준 기법.

---

**관련 문서** — [radix sort](radix-sort.md)
