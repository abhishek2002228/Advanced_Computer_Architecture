# Out-of-Order Processor Simulation

This timing simulator models register renaming, instruction scheduling, execution latency, and in-order commit. Pipeline width, scheduling policy, and load latency are configurable.

## Renaming and execution

The Register Alias Table (RAT) maps each architectural register to its latest producer in the Reorder Buffer (ROB). At issue, an instruction reads producer tags for its sources and allocates a ROB entry for its destination. Source readiness follows the producing instruction, removing false register dependences while preserving true dependences.

The 32-entry ROB is a circular buffer with valid, executing, and ready bits. In-order scheduling stops at the oldest instruction waiting for an operand. Out-of-order scheduling skips waiting instructions and selects the oldest ready instructions. Both policies issue at most the pipeline width each cycle.

The execution queue holds operations until their latency expires. Writeback broadcasts producer tags to waiting instructions and marks completed ROB entries ready. Commit removes ready entries from the head in program order. It clears a RAT mapping only if that mapping still names the committing producer; a younger producer's mapping remains valid.

## Build and execution

Run from the repository root with the inputs described in [Trace inputs](../data/README.md):

```sh
make -C out_of_order_execution/src
out_of_order_execution/src/sim -pipewidth 2 -schedpolicy 1 -loadlatency 4 data/traces/pipeline/mcf.ptr.gz
python3 scripts/run_experiments.py ooo
```

`-schedpolicy 0` selects in-order scheduling, and `1` selects out-of-order scheduling. `-loadlatency` sets load execution latency in cycles.

## Results

The table gives two-wide CPI. IO and OOO identify the scheduling policy; L1 and L4 identify load latencies of one and four cycles.

| Workload | IO / L1 | OOO / L1 | IO / L4 | OOO / L4 |
| --- | --- | --- | --- | --- |
| bzip2 | 0.503 | 0.500 | 0.511 | 0.501 |
| gcc | 0.504 | 0.500 | 0.518 | 0.500 |
| libq | 0.500 | 0.500 | 0.500 | 0.500 |
| mcf | 0.712 | 0.500 | 1.053 | 0.605 |

With four-cycle loads, out-of-order scheduling reduces mcf CPI from 1.053 to 0.605, a 1.74x speedup. Independent instructions execute while load dependents wait. The other streams are closer to the ideal two-wide CPI of 0.5 and improve less. At width one, mcf improves from 1.451 to 1.001 CPI under the same load latency.

`results/recorded_cpi.csv` contains CPI for widths one and two. The sweep script writes new logs to `results/runs/`. The [report](report/report.pdf) and [LaTeX source](report/report.tex) describe renaming, scheduling, and commit.
