# Optimized Weighted Sum Benchmarks

Build target:

```text
weighted_sum_opt_bench
```

This target keeps the weighted-sum HE calculation fully encrypted:

```text
Enc(amount) * Enc(risk_weight)
```

## Variant

```text
weighted_sum_amount_risk_opt_multiply_add_then_sum
```

Flow:

```text
for each chunk:
  Enc(amount_chunk) * Enc(risk_weight_chunk)
        |
        v
  weighted_chunk_ciphertext

add weighted_chunk_ciphertexts slot-wise
        |
        v
one EvalSum at the end
        |
        v
decrypt scalar SUM(amount * risk_weight)
```

This is meant to reduce repeated rotation-heavy `EvalSum` calls.
