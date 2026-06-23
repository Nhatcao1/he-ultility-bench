# HE Utility Bench

Aggregation benchmark scaffold for comparing plaintext `SUM(amount)` with an
OpenFHE CKKS packed `EvalSum` implementation.

## Planning Notes

- [CKKS depth recommendations](docs/CKKS_DEPTH_RECOMMENDATIONS.md)
- [Encrypted comparison next steps](docs/ENCRYPTED_COMPARISON_NEXT_STEPS.md)
- [Linear score benchmark](docs/LINEAR_SCORE_BENCH.md)
- [BinFHE channel LUT benchmark](docs/BINFHE_CHANNEL_LUT_BENCH.md)
- [FedAvg merge benchmark](docs/FEDAVG_BENCH_PLAN.md)
- [Polynomial ML CKKS benchmark](docs/POLY_ML_BENCH_PLAN.md)
- [Dense 4x8 layer benchmark](docs/DENSE_LAYER_BENCH_PLAN.md)
- [Tiny trig function-eval benchmark](docs/FUNCTION_EVAL_TRIG_BENCH_PLAN.md)
- [Rolling average CKKS benchmark](docs/ROLLING_AVG_BENCH_PLAN.md)
- [OpenFHE optimization plan](docs/OPTIMIZATION_PLAN.md)
- [Optimized benchmark workspace](optimized/README.md)
- [PSI + OpenFHE join plan](docs/PSI_OPENFHE_JOIN_PLAN.md)
- [PSI install notes](docs/PSI_INSTALL.md)

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

For tiny encrypted lookup/join/compare feasibility data:

```bash
python3 scripts/generate_tiny_crypto_query_data.py
```

This writes a deterministic tiny dataset under:

```text
data/generated_tiny_crypto_query/join_lookup_16/
```

Use this generator for encrypted-key lookup/join experiments. It is not meant
for throughput claims.

## Run Tiny Crypto Query Benchmarks

Generate the tiny fixture first:

```bash
python3 scripts/generate_tiny_crypto_query_data.py
```

Then run the one-hot encrypted lookup and join-product benchmarks:

```bash
./build/utility_bench \
  --tiny-query-dir data/generated_tiny_crypto_query/join_lookup_16 \
  --bench all_tiny_query \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 16 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_tiny_query.csv
```

Available tiny query benchmarks:

```text
tiny_lookup_onehot_risk_weight
tiny_join_onehot_amount_risk
all_tiny_query
```

These benchmarks use encrypted one-hot customer masks and encrypted
`risk_weight` values. `tiny_join_onehot_amount_risk` also encrypts `amount`.
They avoid scalar encrypted equality for now; scalar equality and range
comparison remain separate scheme-switching work.

For FedAvg merge fixtures:

```bash
python3 scripts/generate_fedavg_fixtures.py
python3 scripts/test_fedavg_fixtures.py
```

For larger FedAvg timing fixtures:

```bash
python3 scripts/generate_fedavg_fixtures.py --fixtures flat_100k_c4
python3 scripts/generate_fedavg_fixtures.py --fixtures flat_1m_c4
python3 scripts/generate_fedavg_fixtures.py --fixtures flat_10m_c4
python3 scripts/test_fedavg_fixtures.py --fixtures flat_100k_c4
python3 scripts/test_fedavg_fixtures.py --fixtures flat_1m_c4
python3 scripts/test_fedavg_fixtures.py --fixtures flat_10m_c4
```

For dense-layer matrix fixtures:

```bash
python3 scripts/generate_dense_layer_data.py \
  --sizes 1000 100000 1000000 \
  --out data/generated_dense \
  --seed 42 \
  --chunk-size 100000
```

Run the dense 4x8 layer benchmark:

```bash
rm -f results/dense_layer_100k.csv

./build/dense_layer_bench \
  --fixture data/generated_dense/dense4x8_100k \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/dense_layer_100k.csv
```

Run tiny trig function-evaluation benchmarks:

```bash
rm -f results/function_eval_trig_tiny.csv

./build/function_eval_bench \
  --bench all_trig_tiny \
  --degrees 15 30 45 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 0 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/function_eval_trig_tiny.csv
```

Run the FedAvg benchmark:

```bash
./build/fedavg_bench \
  --fixture data/generated_fedavg/mini_mlp_75_c4 \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/fedavg_results.csv
```

Run the linear score benchmark:

```bash
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
```

Run the BinFHE channel LUT benchmark:

```bash
./build/binfhe_lut_bench \
  --data data/generated/medium_100k/transactions.csv \
  --backend all \
  --threads 1 \
  --repeat 3 \
  --max-rows 100 \
  --binfhe-ring-dim 4096 \
  --binfhe-logq 12 \
  --results results/original/binfhe_channel_lut_100_repeat3.csv
```

Run larger FedAvg benchmarks across several OpenFHE thread settings:

```bash
./build/fedavg_bench \
  --fixture data/generated_fedavg/flat_100k_c4 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/fedavg_results_100k_threads.csv

./build/fedavg_bench \
  --fixture data/generated_fedavg/flat_1m_c4 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/fedavg_results_1m_threads.csv

./build/fedavg_bench \
  --fixture data/generated_fedavg/flat_10m_c4 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/fedavg_results_10m_threads.csv
```

Run polynomial ML-style CKKS depth and bootstrap benchmarks:

```bash
./build/poly_ml_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench poly_score_degree3 \
  --bench poly_score_degree7 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 7 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/poly_ml_depth_100k.csv

./build/poly_ml_bench \
  --data data/generated/medium_100k/transactions.csv \
  --max-rows 100 \
  --bench poly_score_degree7_bootstrap \
  --backend openfhe_ckks \
  --threads 1 \
  --repeat 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 16 \
  --ckks-depth 12 \
  --ckks-scale-bits 45 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 12 \
  --bootstrap-level-budget 5 5 \
  --results results/original/poly_degree7_bootstrap_100_repeat1.csv
```

For CKKS aggregation, `tiny_1k` is only a smoke test. It can be smaller than the
available CKKS slot count, so it may underfill SIMD slots. The benchmark does
not require ring-shaped row counts; it records `slots_per_ciphertext`,
`ciphertext_count`, padding, and slot utilization in the result CSV.

## Build

Plain C++ only:

```bash
cmake -S . -B build
cmake --build build
```

With OpenFHE CKKS enabled on the server:

```bash
cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE
cmake --build build --parallel $(nproc)
```

The OpenFHE static docs recommend keeping OpenFHE's OpenMP build enabled for
CKKS/BGV/BFV-style RNS work and controlling runtime threads with
`OMP_NUM_THREADS`. The runner also calls `omp_set_num_threads` when OpenMP is
visible at compile time, so `--threads 1 4 8` should produce separate OpenFHE
thread-count rows when OpenFHE was built with `WITH_OPENMP=ON`.

## Run Aggregation Benchmark

Plain baseline only:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --bench sum_amount \
  --backend plain_cpp \
  --results results/benchmark_results.csv
```

Plain plus OpenFHE CKKS comparison:

```bash
rm -f results/benchmark_results.csv
./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 16384 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results.csv
```

Weighted aggregate, using `customer_id -> risk_weight` from `customers.csv`:

```bash
rm -f results/benchmark_results_weighted_sum.csv
./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench weighted_sum_amount_risk \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_weighted_sum.csv
```

The weighted benchmark automatically reads `customers.csv` from the same
dataset folder. Use `--customers path/to/customers.csv` to override it.
The OpenFHE path encrypts both `amount` and the expanded `risk_weight` vector
before multiplying them.
Use `--bench all_agg` to run both aggregate benchmarks, `sum_amount` and
`weighted_sum_amount_risk`. `all_agg` does not include encrypted comparison
benchmarks.

Rolling average, using CKKS rotations over `amount`:

`result_value` and `baseline_value` are the mean of all rolling-average outputs,
not the row-count-scaled checksum. Remove old rolling result CSVs before
rerunning after this change.
The rolling implementation uses power-of-two output blocks for `EvalSum`, so
slot utilization can be lower than the simple aggregate benchmarks.

```bash
rm -f results/benchmark_results_rolling_avg_100k.csv
./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_rolling_avg_100k.csv
```

Available rolling benchmarks:

```text
rolling_avg_amount_w3
rolling_avg_amount_w5
rolling_avg_amount_w9
all_rolling
rolling_avg_vector_w3
rolling_avg_vector_w5
rolling_avg_vector_w9
all_rolling_vector
```

`rolling_avg_amount_w*` is the Schema B scalar-summary path: rolling vector
plus encrypted `EvalSum`. `rolling_avg_vector_w*` is the Schema A diagnostic
path: decrypt the rolling vector directly and compute the reported mean after
decode, with no `EvalSum`.

Schema A command for checking the rolling-vector math first:

```bash
rm -f results/benchmark_results_rolling_vector_100k.csv
./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench all_rolling_vector \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_rolling_vector_100k.csv
```

For the all-encrypted numeric version, use:

```bash
rm -f results/benchmark_results_join_weighted_sum_encrypted_100k.csv
./build/utility_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench weighted_sum_amount_risk_encrypted \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_join_weighted_sum_encrypted_100k.csv
```

`weighted_sum_amount_risk_encrypted` is kept as an explicit alias for
`weighted_sum_amount_risk`; both encrypt `amount` and the expanded
`risk_weight` vector before the homomorphic multiply. The current medium join
path still expands `customer_id -> risk_weight` before encryption; fully
encrypted join-key matching is a separate encrypted equality/join problem.

OpenFHE encrypted comparison only for `amount > 5000`:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --bench compare_amount_gt_5000 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 16 \
  --ckks-depth 17 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_compare_amount_gt_5000_tiny.csv
```

This benchmark decrypts and checks only the encrypted comparison mask. It does
not multiply the mask by `amount`, does not select rows, and does not run
`EvalSum`.

For a very small comparison debug run, limit the loader to the first 10 rows:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --max-rows 10 \
  --bench compare_amount_gt_5000 \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 16 \
  --ckks-depth 17 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results_compare_amount_gt_5000_first10.csv
```

This benchmark represents:

```sql
SELECT amount > 5000
FROM transactions;
```

The scalar result is the number of rows whose encrypted comparison mask is
true.

Because the result CSV stores scalar values, `result_value` is the checksum /
sum of the comparison mask bits. The OpenFHE version uses CKKS-to-FHEW scheme
switching via OpenFHE comparison APIs, not a precomputed mask.

Start with `tiny_1k` for this benchmark, then scale up only after correctness
and memory use look sane. For `medium_100k`, run `--threads 1` first and use a
larger batch size such as `256` before attempting a full thread sweep. OpenFHE's
scheme switching docs note large memory use for many slots because of the
linear transforms, and the performance docs note that CKKS/FHEW scheme switching
uses higher-level OpenMP parallelization. See
[encrypted comparison next steps](docs/ENCRYPTED_COMPARISON_NEXT_STEPS.md) for
the recommended run order and optimization plan.

Default backend is `plain_cpp`, and the default benchmark is `sum_amount`:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv
```

If `--threads` is omitted, the OpenFHE backend uses the default comparison set:

```text
1 4 8
```

To also save computed operation outputs for inspection:

```bash
./build/utility_bench \
  --data data/generated/tiny_1k/transactions.csv \
  --bench sum_amount \
  --backend all \
  --threads 1 4 8 \
  --results results/benchmark_results.csv \
  --save-outputs \
  --output-dir results/outputs/tiny_1k
```

The benchmark measures compute-only timing for operations such as:

```text
SELECT SUM(amount) FROM transactions;
SELECT SUM(amount * risk_weight) FROM prepared_transactions;
SELECT amount FROM transactions WHERE amount > 5000;
```

CSV loading time is printed separately and is not included in the compute timing.
For weighted aggregation, customer lookup expansion is also printed separately
and excluded from compute timing. The CKKS weighted version encrypts both
`amount` and `risk_weight` before multiplying them.
Output file writing is also excluded from compute timing.
Plain C++ is always measured once as a single-thread baseline. OpenFHE CKKS is
run once per requested thread count and compared back to that same single-thread
plain baseline.
For OpenFHE rows, `total_he_time_ms` includes encode, encrypt, homomorphic
evaluation, decrypt, and decode. OpenFHE context/key generation is recorded as
`setup_time_ms` but is kept separate from the online query timing.

CKKS uses 128-bit security in this first version. Tunable parameters:

```text
--ckks-ring-dim 0          0 lets OpenFHE choose; 16384 is a tested 128-bit default here.
--ckks-batch-size 0        0 uses all slots, normally ring_dim / 2.
--ckks-depth 1             Sum and one multiply-then-sum both fit this first benchmark.
--ckks-scale-bits 50       CKKS scaling modulus size.
--ckks-first-mod-bits 60   CKKS first modulus size.
```

If you already have an older `results/benchmark_results.csv` from before the
current columns were added, remove it before the next run:

```bash
rm -f results/benchmark_results.csv
```

Saved output files use this shape:

```text
results/outputs/tiny_1k/
  plain_sum_amount_threads_1.txt
  plain_weighted_sum_amount_risk_threads_1.txt
  openfhe_ckks_sum_amount_threads_1.txt
  openfhe_ckks_sum_amount_threads_4.txt
  openfhe_ckks_sum_amount_threads_8.txt
  openfhe_ckks_weighted_sum_amount_risk_threads_1.txt
  openfhe_ckks_weighted_sum_amount_risk_threads_4.txt
  openfhe_ckks_weighted_sum_amount_risk_threads_8.txt
```
