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

The executable defaults to:

```text
threads = 1
repeat count = 3
```

It writes each repeat row plus a `_summary_avg` row for each repeated test.

Default optimized-target parameters are intentionally aggressive for additive
testing:

```text
security level = OpenFHE HEStd_128_classic
requested ring dimension = 8192
scale bits = 30
first modulus bits = 40
depth = 1
```

This setting has passed the server-side OpenFHE standards check for the 1m
additive sum experiment.
