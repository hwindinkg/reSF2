#!/usr/bin/env python3
"""Generate the GitHub Pages dashboard: docs/progress.svg, docs/history.svg and
the two HTML pages that embed them (progress + history), like AnyPS5's
`progress.html` / `history.html`.

All numbers come from the same sources as README.md:
  harness/gates.baseline.json  - per-gate metric + the date it last moved
  harness/readme_status.py     - the SYSTEMS checklist (readiness per system)
  git log                      - commits per day (the history series)

Usage:  python harness/readme_charts.py
"""

from __future__ import annotations

import io
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
BASELINE = ROOT / "harness" / "gates.baseline.json"

BG = "#0d1117"
PANEL = "#161b22"
GRID = "#30363d"
TEXT = "#e6edf3"
MUTED = "#8b949e"
GREEN = "#3fb950"
YELLOW = "#d29922"
RED = "#f85149"
BLUE = "#58a6ff"


def esc(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def systems() -> list[tuple[str, int]]:
    """(system, percent) from the shared checklist."""
    sys.path.insert(0, str(ROOT / "harness"))
    import readme_status  # noqa: WPS433 - same directory
    out = []
    for name, items in readme_status.SYSTEMS:
        ok = sum(1 for _, v in items if v)
        out.append((name, round(100 * ok / max(1, len(items)))))
    return out


def gates() -> dict:
    try:
        return json.load(io.open(BASELINE, encoding="utf-8")).get("gates", {})
    except Exception:
        return {}


def colour(pct: int) -> str:
    if pct >= 95:
        return GREEN
    if pct >= 70:
        return YELLOW
    return RED


def progress_svg(rows: list[tuple[str, int]], overall: int) -> str:
    w, row_h, top = 760, 34, 96
    h = top + row_h * len(rows) + 24
    label_w = 250
    bar_w = w - label_w - 120
    p = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
         f'viewBox="0 0 {w} {h}" font-family="Segoe UI,Ubuntu,sans-serif">',
         f'<rect width="{w}" height="{h}" rx="10" fill="{BG}"/>',
         f'<text x="24" y="34" fill="{TEXT}" font-size="20" '
         f'font-weight="600">Readiness by system</text>',
         f'<text x="24" y="58" fill="{MUTED}" font-size="12">'
         f'generated from the gate baseline + the checklist in '
         f'harness/readme_status.py</text>',
         f'<text x="{w-24}" y="40" fill="{colour(overall)}" font-size="26" '
         f'font-weight="700" text-anchor="end">{overall}%</text>']
    y = top
    for name, pct in rows:
        c = colour(pct)
        p.append(f'<text x="24" y="{y+14}" fill="{TEXT}" font-size="13">{esc(name)}</text>')
        p.append(f'<rect x="{label_w}" y="{y+3}" width="{bar_w}" height="14" '
                 f'rx="7" fill="{PANEL}" stroke="{GRID}"/>')
        p.append(f'<rect x="{label_w}" y="{y+3}" width="{max(2, round(bar_w*pct/100))}" '
                 f'height="14" rx="7" fill="{c}"/>')
        p.append(f'<text x="{label_w+bar_w+12}" y="{y+14}" fill="{c}" font-size="13" '
                 f'font-weight="600">{pct}%</text>')
        y += row_h
    p.append("</svg>")
    return "\n".join(p)


def commits_per_day(days: int = 30) -> list[tuple[str, int]]:
    out = subprocess.run(
        ["git", "log", f"--since={days} days ago", "--date=short",
         "--pretty=format:%ad"],
        cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
        errors="replace").stdout
    counts: dict[str, int] = {}
    for line in out.splitlines():
        d = line.strip()
        if d:
            counts[d] = counts.get(d, 0) + 1
    ordered = sorted(counts.items())
    return ordered


def history_svg(series: list[tuple[str, int]], metrics: list[tuple[str, int]]) -> str:
    w, h = 760, 300
    left, right, top, bottom = 56, 24, 56, 44
    plot_w = w - left - right
    plot_h = h - top - bottom
    p = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
         f'viewBox="0 0 {w} {h}" font-family="Segoe UI,Ubuntu,sans-serif">',
         f'<rect width="{w}" height="{h}" rx="10" fill="{BG}"/>',
         f'<text x="24" y="30" fill="{TEXT}" font-size="18" font-weight="600">'
         f'History</text>',
         f'<text x="24" y="48" fill="{MUTED}" font-size="12">'
         f'commits per day (bars) and the survival divergence metric (line)</text>']
    # grid
    for i in range(5):
        gy = top + plot_h * i / 4
        p.append(f'<line x1="{left}" y1="{gy:.1f}" x2="{w-right}" y2="{gy:.1f}" '
                 f'stroke="{GRID}" stroke-width="1"/>')
    if series:
        vmax = max(1, max(v for _, v in series))
        bw = plot_w / max(1, len(series))
        for i, (d, v) in enumerate(series):
            bh = plot_h * v / vmax
            x = left + i * bw + bw * 0.15
            p.append(f'<rect x="{x:.1f}" y="{top+plot_h-bh:.1f}" '
                     f'width="{bw*0.7:.1f}" height="{bh:.1f}" fill="{BLUE}" '
                     f'opacity="0.55"/>')
        p.append(f'<text x="{left}" y="{h-16}" fill="{MUTED}" font-size="11">'
                 f'{esc(series[0][0])}</text>')
        p.append(f'<text x="{w-right}" y="{h-16}" fill="{MUTED}" font-size="11" '
                 f'text-anchor="end">{esc(series[-1][0])}</text>')
        p.append(f'<text x="{left-8}" y="{top+6}" fill="{MUTED}" font-size="10" '
                 f'text-anchor="end">{vmax}</text>')
    if len(metrics) >= 2:
        mmax = max(v for _, v in metrics) or 1
        pts = []
        for i, (d, v) in enumerate(metrics):
            x = left + plot_w * i / (len(metrics) - 1)
            y = top + plot_h - plot_h * v / mmax
            pts.append(f"{x:.1f},{y:.1f}")
            p.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{RED}"/>')
            p.append(f'<text x="{x:.1f}" y="{y-10:.1f}" fill="{RED}" font-size="11" '
                     f'text-anchor="middle">{v}</text>')
        p.append(f'<polyline points="{" ".join(pts)}" fill="none" stroke="{RED}" '
                 f'stroke-width="2"/>')
        p.append(f'<text x="{left}" y="{top-10}" fill="{RED}" font-size="11">'
                 f'diff:survival</text>')
    p.append("</svg>")
    return "\n".join(p)


def metric_history(g: dict) -> list[tuple[str, int]]:
    """The known survival points, oldest first (baseline reason text + date)."""
    pts: list[tuple[str, int]] = []
    surv = g.get("diff:survival", {})
    reason = surv.get("reason", "")
    # the reason records the chain, e.g. "1957 -> 863"
    import re
    chain = re.findall(r"(\d+)\s*->", reason) + [str(surv.get("metric"))]
    date = surv.get("date", "")[:10]
    if len(chain) >= 2:
        pts = [(f"step {i+1}", int(v)) for i, v in enumerate(chain) if v.isdigit()]
        if date:
            pts[0] = (date, pts[0][1])
    return pts


def html_page(title: str, svg_file: str, extra: str = "") -> str:
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title>
<style>
 body{{margin:0;background:{BG};color:{TEXT};font:15px/1.5 "Segoe UI",Ubuntu,sans-serif}}
 .wrap{{max-width:820px;margin:0 auto;padding:32px 20px 64px}}
 h1{{font-size:24px;margin:0 0 6px}} p.sub{{color:{MUTED};margin:0 0 24px}}
 img{{width:100%;height:auto;border-radius:10px;display:block}}
 a{{color:{BLUE};text-decoration:none}} a:hover{{text-decoration:underline}}
 nav{{margin:0 0 24px;font-size:14px}} nav a{{margin-right:16px}}
 table{{border-collapse:collapse;width:100%;margin-top:24px;font-size:14px}}
 th,td{{text-align:left;padding:8px 10px;border-bottom:1px solid {GRID}}}
 th{{color:{MUTED};font-weight:500}} td.n{{text-align:right;font-variant-numeric:tabular-nums}}
</style></head><body><div class="wrap">
<nav><a href="./index.html">progress</a><a href="./history.html">history</a>
<a href="https://github.com/hwindinkg/reSF2">repository</a></nav>
<h1>{esc(title)}</h1><p class="sub">generated from the gate baseline and git history</p>
<img src="{svg_file}" alt="{esc(title)}">
{extra}
</div></body></html>
"""


def main() -> int:
    DOCS.mkdir(exist_ok=True)
    rows = systems()
    # item-weighted, exactly like README (a 1-item system must not weigh as
    # much as a 7-item one)
    sys.path.insert(0, str(ROOT / "harness"))
    import readme_status
    items = [ok for _, lst in readme_status.SYSTEMS for _, ok in lst]
    overall = round(100 * sum(items) / max(1, len(items)))
    g = gates()
    (DOCS / "progress.svg").write_text(progress_svg(rows, overall), encoding="utf-8")
    series = commits_per_day()
    (DOCS / "history.svg").write_text(history_svg(series, metric_history(g)),
                                       encoding="utf-8")
    table = ["<table><tr><th>system</th><th>readiness</th></tr>"]
    for name, pct in rows:
        table.append(f'<tr><td>{esc(name)}</td><td class="n" '
                     f'style="color:{colour(pct)}">{pct}%</td></tr>')
    table.append(f'<tr><td><b>overall</b></td><td class="n">'
                 f'<b style="color:{colour(overall)}">{overall}%</b></td></tr></table>')
    (DOCS / "index.html").write_text(
        html_page("Readiness by system", "progress.svg", "\n".join(table)),
        encoding="utf-8")
    (DOCS / "history.html").write_text(
        html_page("History", "history.svg"), encoding="utf-8")
    (DOCS / ".nojekyll").write_text("", encoding="utf-8")
    print(f"readme_charts: docs/ written (overall {overall}%, "
          f"{len(series)} commit days, {len(rows)} systems)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
