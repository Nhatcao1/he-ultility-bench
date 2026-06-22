# Polynomial Score Optimization Plan

## Scope

Target the existing degree-9 polynomial ML-style benchmark and compare three
flows:

```text
1. Original reference: src/poly_ml_bench.cpp
2. Optimized option 1: power-tree degree-9 after bootstrap
3. Optimized option 2: block degree-9 after bootstrap
```

All variants keep:

```text
CKKS
128-bit security
degree 9
bootstrap enabled
one OpenFHE thread for first comparison
same data and same CKKS parameters
```

## Math

```text
a = amount / 10000
u = x1 / 10000
v = x2 / 10000
w = x3 / 10000

z = 0.50*a + 0.30*u + 0.20*v - 0.10*w - 0.25

p(z) = 0.5
     + 0.197*z
     - 0.004*z^3
     + 0.00008*z^5
     - 0.000001*z^7
     + 0.00000001*z^9

result = SUM(p(z))
```

## Original

```text
Pack amount/x1/x2/x3
        |
        v
Encrypt columns
        |
        v
Compute encrypted z
        |
        v
Bootstrap z
        |
        v
Linear odd-power chain:
z2 = z*z
z3 = z2*z
z5 = z3*z2
z7 = z5*z2
z9 = z7*z2
        |
        v
Weighted polynomial add
        |
        v
EvalSum
        |
        v
Decrypt scalar
```

Expected behavior:

```text
Simple reference flow, but degree-9 powers are chained through z2.
Bootstrap cost is measured separately in bootstrap_time_ms.
```

## Optimized Option 1: Power Tree

```text
Pack + encrypt columns
        |
        v
Compute encrypted z
        |
        v
Bootstrap z
        |
        v
Power tree:
z2 = z*z
z3 = z2*z
z4 = z2*z2
z5 = z4*z
z6 = z4*z2
z7 = z6*z
z8 = z4*z4
z9 = z8*z
        |
        v
Weighted polynomial add
        |
        v
EvalSum
        |
        v
Decrypt scalar
```

Why try it:

```text
It exposes whether OpenFHE benefits from a shallower dependency shape even if
the number of ciphertext multiplications is not minimal.
```

## Optimized Option 2: Block Evaluation

```text
Pack + encrypt columns
        |
        v
Compute encrypted z
        |
        v
Bootstrap z
        |
        v
Precompute:
z2 = z*z
z3 = z2*z
z4 = z2*z2
z5 = z4*z
        |
        v
Low block:
L = 0.5 + c1*z + c3*z3
        |
        v
High block:
H = c5 + c7*z2 + c9*z4
        |
        v
p(z) = L + z5*H
        |
        v
EvalSum
        |
        v
Decrypt scalar
```

Why try it:

```text
It reduces the number of ciphertext multiplications after bootstrap compared
with materializing every odd power separately.
```

## What To Compare

Primary columns:

```text
he_eval_time_ms + bootstrap_time_ms
bootstrap_time_ms
total_he_time_ms
relative_error
actual_ring_dimension
ciphertext_count
slot_utilization
```

Interpretation:

```text
he_eval_time_ms          encrypted arithmetic excluding bootstrap
bootstrap_time_ms        CKKS refresh cost
total_he_time_ms         encode + encrypt + eval + bootstrap + decrypt + decode
setup_time_ms            context/key/setup cost, recorded separately
```
