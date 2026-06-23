# Linear Score Commands

Build on the server:

```bash
cd ~/he-ultility-bench
git pull

cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE

cmake --build build --target linear_score_bench -j"$(nproc)"
```

Run 100k rows:

```bash
rm -f results/original/linear_score_100k_repeat3.csv

./build/linear_score_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/linear_score_100k_repeat3.csv

grep "_summary_avg" results/original/linear_score_100k_repeat3.csv
```

Run 1m rows:

```bash
rm -f results/original/linear_score_1m_repeat3.csv

./build/linear_score_bench \
  --data data/generated/custom_1m/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/original/linear_score_1m_repeat3.csv

grep "_summary_avg" results/original/linear_score_1m_repeat3.csv
```
