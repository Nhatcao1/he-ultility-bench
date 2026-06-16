# HE Utility Bench

Aggregation benchmark scaffold for comparing plaintext `SUM(amount)` with a
future OpenFHE CKKS packed EvalSum/rotation implementation.

## Planning Notes

- [CKKS depth recommendations](docs/CKKS_DEPTH_RECOMMENDATIONS.md)

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

For CKKS aggregation, `tiny_1k` is only a smoke test. It can be smaller than the
available CKKS slot count, so it may underfill SIMD slots. Once CKKS is wired in,
prefer slot-shaped sizes such as:

```bash
python3 scripts/generate_benchmark_data.py --sizes 2048 8192 32768 131072
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run Aggregation Baseline

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --bench sum_amount \
  --threads 1 4 8 \
  --results results/benchmark_results.csv
```

Default run is also `sum_amount` across `1 4 8` threads:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --threads 1 4 8
```

If `--threads` is omitted, the runner uses the default comparison set:

```text
1 4 8
```

To also save computed operation outputs for inspection:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --bench sum_amount \
  --threads 1 4 8 \
  --results results/benchmark_results.csv \
  --save-outputs \
  --output-dir results/outputs/tiny_1k
```

The baseline measures compute-only timing for:

```text
SELECT SUM(amount) FROM transactions;
```

CSV loading time is printed separately and is not included in the compute timing.
Output file writing is also excluded from compute timing.
Each selected benchmark is run once per requested thread count.

If you already have an older `results/benchmark_results.csv` from before the
current `backend` and `threads` columns were added, remove it before the next run:

```bash
rm -f results/benchmark_results.csv
```

Saved output files use this shape:

```text
results/outputs/tiny_1k/
  plain_sum_amount_threads_1.txt
  plain_sum_amount_threads_4.txt
  plain_sum_amount_threads_8.txt
```
