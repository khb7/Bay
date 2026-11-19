# realism-gaps.md 문헌 검증 (2026-11-19)

> 범위: 웹 검색으로 확인 가능한 것만. 원문 전문 미확인 항목은 "미검증" 또는 "2차" 표시. 상태: ✔ 확인 · △ 부분/2차 · ✘ 정정 · ? 미검증.

| # | 문서의 주장 | 검증 결과 / 정정 | 상태 | 출처 |
|---|---|---|---|---|
| 1a | (문서엔 Dean 수 언급 없음, 요청 항목) Ω = H_b/(w_s·T) | Wright & Short 1984 해빈 상태: **H_b(쇄파고)** 사용, Ω<1 reflective, 1~6 intermediate, Ω>6 dissipative. 원문 미확인(일반 교과서 값) | △ | Wright & Short 1984, Mar. Geol. 56:93–118 |
| 1b | 침식/퇴적 판정 | Dean 1973 / Kriebel et al. 1986: **H₀(심해파고)/(wT)** fall-time 변수. Kraus, Larson & Kriebel 1991 "Evaluation of beach erosion and accretion predictors" (Coastal Sediments '91, 572–587)가 H₀/L₀ vs H₀/(wT) 판정 비교 — 현장 99 사례(Kraus & Mason). 정확 임계값(SBEACH 계열 H₀/L₀ = 0.0007·(H₀/wT)³ 경계)은 원문 미확인 | ? | ascelibrary 10.1061/(ASCE)0733-950X(1992)118:2(193); DTIC ADA254663 |
| 1c | Dalrymple 1992 | profile parameter P = gH₀²/(w³T), 경계 ~9000 (H₀ 심해) — 기억값, 원문 미확인 | ? | Dalrymple 1992, JWPCOE 118(2):193 |
| 2 | 젖은 모래: 반사율 W에 지수 감소, 포화 시 마른 대비 ~40~60%, W 수%에서 이미 진해짐 | **지수 모델 ✔**, 포화도(degree of saturation)로 표현하면 토양 종류 무관하게 유사. **가시·NIR은 낮은 수분에서 이미 포화**(SWIR보다 훨씬 빨리) → "수%에서 진해짐" 지지. 단 일부 토양은 임계수분 이후 반사율 **재상승**(Liu 2002, 표면 수막 경면) — 포화 광택 항목과 일치. 40~60% 수치는 원문 미확인 | △ | Lobell & Asner 2002, SSSAJ 66:722–727, doi:10.2136/sssaj2002.7220 |
| 2b | Twomey 1986, Ångström 1925 | 메커니즘(굴절률 매칭 → 내부 다중산란·흡수 증가) 고전 문헌. 수치 미확인 | ? | Twomey, Bohren & Mergenthaler 1986, Appl. Opt. 25:431 |
| 3a | Sunamura 1984 tanβ 식, D별 경사 값 | 식 형태 원문 미확인. Bujan 2019: **78개 현장 연구** 메타분석, D50↑ → tanβ↑. 단 **D50>~1mm 혼합 자갈은 상관 거의 없음**, 조차·modal 파랑도 필요(Woodruff 2021) | △ | Bujan, Cox & Masselink 2019, Mar. Geol. 417:106012; Woodruff 2021 Mar. Geol. |
| 3b | Takeda & Sunamura 1982 B_h ≈ 0.125·H_b^(5/8)·(gT²)^(3/8) | 웹에서 식 직접 확인 실패(CEM 인용으로 기억과 일치). 대안: 소형 수조 B_h ≈ 0.59·H_b(Sunamura 1989, 2차·판독 불확실), 자갈 해빈은 B_h ≈ R2% 가정 | ? | CEM EM 1110-2-1100 Part III-3 |
| 4 | 모세관 상승 h_c ≈ 0.3/D(mm) cm → D=0.2mm 20~40cm (Turner & Nielsen 1997) | **식 자체 단위 오류**: 0.3/0.2 = 1.5 cm 가 되어 20~40cm와 모순. 통상 경험식 h_c ≈ 0.153/r (cm, r=공극 반경 cm) → 세사~중사 **수십 cm**(중사 0.3mm ≈ 15~35cm). 숫자 범위는 타당, 식 표기 정정 필요. Turner & Nielsen 1997 제목은 "Rapid water table fluctuations within the beach face: implications for swash zone sediment mobility?" (Coastal Eng. 32) — h_c 수치 출처로 부적절, Nielsen & Perrochet 2000(모세관 프린지 실험)이 적절 | ✘(식) | Turner & Nielsen 1997 Coastal Eng.; Nielsen & Perrochet 2000 Adv. Water Res. |
| 4b | exit point/seepage face (Turner 1993), 조석 감쇠 길이 L=√(2KD/(nω)) (Nielsen 1990) | Turner 1993 현장 측정에서 모세관 프린지 존재 확인(Li et al. 인용). 식 형태는 Nielsen 1990 선형 해와 일치(기억). L 수십 m 타당 | △ | Nielsen 1990 WRR 26(9) |
| 5 | 표면 증발 수 mm/day, 표면 1~2cm 수 시간 내 마름 (Namikas & Sherman 1995) | 미검증 | ? | — |
| 6 | cusp 간격 λ ≈ 1.5~2 × swash excursion | 자기조직(Werner & Fink 1993, Science 260:968) — **간격 ∝ swash excursion 선형** ✔. Coastal Wiki 정리 계수 ≈ **1.06~2.15** (Dean-Maurmeyer 모델). edge wave 설(Guza & Inman 1975)과 관측으로 구분 불가, 둘 다 유효 | ✔ (1~2배로 넓힐 것) | coastalwiki.org/wiki/Beach_cusps; Coco et al. 2000 JGR 105:21991 |
| 7 | Reynolds dilatancy: 포화 조밀 모래 밟으면 발 주위 순간 마름(밝아짐) | ✔ 실재. 전단 → 공극 증가 → 표면수가 아래로 빨려 들어가 하얗게/마른 듯 보이다가 물이 다시 차오름 | ✔ | Reynolds 1885 Phil. Mag. 20:469; Kabla & Senden 2009 PRL 102:228301 |
| 8a | 안식각 마른 ~32° | 30~35° 범위 일반값 ✔ | ✔ | (교과서) |
| 8b | 강도 W≈1~5% 거의 평탄 최대, 포화 근처 급감 (Pakpour 2012) | Pakpour et al. 2012 "How to construct the perfect sandcastle", Sci. Rep. 2:549 — **최적 수분 ~1% (부피)**, 너무 많으면 불안정. 최대 높이 ∝ 기둥 반경^(2/3) (2차). Hornbaker 1997 / Scheel 2008: 강도는 넓은 W 구간에서 거의 일정 — 문서와 일치 | ✔ | doi:10.1038/srep00549 |

## 정정 요약
1. 2-1의 h_c ≈ 0.3/D(mm) cm 식 → 수치 모순. 범위(세사 20~40cm)만 유지, 식은 h_c ≈ 0.153/r 또는 표값으로 교체. 출처를 Turner & Nielsen 1997 → Nielsen & Perrochet 2000 / Turner 1993로.
2. cusp 계수 1.5~2 → ~1~2 (Dean-Maurmeyer 정리 1.06~2.15).
3. 경사-입경 상관은 D50 ≤ ~1mm 모래에서만; 자갈 혼합은 무상관 (Bujan 2019 해석).
4. 젖은 모래 반사: 가시광은 낮은 수분에서 이미 포화 ✔. 단 포화 이상에서 반사 재상승 가능 → 경면 항과 분리할 것. 40~60% 수치는 미확인.
5. Dean 수 임계: 상태분류(Wright & Short)는 H_b, 침식/퇴적 판정(Dean/Kriebel/Kraus/Dalrymple)은 H₀ 사용 — 혼용 금지. 정확 임계값 미검증.
6. 미검증: Takeda-Sunamura 식, Namikas & Sherman 증발률, Twomey/Ångström 수치, Sunamura 1984 tanβ 식.
