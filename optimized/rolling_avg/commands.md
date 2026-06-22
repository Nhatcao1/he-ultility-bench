# `rolling_avg` Optimization Commands

Run these on the server after pulling the latest repo.

## Build

```bash
cd ~/he-ultility-bench

cmake --build build -j"$(nproc)"
```

## Reference Schema A: Rolling Vector

This is the first baseline for optimization. It measures rolling-window
calculation without the final scalar `EvalSum`.

```bash
rm -f results/original/rolling_vector_100k_repeat3.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling_vector \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/rolling_vector_100k_repeat3.csv

grep "_summary_avg" results/original/rolling_vector_100k_repeat3.csv
```

## Reference Schema B: Rolling Scalar

This includes the final encrypted reduction to one scalar mean.

```bash
rm -f results/original/rolling_scalar_100k_repeat3.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/rolling_scalar_100k_repeat3.csv

grep "_summary_avg" results/original/rolling_scalar_100k_repeat3.csv
```

## Lower-Q Probe, 100k

Only try this after the reference command is clean. This is not yet optimized
code; it is a parameter probe against the reference implementation.

```bash
rm -f results/original/rolling_vector_100k_ring8192_scale30_first40_repeat3.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling_vector \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 8192 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 30 \
  --ckks-first-mod-bits 40 \
  --results results/original/rolling_vector_100k_ring8192_scale30_first40_repeat3.csv

grep "_summary_avg" results/original/rolling_vector_100k_ring8192_scale30_first40_repeat3.csv
```

If OpenFHE rejects ring `8192`, keep security at 128-bit and lower Q only if
accuracy remains acceptable. Do not silently change to lower security.

## Optimized Shared Rotations, 100k

This computes `w3`, `w5`, and `w9` rolling-vector workloads together by sharing
rotations `1..8`.

```bash
rm -f results/optimized_rolling/rolling_shared_w3_w5_w9_100k_repeat3.csv

./build/rolling_avg_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/optimized_rolling/rolling_shared_w3_w5_w9_100k_repeat3.csv

grep "_summary_avg" results/optimized_rolling/rolling_shared_w3_w5_w9_100k_repeat3.csv
```

## Optimized Shared Rotations, Lower-Q Probe

```bash
rm -f results/optimized_rolling/rolling_shared_w3_w5_w9_100k_ring8192_scale30_first40_repeat3.csv

./build/rolling_avg_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 8192 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 30 \
  --ckks-first-mod-bits 40 \
  --results results/optimized_rolling/rolling_shared_w3_w5_w9_100k_ring8192_scale30_first40_repeat3.csv

grep "_summary_avg" results/optimized_rolling/rolling_shared_w3_w5_w9_100k_ring8192_scale30_first40_repeat3.csv
```
