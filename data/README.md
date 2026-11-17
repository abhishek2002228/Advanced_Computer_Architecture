# Trace Inputs

The processor simulators read traces from `traces/pipeline/`. The cache simulator reads traces from `traces/memory/`. These compressed inputs are stored locally and ignored by Git. Build checks generate synthetic inputs and do not require the benchmark traces.

| Directory | Files | Encoding |
| --- | --- | --- |
| `traces/pipeline/` | `bzip2.ptr.gz`, `gcc.ptr.gz`, `libq.ptr.gz`, `mcf.ptr.gz`, `sml.ptr.gz` | gzip-compressed native `TraceRec` records; see each processor's `src/trace.h` |
| `traces/memory/` | `bzip2.mtr.gz`, `lbm.mtr.gz`, `libq.mtr.gz` | gzip-compressed records of a 32-bit instruction address, an 8-bit operation type, and a 32-bit data address |

Processor records use the native `TraceRec` layout, including structure padding. The input producer and simulator must use compatible byte order, field sizes, and alignment. The memory format stores each field consecutively without padding between fields.

Benchmark outputs are stored in each project's `results/` directory. The sweep scripts check that the required inputs exist before starting execution.
