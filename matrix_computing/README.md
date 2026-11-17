# Matrix Computing

The [systolic array](systolic_array/) simulator computes integer matrix products on a 4 x 4 output-stationary grid. Each processing element accumulates one output while A operands move east and B operands move south.

The directory contains source code, a scalar-reference comparison, cycle counts, and calculations of throughput, utilization, and input bandwidth as array size increases. The report includes an array diagram and scaling plots, with larger-array values identified as calculations from the cycle model.

See the [implementation README](systolic_array/README.md), [report](systolic_array/report/report.pdf), and [LaTeX source](systolic_array/report/report.tex).
