# `sum_amount` Optimization Plan

## Goal

Optimize the simplest aggregation benchmark without changing the math:

```sql
SELECT SUM(amount)
FROM transactions;
```

This is the cleanest first optimization target because it uses CKKS packing,
encryption, `EvalSum`, rotations, and ciphertext addition, but does not involve
multiplication, comparison, bootstrap, lookup, or model logic.

## Reference Behavior

Reference code lives in `src/` and should not be changed for this track.

```text
src/main.cpp
src/ckks_sum_amount.h
```

Current HE flow:

```text
amount column
  -> pack up to slots_per_ciphertext values
  -> encrypt each packed vector
  -> EvalSum each ciphertext chunk
  -> EvalAdd chunk sums together
  -> decrypt one final scalar
```

The plain baseline is:

```text
for each row:
    total += amount[row]
```

## Correctness Rule

The optimized implementation must return the same scalar sum as the reference
within normal CKKS approximation error.

Recommended acceptance threshold for this benchmark:

```text
relative_error <= 1e-9
```

If the optimized result is wrong, it is not an optimization.

## What We Try First

| Step | Optimization idea | Why it is safe |
| --- | --- | --- |
| 1 | Run reference with `ckks-batch-size 0` so OpenFHE uses all available slots. | Pure parameter choice; same code and same math. |
| 2 | Compare auto ring dimension vs explicit production ring dimension. | Confirms whether OpenFHE parameter choice is already best for this depth/security. |
| 3 | Keep depth at `1` for reference runs. | Addition-only does not need multiplicative depth, but current CLI expects positive depth. |
| 4 | Record actual ring dimension, slots, ciphertext count, and slot utilization. | Prevents fake speedups from underfilled ciphertexts. |
| 5 | Compare 100k first, then 1m. | 100k confirms behavior; 1m shows SIMD amortization better. |

## Optimized Code Boundary

When we add code, it should live under:

```text
src_optimized/sum_amount/
```

The first optimized executable should be narrow and boring:

```text
sum_amount_opt_bench
```

It should only benchmark `SUM(amount)` so any speed difference is easier to
explain. Do not mix weighted sum, rolling average, comparison, or other benches
into the first optimized target.

## Candidate Code Optimizations Later

| Candidate | Expected benefit | Notes |
| --- | --- | --- |
| One-purpose executable | Less branching and less benchmark dispatch noise | Mostly cleanliness; HE time should dominate. |
| Reuse context/keys for repeated runs | Separates online eval speed from setup cost | Must report separately from end-to-end speed. |
| Pre-size packed vectors to full slot count | May make packing behavior more explicit | Need verify it does not change last-chunk correctness. |
| Binary tree addition for chunk sums | May reduce serial dependency when there are many chunks | More useful for 1m+ rows than 100k. |
| Parameter-only sweep | Finds better batch/ring settings before changing code | Safest first pass. |

## Do Not Optimize By

| Bad shortcut | Why not |
| --- | --- |
| Dropping encryption/decryption from total HE time | That creates an eval-only metric, not an end-to-end metric. Keep both columns. |
| Changing the input data | Makes original vs optimized comparison unfair. |
| Summing plaintext chunks outside HE | Breaks the encrypted-computation purpose. |
| Using a different precision target without recording it | Can hide accuracy loss as speedup. |

## First Success Criteria

The first useful result is a table like this:

| Dataset | Threads | Reference total HE | Optimized total HE | Reference eval | Optimized eval | Relative error |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 100k | 1 | TBD | TBD | TBD | TBD | TBD |
| 100k | 4 | TBD | TBD | TBD | TBD | TBD |
| 100k | 8 | TBD | TBD | TBD | TBD | TBD |
| 1m | 1 | TBD | TBD | TBD | TBD | TBD |
| 1m | 4 | TBD | TBD | TBD | TBD | TBD |
| 1m | 8 | TBD | TBD | TBD | TBD | TBD |
