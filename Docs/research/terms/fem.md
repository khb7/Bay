# FEM 메시 (유한요소 메시)

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

**FEM(Finite Element Method, 유한요소법)이 쓰는 격자.** 물체를 삼각형(2D)·사면체(3D)로 잘게
쪼갠 것이고, 그 꼭짓점(node) 위치가 곧 미지수다. 이 문서에 있는 이유는 우리가 쓰기
때문이 아니라 **[MPM](mpm.md)과 [PBD](pbd.md)가 둘 다 "FEM이 아닌 것"으로 자기를 설명하기** 때문이다.

## 하는 일

```
1. 물체를 요소(element)로 쪼갠다                    보통 사면체
2. 변형은 꼭짓점 위치로만 표현, 요소 내부는 보간    ← 여기서 "유한"
3. 요소마다 변형 → 응력 → 절점력                    F 에서 σ 를 구성모델로
4. 전체를 큰 연립방정식으로 묶어 푼다                대칭 선형계
```

3번이 [변형 그래디언트](deformation-gradient.md) `F` → [구성 모델](constitutive-model.md) → [응력](stress.md)이라
**MPM과 완전히 같다.** 차이는 그 계산을 어디서 하느냐뿐이다.

Unity 비유로는 **Cloth 컴포넌트의 3D 부피 버전**. 다만 Cloth는 거리 제약(PBD 계열)이고
FEM은 진짜 재료 물성(영률·푸아송비)을 받는다. 정확도의 기준선이 FEM이다.

## ★ 메시 뒤틀림 — 이 문서의 요점

메시가 **재료에 붙어 있다**([라그랑주](lagrangian-vs-eulerian.md)). 그래서 재료가 크게 변형되면
사면체가 납작해지고, 부피가 0이나 음수가 되면 계산이 터진다.

```
고무공이 눌린다        ← 괜찮다. 요소가 찌그러져도 이웃 관계는 그대로
모래 한 줌이 쏟아진다  ← 불가능. 이웃이던 알갱이가 30cm 떨어진다
```

**우리가 FEM을 기각한 이유는 속도가 아니라 위상(topology)이다.** 무한히 빨라져도 모래엔
안 맞는다 — 재료가 섞이는 순간 "붙어 있는 메시"라는 전제 자체가 없다.

## 세 갈래

| | 계산 장소 | 이웃 관계 | 크게 흐르면 |
|---|---|---|---|
| **FEM** | 재료에 붙은 메시 | **고정** | 뒤틀려 터짐 |
| **[MPM](mpm.md)** | 매 프레임 새로 깐 격자 | 매 프레임 재구성 | 문제 없음 |
| **우리 ([XPBD](xpbd.md) 입자)** | 메시 없음. 접촉 [제약](constraint.md)만 | 매 프레임 [이웃 탐색](neighbor-search.md) | 문제 없음 |

MPM의 "격자를 매 프레임 새로 깖"은 **FEM의 병에 대한 처방**으로 읽어야 뜻이 통한다.

## 남아 있는 접점

기각했다고 무관한 건 아니다. **[극분해](polar-decomposition.md)를 써서 큰 회전에서도 선형 탄성을
쓰는 co-rotational FEM**의 장치가 MPM 문헌에 그대로 등장한다. `F`를 회전과 늘림으로
쪼개는 [SVD](svd.md) 루틴이 양쪽에서 같은 물건이다.

## 이 프로젝트에서의 위치

[papers.md](../papers.md) §K-10에 *"유한요소 구조해석 — 입자 시뮬이 대신함"*,
[README.md](../README.md)에 *"유한요소 해석은 실시간 불가"* 로 기각돼 있다. 구조 안정성(J·K절)은
정확한 응력장 대신 **XPBD 접촉의 연쇄**로 근사한다 — 힘사슬은 나오지만 응력은 아니다.

---

**관련 문서** — [MPM (Material Point Method)](mpm.md) · [PBD (Position Based Dynamics)](pbd.md) · [SVD (특이값 분해) ★](svd.md) · [XPBD](xpbd.md) · [구성 모델 (constitutive model)](constitutive-model.md) · [극분해 (polar decomposition)](polar-decomposition.md) · [라그랑주 vs 오일러 (MPM의 전제)](lagrangian-vs-eulerian.md) · [변형 그래디언트 F](deformation-gradient.md) · [응력 (stress, σ)](stress.md) · [이웃 탐색 (neighbor search)](neighbor-search.md) · [제약 (constraint)](constraint.md)
