# 헬퍼 클래스 / UE5 접두사

> [← 구현 용어집](../glossary-impl.md) › [1. UE5 ↔ Unity 대응](../glossary-impl.md#1-ue5--unity-대응)

## 헬퍼 클래스

엄밀한 정의가 있는 용어가 아니다. **클래스를 역할로 부르는 비공식 어휘.**
언어 기능이 아니라 관습적 분류다.

> 어떤 작업을 대신 해주려고 존재하는 보조 클래스.
> 도메인의 주인공이 아니고, 반복되는 절차나 자원 관리를 감춰준다.

[`FRHIGPUBufferReadback`](gpu-readback.md)이 없다면 직접 해야 할 일:

```
1. 스테이징 버퍼 생성 (CPU 접근 가능 플래그로)
2. GPU→스테이징 복사 명령 기록
3. 펜스 생성해서 커맨드 리스트에 삽입
4. 매 프레임 펜스 상태 확인
5. 매핑 / 언매핑
6. 렌더 스레드 안전하게 해제
```

헬퍼가 이걸 `EnqueueCopy()` `IsReady()` `Lock()` `Unlock()` 넷으로 줄인다.
**없어도 같은 일을 할 수 있지만 코드가 길어지고 실수하기 쉽다** — 그게 존재 이유.

### 비슷한 이름들

| 이름 | 하는 일 |
|---|---|
| **헬퍼 / 유틸리티** | 보조 절차를 모아둠. 도메인 주인공이 아님 |
| **래퍼(wrapper)** | 기존 API를 감싸 다른 인터페이스로 제공 |
| **파사드(facade)** | 복잡한 하위 시스템 전체를 창구 하나로 |
| **RAII 핸들** | 자원의 획득·해제를 객체 수명에 묶음 (생성자에서 잡고 소멸자에서 놓음) |

`FRHIGPUBufferReadback`은 래퍼 + RAII 성격을 겸한다.

## UE5 접두사 — 이름만 보고 종류를 안다

| 접두사 | 의미 | 예 |
|---|---|---|
| `U` | `UObject` 파생. GC 대상, 리플렉션 있음 | `UEnvironmentClock`, `USandPreset` |
| `A` | `AActor` 파생. 월드에 배치됨 | `ABeachField` |
| **`F`** | **평범한 C++ 클래스/구조체.** GC 없음 | `FRHIGPUBufferReadback`, `FToolStamp` |
| `T` | 템플릿 | `TArray`, `TRefCountPtr` |
| `I` | 인터페이스 | |
| `E` | enum | `EToolOp` |

**헬퍼는 대체로 `F`로 시작한다.** 게임 오브젝트가 아니라 순수한 도구라서
UObject일 이유가 없다. 이름만 보고 "엔진 오브젝트가 아니라 보조 클래스"임을 알 수 있다.

## 이 프로젝트에서

```
UEnvironmentClock     Subsystem  — 시간 상태 소유자
ABeachField           Actor      — 격자 소유자
USandPreset           DataAsset  — 설정값
UBeachSimDirector     Subsystem  — 실행 순서 소유자
FToolStamp            구조체      — 그냥 데이터 묶음
FScenePicker          ★ 헬퍼     — 커서 픽킹 절차를 감춤
```

`FScenePicker`가 전형적인 헬퍼 자리다. 깊이 버퍼를 읽고 역투영하는 절차는
격자의 책임이 아니고, 그렇다고 액터일 이유도 없다.

---

**관련 문서** — [GPU 리드백 (`FRHIGPUBufferReadback`)](gpu-readback.md)
