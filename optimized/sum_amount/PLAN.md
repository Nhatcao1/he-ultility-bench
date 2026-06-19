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

This track is measurement-first. Do not write optimized code until the
reference variance is visible.

| Step | Action | Why it matters |
| --- | --- | --- |
| 1 | Run the plain C++ baseline three times. | Gives normal timing variance before comparing HE. |
| 2 | Run the original unoptimized OpenFHE additive benchmark three times. | Establishes the current reference HE cost. |
| 3 | Use `ckks-ring-dim 0` and `ckks-batch-size 0` first. | Lets OpenFHE pick safe parameters and use available slots. |
| 4 | Keep `ckks-depth 1` first. | Addition-only does not need multiplicative depth, but current CLI expects positive depth. |
| 5 | Record actual ring dimension, slots, ciphertext count, and slot utilization. | Prevents fake speedups from underfilled ciphertexts. |
| 6 | Only then try lower `Q` / smaller ring experiments. | Ring dimension and modulus size are coupled by the security check. |
| 7 | Compare 100k first, then 1m. | 100k confirms behavior; 1m shows SIMD amortization better. |

## Modulus `Q` And Ring Dimension

For CKKS, ring dimension cannot be treated as an isolated speed knob.

Approximate intuition:

```text
larger depth
  -> more modulus-chain levels
  -> larger total coefficient modulus Q
  -> OpenFHE may require larger ring dimension for 128-bit security
  -> each ciphertext operation gets heavier
```

For addition-only `SUM(amount)`, we should not need a large modulus chain.
That means the first safe optimization direction is:

```text
keep depth low
try smaller scale bits only if accuracy stays good
let OpenFHE choose ring dimension first
then try explicit smaller ring dimensions only when OpenFHE accepts them
```

Important note: the current benchmark code does not expose a CLI security-bit
flag. It internally uses OpenFHE `HEStd_128_classic`. So "no specific security
bit" in the command means no extra CLI override, not "no security level."

## Optimized Code Boundary

Optimized code lives under:

```text
src_optimized/sum_amount/
```

The first optimized executable is intentionally narrow:

```text
sum_amount_opt_bench
```

It only benchmarks `SUM(amount)` so any speed difference is easier to explain.
Do not mix weighted sum, rolling average, comparison, or other benches into this
target.

Current variants:

| Variant | Purpose |
| --- | --- |
| `linear_add` | Separated optimized-code baseline that mirrors the reference chunk-sum accumulation shape. |
| `tree_add` | First candidate optimization: compute chunk sums, then combine them with a binary `EvalAdd` tree. |

## Candidate Code Optimizations Later

| Candidate | Expected benefit | Notes |
| --- | --- | --- |
| One-purpose executable | Less branching and less benchmark dispatch noise | Mostly cleanliness; HE time should dominate. |
| Reuse context/keys for repeated runs | Separates online eval speed from setup cost | Must report separately from end-to-end speed. |
| Pre-size packed vectors to full slot count | May make packing behavior more explicit | Need verify it does not change last-chunk correctness. |
| Binary tree addition for chunk sums | May reduce serial dependency when there are many chunks | More useful for 1m+ rows than 100k. |
| Parameter-only sweep | Finds better batch/ring settings before changing code | Safest first pass. |

## Iteration Rule

Each optimization attempt must be compared against both:

```text
1. plain C++ baseline
2. original unoptimized OpenFHE additive benchmark
```

Only accept an optimized variant if:

```text
same input data
same SUM(amount) result
relative_error within threshold
faster than original OpenFHE in either eval-only or total HE time
```

If a variant is only faster because it changes precision, ring security, setup
inclusion, or input packing assumptions, record that clearly instead of calling
it a general speedup.

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
