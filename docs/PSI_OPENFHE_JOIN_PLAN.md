# PSI + OpenFHE Join Plan

This plan is for private joins where CKKS alone is a bad fit. CKKS is good for
real-number arithmetic, but exact key matching is better handled by PSI or an
exact-integer HE scheme.

## Goal

Split private join into two jobs:

```text
PSI:
  privately match join keys

OpenFHE CKKS:
  privately compute numeric expressions after rows are aligned
```

Target query shape:

```sql
SELECT SUM(t.amount * r.risk_weight)
FROM transactions t
JOIN risk_lookup r
  ON t.customer_id = r.customer_id;
```

## Why Not CKKS-Only Join

CKKS can do packed real arithmetic well:

```text
SUM(amount)
SUM(amount * risk_weight)
polynomial score
FedAvg merge
```

But joins need exact key logic:

```text
customer_id_left == customer_id_right
```

That means equality, comparison, branching, or masks. Those are expensive and
awkward in CKKS.

## Architecture Diagram

```text
Party A: Transactions                         Party B: Lookup Table
---------------------                         ---------------------
customer_id, amount                           customer_id, risk_weight
        |                                                |
        |                                                |
        +---------------- PSI key matching --------------+
                         |
                         v
              matched/aligned key positions
                         |
                         v
        +-----------------------------------------------+
        | Build aligned numeric vectors                 |
        | amount[i] corresponds to risk_weight[i]       |
        +-----------------------------------------------+
                         |
                         v
              OpenFHE CKKS numeric compute
                         |
                         v
          Enc(amount) * Enc/Plain(risk_weight)
                         |
                         v
                    EvalSum
                         |
                         v
              encrypted or decrypted aggregate
```

## Tool Roles

| Tool | Role | Best Use |
| --- | --- | --- |
| OpenMined PSI | Simple exact-key PSI baseline | Prove private key intersection before HE numeric compute. |
| Microsoft APSI | Labeled/key-value PSI candidate | Private lookup/join where matched keys carry payloads. |
| OpenFHE CKKS | Numeric encrypted compute | SUM, weighted sum, polynomial score, model-style arithmetic. |

## Phase 1: OpenMined PSI Baseline

Purpose:

```text
Can we privately match transaction keys with lookup keys?
```

Inputs:

```text
transactions.customer_id
customers.customer_id
```

Output:

```text
intersection keys or aligned row ids
```

Then OpenFHE can compute numeric work over the aligned rows.

This phase is not a full private payload join yet. It is the simplest PSI sanity
test.

## Phase 2: APSI Labeled PSI

Purpose:

```text
Can matched keys carry a payload such as risk_weight or lookup row id?
```

Inputs:

```text
key   = customer_id
label = risk_weight or lookup_row_id
```

Output:

```text
matched keys plus associated payloads
```

This is closer to a database join.

## Phase 3: OpenFHE Numeric Compute

After PSI gives aligned numeric vectors:

```text
amount        = [a0, a1, a2, ...]
risk_weight   = [w0, w1, w2, ...]
```

Run CKKS:

```text
Enc(amount) * Enc(risk_weight)
EvalSum
```

or if lookup values are public:

```text
Enc(amount) * Plain(risk_weight)
EvalSum
```

## Privacy Notes

Different PSI modes reveal different things:

```text
PSI:
  may reveal matching keys to one or both parties

PSI cardinality:
  reveals only intersection size

labeled PSI:
  reveals payloads for matched keys

private join-and-compute:
  reveals only aggregate output
```

We must state clearly which privacy level each benchmark uses.

## Benchmark Plan

First measurable benchmark:

```text
psi_key_match_customer_id
```

Metrics:

```text
left_key_count
right_key_count
intersection_count
psi_time_ms
payload_bytes
```

Second benchmark:

```text
psi_then_ckks_weighted_sum
```

Metrics:

```text
psi_time_ms
alignment_time_ms
ckks_encode_time_ms
ckks_encrypt_time_ms
ckks_eval_time_ms
ckks_decrypt_time_ms
total_time_ms
plain_cpp_baseline_ms
absolute_error
relative_error
```

## Install Direction

Server only:

```text
OpenMined PSI for Phase 1
Microsoft APSI for Phase 2
OpenFHE already installed for CKKS numeric compute
```

Local machine:

```text
No heavy PSI build.
Keep docs, fixture generators, and integration scripts only.
```

## Decision

Use OpenMined PSI first if the goal is learning and exact-key intersection.

Use Microsoft APSI when the goal becomes private lookup with payloads.

Keep OpenFHE CKKS for numeric computation after keys are matched or aligned.
