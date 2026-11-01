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


def systems() -> list[tuple[str, int, int, int]]:
    """(system, percent, passed, total) from the shared checklist."""
    sys.path.insert(0, str(ROOT / "harness"))
    import readme_status  # noqa: WPS433 - same directory
    out = []
    for name, items in readme_status.SYSTEMS:
        ok = sum(1 for _, v in items if v)
        out.append((name, round(100 * ok / max(1, len(items))), ok, len(items)))
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


def blend(c1: str, c2: str, t: float) -> str:
    """Mix two `#rrggbb` colours (t=0 -> c1, t=1 -> c2)."""
    a = [int(c1[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(c2[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02x%02x%02x" % tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def squarify(areas: list[float], x: float, y: float, w: float,
             h: float) -> list[tuple[float, float, float, float]]:
    """Squarified treemap (Bruls et al.); the rects keep the input order."""
    n = len(areas)
    if n == 0 or w <= 0 or h <= 0:
        return []
    # the values are ITEM COUNTS: scale them to the rect's area first
    tot = sum(areas)
    if tot <= 0:
        return []
    k = (w * h) / tot
    areas = [a * k for a in areas]
    order = sorted(range(n), key=lambda i: -areas[i])
    rects: list[tuple[float, float, float, float]] = [(0.0, 0.0, 0.0, 0.0)] * n

    def worst(row: list[float], side: float) -> float:
        s_ = sum(row)
        if s_ <= 0 or side <= 0:
            return float("inf")
        rmax, rmin = max(row), min(row)
        if rmin <= 0:
            return float("inf")
        return max(side * side * rmax / (s_ * s_), (s_ * s_) / (side * side * rmin))

    i = 0
    while i < n:
        row: list[tuple[int, float]] = []
        if w >= h:
            while i < n:
                cand = row + [(order[i], areas[order[i]])]
                if row and worst([a for _, a in cand], h) > worst([a for _, a in row], h):
                    break
                row = cand
                i += 1
            total = sum(a for _, a in row)
            cw = total / h if h > 0 else 0.0
            cy = y
            for idx, a in row:
                rh = a / cw if cw > 0 else 0.0
                rects[idx] = (x, cy, cw, rh)
                cy += rh
            x += cw
            w -= cw
        else:
            while i < n:
                cand = row + [(order[i], areas[order[i]])]
                if row and worst([a for _, a in cand], w) > worst([a for _, a in row], w):
                    break
                row = cand
                i += 1
            total = sum(a for _, a in row)
            rh = total / w if w > 0 else 0.0
            cx = x
            for idx, a in row:
                cwd = a / rh if rh > 0 else 0.0
                rects[idx] = (cx, y, cwd, rh)
                cx += cwd
            y += rh
            h -= rh
    return rects


def progress_svg(rows: list[tuple[str, int, int, int]], overall: int) -> str:
    # TREEMAP: one rectangle per system, its AREA proportional to that
    # system's checklist item count, its colour the readiness band. The
    # headline number is the same item-weighted overall as the README.
    w, head, body_h, legend_h = 760, 76, 400, 46
    h = head + body_h + legend_h
    area_x, area_y, area_w = 24.0, float(head), w - 48.0
    areas = [float(n) for _, _, _, n in rows]
    rects = squarify(areas, area_x, area_y, area_w, float(body_h))
    p = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
         f'viewBox="0 0 {w} {h}" font-family="Segoe UI,Ubuntu,sans-serif">',
         f'<rect width="{w}" height="{h}" rx="10" fill="{BG}"/>',
         f'<text x="24" y="34" fill="{TEXT}" font-size="20" '
         f'font-weight="600">Readiness by system</text>',
         f'<text x="24" y="56" fill="{MUTED}" font-size="12">'
         f'treemap: tile area = checklist items in that system; colour = readiness '
         f'(harness/readme_status.py)</text>',
         f'<text x="{w-24}" y="40" fill="{colour(overall)}" font-size="26" '
         f'font-weight="700" text-anchor="end">{overall}%</text>',
         f'<text x="{w-24}" y="58" fill="{MUTED}" font-size="12" '
         f'text-anchor="end">overall (all checklist items)</text>']
    for (name, pct, ok, total), (rx, ry, rw, rh) in zip(rows, rects):
        if rw <= 0 or rh <= 0:
            continue
        c = colour(pct)
        fill = blend(BG, c, 0.18)
        p.append(f'<g><title>{esc(name)}: {ok}/{total} ({pct}%)</title>')
        p.append(f'<rect x="{rx + 2:.1f}" y="{ry + 2:.1f}" width="{max(1.0, rw - 4):.1f}" '
                 f'height="{max(1.0, rh - 4):.1f}" rx="6" fill="{fill}" '
                 f'stroke="{blend(BG, c, 0.45)}"/>')
        p.append(f'<rect x="{rx + 2:.1f}" y="{ry + 2:.1f}" width="{max(1.0, rw - 4):.1f}" '
                 f'height="3" rx="1.5" fill="{c}"/>')
        if rw >= 92 and rh >= 44:
            short = name if len(name) <= 22 else name[:21] + "…"
            p.append(f'<text x="{rx + 10:.1f}" y="{ry + 24:.1f}" fill="{TEXT}" '
                     f'font-size="12" font-weight="600">{esc(short)}</text>')
            p.append(f'<text x="{rx + 10:.1f}" y="{ry + 42:.1f}" fill="{c}" '
                     f'font-size="15" font-weight="700">{pct}%</text>')
            p.append(f'<text x="{rx + 10 + 46:.1f}" y="{ry + 42:.1f}" fill="{MUTED}" '
                     f'font-size="11">{ok}/{total}</text>')
        elif rw >= 52 and rh >= 26:
            p.append(f'<text x="{rx + rw * 0.5:.1f}" y="{ry + rh * 0.5 + 6:.1f}" '
                     f'fill="{c}" font-size="14" font-weight="700" '
                     f'text-anchor="middle">{pct}%</text>')
        p.append('</g>')
    # legend + the item-weighted overall strip (the same number as the README)
    ly = head + body_h + 18
    lx = 24.0
    for label, c in (("≥95% done", GREEN), ("70-94%", YELLOW), ("<70%", RED)):
        p.append(f'<rect x="{lx:.0f}" y="{ly - 9:.0f}" width="12" height="12" rx="3" '
                 f'fill="{blend(BG, c, 0.35)}" stroke="{c}"/>')
        p.append(f'<text x="{lx + 18:.0f}" y="{ly + 1:.0f}" fill="{MUTED}" '
                 f'font-size="12">{esc(label)}</text>')
        lx += 108
    p.append(f'<text x="{w-24}" y="{ly + 1:.0f}" fill="{MUTED}" font-size="12" '
             f'text-anchor="end">{sum(t for *_, t in rows) - sum(o for *_, o, _ in rows)} '
             f'open of {sum(t for *_, t in rows)} items</text>')
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
    table = ["<table><tr><th>system</th><th>readiness</th><th>items</th></tr>"]
    for name, pct, ok, total in rows:
        table.append(f'<tr><td>{esc(name)}</td><td class="n">'
                     f'{pct}%</td><td class="n">{ok}/{total}</td></tr>')
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
