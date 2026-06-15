# HE Utility Bench

Benchmark scaffold for comparing plaintext computation with future OpenFHE
implementations.

## Generate Test Data

```bash
pip install -r requirements.txt
python3 scripts/generate_benchmark_data.py
```

Default datasets are written under `data/generated/`:

```text
tiny_1k/
small_10k/
medium_100k/
```

Generated CSV files are ignored by Git.

## Build Plaintext Baseline

```bash
cmake -S . -B build
cmake --build build
```

## Run Plaintext Baseline

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --scheme plain \
  --bench all_plain \
  --results results/benchmark_results.csv
```

Run one benchmark:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --scheme plain \
  --bench vector_add_x1_x2
```

Run several benchmarks:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --scheme plain \
  --bench vector_add_x1_x2 \
  --bench masked_sum_amount_channel_5
```

To also save computed operation outputs for inspection:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --scheme plain \
  --bench all_plain \
  --results results/benchmark_results.csv \
  --save-outputs \
  --output-dir results/outputs/tiny_1k
```

The first baseline measures compute-only timing for:

```text
vector_add_x1_x2
vector_mul_x1_x2
sum_x1
linear_score
masked_sum_amount_channel_5
masked_count_channel_5
masked_avg_amount_channel_5
```

CSV loading time is printed separately and is not included in the compute timing.
Output file writing is also excluded from compute timing.

Saved output files use this shape:

```text
results/outputs/tiny_1k/
  plain_vector_add_x1_x2.csv
  plain_vector_mul_x1_x2.csv
  plain_sum_x1.txt
  plain_linear_score.csv
  plain_masked_sum_amount_channel_5.txt
  plain_masked_count_channel_5.txt
  plain_masked_avg_amount_channel_5.txt
```
