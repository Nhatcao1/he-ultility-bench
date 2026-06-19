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

## Reference 100k Run

```bash
rm -f results/original/sum_amount_100k.csv

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
  --results results/original/sum_amount_100k.csv
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

## Future Optimized Run

This command is intentionally a placeholder until `sum_amount_opt_bench` exists.

```bash
./build/sum_amount_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/optimized_add/sum_amount_100k.csv
```
