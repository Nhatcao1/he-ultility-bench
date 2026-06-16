# HE Utility Bench

Aggregation benchmark scaffold for comparing plaintext `SUM(amount)` with an
OpenFHE CKKS packed `EvalSum` implementation.

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
  --ckks-ring-dim 8192 \
  --ckks-depth 1 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/benchmark_results.csv
```

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

The benchmark measures compute-only timing for:

```text
SELECT SUM(amount) FROM transactions;
```

CSV loading time is printed separately and is not included in the compute timing.
Output file writing is also excluded from compute timing.
Plain C++ is always measured once as a single-thread baseline. OpenFHE CKKS is
run once per requested thread count and compared back to that same single-thread
plain baseline.
For OpenFHE rows, `total_he_time_ms` includes encode, encrypt, homomorphic
evaluation, decrypt, and decode. OpenFHE context/key generation is recorded as
`setup_time_ms` but is kept separate from the online query timing.

CKKS uses 128-bit security in this first version. Tunable parameters:

```text
--ckks-ring-dim 0          0 lets OpenFHE choose; 8192 forces ring dimension.
--ckks-batch-size 0        0 uses all slots, normally ring_dim / 2.
--ckks-depth 1             Addition aggregation only needs shallow depth.
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
  openfhe_ckks_sum_amount_threads_1.txt
  openfhe_ckks_sum_amount_threads_4.txt
  openfhe_ckks_sum_amount_threads_8.txt
```
