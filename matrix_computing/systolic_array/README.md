# Output-Stationary Systolic Array

This cycle-level simulator computes an integer matrix product on a 4 x 4 grid of multiply-accumulate (MAC) elements. Each element stores one output accumulator while operands move through the grid.

## Array design

Each processing element receives A from the west and B from the north. When both inputs are valid, it accumulates their product and forwards A east and B south. An operand that arrives first waits for its partner. Each element completes after N products.

Left FIFOs hold A rows, and top FIFOs hold B columns. A cycle evaluates all elements using their current inputs, stages transfers to neighbors, refills free boundary inputs, and commits the staged inputs. This ordering limits a transfer to one grid hop per cycle.

The driver uses seed 1234 and compares every output with a scalar matrix product. N is defined as 4 in `main.c`, `pe.c`, and `fifo.c`; the definitions must agree when changing the array size.

## Build and execution

Run from the repository root:

```sh
make -C matrix_computing/systolic_array/src
matrix_computing/systolic_array/src/sim
```

## Results and scaling

The 4 x 4 result matches the scalar reference and completes in 11 calls to the cycle routine. The first call loads boundary inputs and performs no MACs. With this counting convention, the last element of an N x N array completes on call `3*N - 1`.

| N | Cycles | MAC/cycle | Util. % | Input B/cycle |
| --- | --- | --- | --- | --- |
| 4 | 11 | 5.82 | 36.36 | 2.91 |
| 16 | 47 | 87.15 | 34.04 | 10.89 |
| 64 | 191 | 1372.48 | 33.51 | 42.89 |
| 256 | 767 | 21873.81 | 33.38 | 170.89 |

Only N=4 was checked by execution. The larger rows are calculated from the latency model. Average throughput is `N^3 / cycles` MACs per cycle, and utilization is throughput divided by the `N*N` peak rate. Input bandwidth assumes one-byte operands and averages the `2*N*N` input bytes over the operation; output transfer is excluded.

The boundary FIFOs hold `2*N*N` integers, requiring `2*N*N*sizeof(int)` bytes plus queue metadata. The grid also stores accumulators and operand packets. With one-byte elements, input-only arithmetic intensity is `N/2` MAC/byte; including one byte written per output gives `N/3` MAC/byte. The simulator stores C integers and does not enforce an off-chip bandwidth limit.

`results/verification.txt` contains the checked execution, and `results/analytical_scaling.csv` contains the calculated rows. The figures in `results/historical/` plot latency as `3*N - 1` and peak boundary input bandwidth as `2*N` bytes per cycle. That peak bandwidth differs from the operation-average bandwidth in the table.

The [report](report/report.pdf) and [LaTeX source](report/report.tex) describe the cycle model and scaling calculations.
