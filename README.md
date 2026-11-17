# Advanced Computer Architecture

This repository contains C and C++ implementations of processor timing models, cache partitioning, and a systolic array done as a part of ECE6100 Advanced Computer Architecture. The processor and cache simulators read instruction or memory traces. The systolic array simulates matrix multiplication through local accumulation and operand movement between processing elements.

## Implementations

| Project | Implementation |
| --- | --- |
| [Superscalar pipeline](superscalar_pipeline/) | Five-stage pipeline, dependence checks, forwarding, and gshare prediction |
| [Out-of-order execution](out_of_order_execution/) | ROB-based renaming, ready-instruction scheduling, and in-order commit 
| [Cache and memory systems](cache_memory_system/) | Cache hierarchy, DRAM row timing, and static and dynamic partitioning 
| [Systolic array](matrix_computing/systolic_array/) | Output-stationary integer MAC grid  


## Build and verification

The programs require C11 and C++11 compilers, Make, Python 3, and gzip/gunzip. They use POSIX process and timing APIs and target macOS and Linux.

```sh
make
make check


## Experiments

Place compatible trace files in the directories listed in [Trace inputs](data/README.md), then run:

```sh
python3 scripts/run_experiments.py pipeline
python3 scripts/run_experiments.py ooo
python3 scripts/run_experiments.py cache
```

The scripts write execution logs to each project's `results/runs/` directory. Result tables and benchmark outputs are stored under `results/`. Systolic scaling beyond the checked 4 x 4 case is calculated from the cycle model.

The systolic array directory documents its executable and run command. Its cycle count follows the simulated array timing model.

## Reports

Each implementation contains `report/report.tex` and a compiled PDF. Rebuilding requires a TeX distribution with pdfLaTeX and TikZ:

```sh
make reports
```

The reports include vector diagrams and plots generated from the CSV results. Rebuilding the plots requires Python with Matplotlib and NumPy:

```sh
python3 scripts/plot_results.py
make reports
