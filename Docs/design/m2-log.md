# M2 실행 기록

> 계획: [m2-plan.md](m2-plan.md). 시험 명령 (GPU 메시 대조):
> `UnrealEditor-Cmd.exe <uproject> -run=SpikeSweep -mesh -meshframe -AllowCommandletRendering -knramp=8 -isofrac=0.25 -cell=1.25 -bandr=7.5 -smooth=25 -scene=pour -count=3000 -frames=180 -every=60 [-pool=2] [-killhalf | -removehalf]`

## 실행 #1 — T1 입자 풀 · 죽은 칸 (2026-10-27)
- 방식: 죽은 입자는 번호를 그대로 두고 **무덤 자리**(`SandPool::GravePos`, z = 10⁶, 칸마다 50cm 떨어짐)에 고정 (w = 0). 셰이더는 고치지 않았다 — 위치만으로 이웃 · 지면 · 벽 · 메시 영역에서 빠진다.
  - `FSandSpike::KillParticles` (걸린 결합 삭제, 쌍 목록 워밍 유지) · `ReservePool(용량)` (죽은 칸 덧붙임) · `NumAlive` · `IsDead`.
  - CPU 메셔 `FSandSurfaceBuilder::Build` 는 무덤을 빼고 짓는다 (경계 상자). 입자당 대조 배열은 뺀 뒤 번호.
- 결과 (부어 쌓은 블록 3천, 벽 치운 뒤 180 프레임):

| 장면 | 칸 / 산 입자 | 메시 CPU = GPU | 정점 f180 | 최고점 | 시뮬 ms 평균 |
|---|---|---|---|---|---|
| 기준 | 3289 / 3289 | 3/3 | 38538 | 40.4 | 5.09 |
| 풀 ×2 | 6578 / 3289 | 3/3 | 38406 | 39.6 | 5.14 |
| 풀 ×2 + 반 죽임 | 6578 / 1799 | 3/3 | 26628 | 31.4 | 4.75 |
| 반 압축 제거 (대조) | 1794 / 1794 | 3/3 | 26073 | 31.9 | 4.62 |

- 풀 ×2 의 비용 +0.05 ms (죽은 칸은 이웃이 없어 바로 끝난다). 죽임 = 압축 제거와 같은 모양 (정점 2% · 최고점 0.5cm 차 — GPU 시뮬은 실행마다 조금 다르다, M1 #29 참고).
- 남은 것: T2 에서 GPU 쪽에서 직접 죽이기 (지금 `KillParticles` 는 CPU 전량 내려받기 · 올리기).
