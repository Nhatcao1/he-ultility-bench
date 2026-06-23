# BinFHE Product LUT Commands

Build on the server:

```bash
cd ~/he-ultility-bench
git pull

cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE

cmake --build build --target binfhe_lut_bench -j"$(nproc)"
```

Start tiny because each row uses programmable bootstrapping:

```bash
rm -f results/original/binfhe_product_lut_10_repeat3.csv

./build/binfhe_lut_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --max-rows 10 \
  --binfhe-logq 12 \
  --results results/original/binfhe_product_lut_10_repeat3.csv

grep "_summary_avg" results/original/binfhe_product_lut_10_repeat3.csv
```

Then 100 rows:

```bash
rm -f results/original/binfhe_product_lut_100_repeat3.csv

./build/binfhe_lut_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --max-rows 100 \
  --binfhe-logq 12 \
  --results results/original/binfhe_product_lut_100_repeat3.csv

grep "_summary_avg" results/original/binfhe_product_lut_100_repeat3.csv
```

Then 1k rows only if 100 rows is sane:

```bash
rm -f results/original/binfhe_product_lut_1k_repeat3.csv

./build/binfhe_lut_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --max-rows 1000 \
  --binfhe-logq 12 \
  --results results/original/binfhe_product_lut_1k_repeat3.csv

grep "_summary_avg" results/original/binfhe_product_lut_1k_repeat3.csv
```
