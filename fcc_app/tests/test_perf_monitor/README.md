# Performance monitor host tests

Run from the repository root:

```sh
cmake -G Ninja -S fcc_app/tests/test_perf_monitor -B /tmp/radar-perf-tests
cmake --build /tmp/radar-perf-tests
ctest --test-dir /tmp/radar-perf-tests --output-on-failure

cmake -G Ninja -S fcc_app/tests/test_frame_handler -B /tmp/radar-handler-perf-tests -DPERF_TEST_ENABLED=ON
cmake --build /tmp/radar-handler-perf-tests
ctest --test-dir /tmp/radar-handler-perf-tests --output-on-failure

cmake -G Ninja -S fcc_app/tests/test_uart_tx -B /tmp/radar-tx-perf-tests -DPERF_TEST_ENABLED=ON
cmake --build /tmp/radar-tx-perf-tests
ctest --test-dir /tmp/radar-tx-perf-tests --output-on-failure
```

The monitor suite registers 12 CTest cases. `perf_monitor_tests` runs nine collector cases, and `perf_reporter_tests` runs the existing reporter case and the cycle-summary case; `perf_reporter_timing_tests` checks ns conversion with a synthetic 100 MHz timer. Both use the common test runner: no argument runs all cases, a case name selects one, and an unknown name returns 2. The collector fixture resets private state through an included production source; firmware has no diagnostic reset API.

| Case | Behavior checked |
|---|---|
| `test_execution_aggregation` | Independent predict/correct aggregates, zero/UINT64_MAX samples, count/total overflow exclusion |
| `test_execution_summary_cycles/ns` | Empty samples, independent summaries, integer mean and unit conversion with/without timing support |
| `test_counter_saturation` | Counters do not wrap; timing aggregate freezes consistently; huge elapsed values clamp |
| `test_timing_boundaries` | 0/1/19/20/21ms, strict >20ms budget, separate queue/processing/total time |
| `test_failed_and_missing_submit` | Failed submit is excluded from successful timing; missing submit is separate |
| `test_context_cleanup` | No previous measurement reuse after end; duplicate completion ignored |
| `test_invalid_timestamps` | Negative receive time and backwards start/completion excluded |
| `test_queue_and_tx_counts` | Independent RX/TX queues, sampled depth, polling completion vs encode failure |
| `test_snapshot_contract` | NULL returns -EINVAL; reading does not clear counters |
| `test_concurrent_snapshots` | Two pthread producers, 20,000 updates, snapshots preserve the count/depth invariant |
| `test_summary_and_stack_queries` | Empty samples, nearest-rank percentile boundaries, overflow bin, stack minimum, unavailable query and recovery |

The handler's `PERF_TEST_ENABLED=ON` build runs the existing 31 cases plus `test_perf_measurement_paths`: real timing hooks across success, stale/invalid input, encoder failure, full queue, recovery, controls/timeout, and RX submit validation. The TX variant reuses four cases and adds assertions for queue rejection, observed occupancy, successful polling and encoder failure. Total performance-enabled executions: 12 + 32 + 4 = 48.

A pthread mutex stands in for `k_spinlock`; this checks concurrent host updates/snapshots but not Zephyr IRQ exclusion or timing. Handler/TX scheduling, uptime, stack queries and UART are mocked. Firmware builds check the actual Zephyr APIs. No host test establishes board stack headroom, physical delivery, latency, or 30-minute load stability. See [measurement contract and board procedure](../../../docs/performance-monitoring.md).

The separate `tests/test_perf_latency_guard.py` suite checks negative inputs with assertions on/off and signed/unsigned boundaries (12 cases). Run it with `python -m pytest -q tests/test_perf_latency_guard.py`. These tests do not establish actual estimator execution times.

## Real-board check

Build `build_perf` with `perf.conf` and a separate normal `build` image first. The bench script verifies both configurations, flashes the performance image, records both UARTs, and attempts normal-image restoration even on failure. Forced process termination or power loss can prevent restoration.

```sh
python fcc_app/tests/test_perf_monitor/board_check.py \
  --data-port DATA --console-port CONSOLE --workspace /path/to/zephyrproject \
  --output /tmp/new-perf-smoke --duration 10 --smoke
# Run separately after the short check passes. Do not reuse an existing output directory.
python fcc_app/tests/test_perf_monitor/board_check.py \
  --data-port DATA --console-port CONSOLE --workspace /path/to/zephyrproject \
  --output /tmp/new-perf-load --duration 1800
```

Use `--west /path/to/west` and `--git /path/to/git` when they are not on PATH. Keep the host awake for the sustained run (on macOS, prefix the command with `caffeinate -i`). Both serial ports are exclusively owned by this process.

The fixed load is a stationary 1,000m/15-degree target, 50Hz MEAS_REPORT and 1Hz STATUS at 115200 8N1. No target noise or synthetic consumer suspension is enabled. Missed host slots are skipped instead of sent in catch-up bursts and are reported separately. Host rate, skipped slots and maximum write gap are descriptive statistics, not acceptance gates (policy revision 2026-11-19). The primary performance metric is PC write-to-TRACK-read RTT: count, p50/p95/p99/max. Load-only samples exclude smoke recovery traffic. Default RTT acceptance requires p99 <=20ms and maximum <=100ms, representing one and five 50Hz periods. These are initial project budgets, not certified system requirements. `--pc-rtt-p99-ms` and `--pc-rtt-max-ms` override positive finite budgets before a run; applied values are saved. RTT summaries remain available when a budget fails. Compare like-for-like load and logging conditions; a substantially lower observed rate is not evidence for 50Hz throughput. Board acceptance requires matching responses, zero diagnostic errors/queue rejection, RX-to-submit latency at most 20ms, complete continuous performance summaries, and available nonzero sampled stack headroom.

The short mode also sends an invalid range and INIT/RESET, verifies recovery, and checks that one invalid measurement increments `without_submit` while controls and the timeout event do not add timing samples. Queue saturation and stale injection remain covered by the separate transport stress test; this fixture does not pause consumers.

Raw console text, session CSVs, configurations, ELF/script hashes, complete parsed summaries and assertions are saved. `tests/test_perf_monitor.py` checks summary chunk boundaries, incomplete records, stack query failures, fatal/log-loss detection and percentile rank without serial access. These 17 Python cases also check old/new summary compatibility; optional estimator fields are retained in the bench JSON and raw console log without adding execution-time acceptance thresholds. They are distinct from the 12 C collector/reporter cases.

Console output now uses `PERF summary:`, `over_budget_count`, `unused_bytes`, and `min_unused_bytes`. The parser accepts old and new spellings and preserves legacy JSON keys. Empty execution aggregates use `status=no_samples`; latency aggregates also expose sample status.
