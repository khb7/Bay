# -*- coding: utf-8 -*-
"""terms/ 전체를 훑어 '관련 문서' 푸터를 재계산한다. 몇 번 돌려도 결과가 같다."""
import re, pathlib, collections

ROOT = pathlib.Path(r"C:\Users\onebe\Bay\Docs\research")
TERMS = ROOT / "terms"
FOOTER = re.compile(r'\n*---\n\n\*\*관련 문서\*\* — .*\n*\Z')

titles, bodies = {}, {}
for f in sorted(TERMS.glob("*.md")):
    if f.name == "README.md":
        continue
    text = f.read_text(encoding='utf-8')
    text = FOOTER.sub("\n", text)
    text = re.sub(r'\n*(?:---\n)+\Z', "\n", text)
    bodies[f.stem] = text.rstrip() + "\n"
    titles[f.stem] = text.splitlines()[0][2:].strip()

out = collections.defaultdict(set)
for slug, text in bodies.items():
    for m in re.finditer(r'\]\(([a-z0-9-]+)\.md[#)]', text):
        if m.group(1) != slug and m.group(1) in bodies:
            out[slug].add(m.group(1))
inc = collections.defaultdict(set)
for a, bs in out.items():
    for b in bs:
        inc[b].add(a)

for slug, text in bodies.items():
    rel = sorted(out[slug] | inc[slug], key=lambda s: titles[s])
    if rel:
        text += "\n---\n\n**관련 문서** — " + " · ".join(
            "[%s](%s.md)" % (titles[s], s) for s in rel) + "\n"
    (TERMS / ("%s.md" % slug)).write_text(text, encoding='utf-8')

orphans = [s for s in bodies if not (out[s] | inc[s])]
print("문서 %d개, 링크 %d개" % (len(bodies), sum(len(v) for v in out.values())))
print("고립 문서 %d개: %s" % (len(orphans), ", ".join(sorted(orphans))))
