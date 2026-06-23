# OpenFHE Optimization Plan

This note turns the external performance report into actions we can actually
test in `utility_bench`. The report contains useful hints, but most of the
claimed speed comes from undisclosed implementation details, so we should treat
it as a checklist for better experiments rather than a recipe.

## Non-Negotiable Rule

Optimization must happen under encryption. Do not add or keep benchmark
variants that aggregate, filter, join, mask, or otherwise compute the target
answer before encryption. Plain C++ is only the baseline, not an optimized HE
path.

## What Is Worth Doing Now

| Item | Why it matters | How we use it |
| --- | --- | --- |
| SIMD/batching visibility | HE speed claims often count many slot values inside one ciphertext. Without slot data, "ops/sec" is ambiguous. | Always report batch size, slots per ciphertext, ciphertext count, and slot utilization. Compare tiny, 100k, and 1m inputs separately. |
| Separate setup from online work | Context setup, key generation, eval-key generation, encoding, and encryption can dominate end-to-end time. | Keep `setup_time_ms`, `encode_time_ms`, `encrypt_time_ms`, `he_eval_time_ms`, `decrypt_time_ms`, and `decode_time_ms` separate. Report both eval-only and total HE slowdown. |
| Thread sweep | The report used single-thread mode, which is useful for fairness but not a real speed optimization. | Keep running `--threads 1 4 8`. Treat the 1-thread result as the clean baseline and multi-thread results as scaling behavior. |
| Parameter sweep | Ring dimension, depth, modulus chain, and scale bits decide the speed/accuracy/security tradeoff. | For serious benches, sweep `ckks-depth`, `ckks-scale-bits`, `ckks-first-mod-bits`, and `ckks-batch-size`, while recording actual ring dimension chosen by OpenFHE. |
| Build/runtime configuration | `-O3`, native CPU flags, OpenMP, and OpenFHE build options can change timings a lot. | Add a benchmark metadata note for compiler flags and OpenFHE build options when running on the server. Do not compare results from unknown builds as if they are equal. |
| Precompute and reuse constants | HE code can look much faster if plaintext constants, eval keys, and encoded fixed values are reused. | Only precompute values that do not contain row-level target results. Never precompute the aggregation, filter result, lookup result, or mask that the HE benchmark is supposed to compute. |
| Ciphertext size | Lower ciphertext expansion can improve memory, serialization, and network latency. | Measure serialized ciphertext bytes for benches where transfer/storage matters, especially FedAvg and future client/server tests. |
| Bounded input range | Smaller numeric domains can allow cheaper approximation or fixed-point choices. | Record input scaling and value range for comparison, trig, polynomial, and dense-layer benches. |

## Worth Doing Later

| Item | Why later |
| --- | --- |
| Raw binary / NPY input for C++ | Useful for removing CSV parsing overhead, but current timing already excludes loading from the main compute numbers. |
| Online-only warm mode | Useful when comparing against vendor-style online throughput. Add only after the standard end-to-end columns stay stable. |
| Serialization/network benchmark | Important for deployment, but separate from core HE arithmetic timing. |
| OpenFHE vs SEAL vs other libraries | Good comparison later, but only if we match scheme, security, ring dimension, depth, and batching. |
| GPU/FPGA/ASIC acceleration | Do not plan around it until hardware and library support are real in our environment. |
| Fixed-point specialization | Useful for comparison and function evaluation, but it can change accuracy and input limits. Treat as a separate benchmark variant. |

## Not Worth Chasing From The Report

| Claim | Why we should not chase it directly |
| --- | --- |
| Proprietary "homomorphic construction algorithm" | No scheme, equations, parameters, or implementation details are disclosed. |
| Integrated appliance speed | CPU/memory are listed, but no accelerator or software mechanism is explained. |
| Same security level | The numeric security level, ring dimension, and modulus chain are missing. |
| Native API list | Exposing `sum`, `mean`, `sin`, `cos`, or `tan` as APIs does not prove those operations are cheaper internally. |
| Huge operations-per-second number | Without values-per-ciphertext and setup/precompute rules, the number cannot be mapped to OpenFHE timings. |

## Benchmark Metadata We Should Preserve

Every serious result row should either include these columns or point to a
metadata file with them:

| Category | Fields |
| --- | --- |
| Workload | benchmark name, rows, selected columns, input value range, input scaling |
| Plain baseline | baseline type, baseline thread count, plain compute time |
| HE parameters | scheme, security bits, requested ring dimension, actual ring dimension, depth, scale bits, first modulus bits, batch size |
| Packing | slots per ciphertext, ciphertext count, slot utilization, padding slots |
| Timing | setup, encode, encrypt, eval, decrypt, decode, total HE time |
| Accuracy | baseline value, HE value, absolute error, relative error |
| Runtime | OpenFHE thread count, compiler/build flags when known, whether OpenMP is enabled |
| Reuse policy | whether keys, eval keys, plaintext constants, masks, or encoded vectors were reused |
| Payload | serialized ciphertext bytes when transfer/storage is part of the question |

## Immediate Priority Order

1. Keep correctness first. A fast wrong HE result is not a benchmark.
2. Compare operation-only and end-to-end slowdown for each bench.
3. Prefer 100k and 1m data for SIMD-heavy throughput claims.
4. Use tiny inputs only for feasibility and debugging.
5. Record actual OpenFHE parameters, not only requested CLI values.
6. Add build metadata before making serious cross-machine comparisons.
7. Measure ciphertext size only when communication or storage matters.

## How This Changes Our Existing Plan

The next optimization work should not be "make every operation faster" in a
vague way. It should be:

```text
For each benchmark:
  1. Confirm correctness on tiny/small data.
  2. Run 100k single-thread as the clean reference.
  3. Run 100k with 1/4/8 threads.
  4. Run 1m only after correctness is stable.
  5. Compare batch size and actual ring dimension.
  6. Record whether setup/precompute is included.
```

This keeps us honest against reports that hide setup, packing, parameters, or
hardware details.
