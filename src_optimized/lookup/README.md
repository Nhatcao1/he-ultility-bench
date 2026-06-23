# Encrypted Lookup Benchmarks

Build target:

```text
lookup_opt_bench
```

This benchmark tests lookup only:

```text
lookup_value = channel_lookup[channel_id]
```

It does not multiply by `amount`, does not use `risk_weight`, and does not
run a final HE aggregate.

## HE Privacy Rule

The HE path uses encrypted inputs for the lookup:

```text
Enc(one_hot(channel_id))
Enc(channel_lookup_value)
```

The plaintext baseline is normal C++ lookup and is only there as the speed and
correctness reference.

## Current Lookup Table

The benchmark uses a small deterministic lookup table for generated
`channel_id` values:

```text
channel_id 0 -> 0.500
channel_id 1 -> 0.625
channel_id 2 -> 0.750
...
channel_id 9 -> 1.625
```

## Encrypted Flow

```text
encrypted one-hot masks:
  key0 mask = Enc([1,0,0,0,...])
  key1 mask = Enc([0,1,0,0,...])
  key2 mask = Enc([0,0,1,0,...])

encrypted lookup values:
  key0 value = Enc([0.500,0.500,0.500,...])
  key1 value = Enc([0.625,0.625,0.625,...])
  key2 value = Enc([0.750,0.750,0.750,...])

for each key:
  Enc(mask_key) * Enc(value_key)
        |
        v
  encrypted selected value for that key

add all key results slot-wise
        |
        v
encrypted lookup output vector
        |
        v
decrypt only to verify checksum accuracy
```

No rotations are required for the lookup itself.
