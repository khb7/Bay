# 1-1. Subsystem — 수명으로 고르는 싱글턴

> [← 구현 용어집](../glossary-impl.md) › [1. UE5 ↔ Unity 대응](../glossary-impl.md#1-ue5--unity-대응)

Unity에서 `static Instance` + `DontDestroyOnLoad`로 하던 것을 엔진이 대신 관리한다.
씬에 배치할 필요가 없고, 중복도 삭제도 스폰 순서 문제도 없다.

| 종류 | 수명 | 예 |
|---|---|---|
| `UEngineSubsystem` | 엔진 시작~종료 | 전역 설정 |
| `UEditorSubsystem` | 에디터 | 에디터 툴 |
| `UGameInstanceSubsystem` | 게임 전체. **레벨 전환에도 유지** | 저장 데이터, 진행도 |
| **`UWorldSubsystem`** | **레벨 하나** | 그 레벨의 시뮬 상태 |
| `ULocalPlayerSubsystem` | 로컬 플레이어 | 입력 설정 |

```cpp
UEnvironmentClock* Clock = GetWorld()->GetSubsystem<UEnvironmentClock>();
```

**주의 ①** 직렬화가 자동이 아니다. 저장하려면 `SaveGame` 객체에 수동으로 옮겨야 한다.
**주의 ②** Tick 순서 제어가 약하다. 순서가 중요하면 **Tick을 도는 객체를 하나만 두고**
나머지를 호출당하게 만든다(오케스트레이터 패턴). 이 프로젝트의 `BeachSimDirector`가 그것.
