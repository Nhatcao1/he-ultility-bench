# Polynomial ML CKKS Benchmark

This benchmark tests a machine-learning-style nonlinear score over the existing
`transactions.csv` data. It is separate from the aggregation/query benchmarks
and the FedAvg merge benchmark.

## Goal

Stress CKKS depth, precision, SIMD packing, ciphertext-ciphertext
multiplication, rotations for final summation, and optional bootstrapping.

The benchmark computes:

```text
a = amount / 10000
u = x1 / 10000
v = x2 / 10000
w = x3 / 10000

z = 0.50*a + 0.30*u + 0.20*v - 0.10*w - 0.25
p(z) = 0.5 + 0.197*z - 0.004*z^3 + 0.00008*z^5 - 0.000001*z^7 + 0.00000001*z^9

result = SUM(p(z))
```

Lower-degree benchmarks stop the polynomial at degree 3 or degree 7.

## Benchmarks

| Benchmark | Degree | Bootstrap | Purpose |
| --- | ---: | --- | --- |
| `poly_score_degree3` | 3 | No | First nonlinear ML-style activation. |
| `poly_score_degree7` | 7 | No | Deeper ciphertext-ciphertext multiplication chain. |
| `poly_score_degree9` | 9 | No | Higher depth and precision stress without refresh. |
| `poly_score_degree9_bootstrap` | 9 | Yes | Measures CKKS bootstrap cost before degree-9 polynomial evaluation. |
| `all_poly` | mixed | mixed | Runs every polynomial benchmark. |

`poly_score_degree9_bootstrap` bootstraps the encrypted linear score `z` before
evaluating the polynomial. This is intentionally a bootstrap timing/accuracy
path, not an exact emulation of a production neural network.

## Timing Boundary

CSV loading is outside the benchmark timing.

Plain C++ baseline measures:

```text
normalize columns
linear score
polynomial activation
sum
```

OpenFHE rows measure:

```text
encode
encrypt
encrypted linear score
optional bootstrap
encrypted polynomial activation
EvalSum
decrypt
decode
```

OpenFHE setup, bootstrap setup, and bootstrap key generation are recorded
separately and excluded from `total_he_time_ms`.

## Build

```bash
cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE

cmake --build build --parallel $(nproc)
```

## Run

Degree 3 and 7 depth test:

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
```

Degree 9 without bootstrapping:

```bash
./build/poly_ml_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench poly_score_degree9 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 9 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/poly_ml_degree9_100k.csv
```

Degree 9 with CKKS bootstrapping:

```bash
./build/poly_ml_bench \
  --data data/generated/medium_100k/transactions.csv \
  --bench poly_score_degree9_bootstrap \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 9 \
  --ckks-scale-bits 59 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 10 \
  --bootstrap-level-budget 4 4 \
  --results results/poly_ml_degree9_bootstrap_100k.csv
```

For the first bootstrap smoke test, reduce rows or slots:

```bash
./build/poly_ml_bench \
  --data data/generated/medium_100k/transactions.csv \
  --max-rows 1000 \
  --bench poly_score_degree9_bootstrap \
  --backend all \
  --threads 1 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 1024 \
  --ckks-depth 9 \
  --ckks-scale-bits 59 \
  --ckks-first-mod-bits 60 \
  --bootstrap-levels-after 10 \
  --bootstrap-level-budget 4 4 \
  --results results/poly_ml_bootstrap_smoke.csv
```

## Metrics

```text
plain_time_ms
setup_time_ms
bootstrap_setup_time_ms
bootstrap_keygen_time_ms
encode_time_ms
encrypt_time_ms
he_eval_time_ms
bootstrap_time_ms
decrypt_time_ms
decode_time_ms
total_he_time_ms
absolute_error
relative_error
ciphertext_count
slots_per_ciphertext
slot_utilization
```

Watch `absolute_error` and `relative_error` closely as degree increases. That is
where CKKS precision pressure should become visible.
