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

No optimized C++ variant is committed yet. Start here when we are ready to add
`sum_amount_opt_bench`.
