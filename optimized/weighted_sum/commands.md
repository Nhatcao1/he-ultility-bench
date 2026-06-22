# Weighted Sum Optimization Commands

Run on the server after pulling.

## Build

```bash
cd ~/he-ultility-bench
git pull

cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE

cmake --build build --target utility_bench weighted_sum_opt_bench -j"$(nproc)"
```

## Original All-Encrypted Weighted Sum, 1m

```bash
rm -f results/original/weighted_sum_encrypted_1m_repeat3.csv

./build/utility_bench \
  --data data/generated/custom_1m/transactions.csv \
  --bench weighted_sum_amount_risk \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/weighted_sum_encrypted_1m_repeat3.csv

grep "_summary_avg" results/original/weighted_sum_encrypted_1m_repeat3.csv
```

## Optimized Multiply-Add-Then-Sum, 1m

```bash
rm -f results/optimized_weighted/weighted_sum_multiply_add_then_sum_1m_repeat3.csv

./build/weighted_sum_opt_bench \
  --data data/generated/custom_1m/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/optimized_weighted/weighted_sum_multiply_add_then_sum_1m_repeat3.csv

grep "_summary_avg" results/optimized_weighted/weighted_sum_multiply_add_then_sum_1m_repeat3.csv
```
