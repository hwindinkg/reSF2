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
    w, row_h, top = 760, 34, 116
    h = top + row_h * (len(rows) + 1) + 24
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
         f'font-weight="700" text-anchor="end">{overall}%</text>',
         f'<text x="{w-24}" y="58" fill="{MUTED}" font-size="12" '
         f'text-anchor="end">overall (all checklist items)</text>']
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
    # the overall row: the SAME number as the heading/README (all items at
    # once), so the bars and the headline can never read as different metrics
    c = colour(overall)
    p.append(f'<line x1="24" y1="{y+2}" x2="{w-24}" y2="{y+2}" stroke="{GRID}"/>')
    p.append(f'<text x="24" y="{y+18}" fill="{TEXT}" font-size="13" '
             f'font-weight="700">overall (all items)</text>')
    p.append(f'<rect x="{label_w}" y="{y+7}" width="{bar_w}" height="14" '
             f'rx="7" fill="{PANEL}" stroke="{GRID}"/>')
    p.append(f'<rect x="{label_w}" y="{y+7}" width="{max(2, round(bar_w*overall/100))}" '
             f'height="14" rx="7" fill="{c}"/>')
    p.append(f'<text x="{label_w+bar_w+12}" y="{y+18}" fill="{c}" font-size="13" '
             f'font-weight="700">{overall}%</text>')
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
    """Two separate panels: commits/day (bars) and the survival metric (line).

    The first version drew both series in ONE plot area, so the bar tops, the
    line and its value labels collided. Each panel now owns its own box, axis
    and labels.
    """
    w, h = 760, 420
    p = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
         f'viewBox="0 0 {w} {h}" font-family="Segoe UI,Ubuntu,sans-serif">',
         f'<rect width="{w}" height="{h}" rx="10" fill="{BG}"/>',
         f'<text x="24" y="30" fill="{TEXT}" font-size="18" font-weight="600">'
         f'History</text>',
         f'<text x="24" y="48" fill="{MUTED}" font-size="12">'
         f'commits per day (top) and the survival divergence metric (bottom)'
         f'</text>']

    def panel(y0: int, y1: int, title: str, unit_colour: str) -> None:
        p.append(f'<rect x="64" y="{y0}" width="{w-64-24}" height="{y1-y0}" '
                 f'fill="{PANEL}" stroke="{GRID}" rx="6"/>')
        p.append(f'<text x="76" y="{y0+18}" fill="{unit_colour}" font-size="11" '
                 f'font-weight="600">{esc(title)}</text>')

    # ---- panel A: commits per day -----------------------------------------
    a0, a1 = 66, 214
    panel(a0, a1, "commits / day", BLUE)
    if series:
        vmax = max(1, max(v for _, v in series))
        plot_x, plot_w = 76, w - 76 - 40
        plot_y, plot_h = a0 + 28, (a1 - 10) - (a0 + 28)
        bw = plot_w / max(1, len(series))
        for i, (d, v) in enumerate(series):
            bh = plot_h * v / vmax
            x = plot_x + i * bw + bw * 0.18
            p.append(f'<rect x="{x:.1f}" y="{plot_y+plot_h-bh:.1f}" '
                     f'width="{bw*0.64:.1f}" height="{bh:.1f}" fill="{BLUE}" '
                     f'rx="1.5"/>')
        p.append(f'<text x="{plot_x-6}" y="{plot_y+8}" fill="{MUTED}" '
                 f'font-size="10" text-anchor="end">{vmax}</text>')
        p.append(f'<text x="{plot_x-6}" y="{plot_y+plot_h}" fill="{MUTED}" '
                 f'font-size="10" text-anchor="end">0</text>')
        p.append(f'<text x="{plot_x}" y="{a1-4}" fill="{MUTED}" font-size="10">'
                 f'{esc(series[0][0])}</text>')
        p.append(f'<text x="{w-40}" y="{a1-4}" fill="{MUTED}" font-size="10" '
                 f'text-anchor="end">{esc(series[-1][0])}</text>')

    # ---- panel B: the survival metric -------------------------------------
    b0, b1 = 232, 380
    panel(b0, b1, "diff:survival (frames diverging from the oracle, lower is better)",
          RED)
    if metrics:
        plot_x, plot_w = 76, w - 76 - 40
        plot_y, plot_h = b0 + 28, (b1 - 26) - (b0 + 28)
        mmax = max(1, max(v for _, v in metrics))
        # y grid: 0 and max
        for frac, val in ((0.0, 0), (1.0, mmax)):
            gy = plot_y + plot_h * (1 - frac)
            p.append(f'<line x1="{plot_x}" y1="{gy:.1f}" x2="{plot_x+plot_w}" '
                     f'y2="{gy:.1f}" stroke="{GRID}" stroke-width="1"/>')
            p.append(f'<text x="{plot_x-6}" y="{gy+4:.1f}" fill="{MUTED}" '
                     f'font-size="10" text-anchor="end">{val}</text>')
        n = len(metrics)
        pts = []
        for i, (d, v) in enumerate(metrics):
            x = plot_x + (plot_w * i / (n - 1) if n > 1 else plot_w / 2)
            y = plot_y + plot_h * (1 - v / mmax)
            pts.append(f"{x:.1f},{y:.1f}")
            p.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{RED}"/>')
            # value above the point, label below the axis
            p.append(f'<text x="{x:.1f}" y="{y-10:.1f}" fill="{RED}" '
                     f'font-size="12" font-weight="600" text-anchor="middle">'
                     f'{v}</text>')
            p.append(f'<text x="{x:.1f}" y="{b1-8}" fill="{MUTED}" font-size="10" '
                     f'text-anchor="middle">{esc(d)}</text>')
        if n > 1:
            p.append(f'<polyline points="{" ".join(pts)}" fill="none" '
                     f'stroke="{RED}" stroke-width="2"/>')
    p.append("</svg>")
    return "\n".join(p)


def metric_history(g: dict) -> list[tuple[str, int]]:
    """The known survival points, oldest first (baseline reason text + date)."""
    pts: list[tuple[str, int]] = []
    surv = g.get("diff:survival", {})
    reason = surv.get("reason", "")
    # the reason records the chain, e.g. "1957 -> 863"
    import re
    chain = [int(v) for v in re.findall(r"(\d+)\s*->", reason)]
    cur = surv.get("metric")
    if isinstance(cur, int):
        chain.append(cur)
    date = surv.get("date", "")[:10]
    if len(chain) >= 2:
        pts = [(date or "before", chain[0]), ("now", chain[-1])]
    elif chain:
        pts = [(date or "now", chain[0])]
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
