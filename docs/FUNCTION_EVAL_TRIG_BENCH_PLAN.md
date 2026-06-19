# Tiny Trig Function Evaluation Benchmark

This benchmark checks whether OpenFHE CKKS can evaluate small encrypted
trigonometric functions using Chebyshev approximation.

It is a feasibility and accuracy test, not a throughput benchmark.

## Input Vector

```text
x = [-0.75, -0.50, -0.25, 0.00, 0.25, 0.50, 0.75]
```

This range is intentionally small:

```text
sin and cos are smooth here
tan is also safe because the range stays away from pi/2
```

## Functions

```text
sin_tiny
cos_tiny
tan_tiny
all_trig_tiny
```

OpenFHE has direct helpers for:

```text
EvalSin
EvalCos
```

`tan` is evaluated through:

```text
EvalChebyshevFunction([](double x) { return std::tan(x); }, ...)
```

## Degree Sweep

Default degrees:

```text
15
30
45
```

Depth is chosen from the OpenFHE function-evaluation guidance:

```text
degree 15 -> depth 6
degree 30 -> depth 7
degree 45 -> depth 7
```

You can override depth with:

```text
--ckks-depth N
```

Use `--ckks-depth 0` for automatic depth selection.

## Plain Baseline

```text
for each x:
  baseline_sin = std::sin(x)
  baseline_cos = std::cos(x)
  baseline_tan = std::tan(x)
```

## OpenFHE Path

```text
tiny input vector
  -> MakeCKKSPackedPlaintext
  -> Encrypt
  -> EvalSin / EvalCos / EvalChebyshevFunction(tan)
  -> Decrypt
  -> compare each slot against C++ std math
```

## Metrics

The result CSV records:

```text
mean_absolute_error
max_absolute_error
baseline_values
result_values
he_eval_time_ms
total_he_time_ms
degree
multiplicative_depth
```

Values are stored directly because the vector is tiny.

## Build

```bash
cmake --build build --parallel "$(nproc)"
```

## Run

```bash
rm -f results/function_eval_trig_tiny.csv

./build/function_eval_bench \
  --bench all_trig_tiny \
  --degrees 15 30 45 \
  --backend all \
  --threads 1 4 8 \
  --ckks-ring-dim 0 \
  --ckks-batch-size 0 \
  --ckks-depth 0 \
  --ckks-scale-bits 50 \
  --ckks-first-mod-bits 60 \
  --results results/function_eval_trig_tiny.csv
```

## Expected Notes

`sin_tiny` and `cos_tiny` should be more stable than `tan_tiny`.

If `tan_tiny` is inaccurate, try:

```text
--degrees 30 45 60
```

If `tan_tiny` becomes unstable, shrink the interval:

```text
use a smaller input vector in code, then set matching lower/upper bounds
```

The Chebyshev bounds must cover the encrypted input vector. The CLI will reject
bounds that do not include the fixed `[-0.75, 0.75]` input range.
