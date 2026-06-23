# Benchmark Test Summary

This file summarizes the benchmarks in `utility_bench`: what math each test
does, what the plain C++ baseline does, and what happens under OpenFHE.

## `sum_amount`

Math:

```text
result = sum(amount_i)
```

Plain baseline:

```text
for each row:
  total += amount_i
```

HE behind the scenes:

```text
amount column
  -> pack amount values into CKKS SIMD slots
  -> encrypt each packed chunk
  -> EvalSum slots inside each ciphertext
  -> EvalAdd chunk totals together
  -> decrypt one scalar total
```

Main HE operations: packed encoding, encryption, `EvalSum`, `EvalAdd`, decrypt.

## `linear_score_vector`

Math:

```text
score_i = 0.00030*x1_i
        - 0.00020*x2_i
        + 0.00015*x3_i
        + 0.00005*amount_i
        + 0.10000
```

Plain baseline:

```text
for each row:
  checksum += score_i
```

HE behind the scenes:

```text
x1, x2, x3, amount
  -> pack each feature column into CKKS SIMD slots
  -> encrypt each feature chunk

weights and bias
  -> encrypt repeated packed vectors

encrypted score vector
  -> EvalMult encrypted feature by encrypted weight
  -> EvalAdd all terms
  -> EvalAdd encrypted bias
  -> decrypt score vector for checksum accuracy
```

Main HE operations: ciphertext-ciphertext multiply and ciphertext addition.
No rotations and no final HE aggregation.

## `binfhe_lut_channel_risk_code`

Math:

```text
risk_code_i = channel_risk_table[channel_id_i]
```

Plain baseline:

```text
for each row:
  checksum += channel_risk_table[channel_id_i]
```

HE behind the scenes:

```text
channel_id
  -> encrypt as a BinFHE/LWE scalar ciphertext
  -> EvalFunc with arbitrary channel-risk LUT
  -> encrypted channel risk code
  -> decrypt only for checksum accuracy
```

Main HE operation: BinFHE/FHEW programmable bootstrapping via `EvalFunc`.
No one-hot encoding, no CKKS arithmetic lookup, and no weighted sum.

## `weighted_sum_amount_risk`

Math:

```text
result = sum(amount_i * risk_weight_i)
```

Plain baseline:

```text
for each row:
  total += amount_i * risk_weight_i
```

HE behind the scenes:

```text
amount column
  -> encrypt packed CKKS amount chunks

risk_weight column
  -> encrypt packed CKKS risk_weight chunks

encrypted amount * encrypted risk_weight
  -> EvalMult(cipher_amount, cipher_weight)
  -> EvalSum weighted values
  -> EvalAdd chunk totals
  -> decrypt one scalar total
```

Main HE operations: ciphertext-ciphertext multiply, rescale/noise growth,
`EvalSum`, `EvalAdd`.

## `weighted_sum_amount_risk_encrypted`

Compatibility alias for `weighted_sum_amount_risk`. Both names now use the
all-encrypted path.

Math:

```text
result = sum(amount_i * risk_weight_i)
```

Plain baseline:

```text
same math as weighted_sum_amount_risk
```

HE behind the scenes:

```text
amount column
  -> pack and encrypt

risk_weight column
  -> pack and encrypt

encrypted amount * encrypted risk_weight
  -> EvalMult(cipher_amount, cipher_weight)
  -> EvalSum weighted values
  -> EvalAdd chunk totals
  -> decrypt one scalar total
```

Main HE operations: ciphertext-ciphertext multiply, rescale/noise growth,
`EvalSum`, `EvalAdd`.

## `compare_amount_gt_5000`

Math:

```text
result = count(amount_i > 5000)
```

Plain baseline:

```text
for each row:
  total += amount_i > 5000 ? 1 : 0
```

HE behind the scenes:

```text
amount column
  -> pack and encrypt

threshold 5000
  -> pack repeated 5000 values
  -> encrypt once and reuse

encrypted threshold vs encrypted amount
  -> CKKS/FHEW scheme-switching comparison
  -> encrypted 0/1 comparison mask
  -> decrypt mask
  -> count decoded 1 values for correctness report
```

Main HE operations: CKKS/FHEW scheme switching comparison. This benchmark
intentionally does no selected-amount multiplication and no `EvalSum`.

## `rolling_avg_amount_w3`

Current implementation: Schema B, scalar summary.

Math:

```text
rolling_i = (amount_i + amount_{i+1} + amount_{i+2}) / 3
result = mean(rolling_i)
```

Plain baseline:

```text
for each valid row i:
  rolling_i = (amount_i + amount_{i+1} + amount_{i+2}) / 3
result = average of all rolling_i
```

HE behind the scenes:

```text
amount column
  -> pack with overlap for chunk boundaries
  -> encrypt

encrypted amount vector
  -> EvalRotate by 1
  -> EvalRotate by 2
  -> EvalAdd original + rotated vectors
  -> EvalMult by plaintext mask 1/3
  -> ModReduceInPlace after mask multiplication
  -> encrypted rolling average vector
  -> EvalSum rolling outputs
  -> decrypt scalar checksum
  -> divide by number of rolling outputs
```

Main HE operations: rotations, additions, plaintext mask multiply, `EvalSum`.

Schema A diagnostic benchmark: `rolling_avg_vector_w3`.

```text
amount column
  -> pack with overlap
  -> encrypt
  -> EvalRotate by 1 and 2
  -> EvalAdd original + rotated vectors
  -> EvalMult by plaintext 1/3 mask
  -> ModReduceInPlace after mask multiplication
  -> decrypt rolling-average vector directly
  -> compare each rolling_i against C++ rolling_i
```

Schema A removes `EvalSum` and scalar reporting. Use it to verify that rolling
average itself is correct before trusting the scalar summary.

## `rolling_avg_amount_w5`

Current implementation: Schema B, scalar summary.

Math:

```text
rolling_i = (amount_i + ... + amount_{i+4}) / 5
result = mean(rolling_i)
```

HE behind the scenes:

```text
encrypted amount
  -> EvalRotate by 1, 2, 3, 4
  -> add all shifted vectors
  -> multiply by plaintext 1/5 mask
  -> ModReduceInPlace after mask multiplication
  -> EvalSum rolling outputs
  -> decrypt scalar result
```

Compared with `w3`, this uses more rotations.

Schema A diagnostic benchmark: `rolling_avg_vector_w5`. It uses the same
rotations and plaintext `1/5` mask, then decrypts the rolling-average vector
directly without `EvalSum`.

## `rolling_avg_amount_w9`

Current implementation: Schema B, scalar summary.

Math:

```text
rolling_i = (amount_i + ... + amount_{i+8}) / 9
result = mean(rolling_i)
```

HE behind the scenes:

```text
encrypted amount
  -> EvalRotate by 1, 2, 3, 4, 5, 6, 7, 8
  -> add all shifted vectors
  -> multiply by plaintext 1/9 mask
  -> ModReduceInPlace after mask multiplication
  -> EvalSum rolling outputs
  -> decrypt scalar result
```

This is the heaviest rolling-average variant because it needs 8 rotations per
chunk.

Schema A diagnostic benchmark: `rolling_avg_vector_w9`. It uses the same
rotations and plaintext `1/9` mask, then decrypts the rolling-average vector
directly without `EvalSum`.

## Polynomial ML-Style Benchmarks

These are run by `poly_ml_bench`, not the main `utility_bench` binary.

Shared linear score:

```text
z_i = 0.50 * amount_i/10000
    + 0.30 * x1_i/10000
    + 0.20 * x2_i/10000
    - 0.10 * x3_i/10000
    - 0.25
```

Shared HE setup:

```text
amount, x1, x2, x3
  -> normalize
  -> pack each column into CKKS slots
  -> encrypt each column
  -> compute encrypted z_i with scalar EvalMult and EvalAdd
```

### `poly_score_degree3`

Math:

```text
p3(z) = 0.5 + 0.197*z - 0.004*z^3
result = sum(p3(z_i))
```

HE behind the scenes:

```text
encrypted z
  -> EvalMult z*z
  -> EvalMult z^2*z = z^3
  -> multiply powers by plaintext coefficients
  -> EvalAdd polynomial terms
  -> EvalSum all rows
  -> decrypt scalar result
```

### `poly_score_degree7`

Math:

```text
p7(z) = p3(z) + 0.00008*z^5 - 0.000001*z^7
result = sum(p7(z_i))
```

HE behind the scenes:

```text
encrypted z
  -> build z^3, z^5, z^7 by repeated EvalMult
  -> multiply powers by plaintext coefficients
  -> add polynomial terms
  -> EvalSum
  -> decrypt scalar result
```

This stresses multiplicative depth more than degree 3.

### `poly_score_degree9`

Math:

```text
p9(z) = p7(z) + 0.00000001*z^9
result = sum(p9(z_i))
```

HE behind the scenes:

```text
encrypted z
  -> build z^3, z^5, z^7, z^9
  -> multiply powers by plaintext coefficients
  -> add polynomial terms
  -> EvalSum
  -> decrypt scalar result
```

This is the deepest non-bootstrap polynomial test.

### `poly_score_degree7_bootstrap`

Math:

```text
same p7(z) as poly_score_degree7
```

HE behind the scenes:

```text
encrypted z
  -> EvalBootstrap(z)
  -> then compute degree-7 polynomial
  -> EvalSum
  -> decrypt scalar result
```

This is the preferred first CKKS bootstrap benchmark because it is lighter than
degree 9 while still exercising bootstrap plus nonlinear polynomial evaluation.

### `poly_score_degree9_bootstrap`

Math:

```text
same p9(z) as poly_score_degree9
```

HE behind the scenes:

```text
encrypted z
  -> EvalBootstrap(z)
  -> then compute degree-9 polynomial
  -> EvalSum
  -> decrypt scalar result
```

This measures the cost of CKKS bootstrapping plus the degree-9 polynomial.

## Dense 4x8 Layer Benchmark

This is run by `dense_layer_bench`.

Math:

```text
Y = X @ W + b

X: rows x 4
W: 4 x 8
b: 8
Y: rows x 8
```

Plain baseline:

```text
for each row:
  for each output neuron:
    y[row][out] = bias[out]
    for each input feature:
      y[row][out] += x[row][in] * W[in][out]
```

HE behind the scenes:

```text
x0, x1, x2, x3 feature columns
  -> pack each column into CKKS slots
  -> encrypt each feature column

for each output neuron j:
  ct_yj =
      EvalMult(ct_x0, W[0][j])
    + EvalMult(ct_x1, W[1][j])
    + EvalMult(ct_x2, W[2][j])
    + EvalMult(ct_x3, W[3][j])
    + plaintext bias[j]

  decrypt output vector
  compare every y[row][j] against C++ baseline
```

This is a small dense neural-network layer. It is matrix-vector/matrix-output
inference over encrypted input data with plaintext model weights.

## Tiny Trig Function-Eval Benchmark

This is run by `function_eval_bench`.

Math:

```text
x = [-0.75, -0.50, -0.25, 0.00, 0.25, 0.50, 0.75]

sin(x)
cos(x)
tan(x)
```

Plain baseline:

```text
std::sin(x)
std::cos(x)
std::tan(x)
```

HE behind the scenes:

```text
x vector
  -> pack into CKKS slots
  -> encrypt
  -> EvalSin / EvalCos / EvalChebyshevFunction(tan)
  -> decrypt vector
  -> compare each slot against C++ baseline
```

Default degree sweep:

```text
15, 30, 45
```

This tests Chebyshev function approximation, multiplicative depth pressure,
and approximation error on a tiny inspectable vector.

## Federated Averaging Benchmark

This is run by `fedavg_bench`.

Math:

```text
global_param_j = sum(client_examples_c / total_examples * client_param_cj)
```

Plain baseline:

```text
load client JSON weights
  -> flatten model tensors
  -> weighted average per parameter
  -> unflatten back to layer shapes
```

HE behind the scenes:

```text
client model weights
  -> flatten tensors into one vector per client
  -> split vector into CKKS-sized chunks
  -> pack each chunk
  -> encrypt each client chunk
  -> serialize ciphertext in memory
  -> deserialize ciphertext
  -> EvalMult by public client alpha
  -> EvalAdd all client chunks for same chunk index
  -> decrypt merged global chunk
  -> decode and unflatten result
```

Main HE operations: packed encoding, encryption, serialization,
deserialization, ciphertext-plaintext scalar multiply, ciphertext addition,
decrypt.

## Quick Operation Map

| HE operation type | Covered by |
| --- | --- |
| Packed SIMD addition | `sum_amount`, rolling average, FedAvg |
| Ciphertext-plaintext multiplication | `weighted_sum_amount_risk`, rolling average mask, polynomial coefficients, FedAvg alpha |
| Ciphertext-ciphertext multiplication | `weighted_sum_amount_risk_encrypted`, polynomial powers |
| Dense layer matrix math | `dense_layer_bench` |
| Chebyshev function evaluation | `function_eval_bench` |
| Slot rotations | `rolling_avg_amount_w3/w5/w9` |
| Slot reduction / aggregation | `sum_amount`, weighted sums, rolling average scalar result, polynomial scores |
| Scheme-switching comparison | `compare_amount_gt_5000` |
| Bootstrapping | `poly_score_degree7_bootstrap`, `poly_score_degree9_bootstrap` |
| Serialization/deserialization | FedAvg benchmark |

## Notes

- CSV load time is separate from compute time.
- Plain C++ baseline is the correctness and speed reference.
- CKKS results are approximate, so accuracy is checked by absolute and relative error.
- `total_he_time_ms` includes encode, encrypt, HE evaluation, decrypt, and decode.
- `he_eval_time_ms` is only encrypted computation after encryption.
- Rolling average currently reports one scalar: the mean of all rolling-average outputs.
