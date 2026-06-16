# OpenFHE Benchmark Plan

## Goal

Measure how much performance cost Homomorphic Encryption adds compared with normal plaintext computation, especially for workloads that look like data engineering, database, Spark, and simple ML inference.

The main question is:

> Is the privacy benefit of HE worth the extra time, memory, storage, and engineering complexity for our target use case?

This plan starts simple, then moves toward query-style workloads such as filter, aggregation, group-by, and joins.

## Core Benchmark Rule

Always separate the cost into two views.

| View | What it measures | Why it matters |
| --- | --- | --- |
| Operation-only slowdown | Plain operation time vs OpenFHE `Eval*` time | Shows the direct HE computation cost. |
| End-to-end slowdown | Plain operation time vs encode + encrypt + eval + decrypt + decode | Shows the real application cost. |

Basic formulas:

```text
operation_slowdown = he_eval_time / plain_compute_time

end_to_end_slowdown =
    (encode_time + encrypt_time + he_eval_time + decrypt_time + decode_time)
    / plain_compute_time
```

## Baselines

Use more than one baseline, but keep them clearly separated.

| Baseline | Purpose | Notes |
| --- | --- | --- |
| Plain C++ vector computation | Fairest low-level compute baseline | Best for comparing OpenFHE operation cost. |
| NumPy / pandas | Python data science baseline | Useful because preprocessing may happen in Python. |
| DuckDB | Single-machine SQL baseline | Good for scan, filter, aggregation, join, and group-by. |
| Spark | Distributed data baseline | Use later, after small local benchmarks are stable. |
| OpenFHE | Encrypted computation | Compare operation-only and end-to-end cost separately. |

Do not use only database read speed as the baseline. Reading Parquet, CSV, or database pages measures storage and I/O, not just computation.

## Data Strategy

The current `gencode.ipynb` creates a large synthetic table with text, category, timestamp, integer, float, and label columns.

For OpenFHE, prepare numeric benchmark extracts instead of feeding the whole Parquet dataset directly.

| Dataset | Rows | Purpose |
| --- | ---: | --- |
| `tiny_1k.csv` | 1,000 | Debug correctness quickly. |
| `small_10k.csv` | 10,000 | Early OpenFHE experiments. |
| `medium_100k.csv` | 100,000 | First serious timings. |
| `large_1m.csv` | 1,000,000 | Stress test after implementation is stable. |

For encrypted-key query primitives, use a separate tiny dataset. Fully encrypted
lookup/join/compare experiments are feasibility tests first, not throughput
tests.

| Dataset | Rows | Purpose |
| --- | ---: | --- |
| `generated_tiny_crypto_query/join_lookup_16` | 16 transactions, 16 customers | Encrypted equality, one-hot lookup, tiny encrypted join, and comparison masks. |

Generate it with:

```bash
python3 scripts/generate_tiny_crypto_query_data.py
```

Recommended columns:

| Column | Type | Usage |
| --- | --- | --- |
| `row_id` | integer | Needed for joins and result checking. |
| `customer_id` | integer | Join/group-by key. |
| `product_id` | integer | Encoded from product text. |
| `channel_id` | integer | Encoded from channel text. |
| `event_day` | integer | Timestamp converted to day offset. |
| `x1`, `x2`, `x3` | float | CKKS vector/math tests. |
| `i1`, `i2` | integer | BFV/BGV integer tests. |
| `amount` | float or scaled integer | Aggregation/query tests. |
| `label` | integer | ML correctness check. |

## File Format Plan

| Format | Use | Recommendation |
| --- | --- | --- |
| Parquet | Full synthetic source dataset | Keep for Spark/DuckDB tests. |
| CSV | Debuggable benchmark input | Use for small and medium tests. |
| NPY / NPZ | Fast Python numeric input | Good for NumPy and preprocessing. |
| Raw binary | Fast C++ input | Add later after schema is stable. |
| JSON | Benchmark configs | Use for parameters, not large data. |
| CSV results | Benchmark output | Easy to compare and plot. |

## Operations To Benchmark

### Phase 1: Aggregation Kernels

Start with aggregation because it maps naturally to database and Spark-style
workloads, and it shows CKKS SIMD packing behavior clearly.

| Operation | Plain baseline | OpenFHE operation | What it teaches |
| --- | --- | --- | --- |
| Sum | `SUM(amount)` | packed `EvalSum` | Addition, rotation, packing efficiency |
| Weighted sum | `SUM(amount * weight)` | `EvalMult` then `EvalSum` | First multiplication-heavy aggregate |
| Dot product | `SUM(x_i * w_i)` | `EvalInnerProduct` or `EvalMult` + `EvalSum` | ML inference core |
| Sum of products | `SUM(x1 * x2)` | ciphertext-ciphertext `EvalMult` + `EvalSum` | Multiplicative depth and rescale cost |
| Sum of squares | `SUM(x1 * x1)` | ciphertext square + `EvalSum` | Variance/stddev building block |
| Count via mask | `SUM(mask)` | packed `EvalSum(mask)` | Count/query predicate building block |
| Masked sum | `SUM(amount * mask)` | `EvalMult` + `EvalSum` | `WHERE` clause style aggregation |

Metrics:

```text
rows
slots_per_ciphertext
ciphertext_count
plain_time_ms
encode_time_ms
encrypt_time_ms
he_eval_time_ms
decrypt_time_ms
decode_time_ms
total_he_time_ms
operation_slowdown
end_to_end_slowdown
max_error
```

Multiplication is the next useful test after additive sum, but we should not
benchmark toy `x1 * x2` alone as the main story. The useful query form is:

```sql
SELECT SUM(amount * risk_weight)
FROM joined_or_lookup_data;
```

This gives us one multiplication per row plus aggregation, which is closer to
real analytics and simple ML inference.

### Phase 2: Query-Like Operations

These are closer to database and Spark workloads.

| SQL-style operation | Plain version | HE-friendly version | Difficulty |
| --- | --- | --- | --- |
| Projection | `SELECT x1, x2` | Select columns before encryption | Easy |
| Filter by known category | `WHERE channel_id = k` | Multiply values by precomputed mask | Easy if mask is plaintext/precomputed |
| Count with mask | `COUNT(*) WHERE channel_id = k` | `SUM(mask)` | Easy if mask exists |
| Sum with mask | `SUM(amount) WHERE channel_id = k` | `SUM(amount * mask)` | Medium |
| Average with mask | `SUM(amount * mask) / SUM(mask)` | encrypted numerator, count handling required | Medium |
| Lookup by public key | `amount * lookup_weight[key]` | Build plaintext weight vector, then multiply | Medium |
| Lookup by private key | Hidden key lookup | Requires equality tests or ORAM-like design | Hard |
| Range filter | `WHERE amount > t` | encrypted comparison or polynomial approximation | Hard |
| Group-by | `GROUP BY channel_id` | one mask per group, repeated aggregation | Medium to hard |
| Sort / order by | `ORDER BY amount` | Usually not HE-friendly | Very hard |
| Top-k | `ORDER BY amount LIMIT k` | Requires comparisons and selection | Very hard |

Important note:

```text
In HE, WHERE usually becomes a mask.

filtered_value[i] = value[i] * mask[i]
```

Start with plaintext or precomputed masks. Then benchmark encrypted predicate generation separately.

Encrypted range filters such as `WHERE amount > 5000` are now tracked as an
advanced benchmark because OpenFHE evaluates them through CKKS/FHEW
scheme-switching comparison rather than cheap CKKS arithmetic. The current
follow-up plan is in
[docs/ENCRYPTED_COMPARISON_NEXT_STEPS.md](docs/ENCRYPTED_COMPARISON_NEXT_STEPS.md).

### Phase 3: Lookup Operations

Lookups are common in feature engineering: customer risk weight, product price,
region coefficient, channel coefficient, and so on.

Benchmark lookup in levels.

| Lookup level | Description | Leakage / practicality | First benchmark? |
| --- | --- | --- | --- |
| Level 1: Plain lookup baseline | C++/DuckDB maps `customer_id -> risk_weight` | Plaintext baseline | Yes |
| Level 2: Pre-expanded plaintext lookup | Create `risk_weight` vector before encryption | Leaks lookup result during preprocessing | Yes |
| Level 3: Public-key lookup | `customer_id` visible, encrypted value column | Leaks keys/access pattern, practical | Yes |
| Level 4: Encrypted-key lookup | Keys hidden and matched under HE | Expensive/research-style | No |

Recommended first lookup benchmark:

```sql
SELECT SUM(t.amount * c.risk_weight)
FROM transactions t
JOIN customers c
    ON t.customer_id = c.customer_id;
```

HE-friendly first version:

```text
1. Plain preprocessing creates aligned vectors:
   amount[i], risk_weight_for_customer[i]
2. Encrypt amount and optionally risk_weight.
3. Benchmark SUM(amount * risk_weight).
```

This makes lookup cost and HE multiplication cost visible separately.

### Phase 4: Join-Like Operations

Joins are common in databases and Spark, but they are difficult under HE because they often reveal access patterns or require many equality checks.

Benchmark joins in levels.

| Join level | Description | Purpose |
| --- | --- | --- |
| Level 1: Plain join baseline | DuckDB/Spark joins on plaintext data | Understand normal cost. |
| Level 2: Pre-joined encrypted input | Join before encryption, then run HE compute | Isolate HE compute after data preparation. |
| Level 3: Join via plaintext join keys | Join keys remain visible, values encrypted | Practical but leaks join keys/access pattern. |
| Level 4: Encrypted equality join | Compare encrypted keys under HE | Very expensive; research-style benchmark. |

Recommended first join benchmark:

```sql
SELECT
    t1.customer_id,
    SUM(t1.amount * t2.risk_weight)
FROM transactions t1
JOIN customers t2
    ON t1.customer_id = t2.customer_id
GROUP BY t1.customer_id;
```

HE-friendly first version:

```text
Do the join in plaintext.
Encrypt the joined numeric vectors.
Benchmark encrypted weighted sum / aggregation.
```

This separates the HE cost from the database join cost.

Join benchmarks should report two costs separately:

```text
join_preprocess_time_ms
he_compute_time_ms
```

For the first code version, keep join preprocessing outside HE and focus on
the encrypted post-join aggregate. Later, compare against DuckDB/Spark join
baselines so we know the normal database cost too.

## HE Operator Criteria For ML-Like Workloads

Do not start with a full ML framework or model-training pipeline. For OpenFHE,
start with the encrypted operators that ML inference eventually needs.

The first ML-related benchmark should be a linear weighted sum:

```text
score =
    0.30 * x1
  + 0.20 * x2
  + 0.10 * x3
  + i1 / 50000
  + i2 / 50000
  + bias
```

This matches the logic used in `gencode.ipynb` to create the synthetic label.
It is also the core operator behind linear regression, logistic-regression
score computation, linear SVM, and the linear layers of small neural networks.

Important rule:

```text
Benchmark encrypted score computation first.
Do not benchmark encrypted classification threshold yet.
```

The threshold/classification step needs comparison, which is the same expensive
class of operation as encrypted `WHERE amount > 5000`.

| Version | Description |
| --- | --- |
| Plain C++ | Fast compute baseline. |
| NumPy | Data science baseline. |
| CKKS encrypted features + plaintext weights | Practical HE inference benchmark. |
| CKKS encrypted features + encrypted weights | More private, more expensive. |

### Operator Priority

| Priority | Benchmark | OpenFHE operation shape | ML meaning |
| ---: | --- | --- | --- |
| 1 | `linear_score_plain_weights` | `EvalMult(ct, plaintext weight)` + `EvalAdd` | Batched linear model over rows |
| 2 | `inner_product_plain_weights` | `EvalInnerProduct(ct, plaintext weights)` or `EvalMult` + `EvalSum` | Dot product / linear score for feature vectors |
| 3 | `linear_score_encrypted_weights` | `EvalMult(ct_feature, ct_weight)` + `EvalAdd` | Private features and private weights |
| 4 | `polynomial_activation` | `EvalPoly` / `EvalChebyshevFunction` | Approximate sigmoid/tanh/ReLU-like activation |
| 5 | `tiny_linear_layer` | rotations + weighted sums | Small neural-network linear layer |

Start with `linear_score_plain_weights`. It is close to real inference but does
not drag in encrypted comparison, argmax, or branching.

### OpenFHE APIs To Reuse

| OpenFHE API / example | Use in this project |
| --- | --- |
| `EvalLinearWSum` / `linearwsum-evaluation.cpp` | Weighted sum over encrypted values with public weights. |
| `EvalInnerProduct` / `inner-product.cpp` | Dot product style benchmark. |
| `EvalMult(ct, plaintext)` | Public model weights, encrypted features. |
| `EvalMult(ct1, ct2)` | Private model weights or product features. |
| `EvalSum` | Reduce packed slots after multiply. |
| `EvalRotate` / `rotation.cpp` | Packing/layout experiments for matrix-vector style workloads. |
| `EvalPoly` / `polynomial-evaluation.cpp` | Polynomial activations. |
| `EvalChebyshevFunction` / `function-evaluation.cpp` | Smooth function approximations such as logistic-like activation. |

### Recommended Helper Layer

Keep the helper thin. It should wrap OpenFHE operator patterns, not become an
ML framework.

| Helper | Responsibility |
| --- | --- |
| `CkksContextFactory` | Build CKKS context from security, depth, ring, scale, and batch settings. |
| `CkksPackedVector` | Pack numeric columns into plaintext/ciphertext chunks. |
| `CkksLinearOps` | Linear weighted sum, dot product, and batched score operators. |
| `CkksPolynomialOps` | Polynomial and Chebyshev function evaluation. |
| `CkksLayoutPlanner` | Record slots, chunks, rotations, and packing strategy. |
| `CkksMetrics` | Timing, accuracy, slot utilization, and parameter reporting. |

### Metrics

```text
prediction_time
HE slowdown
numerical_error
classification_accuracy_difference
ciphertext_size
memory_usage
```

For the first implementation, report the same timing columns as the aggregation
benchmarks:

```text
plain_time_ms
encode_time_ms
encrypt_time_ms
he_eval_time_ms
decrypt_time_ms
decode_time_ms
total_he_time_ms
operation_slowdown
end_to_end_slowdown
absolute_error
relative_error
```

Accuracy should compare the decrypted HE score against the plain C++ score.
Classification accuracy can be computed after decrypt for reporting, but it is
not part of the encrypted benchmark until we intentionally revisit encrypted
comparison.

## OpenFHE Scheme Plan

| Scheme | Use for | Avoid starting with |
| --- | --- | --- |
| CKKS | Floats, approximate math, ML inference, weighted sums | Exact category equality and exact counts |
| BFV | Exact integers, counts, masks, category IDs | Floating-point ML |
| BGV | Exact integers, counts, masks, category IDs | Floating-point ML |
| FHEW / TFHE | Boolean logic, comparisons, threshold-like predicates | Large vector analytics as the first test |

Practical first choice:

```text
Use CKKS for vector math and ML inference.
Use BFV/BGV later for exact count/mask experiments.
Use FHEW/TFHE later for encrypted WHERE predicates and comparisons.
```

## Repository And Server Layout

Keep the benchmark repo separate from OpenFHE source, build, and install outputs.

Local machine layout:

```text
Viettel/
  utility_bench/                 # this benchmark repo
  openfhe-development/           # optional OpenFHE source clone for reading/reference
  openfhe-development-static-docs/
```

Remote server layout:

```text
~/projects/
  utility_bench/                 # git clone of this benchmark repo

~/libs/
  openfhe-development/           # git clone of OpenFHE source
  openfhe-build/                 # CMake build directory
  openfhe-install/               # local install prefix used by utility_bench
```

Do not commit OpenFHE source, build files, generated datasets, or benchmark results into `utility_bench`.

Recommended flow:

```text
1. Develop benchmark code in utility_bench.
2. Push utility_bench to GitHub.
3. Pull utility_bench on the remote server.
4. Clone and build OpenFHE separately on the remote server.
5. Point utility_bench CMake to the remote OpenFHE install path.
```

Example CMake configuration on the server:

```bash
cmake -S . -B build \
  -DOpenFHE_DIR=$HOME/libs/openfhe-install/lib/OpenFHE
```

OpenFHE build and install directories are machine-specific. They should be created on the server where benchmarks will actually run.

## Project Structure

Suggested benchmark folder:

```text
he-benchmark/
  CMakeLists.txt

  configs/
    ckks_sum_amount_depth1_auto.json
    ckks_sum_amount_depth1_ring8192.json
    ckks_sum_amount_depth2_auto.json

  data/
    generated/
    convert_parquet_to_numeric.py
    tiny_1k.csv
    small_10k.csv
    medium_100k.csv
    large_1m.csv

  src/
    main.cpp
    benchmark_timer.h
    data_loader.h
    result_writer.h
    plaintext_baseline.cpp
    duckdb_baseline.cpp
    openfhe_context.cpp
    openfhe_vector_ops.cpp
    openfhe_weighted_sum.cpp
    openfhe_masked_query.cpp
    openfhe_linear_inference.cpp

  scripts/
    generate_benchmark_data.py
    run_all.sh
    summarize_results.py
    plot_results.py

  results/
    benchmark_results.csv
```

## Wrapper Code To Prepare

| Wrapper | Responsibility |
| --- | --- |
| `BenchmarkTimer` | Consistent timing for every stage. |
| `DataLoader` | Load CSV/NPY/binary numeric data. |
| `PlaintextBaseline` | Plain vector math, filters, aggregations. |
| `DuckDBBaseline` | SQL-style local baseline for scan/filter/join/group-by. |
| `OpenFHEContextFactory` | Create CKKS/BFV/BGV contexts from config. |
| `OpenFHEEncoder` | Pack vectors into plaintexts. |
| `OpenFHEEncryptor` | Encrypt/decrypt data and track time. |
| `OpenFHEOps` | EvalAdd, EvalMult, rotations, reductions. |
| `ResultWriter` | Write benchmark rows to CSV. |
| `CorrectnessChecker` | Compare plaintext and decrypted HE results. |

## Result Table Schema

Save every benchmark run to one CSV file.

```text
run_id
date
machine
operation
backend
rows
columns
slots_per_ciphertext
ciphertext_count
multiplicative_depth
scale_mod_size
batch_size
threads
plain_time_ms
encode_time_ms
encrypt_time_ms
he_eval_time_ms
decrypt_time_ms
decode_time_ms
total_he_time_ms
operation_slowdown
end_to_end_slowdown
max_error
mean_error
memory_mb
input_size_mb
ciphertext_size_mb
notes
```

## Recommended Execution Order

| Step | Task | Success condition |
| ---: | --- | --- |
| 1 | Generate `tiny_1k.csv` numeric data | Can inspect the file manually. |
| 2 | Implement plain C++ `SUM(amount)` baseline | Single-thread result and time recorded. |
| 3 | Implement CKKS `SUM(amount)` | Decrypted sum close to plaintext result. |
| 4 | Add timing breakdown | Encode/encrypt/eval/decrypt/decode reported separately. |
| 5 | Implement CKKS weighted sum `SUM(amount * weight)` | Multiplication plus aggregation works. |
| 6 | Add lookup-expanded weighted sum | `customer_id -> risk_weight` vector prepared and tested. |
| 7 | Add DuckDB baseline for simple SQL queries | Plain SQL timings recorded. |
| 8 | Implement masked sum with precomputed mask | HE aggregation works for `WHERE channel_id = k`. |
| 9 | Add group-by via repeated masks | One encrypted aggregation per category. |
| 10 | Add join benchmarks | Start with pre-joined data, then visible-key join. |
| 11 | Explore encrypted comparisons | Treat as advanced/research benchmark. |

## First Concrete Experiments

### Experiment 1: Aggregation Sum

```sql
SELECT SUM(amount)
FROM transactions;
```

Compare:

```text
Plain C++ single-thread baseline
OpenFHE CKKS EvalSum operation-only
OpenFHE CKKS encode + encrypt + EvalSum + decrypt + decode
```

### Experiment 2: Weighted Aggregation / Multiplication

```sql
SELECT SUM(amount * risk_weight)
FROM prepared_transactions;
```

Compare:

```text
Plain C++ single-thread baseline
OpenFHE CKKS ciphertext-plaintext multiplication + EvalSum
OpenFHE CKKS ciphertext-ciphertext multiplication + EvalSum
```

This is the first serious multiplication benchmark. It also becomes the core
building block for lookup, join, and ML scoring.

### Experiment 3: Lookup-Expanded Weighted Sum

```sql
SELECT SUM(t.amount * c.risk_weight)
FROM transactions t
JOIN customers c
    ON t.customer_id = c.customer_id;
```

First HE version:

```text
Plain preprocessing:
  risk_weight_for_row[i] = customers[customer_id[i]].risk_weight

HE compute:
  SUM(amount[i] * risk_weight_for_row[i])
```

Compare:

```text
Plain C++ lookup + sum
DuckDB join + sum
OpenFHE CKKS post-lookup weighted sum
```

### Experiment 4: Query With WHERE Mask

```sql
SELECT SUM(amount)
FROM data
WHERE channel_id = 5;
```

HE version:

```text
mask[i] = 1 if channel_id[i] == 5 else 0
result = SUM(amount[i] * mask[i])
```

Start with plaintext/precomputed mask. Later test encrypted mask generation.

### Experiment 5: Join Benchmark

```sql
SELECT
    t.customer_id,
    SUM(t.amount * c.risk_weight)
FROM transactions t
JOIN customers c
    ON t.customer_id = c.customer_id
GROUP BY t.customer_id;
```

Start with:

```text
1. DuckDB/C++ plaintext join baseline.
2. Pre-joined encrypted numeric vectors.
3. Visible-key join with encrypted values.
4. Encrypted-key equality join only if the earlier levels justify it.
```

### Experiment 6: Tiny Encrypted-Key Lookup / Join

Use `scripts/generate_tiny_crypto_query_data.py` for this, not the normal
transaction generator.

Start with tiny data only:

```text
16 transactions
16 customer keys
```

Test two representations:

| Representation | What it tests |
| --- | --- |
| Scalar encrypted key | Direct encrypted equality feasibility. |
| One-hot encrypted key | HE-friendly lookup as a dot product / masked selection. |

Possible query primitives:

```text
encrypted_eq_customer_id
encrypted_lookup_onehot_risk_weight
encrypted_join_tiny_equality_mask
encrypted_join_tiny_onehot
encrypted_compare_amount_gt_threshold
```

This is deliberately separate from the 1k/10k/100k performance datasets because
naive fully encrypted joins can scale as:

```text
transactions_rows * customers_rows
```

The first goal is correctness and usability, not speed.

## What To Avoid At The Start

| Avoid first | Reason |
| --- | --- |
| Full 57M row dataset | Too large before correctness and timing are stable. |
| Full SQL compatibility | HE is not a drop-in replacement for SQL engines. |
| Encrypted joins as the first query benchmark | Expensive and conceptually tricky. |
| Encrypted sorting/top-k | Requires many comparisons and selection steps. |
| ML training under HE | Much harder than inference. |
| Mixing I/O and compute measurements too early | Makes slowdown numbers hard to explain. |

## Decision Framework

After each benchmark, classify the use case.

| Result | Interpretation |
| --- | --- |
| HE is less than 10x slower | Very promising. |
| HE is 10x to 100x slower | Possibly acceptable for high-value privacy use cases. |
| HE is 100x to 1000x slower | Only acceptable if privacy value is very high or workload is offline. |
| HE is more than 1000x slower | Reconsider design, reduce operation complexity, or use hybrid privacy methods. |

Also consider:

```text
Can preprocessing happen before encryption?
Can query predicates be simplified?
Can keys/categories stay visible?
Can masks be precomputed?
Can batching reduce ciphertext count?
Can we tolerate approximate answers?
Can the workload run offline instead of interactively?
```

## Short-Term Next Step

Create the first benchmark implementation for:

```text
CKKS SUM(amount)
plain C++ single-thread baseline
CSV result logging
```

Then expand to:

```text
CKKS SUM(amount * risk_weight)
lookup-expanded weighted sum
masked SUM WHERE category = k
DuckDB baseline
group-by using repeated masks
join baseline with pre-joined encrypted data
```
