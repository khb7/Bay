# CFL 조건 (Courant–Friedrichs–Lewy)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

> **한 스텝 동안 정보가 셀 하나를 넘어가면 안 된다.**

`dt < cellSize / 파동속도`. 어기면 시뮬이 발산한다(값이 무한대로 튐).
셀을 절반으로 줄이면 `dt`도 절반이어야 하므로 **비용이 해상도의 제곱이 아니라 세제곱**으로 는다.

---

**관련 문서** — [서브스텝 (substep)](substep.md) · [시간 적분 (time integration) — 적분기의 종류](time-integration.md)
