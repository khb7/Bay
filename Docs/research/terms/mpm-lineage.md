# MPM 계보

> [← 용어집](../glossary.md) › [6. 수치해석 / 시뮬레이션 기법](../glossary.md#6-수치해석--시뮬레이션-기법)

| | |
|---|---|
| PIC (Harlow, 1960s) | 플라즈마 물리. 입자+격자의 원조 |
| FLIP (Brackbill & Ruppel, 1986) | 유체용 개량 |
| **[MPM](mpm.md) (Sulsky, Chen, Schreyer, 1994)** | **고체 역학으로 확장 = MPM 탄생** |
| **Stomakhin et al. (2013), "A material point method for snow simulation"** | **그래픽스 도입. 〈겨울왕국〉의 눈** |
| Klár 2016 / Tampubolon 2017 | UCLA 계보. 모래 → 모래+물 |

**입문 교재**: Jiang, Schroeder, Teran, Stomakhin, Selle (2016),
*"The Material Point Method for Simulating Continuum Materials"*, SIGGRAPH Course Notes.
논문보다 훨씬 친절하다.

**우리가 안 쓰는 이유**: 매 프레임 P2G/G2P가 전 입자 × 격자 커널 27점을 왕복 → 무거움.
격자 해상도가 곧 디테일 한계인데 비용은 세제곱으로 늘어남. GPU MPM으로도 16ms [예산](budget.md)은 무리.
→ **근거와 파라미터 곡선의 출처로만 쓰고, 구현은 [하이트필드](heightfield.md)(D) + [PBD](pbd.md)(C)로.**

---

**관련 문서** — [MPM (Material Point Method)](mpm.md) · [PBD (Position Based Dynamics)](pbd.md) · [예산 (budget)](budget.md) · [하이트필드 (heightfield) ★](heightfield.md)
