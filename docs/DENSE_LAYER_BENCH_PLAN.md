# Dense 4x8 Layer Benchmark

This benchmark tests a small dense neural-network layer:

```text
Y = X @ W + b
```

It is more matrix-like than the earlier weighted-sum tests.

## Shape

```text
X: rows x 4
W: 4 x 8
b: 8
Y: rows x 8
```

For one row:

```text
y0 = x0*w00 + x1*w10 + x2*w20 + x3*w30 + b0
y1 = x0*w01 + x1*w11 + x2*w21 + x3*w31 + b1
...
y7 = x0*w07 + x1*w17 + x2*w27 + x3*w37 + b7
```

## Fixture Files

Generated under:

```text
data/generated_dense/dense4x8_100k/
  features.csv
  weights.csv
  bias.csv
  metadata.json
```

`features.csv`:

```text
row_id,x0,x1,x2,x3
```

`weights.csv`:

```text
input_index,w0,w1,w2,w3,w4,w5,w6,w7
```

`bias.csv`:

```text
output_index,bias
```

## Plain C++ Baseline

```text
for each row:
  for each output neuron:
    y[row][out] = bias[out]
    for each input feature:
      y[row][out] += x[row][in] * W[in][out]
```

The CSV reports:

```text
baseline_mean = mean(all Y values)
```

## OpenFHE CKKS Path

Weights and bias are plaintext model parameters. Input features are encrypted.

```text
x0 column -> pack -> encrypt
x1 column -> pack -> encrypt
x2 column -> pack -> encrypt
x3 column -> pack -> encrypt

For each output neuron j:
  ct_yj =
      EvalMult(ct_x0, W[0][j])
    + EvalMult(ct_x1, W[1][j])
    + EvalMult(ct_x2, W[2][j])
    + EvalMult(ct_x3, W[3][j])
    + plaintext bias[j]

  decrypt ct_yj
  compare vector against C++ y[:, j]
```

This is Schema A style: output vectors are decrypted and compared directly.
There is no `EvalSum` and no bootstrapping.

## What It Measures

| HE operation | Why it matters |
| --- | --- |
| CKKS packing | Dense inference uses packed batches. |
| Feature encryption | User/input data is encrypted. |
| Ciphertext-plaintext multiplication | Public model weights applied to encrypted features. |
| Ciphertext addition | Feature contributions are accumulated into output neurons. |
| Plaintext bias add | Standard dense-layer bias. |
| Vector decode accuracy | Compares every decrypted output against C++ baseline. |

## Generate Fixtures

```bash
python3 scripts/generate_dense_layer_data.py \
  --sizes 1000 100000 1000000 \
  --out data/generated_dense \
  --seed 42 \
  --chunk-size 100000
```

## Build

```bash
cmake --build build --parallel "$(nproc)"
```

## Run 100k

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

## Run 1M

```bash
rm -f results/dense_layer_1m.csv

./build/dense_layer_bench \
  --fixture data/generated_dense/dense4x8_1m \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 2 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/dense_layer_1m.csv
```

## Expected Result

`mean_absolute_error` and `max_absolute_error` should be small. This is a
linear CKKS workload, so accuracy should be much better than polynomial or
comparison workloads.
