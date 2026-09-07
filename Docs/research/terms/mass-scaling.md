# 질량 스케일링 (mass scaling)

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

**솔버 안에서만 역질량 `w = 1/m` 을 손대는 것.** 실제 질량은 그대로 두고, 제약을 풀 때
쓰는 유효 질량만 바꾼다.

## 왜 하는가 — 질량비가 수렴을 죽인다

무거운 것이 가벼운 것을 누르면 [반복](iteration-relaxation.md)이 폭발적으로 많이 필요하다.
가벼운 쪽이 보정을 거의 다 흡수해버려서(`w`가 크니까) 무거운 쪽은 거의 안 움직이고,
정보가 아래로 안 내려간다. 깊은 더미가 정확히 이 상황이다.

```
같은 질량 두 입자      보정을 5:5로 나눠 가짐          ← 잘 수렴
1000:1 질량비          가벼운 쪽이 999를 가져감        ← 무거운 쪽 하중이 전달 안 됨
```

## [Guendelman](../papers.md) 무한 질량의 연속판

§C3의 shock propagation은 아래층을 **`w = 0`(무한 질량)** 으로 굳힌다. 극단이라 부작용이 크다.
질량 스케일링은 같은 축을 연속적으로 쓴다 — `w`를 0으로 보내는 대신 **몇 배 줄인다.**

| | 아래층 처리 | 성질 |
|---|---|---|
| shock propagation | `w = 0` | 이진. 굳으면 위층이 전부 비켜나야 함 |
| 질량 스케일링 | `w ← w / k` | 연속. `k`로 강도를 조절 |

## ⚠ 명칭 주의 — Tonge 2012는 정확히는 mass splitting

[papers.md](../papers.md) §C3이 대안으로 지목한 **Tonge, Benevolenski, Voroshilov (2012),
"Mass Splitting for Jitter-Free Parallel Rigid Body Simulation"** (SIGGRAPH)의 기법 이름은
*mass splitting* 이다. 한 물체의 질량을 그 물체에 걸린 제약 개수로 **쪼개 나눠주는** 것으로,
[Jacobi 제약 평균화](gauss-seidel-jacobi-sor.md)를 질량 쪽에서 정당화한 판에 가깝다.

**"질량 스케일링"은 그 상위 계열의 이름**이고, 솔버가 유효 질량을 조작하는 기법 전반을 가리킨다.
문헌을 찾을 때 두 단어를 다 넣을 것.

## ★ 이 프로젝트에서의 위험 — 교정 실험을 오염시킨다

M0에서 이걸 켠 채로 [안식각](angle-of-repose.md)을 재면 안 된다.

```
유효 질량을 건드림 → 접촉에 실리는 λ_법선이 바뀜 → μ·λn 마찰 예산이 바뀜
                                                     ↓
                                        안식각이 물성이 아니라 솔버 설정의 함수가 된다
```

[papers.md](../papers.md) §C3이 무한 질량에 대해 경고한 것과 같은 병이다 —
*"얼리면 더미가 인위적으로 단단해져 안식각이 높게 측정된다."* 정도만 약할 뿐이다.

> **순서: 교정을 먼저 끝내고, 성능 대책으로 나중에 켠다.** 켠 뒤에는 경사판 실험을
> 다시 돌려 각이 움직였는지 확인해야 한다. 움직였으면 그만큼이 인공물이다.

---

**관련 문서** — [Gauss-Seidel / Jacobi / SOR ★](gauss-seidel-jacobi-sor.md) · [라그랑주 승수 (Lagrange multiplier, λ)](lagrange-multiplier.md) · [반복 (iteration) / 완화 (relaxation)](iteration-relaxation.md) · [안식각 (angle of repose)](angle-of-repose.md)
