# `rolling_avg` Optimization Findings

No optimized rolling-average variant has been measured yet.

Initial baseline to collect:

```text
1. all_rolling_vector, 100k, threads=1, repeat=3
2. all_rolling, 100k, threads=1, repeat=3
3. all_rolling_vector, lower-Q probe if accepted
```

Record summary rows only.
