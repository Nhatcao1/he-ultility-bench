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

## Three Plain C++ Baseline 100k Runs

```bash
rm -f results/original/sum_amount_plain_100k_run1.csv
rm -f results/original/sum_amount_plain_100k_run2.csv
rm -f results/original/sum_amount_plain_100k_run3.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend plain_cpp \
  --threads 1 \
  --results results/original/sum_amount_plain_100k_run1.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend plain_cpp \
  --threads 1 \
  --results results/original/sum_amount_plain_100k_run2.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend plain_cpp \
  --threads 1 \
  --results results/original/sum_amount_plain_100k_run3.csv
```

## Three Original Unoptimized Additive 100k Runs

These use the existing reference code. Security is not passed as a CLI option;
the current code uses OpenFHE `HEStd_128_classic` internally.

```bash
rm -f results/original/sum_amount_100k_run1.csv
rm -f results/original/sum_amount_100k_run2.csv
rm -f results/original/sum_amount_100k_run3.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/sum_amount_100k_run1.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/sum_amount_100k_run2.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/sum_amount_100k_run3.csv
```

## Reference 1m Run

```bash
rm -f results/original/sum_amount_1m.csv

./build/utility_bench \
  --data data/generated/custom_1m/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/sum_amount_1m.csv
```

## Parameter Sweep To Try Before New Code

Use this only after the reference run is correct. Keep one result file per
parameter choice so the comparison stays readable.

### Batch-size sweep

```bash
rm -f results/optimized_add/sum_amount_100k_batch4096.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 4096 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/optimized_add/sum_amount_100k_batch4096.csv
```

### Lower-`Q` sweep

For addition-only sum, try reducing scale and first modulus bits before forcing
a smaller ring dimension. If accuracy stays good, this may let OpenFHE choose a
lighter parameter set.

```bash
rm -f results/optimized_add/sum_amount_100k_scale40_first50.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 40 \
  --ckks-first-mod-bits 50 \
  --results results/optimized_add/sum_amount_100k_scale40_first50.csv
```

### Explicit smaller-ring trial

Only keep this result if OpenFHE accepts the parameters and the accuracy is
still good. If OpenFHE rejects the ring dimension, that is useful information,
not a failure of the benchmark.

```bash
rm -f results/optimized_add/sum_amount_100k_ring8192_scale40_first50.csv

./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 8192 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 40 \
  --ckks-first-mod-bits 50 \
  --results results/optimized_add/sum_amount_100k_ring8192_scale40_first50.csv
```

## Future Optimized Run

The optimized executable is separate from the reference executable. Start with
the same 100k input and low-`Q` parameters we want to investigate.

```bash
rm -f results/optimized_add/sum_amount_opt_100k_scale40_first50.csv

./build/sum_amount_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --variant both \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 40 \
  --ckks-first-mod-bits 50 \
  --results results/optimized_add/sum_amount_opt_100k_scale40_first50.csv
```

For the explicit smaller-ring trial:

```bash
rm -f results/optimized_add/sum_amount_opt_100k_ring8192_scale40_first50.csv

./build/sum_amount_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --variant both \
  --threads 1 4 8 \
  --ckks-ring-dim 8192 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 40 \
  --ckks-first-mod-bits 50 \
  --results results/optimized_add/sum_amount_opt_100k_ring8192_scale40_first50.csv
```
