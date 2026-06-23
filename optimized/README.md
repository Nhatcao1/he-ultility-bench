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
| `weighted_sum` | Planning | All-encrypted `SUM(amount * risk_weight)`; compare per-chunk EvalSum with add-then-sum. |
| `rolling_avg` | Planning | Rotation-heavy benchmark; optimize Schema A vector path before scalar reduction. |
| `poly_score` | Planning | Degree-9 CKKS polynomial with bootstrap; compare original, power-tree, and block evaluation. |

## Result Layout

Use these result folders when running on the server:

```text
results/original/
results/optimized/
results/optimized_add/
results/optimized_weighted/
results/optimized_rolling/
results/optimized_poly/
results/comparisons/
```

Use `results/optimized_add/` for additive aggregation optimization work such
as `sum_amount`. Keep generic future optimized outputs in `results/optimized/`.

Generated result files stay ignored by Git.
