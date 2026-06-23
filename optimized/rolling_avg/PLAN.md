# `rolling_avg` Optimization Plan

## Goal

Optimize the CKKS rolling-average benchmark without changing the math:

```text
rolling_i = (amount_i + amount_{i+1} + ... + amount_{i+w-1}) / w
```

Current window sizes:

```text
w = 3, 5, 9
```

Reference code stays in:

```text
src/ckks_sum_amount.h
src/main.cpp
```

Optimized code will live separately in:

```text
src_optimized/rolling_avg/
```

## Current Reference Flow

For one ciphertext chunk:

```text
amount rows with overlap
        |
        v
Enc([a0, a1, a2, a3, ...])
        |
        v
EvalRotate by 1..w-1
        |
        v
EvalAdd rotated ciphertexts
        |
        v
EvalMult by plaintext average mask [1/w, 1/w, ..., 0]
        |
        v
ModReduce
        |
        v
encrypted rolling-average vector
```

Schema A stops here and decrypts the rolling vector.

Schema B continues:

```text
encrypted rolling-average vector
        |
        v
EvalSum valid output slots
        |
        v
encrypted scalar checksum per chunk
        |
        v
EvalAdd chunk checksums
        |
        v
decrypt scalar and divide by output row count
```

## Is The Current Algorithm Good Enough?

For correctness, yes. The current flow uses the right CKKS primitives for a
row-window operation:

| Current choice | Why it is reasonable |
| --- | --- |
| Overlap packing | Prevents rotation wraparound from creating fake windows. |
| Slot rotations | Natural CKKS way to align `amount_{i+k}` beside `amount_i`. |
| Plaintext average mask | Avoids ciphertext division; multiplication by `1/w` is cheap. |
| Schema A vector check | Tests rolling-window math without final scalar reduction. |
| Schema B scalar result | Measures rolling window plus aggregation into one report value. |

The main weakness is cost, not correctness:

```text
per chunk rotations ~= w - 1
Schema B extra rotations ~= log2(valid_output_slots)
```

So `w=9` is much heavier than `w=3`, and Schema B is heavier than Schema A.

## Optimization Ideas

| Idea | Try? | Why |
| --- | --- | --- |
| Increase useful batch size | Yes | More rows per ciphertext means fewer chunks, fewer encrypt/decrypt cycles, and fewer final reductions. |
| Fill all available SIMD slots | Yes | Rolling uses overlap, so track usable output slots separately from raw slots. |
| Pack values from multiple rows together | Already doing | Current column packing is the correct baseline shape. |
| Pack several output channels together | Later | Useful if we compute `w3`, `w5`, `w9` together or multiple rolling features together. |
| Row-wise packing | Mostly no | Rolling windows are naturally column/sequence oriented; row-wise packing usually makes rotations harder. |
| Lower multiplicative depth | Yes | Rolling only needs one plaintext multiplication after additions, so depth should stay low. |
| Lower `scalingModSize` | Yes | Like additive sum, lower Q may allow smaller/faster accepted parameters if accuracy stays good. |
| Design packing to avoid rotations | Hard, investigate | True rolling windows require adjacent-slot alignment. We can reduce rotations only by changing output shape or precomputing plaintext-side data, which may weaken the encrypted-data assumption. |
| Parallelize across independent ciphertexts | Later | Chunks are independent before final scalar reduction. Start with one-thread repeat summaries first. |

## Candidate Variants

### `shared_rotations_w3_w5_w9_vector`

Implemented first optimized-code candidate.

Purpose:

```text
Compute w3, w5, and w9 rolling-vector workloads in one pass by sharing
rotations 1..8.
```

Output:

```text
result_value = mean(w3 rolling outputs)
             + mean(w5 rolling outputs)
             + mean(w9 rolling outputs)
```

This is a combined workload row, not a per-window row.

### `vector_reference_copy`

Possible later separated optimized-code baseline that mirrors
`rolling_avg_vector_w*`.

Purpose:

```text
Prove the optimized executable matches reference behavior window-by-window if
the combined shared-rotation result is hard to interpret.
```

### `lower_q_vector`

Same algorithm as Schema A, but test lower modulus settings:

```text
ring=8192 if accepted
depth=2
scale bits maybe 30 or 40
first mod bits maybe 40 or 50
```

Purpose:

```text
Find whether rolling can use the same cheaper Q region as additive sum.
```

### `shared_rotations_multi_window`

Compute multiple windows from the same encrypted amount chunk:

```text
rot1 = Rotate(ct, 1)
rot2 = Rotate(ct, 2)
...
rot8 = Rotate(ct, 8)

w3 uses rot1, rot2
w5 uses rot1..rot4
w9 uses rot1..rot8
```

Purpose:

```text
If the report needs w3/w5/w9 together, do not recompute the same rotations
three separate times.
```

This is likely the most meaningful rolling-specific optimization.

## Avoid Fake Optimizations

Do not optimize by:

| Shortcut | Why not |
| --- | --- |
| Precomputing rolling windows in plaintext | Breaks the encrypted-data benchmark. |
| Doing any target calculation before encryption | Violates the rule that optimization must happen under encryption. |
| Dropping overlap rows | Creates rotation wraparound errors. |
| Comparing Schema A timing against Schema B timing | Schema B includes final `EvalSum`; Schema A does not. |
| Lowering precision without recording error | Could hide wrong results as speed. |
| Counting setup/keygen as primitive calculation | Setup is reported separately. |

## First Experiment Order

```text
1. Run reference Schema A: all_rolling_vector, repeat=3, one thread.
2. Run reference Schema B: all_rolling, repeat=3, one thread.
3. Run optimized shared_rotations_w3_w5_w9_vector.
4. Try lower-Q vector settings.
5. If needed, create vector_reference_copy for window-by-window comparison.
6. Only after Schema A is stable, optimize scalar Schema B.
```

## Metrics To Compare

Use summary rows only:

```text
operation ends with _summary_avg
backend ends with _summary
```

Main columns:

```text
plain_time_ms
he_eval_time_ms          # real encrypted rolling calculation
total_he_time_ms         # encode + encrypt + eval + decrypt + decode
setup_time_ms            # separate key/context setup
rotation_count_reported
ciphertext_count
slot_utilization
relative_error
```
