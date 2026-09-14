# mjsonrpc Benchmarks

Performance benchmark suite for mjsonrpc, covering **speed**, **memory
usage** and **process resource usage**. Designed to run locally and in CI on
every push / pull request.

## Quick start

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DMJSONRPC_BUILD_BENCHMARKS=ON
cmake --build build-bench

# run the full suite (self checks on)
./output/mjsonrpc-benchmark --check

# machine-readable + markdown output (used by CI)
./output/mjsonrpc-benchmark --json benchmark-results.json \
                            --markdown benchmark-summary.md --check

# fast local iteration
./output/mjsonrpc-benchmark --quick --filter process_str
```

> Always configure with `-DCMAKE_BUILD_TYPE=Release`: benchmark numbers are
> meaningless without optimizations.

## What is measured

| Dimension | Metrics |
|---|---|
| Speed | wall-clock `ns/op` (median / min / stddev over 7 samples), `ops/s`, process `CPU ns/op` |
| Memory | `allocs/op`, `bytes/op`, peak live bytes during the operation |
| Resource usage | peak RSS, user/system CPU time, page faults, context switches of the whole run |

Methodology: each benchmark is calibrated to run ~30 ms per sample, warmed up
twice, then measured over 7 samples; the median is reported (`--quick` uses
3 samples and a shorter calibration). Memory is tracked through the
`mjrpc_set_memory_hooks()` and `cJSON_InitHooks()` hooks, so every allocation
made by the library and by cJSON is accounted for.

## Benchmarks

| Name | Measures |
|---|---|
| `handle_create_destroy` | handle lifecycle cost |
| `add_method_1000` | registering 1000 methods incl. hash-table growth/rehash |
| `request_str_small` | client-side request string generation |
| `request_str_params` | request generation with a 5-field params object |
| `process_str_simple` | full server pipeline: parse + dispatch + serialize |
| `process_str_with_params` | same, with object params and field lookups |
| `process_str_notification` | notification path (no response) |
| `process_str_method_not_found` | error path: unknown method |
| `process_str_parse_error` | error path: invalid JSON |
| `process_cjson_direct` | dispatch of a pre-parsed cJSON request |
| `process_str_batch_10/100` | batch requests (per batch) |
| `roundtrip` | `request_str` + `process_str` end-to-end |
| `process_str_4threads` | aggregate throughput with 4 threads, one handle each |

## Self checks (exit codes)

`--check` enables two hard checks:

1. **Memory leaks** – every byte allocated through the hooks must be freed
   again by the end of the run (allocation/free counters must balance).
2. **Conservative speed floors** – each benchmark must stay above a lower
   bound that is roughly an order of magnitude below typical values, so it
   only trips on catastrophic regressions (e.g. accidental O(n²) behavior)
   and never on noisy machines.

Exit codes: `0` success, `1` usage error, `2` self-check failure.

## Regression comparison against a baseline

`benchmark/tools/compare_bench.py` compares a run against a stored baseline:

```bash
python3 benchmark/tools/compare_bench.py \
    --baseline benchmark/baseline.json \
    --current  benchmark-results.json \
    --summary-md compare-summary.md
```

- Speed: warn when ≥ 20 % slower, fail when ≥ 50 % slower (configurable with
  `--warn-pct` / `--fail-pct`).
- Allocations: warn from 1.5×, fail from 2× per-op allocation count.
- Missing baseline file → comparison is skipped (exit 0).

### Seeding / updating the baseline

The baseline is a full JSON report recorded on the CI runner:

1. Run the **Benchmark** workflow via *workflow_dispatch* with
   `update_baseline = true`.
2. The job commits the new report to `benchmark/baseline.json` on the branch
   it ran from.

Re-seed the baseline whenever GitHub rotates CI hardware or the thresholds
produce false positives.

## CI

The GitHub Actions workflow `.github/workflows/benchmark.yml` runs on every
push / PR to `main`:

1. builds a Release configuration with `-DMJSONRPC_BUILD_BENCHMARKS=ON`,
2. runs the suite with `--check`,
3. writes the results table and the baseline comparison to the job summary,
4. uploads `benchmark-results.json` as an artifact (30 days),
5. fails the job on self-check failures or hard regressions (≥ 50 % slower /
   ≥ 2× allocations vs baseline).

`workflow_dispatch` inputs: `quick` (fast run) and `update_baseline`.
