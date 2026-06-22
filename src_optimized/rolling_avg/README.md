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
rolling-average code should live here and build as a separate executable later,
following the same separation pattern as `src_optimized/sum_amount/`.

## Reference Variants

| Reference benchmark | Meaning |
| --- | --- |
| `rolling_avg_vector_w3/w5/w9` | Schema A: compute encrypted rolling-average vector, decrypt vector, compare to C++. No final `EvalSum`. |
| `rolling_avg_amount_w3/w5/w9` | Schema B: compute encrypted rolling-average vector, then `EvalSum` outputs to one scalar mean. |

Optimization should start with Schema A because it isolates the rolling-window
math from the scalar reduction path.
