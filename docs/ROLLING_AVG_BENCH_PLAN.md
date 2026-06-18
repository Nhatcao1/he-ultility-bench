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

The result CSV stores the mean of the rolling-average output vector:

```text
AVG(all rolling-average outputs)
```

That keeps the existing result schema while avoiding a giant row-count-scaled
checksum that looks wrong on large datasets.

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
| Chunk checksum | `EvalSum` | Sums masked rolling-average slots over a power-of-two length. |
| Chunk accumulation | `EvalAdd` | Adds chunk checksums. |
| Decrypt | `Decrypt` | Decrypts one scalar checksum before final reporting normalization. |

No bootstrapping is used. Rolling average is shallow in multiplicative depth;
it is mainly a rotation and packing benchmark.

## Benchmarks

```text
rolling_avg_amount_w3
rolling_avg_amount_w5
rolling_avg_amount_w9
all_rolling
```

Larger windows create more explicit rotations:

```text
w3 -> 2 rotations per ciphertext chunk before EvalSum
w5 -> 4 rotations per ciphertext chunk before EvalSum
w9 -> 8 rotations per ciphertext chunk before EvalSum
```

`rotation_count_reported` also includes a simple estimate for the rotations
used by `EvalSum`.

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
  --ckks-depth 1 \
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
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_rolling_avg_10k.csv
```
