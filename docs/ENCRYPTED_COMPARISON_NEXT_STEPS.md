# Encrypted Comparison Next Steps

This note tracks what to do after the first `select_amount_gt_5000` benchmark.

The benchmark represents:

```sql
SELECT amount
FROM transactions
WHERE amount > 5000;
```

The plain C++ version is trivial, but the encrypted OpenFHE version is not.
For CKKS, `amount > 5000` is not a native cheap arithmetic operation like
`EvalAdd`, `EvalMult`, or `EvalSum`. The current implementation uses OpenFHE
CKKS-to-FHEW scheme switching so OpenFHE can evaluate the comparison and return
a mask-like result.

That makes this benchmark much heavier than `SUM(amount)` or
`SUM(amount * risk_weight)`.

## Current Lesson

Do not treat encrypted `WHERE amount > threshold` as a normal database filter.

For `100000` rows with `--ckks-batch-size 16`, the runner needs about:

```text
100000 / 16 = 6250 comparison chunks
```

Each chunk does substantially more work than the aggregation benchmarks:

```text
encode amount
encrypt amount
encode threshold
encrypt threshold
scheme-switch comparison
multiply amount by comparison mask
decrypt selected values
decode selected values
sum selected checksum locally
```

So a 100k encrypted comparison can look frozen even while it is still working.
Start smaller, then increase the batch size carefully.

## Recommended Run Order

Use single-thread first so one run can finish and produce a result row.

### First 10 Rows Debug Test

Use this when checking whether the encrypted comparison path works at all.
`--max-rows` stops the CSV loader after the first N transaction rows, so no
special dataset file is needed.

```bash
cd ~/he-ultility-bench

rm -f results/benchmark_results_select_amount_gt_5000_first10.csv

./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --max-rows 10 \
  --bench select_amount_gt_5000 \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 16 \
  --ckks-depth 17 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_select_amount_gt_5000_first10.csv
```

### Tiny Smoke Test

```bash
cd ~/he-ultility-bench

rm -f results/benchmark_results_select_amount_gt_5000_tiny_b16.csv

./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --bench select_amount_gt_5000 \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 16 \
  --ckks-depth 17 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_select_amount_gt_5000_tiny_b16.csv
```

### 100k With Larger Chunks

Try `256` first. If it is still too slow, try `512`. Do not sweep
`--threads 1 4 8` until the single-thread run finishes.

```bash
cd ~/he-ultility-bench

rm -f results/benchmark_results_select_amount_gt_5000_100k_b256.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench select_amount_gt_5000 \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 256 \
  --ckks-depth 17 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_select_amount_gt_5000_100k_b256.csv
```

## Why `--ckks-ring-dim 0`

For encrypted comparisons, let OpenFHE choose a security-compliant ring
dimension first:

```text
--ckks-ring-dim 0
```

The comparison path needs much larger parameters than the simple aggregation
path. Pinning a ring dimension too early can fail security checks or produce a
bad benchmark configuration. After a run succeeds, record the actual ring
dimension from the result CSV and reuse it later for reproducibility.

## Performance Notes From OpenFHE Docs

The static performance notes matter here:

| Area | Recommendation for this benchmark |
| --- | --- |
| CKKS/FHEW scheme switching | Expect much higher cost than plain CKKS arithmetic. |
| OpenMP | Sweep thread counts only after a single-thread run finishes. |
| Hyperthreading | Do not assume logical cores are faster; keep thread count at or below physical cores. |
| Memory | Scheme switching can use large memory for transforms and keys; avoid large slot counts until smaller runs are stable. |
| Key generation/setup | Setup time is reported separately; online timing is encode/encrypt/eval/decrypt/decode. |

## Next Code Optimizations

These are the next useful implementation changes, in order.

| Priority | Change | Why |
| ---: | --- | --- |
| 1 | Reuse the encrypted threshold ciphertext for full chunks | Avoid encrypting `[5000, 5000, ...]` for every chunk. |
| 2 | Add optional progress logging outside serious timing mode | Avoid silent multi-minute runs while keeping default benchmark timings clean. |
| 3 | Add a `--max-rows` debug option | Run first N rows from a larger dataset without generating new files. |
| 4 | Add a comparison batch-size sweep script | Produce `16, 64, 128, 256, 512` result rows consistently. |
| 5 | Add memory measurement outside hot timing loops | Confirm whether large runs are CPU-bound or memory-bound. |

The first optimization is the most important. Threshold `5000` is public and
constant for the whole query, so it should not need to be encoded and encrypted
fresh for every full chunk.

## What Not To Optimize Yet

Do not spend time on these until tiny and 100k single-thread runs are stable.

| Item | Reason |
| --- | --- |
| 1m encrypted comparison | Too expensive before chunking and threshold reuse are understood. |
| Full thread sweep | It multiplies runtime before we know one path finishes. |
| Join with encrypted comparison | Adds join complexity on top of the hardest current primitive. |
| Sorting or top-k | Requires many comparisons and selections; not a near-term benchmark. |

## Near-Term Benchmark Path

Use this order for the next few days:

```text
1. Confirm tiny encrypted comparison correctness.
2. Confirm 100k encrypted comparison finishes with larger batch size.
3. Implement threshold-ciphertext reuse.
4. Re-run tiny and 100k to measure improvement.
5. Sweep batch size.
6. Sweep threads.
7. Only then decide whether encrypted range WHERE is practical enough for this project.
```

This result may show that encrypted comparison is too expensive for broad
database-style filtering. That is still useful: it tells us to prefer
HE-friendly query forms such as plaintext/precomputed masks, lookup-expanded
weighted sums, and aggregation after preprocessing.
