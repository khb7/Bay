# 극분해 (polar decomposition)

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

[SVD](svd.md)의 사촌. 그래픽스에서는 이쪽이 더 흔하다.
```
M = R · S          R = U·Vᵀ  (순수 회전)
                   S = V·Σ·Vᵀ (순수 늘어남)
```
**"이 [변형](strain.md)에서 가장 가까운 회전만 뽑아내라"** 는 뜻.

쓰이는 곳:
- **[PBD](pbd.md)의 [shape matching](shape-matching.md)** (Macklin 2014) — 찌그러진 입자 덩어리에서 원래 형태의 방향을 뽑음
- **Co-rotational [FEM](fem.md)** — 큰 회전에서도 선형 탄성을 쓰게 해주는 표준 기법
- 스키닝, IK, 메시 변형 보정

---

**관련 문서** — [FEM 메시 (유한요소 메시)](fem.md) · [PBD (Position Based Dynamics)](pbd.md) · [SVD (특이값 분해) ★](svd.md) · [shape matching](shape-matching.md) · [변형 (strain, ε)](strain.md)
