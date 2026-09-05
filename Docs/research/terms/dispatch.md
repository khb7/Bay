# 디스패치 (dispatch)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

[컴퓨트 셰이더](compute-shader.md)를 실행시키는 호출. "스레드를 몇 개 띄울지"를 지정한다.
512² 격자 → 26만 스레드. 스레드 하나가 셀 하나를 맡는다.

---

**관련 문서** — [Simulation Stages (Niagara)](simulation-stages.md) · [★ 그래프 컬러링 — 현대적 답](graph-coloring.md) · [배리어 (barrier)](barrier.md) · [컴퓨트 셰이더 (compute shader)](compute-shader.md) · [패스 (pass)](pass.md)
