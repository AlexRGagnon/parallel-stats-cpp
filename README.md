# Parallel Stats (C++20)

A bounded worker pool and deterministic parallel numerical reduction, with explicit backpressure, exception delivery, and reproducible local benchmarks. No third-party libraries or hosted compute.

## Build and test

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Visual Studio builds, run `build/Release/stats-benchmark.exe 2000000 4 7`; on single-configuration builds, use `build/stats-benchmark`. Arguments are observation count, workers, and repetitions. The JSON output records all timing samples, median speedup, input seed, and numerical equality. Pool construction/join overhead is included. Cases alternate serial/parallel order after a warm-up. Measurements apply only to the recorded machine/workload; small jobs can be slower in parallel.

## Contracts

`BoundedPool(workers, capacity)` accepts nullary callables and returns typed futures. Submission blocks when the pending queue is full. Multiple external submitters are supported. `close()` is idempotent, rejects new submissions, wakes blocked submitters, and lets accepted tasks drain. Destruction joins all workers. User exceptions propagate through futures without killing a worker. Move-only callables are supported through owned packaged tasks.

Nested submission to the same pool is rejected, avoiding a common finite-pool starvation deadlock. Tasks must terminate, must not destroy their own pool, and must not block on other tasks scheduled in the same pool. Destruction must not race with external method calls. There is no forcible cancellation, task deadline, lock-free guarantee, fairness guarantee, or real-time scheduling.

`parallel_summarize` computes mean and variance using Welford updates and pairwise moment merges. Fixed chunk boundaries and ordered merges make results identical across worker counts on the same build for the same chunk size. Floating-point results need not match across compilers/platforms or different chunk sizes. Nonfinite input and arithmetic overflow raise exceptions. Empty summaries have count zero; requesting an unavailable variance raises a domain error. The public `Moments` fields should not be externally mutated.

The pool holds at most `capacity` pending tasks plus `workers` active tasks. The reduction additionally caps outstanding futures at `workers + capacity`; it does not retain one future per input chunk. Input memory is caller-owned and must remain unchanged until the synchronous reduction returns. Task closures themselves can allocate memory, so a task-count bound is not a byte-level heap limit.

Thirteen individually registered tests check backpressure, close/drain, blocked submitter wake-up, nested submission rejection, task exceptions, concurrent producers, numerical reference cases, deterministic worker-count comparisons, nonfinite input, and repeated lifecycle stress. Test checks are active in Release builds; a 30-second CTest timeout catches hangs. These tests are not a substitute for ThreadSanitizer or a formal race proof.

See `VALIDATION.md` and `examples/` for actual measurements. MIT licensed. No GitHub Actions.
