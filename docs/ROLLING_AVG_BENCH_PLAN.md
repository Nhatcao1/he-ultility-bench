# Rolling Average CKKS Benchmark

This benchmark tests a rolling numeric window over `transactions.amount`.
It is not a full SQL/Spark window-function benchmark. It is a CKKS SIMD
rotation benchmark.

## Operation

For window size `w = 3`:

```text
input amount:
  [a0, a1, a2, a3, a4, ...]

rolling_avg_amount_w3:
  [
    (a0 + a1 + a2) / 3,
    (a1 + a2 + a3) / 3,
    (a2 + a3 + a4) / 3,
    ...
  ]
```

The current result CSV stores the mean of the rolling-average output vector:

```text
AVG(all rolling-average outputs)
```

That keeps the existing result schema while avoiding a giant row-count-scaled
checksum that looks wrong on large datasets.

## Schema A vs Schema B

The rolling average can be tested in two useful ways.

### Schema A: Rolling Vector Correctness

Schema A tests only the rolling-average operation itself.

```text
encrypted amount
        |
        v
EvalRotate + EvalAdd
        |
        v
EvalMult by plaintext 1/window mask
        |
        v
encrypted rolling-average vector
        |
        v
decrypt vector
        |
        v
compare rolling_i values against plain C++
```

Output shape:

```text
[rolling_0, rolling_1, rolling_2, ...]
```

What it answers:

```text
Are rotations, overlap packing, and division-by-window correct?
```

What it avoids:

```text
No EvalSum.
No scalar checksum.
No final "mean of rolling averages" reporting step.
```

This is the best next diagnostic path because failed runs show the scalar HE
result near zero while the plain result is around `5003`. That failure can come
from the reduction/reporting path, so we should verify the vector before blaming
the rolling math.

### Schema B: Scalar Summary

Schema B is the currently implemented benchmark.

```text
encrypted amount
        |
        v
encrypted rolling-average vector
        |
        v
EvalSum rolling outputs
        |
        v
encrypted scalar checksum
        |
        v
decrypt scalar
        |
        v
divide by output row count
```

Output shape:

```text
single scalar = mean(all rolling_i)
```

What it answers:

```text
What is the full cost of rolling average plus encrypted reduction?
```

Current concern:

```text
The reported HE scalar result has been effectively zero in server runs.
That means Schema B is not yet trustworthy for correctness.
Schema A exists to isolate the problem.
```

Implemented Schema A benchmark names:

```text
rolling_avg_vector_w3
rolling_avg_vector_w5
rolling_avg_vector_w9
all_rolling_vector
```

## CKKS Flow

```text
transactions.amount
        |
        v
Pack overlapping amount values into CKKS slots
using a power-of-two output block
        |
        v
Encrypt amount vector
        |
        v
ct0 = Enc([a0, a1, a2, a3, ...])
        |
        +----------------------------+
        |                            |
        v                            v
EvalRotate(ct0, 1)           EvalRotate(ct0, 2)
rotate left by 1             rotate left by 2
        |                            |
        +-------------+--------------+
                      |
                      v
EvalAdd:
  ct_sum = ct0 + rot1 + rot2
        |
        v
ct_sum slots:
  [
    a0+a1+a2,
    a1+a2+a3,
    a2+a3+a4,
    ...
  ]
        |
        v
EvalMult with plaintext mask:
  [1/3, 1/3, 1/3, ..., 0, 0]
        |
        v
ModReduceInPlace after mask multiplication
        |
        v
encrypted rolling averages
        |
        v
EvalSum valid output slots
using a power-of-two reduction length
        |
        v
encrypted checksum for this chunk
        |
        v
EvalAdd chunk checksums
        |
        v
Decrypt final checksum
        |
        v
Divide by output row count for CSV result_value
```

## Why Overlap Packing

CKKS rotations wrap inside one ciphertext:

```text
[a0, a1, a2, ..., aN] rotated by 1 -> [a1, a2, ..., aN, a0]
```

That wraparound is wrong for real rolling windows. The benchmark avoids fake
wraparound by packing overlap rows at the end of each chunk:

```text
window = 3
usable output slots = slots_per_ciphertext - 2

chunk input:
  [a0, a1, a2, ..., aK, aK+1]

valid outputs:
  (a0+a1+a2)/3
  ...
  (aK-1+aK+aK+1)/3
```

The overlap values are read by rotations but are not output positions.

## Power-Of-Two Reduction Blocks

OpenFHE packed reductions are most reliable with power-of-two slot shapes.
The benchmark therefore does not use every possible output slot in each
ciphertext. Instead, it chooses:

```text
output_slots_per_ciphertext =
  largest power of two <= slots_per_ciphertext - (window_size - 1)
```

For example, with `8192` slots and `w3`:

```text
slots_per_ciphertext - overlap = 8192 - 2 = 8190
output_slots_per_ciphertext = 4096
```

That gives lower slot utilization, but the `EvalSum` reduction length is a
clean power of two. The final partial chunk is also padded and masked so
`EvalSum` sees a power-of-two length while only real output rows contribute to
the result.

## HE Operations Used

| Stage | OpenFHE operation | Notes |
| --- | --- | --- |
| Pack | `MakeCKKSPackedPlaintext` | Outside HE math, timed as encode. |
| Encrypt | `Encrypt` | Encrypts the amount vector. |
| Rotate | `EvalRotate` | Main cost being tested. |
| Add rotations | `EvalAdd` | Adds rotated ciphertexts. |
| Divide by window | `EvalMult` with plaintext mask | Multiplies by `1/window`; no encrypted division. |
| Reduce after mask | `ModReduceInPlace` | Required after plaintext mask multiplication, following OpenFHE masking examples. |
| Chunk checksum | `EvalSum` | Sums masked rolling-average slots over a power-of-two length. |
| Chunk accumulation | `EvalAdd` | Adds chunk checksums. |
| Decrypt | `Decrypt` | Decrypts one scalar checksum before final reporting normalization. |

No bootstrapping is used. Rolling average is shallow in multiplicative depth;
it is mainly a rotation and packing benchmark. The code enforces minimum CKKS
depth `2` for rolling paths because the plaintext average mask multiplication
still consumes level/scale budget after rotations.

## Benchmarks

```text
rolling_avg_amount_w3
rolling_avg_amount_w5
rolling_avg_amount_w9
all_rolling
rolling_avg_vector_w3
rolling_avg_vector_w5
rolling_avg_vector_w9
all_rolling_vector
```

Larger windows create more explicit rotations:

```text
w3 -> 2 rotations per ciphertext chunk before EvalSum
w5 -> 4 rotations per ciphertext chunk before EvalSum
w9 -> 8 rotations per ciphertext chunk before EvalSum
```

`rotation_count_reported` also includes a simple estimate for the rotations
used by `EvalSum`.

For Schema A vector checks, `rotation_count_reported` includes only the explicit
rolling-window rotations. There is no `EvalSum` in that path.

## Server Command

```bash
cd ~/he-ultility-bench
git pull

cmake --build build --parallel "$(nproc)"

rm -f results/benchmark_results_rolling_avg_100k.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_rolling_avg_100k.csv
```

If the full-slot run is too slow, try smaller `--max-rows` first:

```bash
./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench rolling_avg_amount_w3 \
  --backend all \
  --threads 1 \
  --max-rows 10000 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_rolling_avg_10k.csv
```

## Schema A Server Command

Run this before trusting the scalar-summary path:

```bash
cd ~/he-ultility-bench
git pull

cmake --build build --parallel "$(nproc)"

rm -f results/benchmark_results_rolling_vector_100k.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling_vector \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_rolling_vector_100k.csv
```

Expected result:

```text
result_value should be close to baseline_value, around the normal amount scale
of roughly 5000 for generated data.
```
