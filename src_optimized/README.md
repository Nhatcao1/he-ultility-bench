# Optimized C++ Benchmarks

This directory is reserved for experimental optimized C++ implementations.

Do not move reference code out of `src/`. Optimized code should be copied or
reimplemented here so old results remain reproducible.

Expected layout:

```text
src_optimized/
  sum_amount/
  weighted_sum/
  rolling_avg/
  poly_score/
  lookup/
  dense_layer/
  trig/
```

Each optimized target should build as a separate binary so the reference
benchmarks remain reproducible.

## Lookup

```text
lookup/
```

Encrypted one-hot lookup for small-domain `channel_id` values. This track is
lookup-only and intentionally excludes weighted sum, amount multiplication, and
final HE aggregation.
