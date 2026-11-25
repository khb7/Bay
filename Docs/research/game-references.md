# 게임 · 실시간 그래픽 레퍼런스 — 파도 · 해안 · 모래 (2026-11-25 조사)

> 서브에이전트 두 갈래 조사 정리. ✓ = 1 차 자료 (슬라이드 · 논문 원문), △ = 2 차 (기사 · 문서 요약 · 포럼).
> 결론: **해변 규모의 쇄파 물리 · swash · 파도에 의한 모래 이동 · IG/setup 을 하는 출시작은 찾지 못함** — 우리 차별점이자 참고할 선례가 없는 영역.

## 1. 파도 · 해안

| 사례 | 방식 | 해안 · 쇄파 | 비용 · 메모 |
|---|---|---|---|
| Horizon Forbidden West (SIGGRAPH 2022 Advances, Malan) ✓ | 바다 FFT · 해안은 아티스트 곡선 (wavefront quad, Coons 보간) | Houdini 쇄파 단면 **하나** 를 변위 텍스처로 bake → compute 로 메시 변형. foam 은 정점색 (밀도 부족 popping) | "Surface foam … we'd like to improve" — 해변 foam 은 한계로 꼽음 |
| Sea of Thieves (SIGGRAPH 2018 Talk) ✓ | Tessendorf FFT · foam = choppiness + 깊이 교차 + feedback blur | 갑판 물 · 폭포 · 개울만 GPU SWE (Mei 2007 virtual pipes). 해변 쇄파 없음 | |
| Atlas (GDC 2019, NVIDIA) ✓ | FFT 4 대역 분할 · 시간만으로 결정적 (서버 동기) | 해안 없음. 항적 · 폭발 = Tessendorf eWave | 풍파 0.5 ms · 상호작용 0.6–0.8 ms (RTX 2080) |
| Uncharted 1–3 (GDC 2012) △ | Yuksel wave particles 타일 + Gerstner swell | 큰 파 = B-spline 수작업. 강 = flow map | geometry clipmap LOD |
| AC III/IV · La Forge 2024 △/✓ | 미리 계산한 FFT 타일 · tiling-and-blending 비주기 바다 | 해안 마스크 (512², 포럼 △) | |
| Crest (Unity) △/✓ | FFT/Gerstner + 다해상도 동적 파 sim | 수심 반파장 이하 감쇠 · 얕은 곳 자동 foam. 문서: "realistic shoreline waves … challenging open problem" ✓ | |
| UE5 Water plugin △ | Gerstner (기본 16 파) · 수심 감쇠 | 쇄파 없음. Niagara Shallow Water (pool · 강) | Still Wakes the Deep: Niagara 2D SWE + readback |
| Fluid Flux (UE 플러그인) △ | 2D SWE + wet-dry · foam/wave 이류 · wetness | 해변 데모 — **우리 구조에 가장 가까운 상용품** | 3.0 Gerstner 추가 |
| MSFS △ | 해변 surf 정적 텍스처 (포럼) | | |

연구 → 게임:
- **Chentanez & Müller 2010 (SCA)** ✓: 경사 지형 SWE + wet-dry, 쇄파 · 폭포를 spray/foam 입자로 (질량 · 운동량 교환), PML 비반사 경계, 이류 FFT 세부. 해변 장면 GTX480 높이장 0.75 ms (전체 9.9 ms).
- Chentanez & Müller 2011 tall cell ✓ · Jeschke 2018 Water Surface Wavelets △ · Jeschke 2020 경계 인식 절차적 파 △ · **Jeschke & Wojtan 2023 SWE + Airy 분산 표면파 분해 (512 m 실시간) △ — 우리 Boussinesq 의 직접 대안** · Wave Curves (Weta 2020, 오프라인) △.

## 2. 모래 · 지형

| 사례 | 방식 | 메모 |
|---|---|---|
| From Dust (GDC Europe 2010, Bel & Chahi) ✓/△ | 격자 물 · 용암 · 침식 · 퇴적 (진흙 = 퇴적물 100 % 물) | CPU SIMD 128 · 멀티스레드 (PS3 SPU), 30 fps △. **유일한 실시간 퇴적 게임** — 하천 규모, 해안 아님. 격자 크기 미공개 |
| Journey (GDC 2013) △ | 셰이더만: diffuse contrast · 랜덤 노멀 · Fresnel rim · ocean specular · glitter · 경사별 ripple 노멀 4 장 | 마른 모래 룩 참고 |
| Batman Arkham Origins 눈 (GDC 2014) ✓ | 발목 높이 ortho 카메라로 아래에서 렌더 → ping-pong 누적 → 조금씩 빼서 다시 채움 | 높이맵 min(512, ¼ 표면) · 갱신 < 1 ms (PS3/360) · 2–4 MB |
| Rise of the Tomb Raider (GPU Pro 7) ✓ | Deferred snow deformation — 렌더 패스에서 함몰 · 솟음 재구성 · adaptive tessellation | 발자국 테두리 솟음 |
| Spintires / MudRunner ✓ | 16 m 블록 · 기본 25² + 진흙 128² 텍스처, 진흙 높이 = 기본 지형 상대 | 물리 CPU · 렌더 GPU, 차량 근처 활성 블록만 |
| Uncharted 3 사막 △ | 발자국 투영 · 사구 붕괴는 Houdini bake | |
| Noita / Sandspiel / Teardown △/✓ | falling-sand CA · 복셀 | 모래 해변엔 직접 해당 없음 |

연구:
- **Mei, Decaudin & Hu 2007** ✓/△: virtual pipe SWE + 용량 기반 침식 · 퇴적 (C = Kc sinα |v|) + semi-Lagrangian 이류 + talus — 사실상 표준, 우리와 같은 계보. Št'ava 2008 (다층 재료) △ · **Jákó & Tóth 2011 thermal erosion (안식각 재분배)** △ · Krištof 2009 SPH + Euler 침식 ~25 k 입자 ✓ · Macklin 2014 Flex 입자 모래 △ · MLS-MPM 모래 (데모 규모) △.
- **젖은 표면 (Lagarde, Remember Me)** ✓: `factor = lerp(1, 0.2, Porosity)` · `Diffuse *= lerp(1, factor, WetLevel)` · `Gloss = lerp(1, Gloss, lerp(1, factor, 0.5·WetLevel))`, 물층 두꺼우면 노멀 평평하게. 근거 Jensen 1999 (기공 속 물의 내부 반사).

## 3. 빌려올 것 (우리 구조 기준)

1. **원거리 FFT ↔ 근해 sim 결합**: 바깥 경계 입사파와 같은 스펙트럼 · 시드로 FFT 원거리를 그리고 sim 가장자리에서 blend (Atlas 결정적 FFT · Chentanez PML).
2. **sim 위 세부**: 격자 아래 잔물결을 sim 유속으로 이류한 FFT/flow-map 노멀로.
3. **쇄파 시각화 하이브리드**: sim 의 쇄파 판정 (나이 · ν) 을 파라미터로 HFW 식 bake 단면 변형 · foam 구동 — 물리는 sim, 모양은 bake.
4. **foam = 소산**: 쇄파 소산 (ν · 에너지 손실) 을 foam 원천으로, feedback blur + 이류 (SoT · Atlas). 물리와 일관.
5. **젖은 모래**: wet-dry 마스크 + 최대 처오름 이력 + 증발 감쇠를 WetLevel 로 Lagarde 식 (porosity 0.6–0.8 △). swash 물막 구간은 평평한 노멀, 마른 곳은 Journey 식 glitter · ripple.
6. **사태 (talus)**: 매 프레임 1–2 회, 안식각 (마른 32–34° △) 을 **수분에 따라 바꿔** 젖은 모래 벽 유지.
7. **두 층 높이**: 저해상도 물리 높이 (퇴적 이송) + 고해상도 세부 오프셋 (발자국 · ripple, Batman 식 ortho 캡처 · 다시 채움, 파가 지나가면 지움).
8. **입자 ↔ 높이장 교환**: 쇄파 · 발길질 · 처오름 끝에서만 높이를 입자로, 착지하면 퇴적 (질량 보존 카운터).
9. **LOD**: 대역 분할 + clipmap, sim 고해상도는 해안 근처만.

## 주요 링크
- advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Water-Malan.pdf
- history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf
- gpuopen.com/gdc-presentations/2019/gdc-2019-agtd6-interactive-water-simulation-in-atlas.pdf
- crest.readthedocs.io/en/stable/user/shallows-and-shorelines.html
- matthias-research.github.io/pages/publications/hfFluid.pdf · tallCells.pdf
- research.nvidia.com/labs/prl/shallow-water-simulation
- gdcvault.com/play/1013667 (From Dust) · 1017742 (Journey) · 1020177 (Batman 눈) · 1025695 (Noita)
- 80.lv/articles/breakdown-mud-and-water-of-spintires
- seblagarde.wordpress.com/2013/04/14/water-drop-3b-physically-based-wet-surfaces
- old.cescg.org/CESCG-2011/papers/TUBudapest-Jako-Balazs.pdf · alanzucconi.com/2019/10/08/journey-sand-shader-1

## 4. 시각 검증 기준 — 해안 영상 · 젖은 모래 광학 (2026-11-25 조사)
- **Narrabeen Argus 영상국** ✓ (UNSW WRL, 2004 설치, 44 m 건물): 10 분 평균 timex 를 낮 30 분마다. 공개 창구 ci.wrl.unsw.edu.au 접속 거부 (라이선스 △). 2016 ECL 침식 timelapse (figshare 4867349) ✓.
- **Dryad 91 년 해안선** ✓ (doi:10.5061/dryad.70rxwdcd4): 해안선 CSV 1930–2021 · ERA5 파랑 · 조위 · 전면 경사 GeoJSON — 영상 없음, 처오름/해안선 검증용.
- Coastalwatch (data.gov.au) △: 웹 링크뿐, 라이선스 미지정. CoastSnap △: 공개 아카이브 없음.
- **timex 밝은 띠 = 평균 쇄파 위치** ✓ (Lippmann & Holman 1990, 사주 위치 대리) → 게임 화면 10 분 평균 단면 밝기의 봉우리 위치 · 폭을 Narrabeen timex 와 비교.
- **거품 감쇠** ✓ (Callaghan, Deane & Stokes 2012 JGR, 쇄파 552 개): 개별 0.2–10.4 s, 실효 평균 1.4–4.8 s, 면적 준지수 감소, 큰 패치가 오래 감. (외해 백파 기준 — 쇄파대 잔류 거품은 더 오래.) → 거품 τ 1.5–5 s, 쇄파 크기에 비례.
- **Carini et al. 2015 JGR** ✓: 열적외선 roller 길이 → Duncan 식 소산, 활성 roller 거품과 잔류 거품 구분 — T4 · T8 소산 진단 교차 확인에 쓸 수 있음.
- **젖음/마름 경계 ≠ 물가선** ✓ (Huisman 2011): 썰물 때 높이 차 최대 2.5 m → 젖은 띠는 최대 처오름 위에 남고 늦게 마름.
- **젖은 모래 광학**: Lekner & Dorf 1988 ✓ — 알베도 마름 0.182 → 젖음 0.091 (**비 0.50**), 원인 = 수막 내부 전반사. Nolet 2014 ✓ (PLoS ONE): 가시광은 함수율 5–25 % 구간 기울기 ≈ 0 → **사실상 마름/젖음 2 단계**. → albedo 를 함수율 ~5 % 미만 1.0×, 이상 0.5–0.6× 계단형 + 포화 시 정반사. 선형 감소는 문헌과 다름.
