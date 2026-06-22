# Polynomial Score Optimization Commands

Run on the server after pulling.

## Build

```bash
cd ~/he-ultility-bench
git pull

cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE

cmake --build build --target poly_ml_bench poly_score_opt_bench -j"$(nproc)"
```

If `OpenFHE_DIR` does not match the server install, use:

```bash
cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DCMAKE_PREFIX_PATH=$HOME/openfhe-install
```

## Original Reference, Degree 9 + Bootstrap

Start with `--max-rows 1000` because CKKS bootstrapping can be very RAM-heavy.
Remove `--max-rows 1000` only after the smoke run is clean.

```bash
rm -f results/original/poly_degree9_bootstrap_1k_repeat3.csv

./build/poly_ml_bench \
  --data data/generated/medium_100k/transactions.csv \
  --max-rows 1000 \
  --bench poly_score_degree9_bootstrap \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 1024 \
  --ckks-depth 9 \
  --ckks-scale-bits 59 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 10 \
  --bootstrap-level-budget 4 4 \
  --results results/original/poly_degree9_bootstrap_1k_repeat3.csv

grep "_summary_avg" results/original/poly_degree9_bootstrap_1k_repeat3.csv
```

## Optimized Option 1: Power Tree + Bootstrap

```bash
rm -f results/optimized_poly/poly_degree9_power_tree_bootstrap_1k_repeat3.csv

./build/poly_score_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --max-rows 1000 \
  --variant power_tree_bootstrap \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 1024 \
  --ckks-depth 9 \
  --ckks-scale-bits 59 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 10 \
  --bootstrap-level-budget 4 4 \
  --results results/optimized_poly/poly_degree9_power_tree_bootstrap_1k_repeat3.csv

grep "_summary_avg" results/optimized_poly/poly_degree9_power_tree_bootstrap_1k_repeat3.csv
```

## Optimized Option 2: Block Evaluation + Bootstrap

```bash
rm -f results/optimized_poly/poly_degree9_block_bootstrap_1k_repeat3.csv

./build/poly_score_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --max-rows 1000 \
  --variant block_bootstrap \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 1024 \
  --ckks-depth 9 \
  --ckks-scale-bits 59 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 10 \
  --bootstrap-level-budget 4 4 \
  --results results/optimized_poly/poly_degree9_block_bootstrap_1k_repeat3.csv

grep "_summary_avg" results/optimized_poly/poly_degree9_block_bootstrap_1k_repeat3.csv
```

## Run Both Optimized Options Together

```bash
rm -f results/optimized_poly/poly_degree9_both_1k_repeat3.csv

./build/poly_score_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --max-rows 1000 \
  --variant all \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 1024 \
  --ckks-depth 9 \
  --ckks-scale-bits 59 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 10 \
  --bootstrap-level-budget 4 4 \
  --results results/optimized_poly/poly_degree9_both_1k_repeat3.csv

grep "_summary_avg" results/optimized_poly/poly_degree9_both_1k_repeat3.csv
```
