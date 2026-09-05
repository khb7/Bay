# 간접 드로우 (DrawIndirect)

> [← 구현 용어집](../glossary-impl.md) › [5. 표현과 렌더링](../glossary-impl.md#5-표현과-렌더링)

**그릴 개수를 CPU가 모를 때 쓴다.**

## 문제

보통 draw call은 "삼각형 N개를 그려라"처럼 개수를 인자로 받는다.
그런데 [등위면 추출](marching-cubes.md)은 **GPU가 삼각형을 만들므로 그 개수도 GPU만 안다.**
CPU로 개수를 가져오려면 [리드백](gpu-readback.md)이 필요하고, 그러면 지연이 생긴다.

## 해법

개수 자체를 GPU 버퍼에 써두고, 드로우 명령이 **그 버퍼를 읽게** 한다.

```cpp
// ArgsBuffer = { IndexCount, InstanceCount, StartIndex, BaseVertex, StartInstance }
//              ↑ 컴퓨트 셰이더가 채운다
RHICmdList.DrawIndexedPrimitiveIndirect(IndexBuffer, ArgsBuffer, Offset);
```

CPU는 "저 버퍼에 적힌 만큼 그려"라고만 말한다. 숫자를 몰라도 된다.

같은 원리로 `DispatchIndirect`도 있다 — 컴퓨트 스레드 수를 GPU가 정할 때.

## 이 프로젝트에서

모래성 메시의 삼각형 수는 형태에 따라 매 프레임 달라진다.
[3단계 경로](../../design/system-design.md)의 3단계에서 쓴다.

---

**관련 문서** — [GPU 리드백 (`FRHIGPUBufferReadback`)](gpu-readback.md) · [Marching Cubes / Surface Nets](marching-cubes.md)
