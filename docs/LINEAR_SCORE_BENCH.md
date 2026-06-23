# Linear Score Benchmark

## Goal

Separate the basic linear calculation primitive from the FedAvg benchmark.

This benchmark is not a full ML pipeline. It measures the cost of computing a
simple encrypted weighted feature score:

```text
score_i = w1*x1_i + w2*x2_i + w3*x3_i + w4*amount_i + bias
```

Current constants:

```text
w1 =  0.00030
w2 = -0.00020
w3 =  0.00015
w4 =  0.00005
b  =  0.10000
```

## Example

One row:

```text
x1     = 1000
x2     = 2000
x3     = 3000
amount = 4000

score = 0.00030*1000
      - 0.00020*2000
      + 0.00015*3000
      + 0.00005*4000
      + 0.10000

score = 0.65
```

The result CSV reports a checksum of the score vector so the HE output can be
compared with the C++ baseline.

## Plain C++ Baseline

```text
for each row:
  score_i = w1*x1_i + w2*x2_i + w3*x3_i + w4*amount_i + bias
  checksum += score_i
```

This is the baseline for both speed and correctness.

## OpenFHE CKKS Flow

All calculation inputs in the HE path are encrypted:

```text
Enc(x1), Enc(x2), Enc(x3), Enc(amount)
Enc(w1), Enc(w2), Enc(w3), Enc(w4)
Enc(bias)
```

Diagram:

```text
Enc(x1)      * Enc(w1) ----\
Enc(x2)      * Enc(w2) -----+
Enc(x3)      * Enc(w3) -----+--> EvalAdd terms --> + Enc(bias)
Enc(amount)  * Enc(w4) ----/                         |
                                                       v
                                               Enc(score vector)
                                                       |
                                                       v
                                      decrypt only for checksum accuracy
```

No rotations and no final HE aggregation are required for the score-vector
calculation.

## Timing Columns

Use:

```text
he_eval_time_ms
```

for the direct encrypted calculation:

```text
EvalMult + EvalAdd
```

Use:

```text
total_he_time_ms
```

when encode, encrypt, decrypt, and decode should be included.

Setup/key generation is recorded separately as `setup_time_ms`.
