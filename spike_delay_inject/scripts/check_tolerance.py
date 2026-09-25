#!/usr/bin/env python3
"""check_tolerance.py - tolerance-band pass/fail over the spike's raw CSVs.

Band (decided 2026-09-21):
    a SAMPLE passes if |actual - requested| <= max(5% of requested, 10 us)
    a CELL   passes if >= 99% of its samples pass
where a cell is one (run, thread, backend, requested delay).

Everything is reported per scenario/condition, never only in aggregate: an overall
pass rate can hide a condition that is badly out of tolerance.

GATE. The exit status is non-zero iff NO Scenario B (simulator-only) run has every
cell of the candidate backend in tolerance inside the delay range eVSSIM actually
sleeps for. Scenario B is the best case, so failing all of it means the routine
itself is inadequate whatever the isolation strategy. Each Scenario B run is listed
separately, because pinned and unpinned baselines do NOT behave alike (RUNS_SUMMARY 4). The range defaults to >= 100 us because eVSSIM's smallest
real sleep is a 132 us page read (RUNS_SUMMARY 2); below ~20 us every sleeping
mechanism fails by construction (13 us wakeup floor) and that is reported, not gated.

usage: check_tolerance.py [RESULTS_DIR] [--backend qemu_co_sleep] [--gate-min-us 100]
                          [--pct 5] [--floor-us 10] [--cell-pass 0.99] [--md OUT.md]
"""
import argparse
import collections
import csv
import glob
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results", nargs="?", default="results")
    ap.add_argument("--backend", default="qemu_co_sleep", help="gated candidate backend")
    ap.add_argument("--gate-min-us", type=int, default=100)
    ap.add_argument("--pct", type=float, default=5.0)
    ap.add_argument("--floor-us", type=float, default=10.0)
    ap.add_argument("--cell-pass", type=float, default=0.99)
    ap.add_argument("--md", default=None)
    a = ap.parse_args()

    # (run, scenario, isolation, colocation, iftype, thread, backend, req_us) -> [n, n_ok]
    cells = collections.defaultdict(lambda: [0, 0])
    files = sorted(glob.glob(os.path.join(a.results, "*.raw.csv")))
    if not files:
        sys.exit(f"check_tolerance: no *.raw.csv under {a.results}")
    for path in files:
        with open(path) as f:
            for r in csv.DictReader(f):
                if r["backend"] == "virtual":      # control: performs no wait
                    continue
                req = int(r["requested_ns"])
                band = max(req * a.pct / 100.0, a.floor_us * 1000.0)
                k = (r["run_label"], r["scenario"], r["isolation"], r["colocation"],
                     r["iftype"], int(r["thread_idx"]), r["backend"], req // 1000)
                c = cells[k]
                c[0] += 1
                c[1] += abs(int(r["delta_ns"])) <= band

    out = []
    out.append("# Tolerance report\n")
    out.append(f"_Source: `{a.results}`. Sample passes if |actual - requested| <= "
               f"max({a.pct:g}% of requested, {a.floor_us:g} us); a cell "
               f"(run, thread, backend, delay) passes at >= {a.cell_pass:.0%} of samples. "
               f"`virtual` is excluded: it performs no wait._\n")

    # ---- per condition x backend: sample pass rate, failing cells, first passing delay
    out.append("## Per scenario / condition\n")
    out.append("| run | scen | isolation | colocation | backend | samples in band | "
               "cells passing | passes from (us) | worst cell |")
    out.append("|---|---|---|---|---|---:|---:|---:|---|")
    by = collections.defaultdict(list)
    for k, v in cells.items():
        by[(k[0], k[1], k[2], k[3], k[6])].append((k, v))
    for g in sorted(by):
        items = by[g]
        n = sum(v[0] for _, v in items)
        ok = sum(v[1] for _, v in items)
        passing = [k for k, v in items if v[1] / v[0] >= a.cell_pass]
        failing_req = {k[7] for k, v in items if v[1] / v[0] < a.cell_pass}
        reqs = sorted({k[7] for k, _ in items})
        above = [q for q in reqs if all(f < q for f in failing_req)]
        frm = str(above[0]) if above else "never"
        wk, wv = min(items, key=lambda kv: kv[1][1] / kv[1][0])
        out.append(f"| `{g[0]}` | {g[1]} | {g[2]} | {g[3]} | `{g[4]}` | {100*ok/n:.2f}% | "
                   f"{len(passing)}/{len(items)} | {frm} | t{wk[5]}({wk[4]}) @{wk[7]} us: "
                   f"{100*wv[1]/wv[0]:.1f}% |")

    # ---- per co-located workload type, contended runs only
    out.append("\n## Scenario A by co-located workload type (delays >= "
               f"{a.gate_min_us} us)\n")
    out.append("| run | colocation | iftype | backend | samples in band | cells passing |")
    out.append("|---|---|---|---|---:|---:|")
    byt = collections.defaultdict(list)
    for k, v in cells.items():
        if k[1] == "A" and k[7] >= a.gate_min_us:
            byt[(k[0], k[3], k[4], k[6])].append(v)
    for g in sorted(byt):
        vs = byt[g]
        n = sum(v[0] for v in vs); ok = sum(v[1] for v in vs)
        p = sum(1 for v in vs if v[1] / v[0] >= a.cell_pass)
        out.append(f"| `{g[0]}` | {g[1]} | {g[2]} | `{g[3]}` | {100*ok/n:.2f}% | {p}/{len(vs)} |")

    # ---- the gate: per Scenario B run, then "best case" across them
    gate = collections.defaultdict(list)
    for k, v in cells.items():
        if k[1] == "B" and k[6] == a.backend and k[7] >= a.gate_min_us:
            gate[k[0]].append((k, v))
    out.append("\n## Gate\n")
    out.append(f"Scenario B, backend `{a.backend}`, requested >= {a.gate_min_us} us, "
               "evaluated per run. Scenario B is the BEST case, so the gate passes if "
               "at least one Scenario B configuration passes every cell: the routine is "
               "then adequate and the failing configuration is an isolation finding, "
               "not a verdict on the routine.\n")
    run_ok = {}
    for run in sorted(gate):
        bad = [(k, v) for k, v in gate[run] if v[1] / v[0] < a.cell_pass]
        run_ok[run] = not bad
        out.append(f"- `{run}`: **{len(gate[run]) - len(bad)}/{len(gate[run])} cells pass**")
        for k, v in sorted(bad):
            out.append(f"  - FAIL t{k[5]} @{k[7]} us: {100*v[1]/v[0]:.2f}% in band")
    if not gate:
        out.append(f"- no Scenario B data for `{a.backend}` - gate cannot be evaluated")
    verdict = "PASS" if any(run_ok.values()) else "FAIL"
    out.append(f"\n**VERDICT: {verdict}**")

    text = "\n".join(out) + "\n"
    print(text)
    if a.md:
        with open(a.md, "w") as f:
            f.write(text)
    sys.exit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
