# Custom HLSL 노드 (Niagara)

> [← 구현 용어집](../glossary-impl.md) › [2. GPU 실행 모델](../glossary-impl.md#2-gpu-실행-모델)

Niagara 그래프 안에 **HLSL 코드를 직접 써넣는 노드.**
비주얼 노드를 수십 개 연결하는 대신 텍스트 상자에 코드를 붙여넣는다.

## 동작 방식

핀을 직접 선언하면 **그 이름이 코드 안의 변수명이 된다.**

```
입력 핀:  float3 A,  float3 B,  float MaxStep
출력 핀:  float3 Out
```
```hlsl
float3 n = normalize(B - A);
Out = A + n * min(length(B - A), MaxStep);
```

본문은 **함수 몸통으로 그대로 삽입**된다. 그래서

- 출력 핀은 코드에서 **반드시 대입**해야 한다
- `return`을 쓰면 생성된 함수 전체가 빠져나간다 → 조기 종료는 `if`로 감쌀 것

Data Interface도 입력 핀으로 받아 코드에서 함수를 호출할 수 있어,
Grid2D 읽기/쓰기를 HLSL 안에서 처리할 수 있다.

## 왜 쓰나

[Simulation Stages 함정 1](simulation-stages.md) 그대로 — **노드로 다 짜면 지옥이다.**
9패스 중 하나만 봐도 이렇다.

```hlsl
// 4. 침식/퇴적
float C = Kc * max(sin(alpha), minSlope) * pow(length(v), 2) * (1 + exposure);
float delta = (C > s) ? Ks * (C - s) : Kd * (s - C);
b -= delta;  s += delta;
```

**세 줄이다.** 노드로 그리면 20개가 넘고 수식을 고칠 때마다 배선을 다시 해야 한다.
읽을 수 있고, 고칠 수 있고, **논문의 식과 눈으로 대조할 수 있다** —
논문 식을 그대로 옮기는 이 프로젝트에서는 사실상 유일한 선택지다.

## 대가

| 함정 | 내용 |
|---|---|
| 컴파일 오류 위치가 엉뚱 | 생성된 코드 기준 줄 번호. 원본 어디인지 직접 찾아야 한다 |
| 자동완성 · 문법 강조 없음 | 평범한 텍스트 상자다 |
| 중단점 없음 | 값을 화면에 색으로 뱉어 보는 게 디버깅 수단 |
| 핀 이름 충돌 | 핀 이름이 그대로 지역변수라 흔한 이름(`n`, `d`)이 위험하다 |
| **★ git diff 불가** | 코드가 `.uasset` **바이너리** 안에 산다 |

## ★ `.ush`로 빼는 전략

시뮬 코드가 바이너리 애셋 안에 들어가면 **커밋에서 무엇이 바뀌었는지 볼 수 없다.**
침식 계수 하나 바꾼 커밋과 패스 순서를 뒤집은 커밋이 구분되지 않는다.
물리 상수를 계속 스윕할 프로젝트에서 큰 손실이다.

**대응 — HLSL은 `.ush` 파일에 두고, 노드에서는 함수 호출만 한다.**

```hlsl
Out = BeachSim_ErosionPass(b, s, v, alpha, exposure);   // 노드 본문은 한 줄
```

| 얻는 것 | |
|---|---|
| diff | `.ush`는 텍스트다 |
| 재사용 | [B] 계층 커스텀 컴퓨트에서 **같은 파일을 쓴다** — `cohesion(W)`, `talus(W)`, `strengthCurve`가 이미 두 계층 공유 |
| 검토 | 논문 식과 나란히 놓고 볼 수 있다 |

셰이더 가상 경로 매핑(`AddShaderSourceDirectoryMapping`)을 모듈 시작 시 걸어야
Niagara가 `#include`를 찾는다. **M0-b에서 [RDG](rdg.md)를 만질 때 어차피 필요한 설정이라
그때 같이 잡으면 된다.**

---

**관련 문서** — [RDG (Render Dependency Graph)](rdg.md) · [Simulation Stages (Niagara)](simulation-stages.md)
