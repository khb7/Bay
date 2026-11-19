# 너울 조건 육지쪽(onshore) 이동 — 문헌 조사

상태: 1차 조사 (2026-11-19). [V] = 이번에 원문/초록으로 확인, [M] = 기억 기반, 원문 확인 필요.

## 0. 결론
- NLSW + 순간 u 기반 MPM은 구조적으로 해안쪽 이동이 약하다: 깊이평균 u는 velocity skewness는 일부 담지만 **acceleration skewness(파면 앞쪽 급경사)**, **경계층 위상지연**, **swash에서 침투 비대칭**, **backwash 때 부유사 지연 침강**을 놓친다. 결과적으로 backwash(얇고 오래가는 흐름)가 우세해져 근해로 이동한다.
- 지금 실험 조건(H0=1 m, Tp=12 s, ws≈0.035 m/s, Ω≈2.4, H0/L0≈0.0045)은 Kraus 등 기준으로 **accretion(berm) 예측**이므로 모델이 틀렸다고 볼 근거가 있다(§4).
- 우선 추천: (1) 가속도 skewness 항 / SANTOSS 계열 준정상 공식을 쓰는 bedload (물리 유도, 파라미터 적음), (2) swash 부유사 이월(pre-suspension + settling lag) 정비, (3) 침투는 0.3 mm 모래에서는 효과가 작으니 단독 해결책으로 쓰지 않는다.

## 1. 메커니즘별 정리

| 메커니즘 | 크기 / 효과 | 파라미터 | 깊이평균 코드에서의 구현 | 물리 유도 vs 튜닝 |
|---|---|---|---|---|
| Velocity skewness (Sk) | 쇄파대 바깥~shoaling 구역 onshore의 주된 원인. 쇄파대 안에서는 undertow에 밀림 | 없음(phase-resolving이면 자동) | NLSW/NH가 비대칭 파형을 직접 풂 → 순간 u³ 형태 MPM이면 이미 일부 반영 | 물리 |
| Acceleration skewness (As) | Hoefel & Elgar 2003 (Science 299:1885, doi:10.1126/science.1081448) [M]: Duck 1994 sandbar 육지쪽 이동을 u³ 모델로는 못 재현, 가속도 항 추가로 재현. Drake & Calantoni 2001 (JGR 106 C9, doi:10.1029/2000JC000611) [M]: DEM 입자 시뮬레이션에서 sheet-flow 순수송이 a_spike 에 비례 | Hoefel & Elgar은 K_a(≈1.4e-4 m s) 와 임계 a_cr 둘 다 **튜닝** | bedload에 q_a = K_a (a_spike − sgn·a_cr) 추가; a_spike = ⟨a³⟩/⟨a²⟩ | 개념 물리, 계수 튜닝 |
| 경계층 위상지연 / 준정상 sheet-flow (SANTOSS) | Van der A et al. 2013 (Coast. Eng. 76:26–42, doi:10.1016/j.coastaleng.2013.01.007) [M]: 반주기별 Shields + 위상지연 + progressive-wave streaming. 비대칭·skewed 진동류 실험 226개 재현 | 공식 상수는 실험 보정됨(새 자유파라미터는 없음) | 파마다 crest/trough 반주기를 분리해야 함 → phase-resolving 코드에선 zero-crossing 분석으로 적용 가능 (SWASH morph 확장 연구들, Rafati et al. 2021 등 [M]) | 반경험적이지만 공개 보정값 그대로 사용 가능 → 사실상 무튜닝 |
| XBeach facSk/facAs (facua) | wave-averaged 코드용: u_a = (facSk·Sk − facAs·As)·u_rms. 기본값 facSk=0.1, facAs=0.1 (구 facua=0.1). 모래사장 침식 과다 보정용으로 0.15–0.3 까지 흔히 튜닝 [M]. 너울 accretion 재현은 여전히 부실하다는 보고 다수 (Roelvink et al. 2018 Coast. Eng. 134:103, doi:10.1016/j.coastaleng.2017.07.010 의 "surfbeat" 한계 논의) | **순수 튜닝** | XBeach surfbeat 전용. NLSW(phase-resolving)에선 불필요/중복 | 튜닝 |
| XBeach-G (자갈) | McCall et al. 2014/2015 (Coast. Eng. 91:231, doi:10.1016/j.coastaleng.2014.06.007) [M]: NH + 지하수(Darcy/Forchheimer) + **가속도 포함 bedload**(Fredsøe식 + inertia 항). berm 형성 재현 | 가속도 계수 일부 보정 | 사용자 구조(NLSW+침투+MPM)와 가장 비슷 → 모래엔 K 너무 작아 침투 효과 약함 | 반물리 |
| Swash 침투/exfiltration | Masselink & Li 2001 (Mar. Geol. 176:139–156, doi:10.1016/S0025-3227(01)00161-X) [V]: 침투량 > uprush 부피의 ~2% 일 때부터 onshore 비대칭 의미 있음 → **K > ~1 cm/s ≈ d > 1.5 mm**. Turner & Masselink 1998 (JGR 103 C13, doi:10.1029/98JC02606) [M]: 침투가 uprush에서 effective weight↓/stabilization 효과로 상쇄 관계, 세립사에선 효과 작음. Butt et al. 2001 (Coast. Eng. 42:35, doi:10.1016/S0378-3839(00)00046-6) [M]: 모래에선 d>~0.4–0.6 mm 부근에서 침투가 순수송 방향을 바꿀 수 있다고 보고 | K(물성)만 — 튜닝 아님 | 이미 구현(Green–Ampt) | 물리 |
| Pre-suspension + settling lag (swash 부유사) | Pritchard & Hogg 2005 (JFM 539, doi:10.1017/S0022112005005379) [M]: bore 붕괴 해석해에서 uprush가 실어온 부유사가 swash 상부에 퇴적 → onshore 성향. Kelly & Dodd 2010 (JFM 664:340, doi:10.1017/S0022112010003782) [M]: NLSW+Exner 결합 swash, 부유사+침강 지연이 순수송 방향을 결정. Zhu & Dodd 2015 (JFM 762:110, doi:10.1017/jfm.2014.643) [M]: 결합 시 순 육지쪽 퇴적 가능. Briganti et al. 2016 (Coast. Eng. 115:22, doi:10.1016/j.coastaleng.2015.09.005) [M] 리뷰: swash 순수송은 모델링 선택(부유사/침강/bed friction)에 매우 민감 | 침식률 계수, 침강(ws 물성), 하상 마찰 | 사용자 코드 optional 부유사 모듈과 동일 구조 | 물리, 단 pickup 계수 경험적 |
| uprush/backwash 마찰·경계층 차이 | 측정: uprush τ_b 가 backwash보다 ~1.5–2배 (Conley & Griffin 2004, Masselink et al.) [M]. 깊이평균 코드에선 별도 f_up>f_back 로 넣는 것이 일반적 | f 비율 — **튜닝** 성격 | Kelly & Dodd/Briganti가 민감도 실험 | 반경험 |
| Jacobsen & Fredsøe | Jacobsen & Fredsøe 2014 (J. Waterway Port Coast. Ocean Eng. 140, doi:10.1061/(ASCE)WW.1943-5460.0000233) [M]: RANS(OpenFOAM) 쇄파 bar 형성; 너울 조건에서 onshore bar 이동 재현은 bed-load 비정상 효과(가속도/경계층) 필요 | – | 3D/2DV RANS, 깊이평균 아님 | 물리 |

## 2. 질문 3 — 침투
- 전환 임계: K ≈ 1e-2 m/s (d ≈ 1.5 mm) [V, Masselink & Li 2001 초록].
- 0.25–0.3 mm 모래 K: Hazen K≈C·d10² (C≈0.01, d10≈0.2 mm) → ~4e-4 m/s. 현장 측정 1e-4–1e-3 m/s 범위 [M]. **5e-4 m/s 는 현실적**. 즉 이 모래에선 침투가 swash 순수송을 뒤집지 못하는 게 정상이며, 침투를 키워서 berm을 만드는 건 비물리 튜닝이다.

## 3. 추천 (물리 근거 순)
1. **반주기 분해 SANTOSS(Van der A 2013) bedload** — 셀별로 u(t) zero-crossing → crest/trough 반주기 Shields, 위상지연, acceleration skewness 자동 포함. 공개 보정 상수만 사용(새 자유파라미터 0). 단점: swash(마른/젖은 반복)에선 정의가 애매 → swash엔 2를 적용.
2. **Swash 부유사 이월 정비 (Kelly & Dodd 2010 / Zhu & Dodd 2015)** — bore 전면 pickup으로 uprush가 실어온 부유사가 flow reversal 때 settling lag로 상부에 퇴적. 이미 있는 모듈을 켜고 swash 상부에서 실제 침강(ws) 적용·건조셀에 퇴적 처리를 확인. 경험 계수는 pickup 하나.
3. **가속도 항 추가 (Hoefel & Elgar 2003 / Drake & Calantoni 2001 / XBeach-G 방식)** — 구현 간단하나 K_a, a_cr 튜닝 필요. 1·2가 부족할 때만.
- 비추천: XBeach facSk/facAs 이식(surfbeat용 튜닝 노브), 침투 K 인위 증가.

## 4. 합격 기준 (검증 필요)
- **Kraus, Larson & Kriebel 1991 (Coastal Sediments '91) [M]**: 침식/퇴적 경계 H0/L0 = M (H0/(ws T))³, M ≈ 0.00070 (대형 수조 LWT 기준), 그보다 작으면 accretion. 본 실험: H0/L0 ≈ 1/(1.56·144) ≈ 0.0045, M·Ω³ ≈ 0.0007·2.5³ ≈ 0.011 → **accretion 영역** (원문 상수 확인 필요). 폭풍(Tp 5 s): H0/L0≈0.026 > M·(≈6)³ 이면 erosion 영역 → 모델은 두 조건이 같은 방향 → 실패 판정 가능.
- Dean number: Ω < ~1–2 accretion, > ~2–6 erosion (Dean 1973; Kraus 1991 평균 경계 ~3.2) [M]. Ω≈2.5 는 경계 근처 → H0/L0 결합 기준(위)을 주 판정으로.
- 정량: Baldock et al. 2011 (Coast. Eng. 58:139?) 대형/소형 수조 accretion 실험의 shoreline 전진·berm 부피 성장률과 비교 [M, 수치 미확인]. 최소 기준: swash 상부(SWL~+R2%) 순부피 변화 > 0, 무게중심 육지쪽 이동, SWL 아래 bar 형성 없음.

## 출처 (검증)
- Masselink & Li 2001 초록: https://repository.geologyscience.ru/handle/123456789/26452
- 나머지 DOI는 기억 기반 [M] — 인용 전 확인 필요.
