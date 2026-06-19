# `sum_amount` Optimization Commands

Run these on the server after pulling the latest repo.

## Build Reference Benchmarks

```bash
cd ~/he-ultility-bench

cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DCMAKE_PREFIX_PATH=/root/openfhe-install \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j"$(nproc)"
```

## Generate 100k And 1m Data

```bash
python3 scripts/generate_benchmark_data.py \
  --sizes 100000 1000000 \
  --out data/generated \
  --seed 42 \
  --chunk-size 100000 \
  --num-customers 10000 \
  --noise-std 300
```

## Reference 1m Run With Repeat Summary

This uses the existing reference code. Security is not passed as a CLI option;
the current code uses OpenFHE `HEStd_128_classic` internally.

The command writes three plain C++ rows, one plain `_summary_avg` row, three
normal OpenFHE rows, and one OpenFHE `_summary_avg` row.

```bash
rm -f results/original/sum_amount_1m_original_scale50_first60_repeat3.csv

./build/utility_bench \
  --data data/generated/custom_1m/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/sum_amount_1m_original_scale50_first60_repeat3.csv
```

## Original Code Lower-Q 1m Run

This is still **not optimized code**. It uses the normal `utility_bench`
reference implementation, but with lower modulus settings.

```bash
rm -f results/original/sum_amount_1m_original_scale40_first50_repeat3.csv

./build/utility_bench \
  --data data/generated/custom_1m/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 40 \
  --ckks-first-mod-bits 50 \
  --results results/original/sum_amount_1m_original_scale40_first50_repeat3.csv
```

## Original Code Lower-Q Hard-Ring 1m Run

This is also still normal/reference code. It tests whether OpenFHE accepts ring
`8192` at 128-bit security with the chosen `Q`.

Server note: `ring=8192 scale=40 first=50 depth=1` was rejected by OpenFHE with
the HE standards check recommending ring `16384`. `ring=8192 scale=30 first=40`
passed, so use `30/40` as the hard-ring additive setting for now.

```bash
rm -f results/original/sum_amount_1m_original_ring8192_scale30_first40_repeat3.csv

./build/utility_bench \
  --data data/generated/custom_1m/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 8192 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 30 \
  --ckks-first-mod-bits 40 \
  --results results/original/sum_amount_1m_original_ring8192_scale30_first40_repeat3.csv
```

## Optimized Code 1m Run With Hard 8192 Ring

The optimized executable defaults to `--ckks-ring-dim 8192`, but the command
keeps the value explicit so result files are self-explanatory. Security remains
OpenFHE `HEStd_128_classic` inside the code.

```bash
rm -f results/optimized_add/sum_amount_opt_1m_ring8192_scale30_first40.csv

./build/sum_amount_opt_bench \
  --data data/generated/custom_1m/transactions.csv \
  --backend all \
  --variant both \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 8192 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 30 \
  --ckks-first-mod-bits 40 \
  --results results/optimized_add/sum_amount_opt_1m_ring8192_scale30_first40.csv
```

## Accuracy Fallback If 30/40 Is Too Noisy

If `scale=30 first=40` is accepted but the CKKS error is too high, fall back to
auto ring with `scale=40 first=50`, or accept ring `16384` for higher precision.

```bash
rm -f results/original/sum_amount_1m_original_scale40_first50_repeat3.csv

./build/utility_bench \
  --data data/generated/custom_1m/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 40 \
  --ckks-first-mod-bits 50 \
  --results results/original/sum_amount_1m_original_scale40_first50_repeat3.csv
```

## What To Compare

Use rows where:

```text
operation ends with _summary_avg
backend ends with _summary
```

Compare:

```text
results/original/...scale50_first60...
results/original/...scale40_first50...
results/original/...ring8192_scale30_first40...
results/optimized_add/...sum_amount_opt...
```

Key columns:

```text
plain_time_ms
he_eval_time_ms
total_he_time_ms
actual_ring_dimension
slots_per_ciphertext
ciphertext_count
relative_error
notes
```
