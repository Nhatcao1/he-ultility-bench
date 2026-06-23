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
| `add_then_sum` | End-to-end encrypted sum: encode/encrypt chunks, add packed ciphertext chunks slot-wise, then run one final `EvalSum`. |
| `parallel_encrypt_add_then_sum` | Same math as `add_then_sum`, but tries chunk-level parallel encode/encrypt before the final add-then-sum eval. |

All variants compute the same scalar `SUM(amount)` and must be compared
against the reference `utility_bench --bench sum_amount` output.

Optimization rule: all benchmarked variants must encrypt the row-level input
before doing the optimized HE calculation. Do not add variants that aggregate,
filter, or otherwise compute the answer before encryption.

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
