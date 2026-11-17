# Superscalar Pipeline and Branch Prediction

This simulator models a five-stage in-order pipeline with configurable width, dependence stalls, data forwarding, and branch direction prediction. It reads committed-path instruction traces and reports cycle count, CPI, and branch statistics.

## Pipeline design

Fetch, decode, execute, memory access, and writeback operate on multiple lanes. Decode checks register and condition-code dependences against older instructions, including older instructions in the same group. A stalled instruction prevents younger instructions from advancing past it.

Execute and memory forwarding can be enabled independently. An ALU result can be forwarded from execute; a load result becomes available after memory access. Writeback is processed before decode, allowing a register written in that cycle to be read by decode.

Branch prediction supports perfect prediction, always taken, and gshare. Gshare XORs the low 12 bits of the instruction address with a 12-bit global history register to index 4,096 two-bit counters. The counters start weakly taken and move toward the resolved direction. A misprediction stalls fetch until writeback. The model does not execute wrong-path instructions or compute register values.

## Build and execution

Run from the repository root with the inputs described in [Trace inputs](../data/README.md):

```sh
make -C superscalar_pipeline/src
superscalar_pipeline/src/sim -pipewidth 2 -enablememfwd -enableexefwd -bpredpolicy 2 data/traces/pipeline/gcc.ptr.gz
python3 scripts/run_experiments.py pipeline
```

## Results

CPI is the number of cycles divided by retired instructions. Scalar and Dual use one and two lanes without forwarding. Forwarding uses two lanes with both forwarding paths. These three configurations use perfect prediction. Taken and Gshare change the predictor on the forwarded two-wide pipeline.

| Workload | Scalar | Dual | Forwarding | Taken | Gshare |
| --- | --- | --- | --- | --- | --- |
| bzip2 | 1.062 | 0.578 | 0.508 | 0.662 | 0.515 |
| gcc | 1.141 | 0.672 | 0.521 | 0.612 | 0.525 |
| libq | 1.182 | 0.727 | 0.545 | 0.545 | 0.545 |
| mcf | 1.363 | 1.018 | 0.658 | 0.796 | 0.661 |
| sml | 1.220 | 0.770 | 0.570 | 0.800 | 0.800 |

For gcc, increasing width lowers CPI from 1.141 to 0.672. Forwarding lowers it to 0.521, giving a 2.19x speedup over the scalar configuration. Gshare produces 0.525 CPI with 1.330% mispredictions; always taken produces 0.612 CPI with 27.503% mispredictions.

The sml trace has 100 instructions and 15 conditional branches. Both predictors miss seven branches and produce 0.800 CPI.

The logs are in `results/recorded/`, and `results/summary.csv` contains the CPI table. The sweep script writes new logs to `results/runs/`. The [report](report/report.pdf) and [LaTeX source](report/report.tex) describe the pipeline and results.
