# -*- coding: utf-8 -*-
"""terms/ 및 papers.md 본문에서 용어 첫 등장 1회를 해당 문서로 링크한다.
   dry run이 기본. --apply 를 붙여야 실제로 쓴다.

   ⚠ 멱등하지 않다. 이미 링크된 문서에 다시 --apply 하면
      "그 다음 등장"에 링크를 하나 더 단다. 새 문서를 여러 개 추가한 뒤
      반드시 dry run으로 목록을 먼저 확인할 것.
      오탐이 보이면 SKIP / OVERRIDE / NEG_PREFIX / PAPERS_SKIP 을 손본다."""
import re, sys, pathlib, collections

ROOT = pathlib.Path(r"C:\Users\onebe\Bay\Docs\research")
TERMS = ROOT / "terms"
APPLY = "--apply" in sys.argv

HANGUL = r'[\uac00-\ud7a3]'
PARTICLE = set("은는이가을를의에와과도로만나란든뿐써서부터까지보다처럼조차마저야밖씩")

# 자동 링크 대상에서 제외 — 용어가 아니라 설명형 제목이거나 너무 일반적인 것
SKIP = {
    "svd-other-uses", "why-simulate-dilation", "stiffness-in-our-project",
    "mechanism-2-vs-3", "sand-composition", "sand-properties", "mpm-lineage",
    "cohesion-vs-adhesion", "why-no-explicit-pressure", "engine-vs-transmission",
    "tamping-vs-stomping", "wet-sand-darkening", "wet-granular-regimes",
    "why-quartz", "lagrangian-vs-eulerian", "mpm-particle-vs-grain",
    "graph-coloring", "dilation-compaction", "elastic-plastic",
    "plasticity-as-clamp", "wave-height-period-length", "effective-saturation",
    "helper-class", "subsystem", "pic-flip-apic", "gauss-seidel-jacobi-sor",
    "iteration-relaxation", "boundary-condition", "deviatoric-volumetric",
}

# 기본 파생이 나쁜 것만 직접 지정
OVERRIDE = {
    "drying-stage-1": ["등속 건조기"],
    "drying-stage-2": ["감속 건조기"],
    "characteristic-length": ["특성 길이"],
    "dean-number": ["Dean 수", "Dean number", "무차원 침강속도"],
    "reflective-dissipative": ["reflective", "dissipative"],
    "hazen-relation": ["Hazen"],
    "significant-wave-height": ["유의파고"],
    "weakly-compressible": ["약압축성"],
    "semi-lagrangian-advection": ["반라그랑주"],
    "render-target-srv-uav": ["Render Target"],
    "uniform-buffer": ["상수 버퍼"],
    "neighbor-grid3d": ["Neighbor Grid3D"],
    "spatial-hash": ["공간 해시"],
    "virtual-heightfield-mesh": ["Virtual Heightfield Mesh", "VHM"],
    "marching-cubes": ["Marching Cubes", "Surface Nets"],
    "depth-picking": ["깊이 버퍼 픽킹"],
    "gpu-readback": ["FRHIGPUBufferReadback", "GPU 리드백"],
    "sync-async": ["비동기"],
    "sleep-wake": ["sleep / wake"],
    "shields-parameter": ["Shields"],
    "iribarren-number": ["Iribarren"],
    "hjulstrom-curve": ["Hjulström"],
    "settling-velocity": ["침강속도"],
    "seepage-force": ["침투력", "유효 중량"],
    "suction-blowing": ["흡입 / 분출"],
    "bed-shear-stress": ["바닥 전단응력"],
    "infiltration-exfiltration": ["침투 / 유출"],
    "darcy-law": ["Darcy"],
    "richards-equation": ["Richards"],
    "field-capacity": ["포장용수량", "field capacity"],
    "capillary-bridge": ["모세관 다리"],
    "capillary-fringe": ["모세관 상승대"],
    "drucker-prager": ["Drucker-Prager"],
    "hencky-strain": ["Hencky 변형", "Hencky"],
    "constitutive-model": ["구성 모델"],
    "polar-decomposition": ["극분해"],
    "deformation-gradient": ["변형 그래디언트"],
    "critical-state": ["임계상태"],
    "reynolds-dilatancy": ["Reynolds 팽창"],
    "angle-of-repose": ["안식각"],
    "friction-angle": ["마찰각"],
    "thermal-erosion": ["열침식"],
    "hydraulic-erosion": ["수력 침식"],
    "shape-matching": ["shape matching"],
    "symmetric-linear-system": ["대칭 선형계"],
    "explicit-implicit": ["명시적 / 암시적"],
    "wave-setup": ["wave setup", "처오름 상승"],
    "wave-group": ["파군"],
    "infragravity-wave": ["장주기파"],
    "depth-limited-breaking": ["수심 제한 쇄파"],
    "fully-developed-sea": ["완전발달해상"],
    "dispersive-sorting": ["분산 정렬"],
    "dispersion-relation": ["분산 관계"],
    "wind-sea-vs-swell": ["풍파", "너울"],
    "offshore-bar": ["사주"],
    "slurry-mudflow": ["슬러리"],
    "free-surface": ["자유표면"],
    "porous-medium": ["다공성 매질"],
    "cfl-condition": ["CFL"],
    "simulation-stages": ["Simulation Stages"],
    "channel-packing": ["채널 패킹"],
    "pipeline-stall": ["파이프라인 스톨"],
    "resource-transition": ["자원 상태 전이"],
    "compute-shader": ["컴퓨트 셰이더"],
    "surface-reconstruction": ["표면 재구성"],
    "return-mapping": ["리턴 매핑"],
    "neighbor-search": ["이웃 탐색"],
    "prefix-sum": ["prefix sum"],
    "radix-sort": ["radix sort"],
}

# 이 앞말이 붙으면 다른 뜻 — 링크하지 않는다
PAPERS_SKIP = {"latency"}

NEG_PREFIX = {
    "strain":  ["천수 ", "소성 ", "탄성 "],
    "latency": ["위상 "],
    "stress":  ["유효 "],
}

titles, forms, texts = {}, {}, {}
for f in sorted(TERMS.glob("*.md")):
    if f.name == "README.md":
        continue
    text = f.read_text(encoding='utf-8')
    slug = f.stem
    titles[slug] = text.splitlines()[0][2:].strip()
    texts[slug] = text
    if slug in SKIP:
        continue
    if slug in OVERRIDE:
        forms[slug] = OVERRIDE[slug]
        continue
    t = re.sub(r'^[\u2605\u26a0]\s*', '', titles[slug])
    t = t.split('\u2014')[0].strip()
    ko = t.split('(')[0].strip().strip('`').rstrip('.')
    fs = [ko] if ko else []
    for m in re.finditer(r'\(([^)]*)\)', t):
        inner = m.group(1).split(',')[0].strip()
        if inner and not re.search(HANGUL, inner):
            fs.append(inner)
    forms[slug] = [x for x in fs if len(x) >= 2]

# 긴 표현 우선
ORDER = sorted(forms, key=lambda s: -max(len(x) for x in forms[s]))


def protected_spans(text):
    """코드펜스 / 인라인코드 / 기존 링크 / 헤딩 / 인용 breadcrumb 구간"""
    spans, infence, pos = [], False, 0
    for line in text.split('\n'):
        end = pos + len(line)
        if re.match(r'^\s*```', line):
            infence = not infence
            spans.append((pos, end))
        elif infence or line.startswith('#') or line.startswith('> [\u2190'):
            spans.append((pos, end))
        pos = end + 1
    for m in re.finditer(r'`[^`\n]*`|\[[^\]]*\]\([^)]*\)', text):
        spans.append((m.start(), m.end()))
    return spans


def blocked(spans, a, b):
    return any(s < b and a < e for s, e in spans)


report = collections.Counter()
samples = collections.defaultdict(list)


def link_up(text, self_slug, prefix):
    spans = protected_spans(text)
    body_start = 0
    m = re.search(r'\n\n', text)
    if m:
        body_start = m.end()
    for slug in ORDER:
        if slug == self_slug:
            continue
        if self_slug is None and slug in PAPERS_SKIP:
            continue
        for form in forms[slug]:
            pat = re.compile(re.escape(form))
            hit = None
            for mm in pat.finditer(text, body_start):
                a, b = mm.span()
                if blocked(spans, a, b):
                    continue
                prev = text[a - 1] if a else ' '
                nxt = text[b] if b < len(text) else ' '
                if re.match(HANGUL, prev) or prev.isalnum() or prev in '/-#':
                    continue
                if re.search(r'\.md\)\s?$', text[:a]):
                    continue
                if nxt.isascii() and nxt.isalnum():
                    continue
                if re.match(HANGUL, nxt) and nxt not in PARTICLE:
                    continue
                if any(text[max(0, a - len(p)):a] == p for p in NEG_PREFIX.get(slug, [])):
                    continue
                hit = mm
                break
            if hit:
                a, b = hit.span()
                rep = "[%s](%s%s.md)" % (form, prefix, slug)
                samples[slug].append((self_slug, text[max(0, a - 28):b + 18].replace('\n', ' ')))
                report[slug] += 1
                text = text[:a] + rep + text[b:]
                spans = protected_spans(text)
                break
    return text


results = {}
for slug, text in texts.items():
    results[TERMS / ("%s.md" % slug)] = link_up(text, slug, "")
p = ROOT / "papers.md"
results[p] = link_up(p.read_text(encoding='utf-8'), None, "terms/")

print("추가되는 링크 총 %d개\n" % sum(report.values()))
for slug, n in report.most_common():
    print("%-32s %3d  | %s" % (slug, n, samples[slug][0][1][:70]))

if APPLY:
    for path, text in results.items():
        path.write_text(text, encoding='utf-8')
    print("\n적용 완료")
else:
    print("\n(dry run — 적용하려면 --apply)")
