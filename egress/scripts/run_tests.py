#!/usr/bin/env python3
"""Run parameter sweeps on the ROOT-Sim 3.0 egress model and emit .dat plots."""

import argparse
import re
import subprocess
from pathlib import Path

SUMMARY_RE = re.compile(
    r"saved=(\d+),\s*deaths=(\d+),\s*total=(\d+),\s*t95=([0-9eE.+\-]+),\s*t_end=([0-9eE.+\-]+)"
)


def find_default_binary() -> Path:
    script_dir = Path(__file__).resolve().parent
    egress_dir = script_dir.parent
    repo_dir = egress_dir.parent
    candidates = [
        Path("build/egress/egress"),
        Path("build/egress"),
        egress_dir / "build" / "egress",
        repo_dir / "build" / "egress" / "egress",
    ]
    for cand in candidates:
        if cand.is_file():
            return cand
    return repo_dir / "build" / "egress" / "egress"


def run_single(cmd: list[str]) -> tuple[int, int, float]:
    proc = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    match = SUMMARY_RE.search(proc.stdout)
    if not match:
        raise RuntimeError(f"Could not parse egress summary from output:\n{proc.stdout}")
    saved = int(match.group(1))
    deaths = int(match.group(2))
    t95 = float(match.group(4))
    return saved, deaths, t95


def main() -> None:
    parser = argparse.ArgumentParser(description="Run parameter sweeps for the egress model.")
    parser.add_argument("--bin", type=Path, default=find_default_binary(), help="Path to egress executable")
    parser.add_argument("-T", "--topology", type=Path, default=None, help="Path to topology.json")
    parser.add_argument("--param", default="env-p", help="Model CLI parameter to sweep (default: env-p)")
    parser.add_argument("--out-prefix", default="knowledge", help="Output .dat file prefix (default: knowledge)")
    parser.add_argument("--start", type=float, default=0.0, help="Sweep start value (default: 0.0)")
    parser.add_argument("--end", type=float, default=1.0, help="Sweep end value (default: 1.0)")
    parser.add_argument("--points", type=int, default=15, help="Number of sweep intervals (default: 15)")
    parser.add_argument("--runs", type=int, default=5, help="Runs averaged per point (default: 5)")
    parser.add_argument("-t", "--termination-time", type=float, default=10000.0, help="Termination time (default: 10000)")
    parser.add_argument("-c", "--ncores", type=int, default=0, help="Worker threads for Time Warp (default: auto)")
    parser.add_argument("--timewarp", action="store_true", help="Use optimistic Time Warp instead of --serial")
    args = parser.parse_args()

    if not args.bin.is_file():
        raise FileNotFoundError(f"egress binary not found at '{args.bin}'. Build the project first or pass --bin.")

    base_cmd = [str(args.bin), "-l", "silent", "-t", str(args.termination_time)]
    if args.timewarp:
        base_cmd.append("--timewarp")
        if args.ncores > 0:
            base_cmd.extend(["-c", str(args.ncores)])
    else:
        base_cmd.append("--serial")

    if args.topology is not None:
        base_cmd.extend(["-T", str(args.topology)])

    data = []
    for i in range(args.points + 1):
        val = args.start + ((args.end - args.start) * i / args.points)
        cmd = base_cmd + [f"--{args.param}", str(val)]

        saved_sum = 0
        deaths_sum = 0
        time_sum = 0.0
        for _ in range(args.runs):
            s, d, t95 = run_single(cmd)
            saved_sum += s
            deaths_sum += d
            time_sum += t95

        saved_avg = saved_sum / args.runs
        deaths_avg = deaths_sum / args.runs
        time_avg = time_sum / args.runs
        data.append((val, saved_avg, deaths_avg, time_avg))
        print(f"{args.param}={val:.4f}: saved={saved_avg:.1f}, deaths={deaths_avg:.1f}, t95={time_avg:.2f}")

    with open(f"{args.out_prefix}-f.dat", "w", encoding="utf-8") as f_fat, \
         open(f"{args.out_prefix}-s.dat", "w", encoding="utf-8") as f_rate, \
         open(f"{args.out_prefix}-t.dat", "w", encoding="utf-8") as f_time:
        for val, saved_avg, deaths_avg, time_avg in data:
            total = saved_avg + deaths_avg
            fatalities_p = (deaths_avg / total) if total > 0 else 0.0
            save_rate = (saved_avg / time_avg) if time_avg > 0 else 0.0
            f_fat.write(f"{val}\t{fatalities_p}\n")
            f_rate.write(f"{val}\t{save_rate}\n")
            f_time.write(f"{val}\t{time_avg}\n")


if __name__ == "__main__":
    main()
