#!/usr/bin/env python3
"""demo_plots.py - static SVG figures for docs/RUNS_SUMMARY.md from the standalone demo's CSVs.
usage: demo_plots.py [RESULTS_DIR ...]   (default results_demo)  -> docs/figs_demo/*.svg
Pure stdlib. Colours are the dataviz reference palette in its fixed categorical order; text wears
ink tokens, identity is carried by direct labels, every mark has a <title> for hover."""
import csv, math, os, sys

SURF, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
C = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]
FONT = "font-family='Inter,Segoe UI,Helvetica,Arial,sans-serif'"
def pct(s, p):
    s = sorted(s); return s[min(len(s) - 1, int(math.ceil(p * len(s))) - 1)]

def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;").replace("'", "&#39;")   # attributes are single-quoted

def frame(w, h, title, sub):
    return [f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {w} {h}' width='{w}' height='{h}' role='img' aria-label='{esc(title)}'>",
            f"<rect width='{w}' height='{h}' fill='{SURF}'/>",
            f"<text x='16' y='26' {FONT} font-size='15' font-weight='600' fill='{INK}'>{esc(title)}</text>",
            f"<text x='16' y='44' {FONT} font-size='11.5' fill='{INK2}'>{esc(sub)}</text>"]

def xaxis(o, x0, x1, y0, y1, ticks, fmt, vmax):
    for t in ticks:
        x = x0 + (x1 - x0) * t / vmax
        o.append(f"<line x1='{x:.1f}' y1='{y0}' x2='{x:.1f}' y2='{y1}' stroke='{GRID}' stroke-width='1'/>")
        o.append(f"<text x='{x:.1f}' y='{y1 + 15}' {FONT} font-size='10.5' fill='{INK2}' text-anchor='middle'>{fmt(t)}</text>")

def bar(x, y, w, h, fill, tip):
    r = min(4, w / 2, h / 2)   # rounded data-end only, square at the baseline
    d = f"M{x:.1f},{y:.1f}h{w - r:.1f}a{r},{r} 0 0 1 {r},{r}v{h - 2 * r:.1f}a{r},{r} 0 0 1 -{r},{r}h-{w - r:.1f}z"
    return f"<path d='{d}' fill='{fill}'><title>{esc(tip)}</title></path>"

def rows_layout(groups, y):
    lay = []
    for g, items in groups:
        lay.append(("g", g, y)); y += 20
        for lab, d in items:
            lay.append(("r", lab, y, d)); y += 22
        y += 8
    return lay, y

def fig_overshoot(S):
    lay, yend = rows_layout(GROUPS, 66)
    W, H, x0, x1, vmax = 860, yend + 34, 300, 830, 40
    o = frame(W, H, "Overshoot of a 132 us delay, by configuration",
              "dot = median, bar end = p99, us past the requested time — shaded = inside the 10 us tolerance floor")
    o.append(f"<rect x='{x0}' y='58' width='{(x1 - x0) * 10 / vmax:.1f}' height='{yend - 58}' fill='{C[0]}' opacity='0.07'/>")
    xaxis(o, x0, x1, 58, yend, [0, 10, 20, 30, 40], lambda t: f"{t}", vmax)
    for it in lay:
        if it[0] == "g":
            o.append(f"<text x='16' y='{it[2] + 12}' {FONT} font-size='11.5' font-weight='600' fill='{INK2}'>{esc(it[1])}</text>")
            continue
        _, lab, y, d = it; s = S[d]
        tip = f"{lab}: p50 +{s['p50']:.1f} us, p99 +{s['p99']:.1f} us, n={s['n']}"
        o.append(f"<text x='{x0 - 10}' y='{y + 12}' {FONT} font-size='11.5' fill='{INK}' text-anchor='end'>{esc(lab)}</text>")
        if s["p50"] > vmax:   # off the scale: say so instead of compressing every other row
            o.append(f"<g><title>{esc(tip)}</title><line x1='{x0}' y1='{y + 8}' x2='{x1 - 9}' y2='{y + 8}' stroke='{C[0]}' stroke-width='2' stroke-dasharray='2 4'/>"
                     f"<path d='M{x1 - 10},{y + 2}l10,6l-10,6z' fill='{C[0]}'/></g>")
            o.append(f"<text x='{x1 - 16}' y='{y + 2}' {FONT} font-size='10.5' font-weight='600' fill='{INK}' text-anchor='end'>off scale: +{s['p50']:.0f} / +{s['p99']:.0f} us (every sleep takes ~1 ms)</text>")
            continue
        xa, xb = x0 + (x1 - x0) * s["p50"] / vmax, x0 + (x1 - x0) * min(s["p99"], vmax) / vmax
        o.append(f"<g><title>{esc(tip)}</title><line x1='{xa:.1f}' y1='{y + 8}' x2='{xb:.1f}' y2='{y + 8}' stroke='{C[0]}' stroke-width='2'/>"
                 f"<line x1='{xb:.1f}' y1='{y + 3}' x2='{xb:.1f}' y2='{y + 13}' stroke='{C[0]}' stroke-width='2'/>"
                 f"<circle cx='{xa:.1f}' cy='{y + 8}' r='4.5' fill='{C[0]}' stroke='{SURF}' stroke-width='2'/>"
                 f"<rect x='{x0}' y='{y - 2}' width='{x1 - x0}' height='20' fill='transparent'/></g>")
        if s["p99"] > vmax:   # p99 clipped: label sits above the bar, right-aligned, with a clip mark
            o.append(f"<text x='{x1 - 4}' y='{y + 2}' {FONT} font-size='10.5' fill='{INK2}' text-anchor='end'>{s['p50']:.1f} / {s['p99']:.1f} »</text>")
        else:
            o.append(f"<text x='{xb + 7:.1f}' y='{y + 12}' {FONT} font-size='10.5' fill='{INK2}'>{s['p50']:.1f} / {s['p99']:.1f}</text>")
    o.append("</svg>"); open(f"{OUT}/overshoot_by_config.svg", "w").write("\n".join(o))

def fig_inband(S):
    lay, yend = rows_layout(GROUPS, 66)
    W, H, x0, x1, vmax = 860, yend + 34, 300, 790, 100
    o = frame(W, H, "Share of 132 us delays inside the tolerance band",
              "|actual − requested| ≤ max(5 %, 10 us), no calibration — dashed line = the 99 % required")
    xaxis(o, x0, x1, 58, yend, [0, 25, 50, 75, 100], lambda t: f"{t} %", vmax)
    for it in lay:
        if it[0] == "g":
            o.append(f"<text x='16' y='{it[2] + 12}' {FONT} font-size='11.5' font-weight='600' fill='{INK2}'>{esc(it[1])}</text>")
            continue
        _, lab, y, d = it; v = S[d]["inband"]; w = (x1 - x0) * v / vmax
        o.append(f"<text x='{x0 - 10}' y='{y + 12}' {FONT} font-size='11.5' fill='{INK}' text-anchor='end'>{esc(lab)}</text>")
        o.append(bar(x0, y, w, 16, C[0], f"{lab}: {v:.2f} % in band"))
        # fixed value column right of the plot, clear of the 99 % reference line
        o.append(f"<text x='{x1 + 50}' y='{y + 12}' {FONT} font-size='10.5' fill='{INK2}' text-anchor='end'>{v:.1f} %</text>")
    xr = x0 + (x1 - x0) * 0.99
    o.append(f"<line x1='{xr:.1f}' y1='54' x2='{xr:.1f}' y2='{yend}' stroke='{INK}' stroke-width='1.5' stroke-dasharray='4 3'/>")
    o.append("</svg>"); open(f"{OUT}/inband_by_config.svg", "w").write("\n".join(o))

OUT = "docs/figs_demo"
GROUPS = [
    ("Untreated: stock util/main-loop.c", [("Scenario B, unpinned", "stock-B-unpinned")]),
    ("Scenario B: nothing else running (patched)", [("unpinned", "B-unpinned"),
                                                    ("driven by aio_poll() instead", "aiopoll-B-unpinned"),
                                                    ("main loop soft-pinned", "B-softpin")]),
    ("Scenario B on the isolcpus + nohz_full boot (patched)", [("unpinned", "iso-unpinned"),
                                                               ("main loop on isolated cpu", "iso-pinned"),
                                                               ("isolated cpu, all housekeeping cpus loaded", "iso-pinned-full")]),
    ("Scenario A: one neighbour (patched)", [("3 workers on adjacent cores, unpinned", "A-adjacent-unpinned"),
                                            ("3 workers on adjacent cores, pinned", "A-adjacent-pinned"),
                                            ("cpu-bound on SMT sibling", "A-smt-cpu"),
                                            ("I/O-bound on same cpu", "A-shared-io"),
                                            ("mixed 50:50 on same cpu", "A-shared-mixed"),
                                            ("cpu-bound on same cpu", "A-shared-cpu")]),
    ("Scenario A: whole host loaded (patched)", [("half load, unpinned", "A-half-unpinned"),
                                                ("full load, unpinned", "A-full-unpinned"),
                                                ("full load, soft-pinned", "A-full-softpin"),
                                                ("full load, SCHED_FIFO main loop", "A-full-rt")]),
]

def find(dirs, label):
    for d in dirs:
        p = os.path.join(d, label + ".raw.csv")
        if os.path.exists(p): return p
    return None

def stats(path, req=132000):
    x = [int(r["delta_ns"]) for r in csv.DictReader(open(path)) if int(r["requested_ns"]) == req]
    x.sort()
    return {"n": len(x), "p50": pct(x, .5) / 1e3, "p99": pct(x, .99) / 1e3,
            "inband": 100 * sum(abs(v) <= 10000 for v in x) / len(x)}

if __name__ == "__main__":
    dirs = sys.argv[1:] or ["results_demo"]
    os.makedirs(OUT, exist_ok=True)
    groups = [(g, [(lab, p) for lab, l in items if (p := find(dirs, l))]) for g, items in GROUPS]
    groups = [(g, items) for g, items in groups if items]
    GROUPS = groups          # the drawing helpers read the module-level GROUPS
    S = {p: stats(p) for _, items in groups for _, p in items}
    # fix_before_after: stock vs patched, unpinned
    st, pa = find(dirs, "stock-B-unpinned"), find(dirs, "B-unpinned")
    if st and pa:
        un, pt = S[st]["p50"] + 132, S[pa]["p50"] + 132
        W, H, x0, x1, vmax = 760, 190, 250, 730, 1100
        o = frame(W, H, "A 132 us qemu_co_sleep_ns() driven by QEMU's main loop",
                    "median actual duration, us, 150,000 sleeps each - dashed line = requested")
        xaxis(o, x0, x1, 62, 140, [0, 200, 400, 600, 800, 1000], str, vmax)
        for i, (lab, v) in enumerate([("stock util/main-loop.c", un), ("with the 9-line fix", pt)]):
            y = 72 + i * 34; w = (x1 - x0) * v / vmax
            o.append(f"<text x='{x0 - 10}' y='{y + 14}' {FONT} font-size='12' fill='{INK}' text-anchor='end'>{esc(lab)}</text>")
            o.append(bar(x0, y, w, 20, C[0], f"{lab}: {v:.1f} us"))
            o.append(f"<text x='{x0 + w + 6:.1f}' y='{y + 14}' {FONT} font-size='12' font-weight='600' fill='{INK}'>{v:.1f} us</text>")
        xr = x0 + (x1 - x0) * 132 / vmax
        o.append(f"<line x1='{xr:.1f}' y1='58' x2='{xr:.1f}' y2='140' stroke='{INK}' stroke-width='1.5' stroke-dasharray='4 3'/>")
        o.append(f"<text x='{xr + 5:.1f}' y='68' {FONT} font-size='10.5' fill='{INK2}'>requested 132</text>")
        o.append("</svg>"); open(f"{OUT}/fix_before_after.svg", "w").write("\n".join(o))
    fig_overshoot(S); fig_inband(S)
    for _, items in groups:
        for lab, p in items:
            s = S[p]; print(f"{lab:42s} n={s['n']:6d} p50={s['p50']:7.2f} p99={s['p99']:7.2f} inband={s['inband']:6.2f}")
