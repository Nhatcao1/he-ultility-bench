# Benchmark Timing Columns

Use these meanings for every benchmark family before comparing optimization
attempts.

| Column | Meaning |
| --- | --- |
| `plain_time_ms` | Plain C++ calculation only. CSV/data loading is outside this value. |
| `setup_time_ms` | OpenFHE context/key setup. Reported separately and excluded from calculation speed. |
| `encode_time_ms` | CKKS plaintext packing/encoding. |
| `encrypt_time_ms` | CKKS encryption before computation. |
| `he_eval_time_ms` | Real encrypted calculation time for normal CKKS benches. This is the main primitive-compute comparison column. |
| `bootstrap_time_ms` | Bootstrap calculation time for polynomial bootstrap benches. Add this to `he_eval_time_ms` for real calculation time. |
| `he_merge_time_ms` | Real encrypted calculation time for FedAvg. This is FedAvg's equivalent of `he_eval_time_ms`. |
| `decrypt_time_ms` | CKKS decryption after computation. |
| `decode_time_ms` | Decode/unpack plaintext result after decryption. |
| `total_he_time_ms` | Lifecycle timing around encrypted computation: encode + encrypt + encrypted calculation + decrypt + decode. Setup/keygen is separate. |
| `peak_rss_kb` | Process peak resident memory reported by the OS at result-write time. |
| `ciphertext_payload_bytes` | Ciphertext payload size in bytes. CKKS rows use a lightweight ciphertext-size estimate unless the benchmark explicitly serializes ciphertexts. |
| `ciphertext_payload_kb` | Same ciphertext payload size in KiB. |
| `serialized_ciphertext_bytes` | FedAvg-specific actual serialized ciphertext payload. |
| `serialized_ciphertext_kb` | FedAvg-specific actual serialized ciphertext payload in KiB. |

For comparison to C++ baseline, prefer:

```text
real calculation slowdown:
  he_eval_time_ms / plain_time_ms

polynomial with bootstrap:
  (he_eval_time_ms + bootstrap_time_ms) / plain_time_ms

FedAvg:
  he_merge_time_ms / plain_aggregate_time_ms

overall lifecycle slowdown:
  total_he_time_ms / plain_time_ms
```

All repeat-capable runners now write individual repeat rows and a `_summary_avg`
row. Use the summary row for plots and reports.

Payload notes:

```text
CKKS benches:
  ciphertext_payload_bytes is estimated from ciphertext count,
  ring dimension, and modulus-chain size.

BinFHE LUT:
  ciphertext_payload_bytes is estimated from LWE ciphertext count,
  LWE dimension, and LWE modulus bits.

FedAvg:
  serialized_ciphertext_bytes is actual serialized ciphertext bytes because
  that benchmark already serializes ciphertexts as part of its workflow.
```
