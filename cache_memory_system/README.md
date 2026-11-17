# Cache, DRAM, and Utility-Based Partitioning

This simulator models private instruction and data caches, a shared L2, and DRAM access latency. It supports single-core and two-core traces and reports cache misses, access delays, and per-core IPC.

## Design basis

The dynamic partitioning design is derived from Moinuddin K. Qureshi and Yale N. Patt's *Utility-Based Cache Partitioning: A Low-Overhead, High-Performance, Runtime Mechanism to Partition Shared Caches*, MICRO 2006. The [original paper (PDF)](https://bpb-us-e1.wpmucdn.com/sites.gatech.edu/dist/8/175/files/2015/08/MICRO-2006-Qureshi-DynamicSetSampling.pdf?bid=175) describes allocating cache capacity according to the benefit each application receives from additional ways.

The implementation uses recency estimates from the active cache, one-way greedy allocation, and global occupancy targets. Independent utility monitors and the paper's lookahead algorithm are not implemented.

## Cache and DRAM design

Each cache line stores its tag, valid and dirty bits, owner, and LRU timestamp. Replacement supports LRU, random selection, static way partitioning, and dynamic way partitioning. Dirty evictions generate writebacks to the next level.

The fixed DRAM path costs 100 cycles. The open-page model has 16 banks and 1 KiB rows, with 45-cycle activation, column access, and precharge costs and a 10-cycle transfer cost. A row hit costs 55 cycles, an access to an empty bank costs 100, and a row conflict costs 145. The closed-page option uses the fixed 100-cycle path.

## Utility monitoring and allocation

On a hit, the monitor counts valid lines in the set with a newer timestamp. If this recency position is k, it increments the hit estimates for capacities larger than k. Each core has its own cumulative hit vector, but recency is measured in the shared set, so the other core's accesses affect the estimate.

Every 1,000,000 monitored accesses, the allocator starts with zero ways assigned and repeatedly gives the next way to the core with the larger estimated marginal hit gain. Ties go to core 0. The counters are cleared after the update. This greedy rule can miss gains that require several ways at once and does not guarantee an optimal allocation.

Static partitioning enforces quotas within each set. Dynamic partitioning converts each way allocation into a cache-wide occupancy target. On replacement, it selects an owner according to occupancy and evicts that owner's LRU line in the accessed set. If no such line is present, it selects a line from the other owner. Invalid entries are used before eviction.

## Build and execution

Run from the repository root with the inputs described in [Trace inputs](../data/README.md):

```sh
make -C cache_memory_system/src
cache_memory_system/src/sim -mode 4 -L2repl 3 data/traces/memory/bzip2.mtr.gz data/traces/memory/libq.mtr.gz
python3 scripts/run_experiments.py cache
```

`-mode` selects a standalone cache (`1`), a hierarchy with fixed DRAM latency (`2`), banked DRAM (`3`), or two cores (`4`). L2 replacement policies are LRU (`0`), random (`1`), static partitioning (`2`), and dynamic partitioning (`3`). `-SWP_core0ways` sets core 0's static quota.

## Results

The multicore runs use a 512 KiB, 16-way L2. The table sums the two reported core IPC values. Static columns give core 0's quota in ways. This sum is not weighted speedup or IPC measured over a common interval.

| Mix | LRU | Static 4 | Static 8 | Static 12 | Dynamic |
| --- | --- | --- | --- | --- | --- |
| mix1 | 1.276 | 1.231 | 1.288 | 1.317 | 1.166 |
| mix2 | 1.342 | 1.361 | 1.387 | 1.393 | 1.213 |
| mix3 | 1.029 | 1.028 | 1.051 | 1.083 | 1.045 |

The workload pairs, in core order, are bzip2 + libq for mix1, bzip2 + lbm for mix2, and lbm + libq for mix3. Dynamic partitioning lowers the sum relative to LRU by 8.6% and 9.6% in the first two mixes and raises it by 1.6% in the third. Static partitioning with twelve ways assigned to core 0 produces the highest sum in all three mixes. The current monitor and allocator therefore do not consistently improve these workloads.

`results/recorded/` contains execution logs, and `results/summary.csv` contains the policy comparison. New runs go to `results/runs/`. The [report](report/report.pdf) and [LaTeX source](report/report.tex) describe utility monitoring, allocation, and replacement.
