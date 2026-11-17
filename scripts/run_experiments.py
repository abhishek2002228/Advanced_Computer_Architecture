#!/usr/bin/env python3
"""Run the configuration matrix and write outputs to separate run directories."""
import argparse, subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("section", choices=["pipeline", "ooo", "cache"])
args = parser.parse_args()
project = {"pipeline": "superscalar_pipeline", "ooo": "out_of_order_execution", "cache": "cache_memory_system"}[args.section]
section = root / project
runs = section / "results/runs"
runs.mkdir(parents=True, exist_ok=True)
subprocess.run(["make", "-C", str(section / "src")], check=True)
inputs = root / "data/traces" / ("memory" if args.section == "cache" else "pipeline")
jobs = []
if args.section == "pipeline":
    configs = {"scalar": ["-pipewidth", "1"], "dual": ["-pipewidth", "2"]}
    forward = ["-pipewidth", "2", "-enablememfwd", "-enableexefwd"]
    configs.update({"dual_forwarding": forward, "always_taken": forward + ["-bpredpolicy", "1"], "gshare": forward + ["-bpredpolicy", "2"]})
    jobs = [(label + "." + work, flags, [work + ".ptr.gz"]) for label, flags in configs.items() for work in ["bzip2", "gcc", "libq", "mcf", "sml"]]
elif args.section == "ooo":
    for width in [1, 2]:
        for latency in [1, 4]:
            for policy, label in [(0, "in_order"), (1, "out_of_order")]:
                for work in ["bzip2", "gcc", "libq", "mcf"]:
                    jobs.append((f"width{width}.{label}.latency{latency}.{work}", ["-pipewidth", str(width), "-schedpolicy", str(policy), "-loadlatency", str(latency)], [work + ".ptr.gz"]))
else:
    configs = {"cache_only": ["-mode", "1"], "fixed_dram": ["-mode", "2", "-L2sizeKB", "1024"], "open_page": ["-mode", "3", "-L2sizeKB", "1024", "-dram_policy", "0"], "closed_page": ["-mode", "3", "-L2sizeKB", "1024", "-dram_policy", "1"]}
    jobs = [(label + "." + work, flags, [work + ".mtr.gz"]) for label, flags in configs.items() for work in ["bzip2", "lbm", "libq"]]
    pairs = [("bzip2", "libq"), ("bzip2", "lbm"), ("lbm", "libq")]
    for mix, pair in enumerate(pairs, 1):
        jobs.append((f"shared_lru.mix{mix}", ["-mode", "4"], [w + ".mtr.gz" for w in pair]))
        for ways in [4, 8, 12]:
            jobs.append((f"static_{ways}ways.mix{mix}", ["-mode", "4", "-L2repl", "2", "-SWP_core0ways", str(ways)], [w + ".mtr.gz" for w in pair]))
        jobs.append((f"dynamic_utility.mix{mix}", ["-mode", "4", "-L2repl", "3"], [w + ".mtr.gz" for w in pair]))
missing = sorted({str(inputs / f) for _, _, files in jobs for f in files if not (inputs / f).is_file()})
if missing:
    raise SystemExit("Missing local inputs:\n" + "\n".join(missing))
for label, flags, files in jobs:
    print(label, flush=True)
    with (runs / (label + ".txt")).open("w") as log:
        subprocess.run([str(section / "src/sim"), *flags, *[str(inputs / f) for f in files]], stdout=log, check=True, timeout=1800)
