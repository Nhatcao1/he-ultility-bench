# Lookup Benchmark Commands

Build on the server:

```bash
cd ~/he-ultility-bench
git pull

cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE

cmake --build build --target lookup_opt_bench -j"$(nproc)"
```

Run 100k rows first:

```bash
rm -f results/optimized_lookup/lookup_channel_weight_vector_100k_repeat3.csv

./build/lookup_opt_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --channel-domain 10 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/optimized_lookup/lookup_channel_weight_vector_100k_repeat3.csv

grep "_summary_avg" results/optimized_lookup/lookup_channel_weight_vector_100k_repeat3.csv
```

Run 1m rows after the 100k result looks correct:

```bash
rm -f results/optimized_lookup/lookup_channel_weight_vector_1m_repeat3.csv

./build/lookup_opt_bench \
  --data data/generated/custom_1m/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --channel-domain 10 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/optimized_lookup/lookup_channel_weight_vector_1m_repeat3.csv

grep "_summary_avg" results/optimized_lookup/lookup_channel_weight_vector_1m_repeat3.csv
```

Use `he_eval_time_ms` for the lookup calculation cost. Use
`total_he_time_ms` when you want encode/encrypt/decrypt/decode included.
