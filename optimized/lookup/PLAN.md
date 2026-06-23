# Lookup Benchmark Plan

## Goal

Measure the cost of an encrypted lookup primitive without mixing in weighted
sum, amount multiplication, or final aggregation.

The test answers this question:

```text
How expensive is it to map encrypted channel IDs to encrypted lookup values?
```

## Math

For each row:

```text
y_i = lookup[channel_id_i]
```

Example:

```text
channel_id = [2, 0, 3, 1]
lookup     = {0: 0.500, 1: 0.625, 2: 0.750, 3: 0.875}

output y   = [0.750, 0.500, 0.875, 0.625]
checksum   = 2.750
```

The checksum is only for reporting correctness in the result CSV. It is not
the benchmarked HE lookup primitive.

## C++ Baseline

```text
for each row:
  y_i = lookup[channel_id_i]

checksum += y_i
```

Timed baseline work:

```text
array indexing + checksum
```

## OpenFHE CKKS Lookup

```text
rows packed into CKKS slots
        |
        v
for each lookup key k:
  encrypted mask_k  = Enc([1 if channel_id_i == k else 0])
  encrypted value_k = Enc([lookup[k], lookup[k], ...])
        |
        v
  term_k = encrypted mask_k * encrypted value_k
        |
        v
encrypted output = term_0 + term_1 + ... + term_9
```

Diagram:

```text
Enc(mask for key 0) * Enc(value 0) ----\
Enc(mask for key 1) * Enc(value 1) -----+--> EvalAdd --> Enc(output vector)
Enc(mask for key 2) * Enc(value 2) ----/
...
Enc(mask for key 9) * Enc(value 9) ----/
```

## What Is Counted

`he_eval_time_ms`:

```text
EvalMult + EvalAdd lookup work only
```

`total_he_time_ms`:

```text
encode + encrypt + EvalMult/EvalAdd + decrypt + decode
```

`setup_time_ms`:

```text
context + key generation
```

Setup is recorded separately.

## Why This Is The First Lookup Shape

The generated data has a small `channel_id` domain, so encrypted one-hot lookup
is a reasonable first HE-native implementation:

```text
small domain -> one encrypted mask per key
no comparison
no rotation
no weighted sum
```

Large domains such as customer IDs should not use this exact shape because the
cost grows with the number of possible keys.
