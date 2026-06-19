# Optimized Benchmark Workspace

This directory tracks optimization attempts separately from the reference
benchmarks in `src/`.

The rule is simple:

```text
src/            = reference implementation, correctness-first
src_optimized/  = experimental optimized implementations
optimized/      = optimization plans, commands, and findings
results/        = generated CSV outputs, not committed
```

Every optimized benchmark should compare three things:

```text
plain C++ baseline
reference OpenFHE benchmark
optimized OpenFHE benchmark
```

The optimized version must compute the same math as the reference version
before we care about speed.

## Current Optimization Tracks

| Track | Status | Notes |
| --- | --- | --- |
| `sum_amount` | Planned | First target because it is the simplest aggregation and shows packing/rotation behavior clearly. |

## Result Layout

Use these result folders when running on the server:

```text
results/original/
results/optimized/
results/comparisons/
```

Generated result files stay ignored by Git.
