# Local validation

Recorded UTC: 2026-10-31T01:41:10.991057+00:00

Windows 11 x64; AMD Ryzen 9 7950X, 16 cores / 32 logical processors; Visual Studio 2022 Build Tools, MSVC 19.44.35214.0; x64 Release. Compile warnings are treated as errors.

## Correctness and concurrency

- **13/13 CTest cases passed**.
- `ctest --repeat until-fail:25`: **325/325 case invocations passed**. Every repetition includes the lifecycle stress test.
- Concurrent producer tests preserve the configured active-worker and queued-task bounds.
- Same-pool nested submissions are rejected; closing wakes blocked producers and drains accepted work; task exceptions reach their futures.
- Fixed chunk boundaries and input-order merges give exact mean/m2 equality against the serial chunked reference for worker counts 1, 2, 4, and 8 on this build.
- Numerical tests include known sample/population variances, large-offset observations, empty inputs, and NaN/infinity rejection.

## Benchmark, not a general performance guarantee

Two million seeded double-precision observations; chunks of 16,384; queue capacity 8; nine timed repetitions after warm-up. Serial and parallel order alternates. Pool startup/join overhead is included. Both paths use the same chunking and merge order. Each timed result is checked for correctness.

| Workers | Serial median ms | Parallel median ms | Median ratio |
|---|---:|---:|---:|
| 1 | 7.8319 | 8.0619 | 0.9715x |
| 2 | 7.8057 | 4.1663 | 1.8735x |
| 4 | 7.8100 | 2.2222 | 3.5145x |
| 8 | 7.7632 | 1.4780 | 5.2525x |

The one-worker pool is slower than the direct serial baseline. All timing samples, not just the best result, are committed in `examples/benchmark-*.json`. Workload size, system load, compiler, memory bandwidth, and CPU topology can change these ratios. Peak heap/RSS, thread fairness, hard real-time latency, ThreadSanitizer, Linux/macOS builds, and cross-platform bitwise reproducibility were not validated.

## Reproduce

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Release --repeat until-fail:25 --output-on-failure
.\build\Release\stats-benchmark.exe 2000000 4 9
```

All work ran locally; no cloud execution, paid APIs, or GitHub Actions. The raw repeated-test log is retained in the local build directory; its SHA-256 and count are in `examples/repeated-tests.json`.
