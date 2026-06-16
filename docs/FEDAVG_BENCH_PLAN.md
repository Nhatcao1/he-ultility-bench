# FedAvg Merge Benchmark

This benchmark is separate from the SQL/query-style operator tests. It simulates
the server-side input immediately before a Federated Learning FedAvg merge.

## Goal

Measure the cost of merging encrypted client model updates:

```text
global = alpha_1 * client_1 + alpha_2 * client_2 + ... + alpha_n * client_n
```

This is a good CKKS workload because it uses:

```text
EvalMult(ciphertext, public scalar)
EvalAdd(ciphertext, ciphertext)
```

It does not require encrypted comparison, encrypted equality, or joins.

## Fixture Generator

Generate small JSON fixtures:

```bash
python3 scripts/generate_fedavg_fixtures.py
```

Validate fixture math without OpenFHE:

```bash
python3 scripts/test_fedavg_fixtures.py
```

Default output:

```text
data/generated_fedavg/
  tiny_mlp_11_c2/
    layout.json
    clients.json
    expected_global.json
    README.json
  mini_mlp_75_c4/
    layout.json
    clients.json
    expected_global.json
    README.json
```

The JSON files are the readable source fixtures. Ciphertext serialization is
measured inside the C++ benchmark after encryption.

## Fixtures

| Fixture | Parameters | Clients | Purpose |
| --- | ---: | ---: | --- |
| `tiny_mlp_11_c2` | 11 | 2 | Human-readable correctness. |
| `mini_mlp_75_c4` | 75 | 4 | Main debug fixture for flatten/chunk/merge/unflatten. |

Larger fixtures can be added later after the OpenFHE path is stable.

## Benchmark Modes

### Plain

```text
clients.json
-> read parameters_flat
-> FedAvg weighted flat vector merge
-> unflatten by layout
-> compare against expected_global.json
```

### OpenFHE CKKS

```text
clients.json
-> read parameters_flat
-> chunk by CKKS slots
-> encode chunks
-> encrypt chunks
-> serialize ciphertext chunks
-> deserialize ciphertext chunks
-> server weighted encrypted merge
-> decrypt global chunks
-> decode and remove padding
-> unflatten by layout
-> compare against expected_global.json
```

This first version assumes `num_examples` is public, so each `alpha_i` is public.

## Run

Build with OpenFHE:

```bash
cmake -S . -B build \
  -DUTILITY_BENCH_WITH_OPENFHE=ON \
  -DOpenFHE_DIR=$HOME/openfhe-install/lib/OpenFHE
cmake --build build --parallel $(nproc)
```

Run the mini fixture:

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

## Metrics

```text
flatten_time_ms
plain_aggregate_time_ms
encode_time_ms
encrypt_time_ms
serialize_time_ms
deserialize_time_ms
he_merge_time_ms
decrypt_time_ms
decode_time_ms
unflatten_time_ms
total_he_time_ms
mae_vs_expected
max_abs_error_vs_expected
plain_payload_bytes
serialized_ciphertext_bytes
```

## Notes

The benchmark decrypts the final aggregate for correctness checking. In a real
FL deployment, final decryption may be done by a key owner or via threshold
decryption rather than by the aggregation server.
