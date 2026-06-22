# Optimized `rolling_avg` Code

This directory is reserved for experimental optimized implementations of:

```sql
rolling AVG(amount) over a fixed row window
```

Reference implementation remains in:

```text
src/ckks_sum_amount.h
src/main.cpp
```

Do not move or rewrite the reference path when testing optimizations. Optimized
rolling-average code lives here and builds as:

```text
rolling_avg_opt_bench
```

## Reference Variants

| Reference benchmark | Meaning |
| --- | --- |
| `rolling_avg_vector_w3/w5/w9` | Schema A: compute encrypted rolling-average vector, decrypt vector, compare to C++. No final `EvalSum`. |
| `rolling_avg_amount_w3/w5/w9` | Schema B: compute encrypted rolling-average vector, then `EvalSum` outputs to one scalar mean. |

Optimization should start with Schema A because it isolates the rolling-window
math from the scalar reduction path.

## Current Optimized Variant

| Variant | Meaning |
| --- | --- |
| `rolling_avg_shared_w3_w5_w9_vector` | Computes `w3`, `w5`, and `w9` Schema A rolling vectors in one pass by sharing rotations `1..8`. |

The result scalar is:

```text
mean(w3 rolling outputs) + mean(w5 rolling outputs) + mean(w9 rolling outputs)
```

This keeps one timing row for one combined workload instead of copying the same
shared runtime into three misleading separate rows.
