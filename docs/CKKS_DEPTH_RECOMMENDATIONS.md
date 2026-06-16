# CKKS Depth Recommendations

This note is for planning `utility_bench` CKKS aggregation benchmarks.

## Confirmation

In OpenFHE CKKS, multiplicative depth is related to the ciphertext modulus chain.
OpenFHE parameters include:

```cpp
parameters.SetMultiplicativeDepth(...);
parameters.SetScalingModSize(...);
parameters.SetFirstModSize(...);
parameters.SetSecurityLevel(...);
parameters.SetRingDim(...);
parameters.SetBatchSize(...);
```

The first modulus and scaling modulus size are used to build the ciphertext
modulus chain:

```text
Q = q_0 * q_1 * ... * q_n * q'
```

where `q_0` uses `firstModSize`, and the other `q_i` use
`scalingModSize`. Higher multiplicative depth usually requires more modulus
levels. The total modulus size then affects the ring dimension needed to satisfy
the selected security level.

For this project, default to:

```text
security_level = 128-bit
scaling_mod_size = 50
first_mod_size = 60
ring_dimension = let OpenFHE choose first
batch_size = 0 / max slots first
```

When intentionally sweeping SIMD behavior, test a fixed ring dimension such as
`8192`, but only if OpenFHE accepts it for the chosen security/modulus settings.

## Important Rule

Addition and rotation do not consume multiplicative depth in the same way
ciphertext-ciphertext multiplication does. Multiplication followed by rescaling
is the main reason CKKS needs additional depth/modulus levels.

So depth should be chosen from the multiplication path, not from the number of
plain additions.

## Starting Depth Table

These are benchmark starting points, not final production guarantees.

| Task / workload | Example expression | Mult depth to start | Why |
| --- | --- | ---: | --- |
| Plain addition sanity check | `x1 + x2 + x3` | 1 | No ciphertext multiplication, but depth 1 is a practical safe default. |
| SUM aggregation | `SUM(x1)` using rotations/adds | 1 | Reductions use rotations and additions; no ciphertext multiplication. |
| AVG aggregation | `SUM(x1) / n` | 1 | Division by plaintext count can happen after decrypt, or as plaintext scalar multiply before/after aggregation. |
| Weighted linear score | `0.3*x1 + 0.2*x2 + 0.1*x3` | 1 | Ciphertext-plaintext multiplies by constants plus additions. |
| Masked SUM with plaintext mask | `SUM(amount * mask)` | 1 | If mask is plaintext, this is ciphertext-plaintext multiplication. |
| Masked SUM with encrypted mask | `SUM(amount * enc_mask)` | 2 | One ciphertext-ciphertext multiply for `amount * mask`, then reduction. |
| Dot product | `SUM(x1 * x2)` | 2 | One ciphertext-ciphertext multiply, then rotations/adds. |
| Polynomial feature degree 2 | `a*x^2 + b*x + c` | 2 | Needs one ciphertext square/multiply. |
| Polynomial feature degree 3 | `a*x^3 + b*x^2 + c*x` | 3 | Needs a multiplication chain up to cubic terms. |
| Group-by via plaintext masks | `SUM(amount * mask_k)` per group | 1 | One plaintext mask per group; repeated masked reductions. |
| Group-by via encrypted masks | `SUM(amount * enc_mask_k)` per group | 2 | One ciphertext-ciphertext multiply per group, plus reductions. |
| Rolling/window SUM | `SUM(amount[i-j])` for window | 1 | Rotation-heavy but multiplication-free. |
| Rolling/window weighted sum | `SUM(w_j * amount[i-j])` | 1 | Rotations plus ciphertext-plaintext multiplications. |
| Logistic/NN inference with polynomial activation degree 2 | linear layer + square activation | 2-3 | Linear layer is shallow; square activation consumes multiplicative depth. |
| Logistic/NN inference with polynomial activation degree 3 | linear layer + cubic activation | 3-4 | Cubic approximation consumes more depth. |
| Multi-layer MLP with polynomial activations | layer + activation repeated | 4+ | Depth grows with activation degree and number of sequential layers. Benchmark case-by-case. |

## First Benchmark Configs

Use these as initial presets.

| Config name | Security | Ring dim | First mod | Scaling mod | Depth | Intended benches |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `ckks_agg_depth1_auto` | 128 | auto | 60 | 50 | 1 | add, sum, avg, plaintext-mask group-by, rolling sum |
| `ckks_agg_depth2_auto` | 128 | auto | 60 | 50 | 2 | dot product, encrypted-mask sum, degree-2 features |
| `ckks_ml_depth4_auto` | 128 | auto | 60 | 50 | 4 | small polynomial ML inference |
| `ckks_agg_depth1_ring8192` | 128 | 8192 | 60 | 50 | 1 | SIMD/ring-size sweep if OpenFHE accepts it |
| `ckks_agg_depth2_ring8192` | 128 | 8192 | 60 | 50 | 2 | multiplication/ring-size sweep if OpenFHE accepts it |

## What To Record

Every CKKS result row should record:

```text
backend
bench
rows
threads
security_level
requested_ring_dimension
actual_ring_dimension
slots_per_ciphertext
ciphertext_count
used_slots_last_ciphertext
multiplicative_depth
first_mod_size
scaling_mod_size
rotation_count
encode_time_ms
encrypt_time_ms
he_eval_time_ms
decrypt_time_ms
decode_time_ms
total_he_time_ms
plain_time_ms
slowdown_vs_plain
max_error
mean_error
```

## Benchmark Design Notes

Use data sizes based on slots:

```text
0.25x slots
1x slots
4x slots
16x slots
```

This avoids misleading tiny tests where most SIMD slots are empty.

For production-like testing, prefer letting OpenFHE choose the ring dimension
from security level and modulus requirements. Use fixed ring dimensions only
for explicit sweeps.

