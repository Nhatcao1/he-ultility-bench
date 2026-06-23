# BinFHE Product LUT Benchmark

## Goal

Benchmark a true encrypted scalar lookup:

```text
Enc(product_id) -> programmable bootstrapping LUT -> Enc(product_risk_code)
```

This is the active lookup benchmark. There is no one-hot mask and no CKKS SIMD
arithmetic lookup.

## Input

Use `product_id` from `transactions.csv`.

Example rows:

```text
row   product_id
0     9
1     19
2     17
3     10
```

## Product LUT

The table is intentionally arbitrary, not linear:

```text
product_id   product_name        risk_code
0            iPhone 16 Pro       11
1            Samsung S25 Ultra   7
2            MacBook Pro M4      15
3            Galaxy Tab          4
4            AirPods Pro         9
5            Apple Watch         13
6            Dell XPS            6
7            ThinkPad X1         8
8            Sony XM6            12
9            Netflix             3
10           Spotify             5
11           YouTube Premium     10
12           ChatGPT Plus        2
13           AWS EC2             14
14           Azure VM            18
15           Google Workspace    16
16           Microsoft 365       1
17           Viettel Fiber       19
18           VNPT Fiber          17
19           FPT Internet        20
```

The code is integer. If needed, interpret `risk_code = 11` as `1.1` outside
the encrypted lookup.

## Plain Baseline

```text
for each row:
  risk_code_i = product_risk_table[product_id_i]
  checksum += risk_code_i
```

## BinFHE/FHEW Flow

```text
product_id
    |
    v
Enc(product_id)
    |
    v
EvalFunc(product_risk_lut)
    |
    v
Enc(product_risk_code)
    |
    v
decrypt only for correctness checksum
```

Concrete example:

```text
product_id       = [9, 19, 17, 10]
risk_code lookup = [3, 20, 19, 5]
checksum         = 47
```

Encrypted version:

```text
Enc(9)  -> EvalFunc LUT -> Enc(3)
Enc(19) -> EvalFunc LUT -> Enc(20)
Enc(17) -> EvalFunc LUT -> Enc(19)
Enc(10) -> EvalFunc LUT -> Enc(5)
```

## Timing

`he_eval_time_ms` measures:

```text
EvalFunc LUT programmable bootstrapping only
```

`total_he_time_ms` measures:

```text
encrypt product IDs + EvalFunc LUT + decrypt risk codes
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
