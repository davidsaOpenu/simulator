#!/usr/bin/env python3
"""demo_report.py - one summary table from the standalone demo's raw CSVs.

usage: demo_report.py [RESULTS_DIR ...] [--md OUT.md] [--band-pct 5] [--floor-us 10]
Reads every *.raw.csv in the given directories (default results_demo), one row per (run, delay):
overshoot p50 / p99 / p99.9, max, count > 50 us late, share inside the tolerance band
|actual - requested| <= max(band%, floor), and the same share with a 4 us calibration.
Exit status is 0; this is a report, not a gate (scripts/check_tolerance.py is the gate).
"""
import argparse, csv, glob, math, os, re, statistics, sys

ORDER = ["stock-B-unpinned", "aiopoll-B-unpinned", "B-unpinned", "B-softpin",
         "iso-unpinned", "iso-pinned", "iso-pinned-full",
         "A-adjacent-unpinned", "A-adjacent-pinned", "A-smt-cpu", "A-shared-io", "A-shared-mixed",
         "A-shared-cpu", "A-half-unpinned", "A-full-unpinned", "A-full-softpin", "A-full-rt"]
DESC = {
    "stock-B-unpinned":    "stock main loop (the bug), nothing else running",
    "aiopoll-B-unpinned":  "patched, driven by aio_poll() (reference)",
    "B-unpinned":          "patched, nothing else running, unpinned",
    "B-softpin":           "patched, main loop soft-pinned to cpu 1",
    "iso-unpinned":        "isolated boot, unpinned (housekeeping cpus)",
    "iso-pinned":          "isolated boot, main loop on isolated cpu 1",
    "iso-pinned-full":     "isolated boot, main loop on cpu 1, all 10 housekeeping cpus loaded",
    "A-adjacent-unpinned": "cpu + io + mixed workers on cores 5-7, unpinned",
    "A-adjacent-pinned":   "cpu + io + mixed workers on cores 5-7, pinned to cpu 1",
    "A-smt-cpu":           "cpu-bound worker on the SMT sibling (cpu 9)",
    "A-shared-io":         "io-bound worker on the same cpu",
    "A-shared-mixed":      "mixed worker on the same cpu",
    "A-shared-cpu":        "cpu-bound worker on the same cpu",
    "A-half-unpinned":     "cpu-bound worker on one thread of every core",
    "A-full-unpinned":     "a worker on every logical cpu, unpinned",
    "A-full-softpin":      "a worker on every logical cpu, soft-pinned",
    "A-full-rt":           "a worker on every logical cpu, main loop SCHED_FIFO 1",
}
for _n in (1, 3, 16):
    DESC.update({
        "co%d" % _n:      "qemu_co_sleep_ns for the whole delay, %d coroutine%s" % (_n, "s" if _n > 1 else ""),
        "hy%d" % _n:      "hybrid: co_sleep to target-20 us, then spin, %d coroutine%s" % (_n, "s" if _n > 1 else ""),
        "hy%d-s5" % _n:   "hybrid, slack 5 us, %d coroutines" % _n,
        "sp%d" % _n:      "spin the whole delay in the coroutine, %d coroutine%s" % (_n, "s" if _n > 1 else ""),
        "co%d-full" % _n: "co_sleep, every host cpu loaded, %d coroutines" % _n,
        "hy%d-full" % _n: "hybrid, every host cpu loaded, %d coroutines" % _n,
        "co%d-iso" % _n:  "co_sleep, main loop on isolated cpu 1, %d coroutines" % _n,
        "hy%d-iso" % _n:  "hybrid, main loop on isolated cpu 1, %d coroutines" % _n,
    })
    ORDER += ["co%d" % _n, "hy%d" % _n, "hy%d-s5" % _n, "sp%d" % _n, "co%d-full" % _n, "hy%d-full" % _n, "co%d-iso" % _n, "hy%d-iso" % _n]

def pct(s, p):
    return s[min(len(s) - 1, int(math.ceil(p * len(s))) - 1)]

def load(path):
    by = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            by.setdefault(int(r["requested_ns"]), []).append(int(r["delta_ns"]))
    return by

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="*", default=["results_demo"])
    ap.add_argument("--md")
    ap.add_argument("--band-pct", type=float, default=5.0)
    ap.add_argument("--floor-us", type=float, default=10.0)
    ap.add_argument("--cal-us", type=float, default=4.0)
    a = ap.parse_args()
    files = {}
    for d in a.dirs:
        for p in glob.glob(os.path.join(d, "*.raw.csv")):
            files[os.path.basename(p)[:-8]] = p
    labels = [l for l in ORDER if l in files] + sorted(l for l in files if l not in ORDER)
    if not labels:
        sys.exit("demo_report: no *.raw.csv found in " + ", ".join(a.dirs))
    out = ["| run | configuration | requested | overshoot mean | p50 / p99 / p99.9 | > 50 us late | max | in band | in band, -%g us | wall (x ideal) | main-loop cpu |" % a.cal_us,
           "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for l in labels:
        by = load(files[l])
        runlines = {}
        logp = files[l][:-8] + ".log"
        if os.path.exists(logp):
            for line in open(logp):
                m = re.search(r"RUN\s+(\d+) us\s+wall ([\d.]+) s \(ideal ([\d.]+) s, x([\d.]+)\)\s+cpu [\d.]+ s \((\d+)% of wall\)", line)
                if m: runlines[int(m.group(1))] = (m.group(2), m.group(4), m.group(5))
        for req in sorted(by):
            s = sorted(by[req]); band = max(req * a.band_pct / 100.0, a.floor_us * 1000)
            inb = 100 * sum(abs(x) <= band for x in s) / len(s)
            inc = 100 * sum(abs(x - a.cal_us * 1000) <= band for x in s) / len(s)
            rl = runlines.get(req // 1000)
            wall = "%s s (x%s)" % (rl[0], rl[1]) if rl else ""
            cpu = "%s %%" % rl[2] if rl else ""
            out.append("| `%s` | %s | %d us | %+.2f us | %+.1f / %+.1f / %+.1f us | %d | %.0f us | %.2f %% | %.2f %% | %s | %s |" % (
                l, DESC.get(l, ""), req // 1000, statistics.fmean(s) / 1e3, pct(s, .5) / 1e3, pct(s, .99) / 1e3, pct(s, .999) / 1e3,
                sum(x > 50000 for x in s), s[-1] / 1e3, inb, inc, wall, cpu))
    text = "\n".join(out) + "\n"
    print(text)
    if a.md:
        open(a.md, "w").write(text)

if __name__ == "__main__":
    main()
