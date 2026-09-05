# SVD (특이값 분해) ★

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

> **어떤 행렬이든 "회전 → 축을 따라 늘리기 → 회전" 세 단계로 쪼갤 수 있다.**

```
M = U · Σ · Vᵀ
    ↑   ↑   ↑
  회전  축별  회전
       스케일
```
- **U, V**: 회전(직교) 행렬. 모양을 안 바꾸고 방향만 돌림
- **Σ**: 대각 행렬. **특이값** σ₁ σ₂ σ₃ = 축을 따라 몇 배 늘렸나

**기하학적 의미 — 선형 변환은 항상 원을 타원으로 보낸다**
```
 단위원 ──Vᵀ──> 단위원 ──Σ──> 축정렬 타원 ──U──> 기울어진 타원
        회전만          x축 σ₁배           최종 방향
        모양 그대로      y축 σ₂배
```
Σ = 타원의 반축 길이 / U = 타원이 향한 방향 / V = 원본의 어느 방향이 그 축으로 갔나

**Unity 연결**: Transform은 rotation + localScale뿐이라 **밀림(shear)을 표현 못 한다.**
SVD는 밀림도 회전+스케일+회전으로 표현한다.
```
M = [1 1]  →  σ₁ = 1.618, σ₂ = 0.618   (곱하면 1 = 행렬식, 넓이 보존)
    [0 1]     "밀림 = 대각선으로 늘리고 반대 대각선으로 줄인 것"
```

**[MPM](mpm.md)에서 쓰는 이유**
```
고무공을 돌린다      → 회전. 재료 멀쩡. 응력 0
고무공을 찌그러뜨린다 → 변형. 응력 발생
```
`F` 안에 회전과 [변형](strain.md)이 뒤섞여 있으면 [응력](stress.md)을 못 구한다. **SVD가 정확히 분리한다.**
U, Vᵀ(회전)는 버리고 Σ(늘어남 3개)만 남김 → 9성분 행렬이 **숫자 3개** 문제로.
여기에 로그를 씌우면([Hencky 변형](hencky-strain.md)) 원뿔 투영이 손으로 풀리는 기하 문제가 된다.
→ Klár 2016이 반복 없이 `if/else` 3분기로 끝낸 근거.

**특이값의 물리적 의미**
```
σ = 1.0   변화 없음        σ = 1.5   1.5배 인장
σ = 0.7   0.7배 압축       σ₁σ₂σ₃    부피 변화율 (= 행렬식)
```
모래가 팽창했는지(곱 > 1) 다져졌는지(< 1)를 바로 읽는다.

**구현**: McAdams, Selle, Tamstorf, Teran, Sifakis (2011),
*"Computing the SVD of 3×3 Matrices with Minimal Branching and Elementary
Floating Point Operations"* — 분기 없이 SIMD로 도는 3×3 전용 고속 알고리즘.
그래픽스 MPM 코드베이스의 사실상 표준.

---

**관련 문서** — [Hencky 변형 (로그 변형)](hencky-strain.md) · [MPM (Material Point Method)](mpm.md) · [SVD의 다른 용도](svd-other-uses.md) · [★ MPM의 입자 ≠ 모래알](mpm-particle-vs-grain.md) · [극분해 (polar decomposition)](polar-decomposition.md) · [등방성 (isotropy) ★](isotropy.md) · [리턴 매핑 (return mapping)](return-mapping.md) · [변형 (strain, ε)](strain.md) · [응력 (stress, σ)](stress.md) · [팽창을 시뮬레이션이 읽어야 하는 이유](why-simulate-dilation.md) · [항복면 (yield surface) ★](yield-surface.md)
