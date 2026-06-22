# Weighted Sum Optimization Plan

## Goal

Optimize the all-encrypted weighted aggregate:

```text
SUM(amount_i * risk_weight_i)
```

HE privacy rule:

```text
amount      -> encrypted
risk_weight -> encrypted
```

No CKKS plaintext multiplier path is used for this benchmark.

## Original Reference Flow

```text
chunk0:
  Enc(amount0) * Enc(weight0)
        |
        v
      EvalSum

chunk1:
  Enc(amount1) * Enc(weight1)
        |
        v
      EvalSum

chunk2:
  Enc(amount2) * Enc(weight2)
        |
        v
      EvalSum

add encrypted scalar chunk sums
        |
        v
decrypt final scalar
```

Cost shape:

```text
N chunks -> N ciphertext-ciphertext multiplies + N EvalSum reductions
```

## Optimized Flow

```text
chunk0:
  Enc(amount0) * Enc(weight0) -> weighted0

chunk1:
  Enc(amount1) * Enc(weight1) -> weighted1

chunk2:
  Enc(amount2) * Enc(weight2) -> weighted2

weighted0 + weighted1 + weighted2 + ...
        |
        v
one packed ciphertext containing slot-wise chunk totals
        |
        v
EvalSum once
        |
        v
decrypt final scalar
```

Cost shape:

```text
N chunks -> N ciphertext-ciphertext multiplies + many EvalAdd + 1 EvalSum
```

## What To Compare

Primary columns:

```text
he_eval_time_ms
total_he_time_ms
absolute_error
relative_error
ciphertext_count
actual_ring_dimension
slot_utilization
```

Expected win, if any:

```text
he_eval_time_ms should fall if repeated EvalSum rotations dominate.
```
