# Optimized Polynomial Score Benchmarks

This directory contains optimized polynomial-score benchmark code. It is
separate from the reference implementation in:

```text
src/poly_ml_bench.cpp
```

Build target:

```text
poly_score_opt_bench
```

## Current Variants

| Variant | Meaning |
| --- | --- |
| `power_tree_bootstrap` | Bootstrap the encrypted linear score, then compute degree-9 powers with an explicit power tree. |
| `block_bootstrap` | Bootstrap the encrypted linear score, then evaluate the degree-9 polynomial in low/high blocks, similar to Paterson-Stockmeyer grouping. |
| `all` | Run both optimized variants. |

Both variants compute the same plaintext target:

```text
z = 0.50*amount + 0.30*x1 + 0.20*x2 - 0.10*x3 - 0.25

p(z) = 0.5
     + 0.197*z
     - 0.004*z^3
     + 0.00008*z^5
     - 0.000001*z^7
     + 0.00000001*z^9

result = SUM(p(z))
```

The benchmark is intentionally about CKKS depth and bootstrap cost. It is not a
full ML pipeline.
