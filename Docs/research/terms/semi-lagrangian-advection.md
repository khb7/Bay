# 반라그랑주 이류 (semi-Lagrangian advection)

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)
> [← 구현 용어집](../glossary-impl.md) › [3. 격자 시뮬레이션](../glossary-impl.md#3-격자-시뮬레이션)
>
> 물리 관점(glossary)과 구현 관점(glossary-impl)을 합친 문서.

`s_new(x) = s_old(x − v·dt)` — 속도를 거슬러 올라가 값을 가져옴. 텍스처 샘플 1회.

---

## 구현 관점 (UE5 / Niagara)

값을 앞으로 밀지 않고, **"이 셀의 값은 한 스텝 전 어디에서 왔나"를 역추적**해 샘플링한다.
```
s_new(x) = s_old(x − v·dt)
```
텍스처 샘플 한 번이면 끝나고 무조건 안정하다. 대가는 수치 확산(값이 뭉개짐).
