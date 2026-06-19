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
  poly_ml/
  dense_layer/
  trig/
```

The first target should be `sum_amount` because it is the smallest useful HE
aggregation benchmark.
