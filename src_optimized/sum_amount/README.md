# Optimized `sum_amount` Code

Future optimized implementations for:

```sql
SELECT SUM(amount)
FROM transactions;
```

Reference implementation remains in:

```text
src/main.cpp
src/ckks_sum_amount.h
```

Current executable:

```text
sum_amount_opt_bench
```

Current variants:

| Variant | Meaning |
| --- | --- |
| `linear_add` | Mirrors the reference chunk-sum accumulation shape. Useful as the separated optimized-code baseline. |
| `tree_add` | Computes each encrypted chunk sum, then combines chunk sums with a binary EvalAdd tree. This may reduce serial dependency when there are many chunks. |

Both variants compute the same scalar `SUM(amount)` and must be compared
against the reference `utility_bench --bench sum_amount` output.
