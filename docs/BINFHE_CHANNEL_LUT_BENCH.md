# BinFHE Channel LUT Benchmark

## Goal

Benchmark a true encrypted scalar lookup:

```text
Enc(channel_id) -> programmable bootstrapping LUT -> Enc(channel_risk_code)
```

This is the active lookup benchmark. There is no one-hot mask and no CKKS SIMD
arithmetic lookup.

The generated transaction data has `channel_id` values `0..9`. The LUT output
codes are kept inside `0..15`, so the benchmark can run with
`--binfhe-ring-dim 4096`, where the current OpenFHE setup gives plaintext space
`p=16`.

The earlier product LUT used product IDs `0..19` and risk codes up to `20`, so
it required `p>=21`. That forced `--binfhe-ring-dim 8192`, and the server killed
the process during BinFHE `BTKeyGen`.

## Input

Use `channel_id` from `transactions.csv`.

Example rows:

```text
row   channel_id
0     2
1     3
2     4
3     9
```

## Channel LUT

The table is intentionally arbitrary, not linear:

```text
channel_id   channel_name   risk_code
0            Facebook       3
1            TikTok         14
2            YouTube        1
3            Instagram      9
4            Telegram       4
5            Zalo           12
6            Website        2
7            Shopee         7
8            Lazada         15
9            Tiki           6
```

The code is integer. If needed, interpret `risk_code = 14` as `1.4` outside
the encrypted lookup.

## Plain Baseline

```text
for each row:
  risk_code_i = channel_risk_table[channel_id_i]
  checksum += risk_code_i
```

## BinFHE/FHEW Flow

```text
channel_id
    |
    v
Enc(channel_id)
    |
    v
EvalFunc(channel_risk_lut)
    |
    v
Enc(channel_risk_code)
    |
    v
decrypt only for correctness checksum
```

Concrete example:

```text
channel_id       = [2, 3, 4, 9]
risk_code lookup = [1, 9, 4, 6]
checksum         = 20
```

Encrypted version:

```text
Enc(2) -> EvalFunc LUT -> Enc(1)
Enc(3) -> EvalFunc LUT -> Enc(9)
Enc(4) -> EvalFunc LUT -> Enc(4)
Enc(9) -> EvalFunc LUT -> Enc(6)
```

## Timing

`he_eval_time_ms` measures:

```text
EvalFunc LUT programmable bootstrapping only
```

`total_he_time_ms` measures:

```text
encrypt channel IDs + EvalFunc LUT + decrypt risk codes
```

`setup_time_ms` records:

```text
BinFHE context + secret key + bootstrapping key + LUT generation
```

## Resource Columns

The result CSV also reports:

```text
peak_rss_kb
ciphertext_payload_bytes
ciphertext_payload_kb
```

For this BinFHE bench, ciphertext payload is a lightweight LWE ciphertext-size
estimate based on:

```text
row count
input ciphertext count
output ciphertext count
LWE dimension
LWE modulus bits
```

It is intentionally not measured by serializing every ciphertext, so payload
reporting does not pollute the timing.
