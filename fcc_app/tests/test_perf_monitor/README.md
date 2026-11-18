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

The monitor suite registers nine CTest cases. `perf_monitor_tests` runs eight collector cases, and `perf_reporter_tests` runs the reporter case. Both use the common test runner: no argument runs all cases, a case name selects one, and an unknown name returns 2. The collector fixture resets private state through an included production source; firmware has no diagnostic reset API.

| Case | Behavior checked |
|---|---|
| `test_counter_saturation` | Counters do not wrap; timing aggregate freezes consistently; huge elapsed values clamp |
| `test_timing_boundaries` | 0/1/19/20/21ms, strict >20ms budget, separate queue/processing/total time |
| `test_failed_and_missing_submit` | Failed submit is excluded from successful timing; missing submit is separate |
| `test_context_cleanup` | No previous measurement reuse after end; duplicate completion ignored |
| `test_invalid_timestamps` | Negative receive time and backwards start/completion excluded |
| `test_queue_and_tx_counts` | Independent RX/TX queues, sampled depth, polling completion vs encode failure |
| `test_snapshot_contract` | NULL returns -EINVAL; reading does not clear counters |
| `test_concurrent_snapshots` | Two pthread producers, 20,000 updates, snapshots preserve the count/depth invariant |
| `test_summary_and_stack_queries` | Empty samples, nearest-rank percentile boundaries, overflow bin, stack minimum, unavailable query and recovery |

The handler's `PERF_TEST_ENABLED=ON` build runs the existing 30 cases plus `test_perf_measurement_paths`: real timing hooks across success, stale/invalid input, encoder failure, full queue, recovery, controls/timeout, and RX submit validation. The TX variant reuses four cases and adds assertions for queue rejection, observed occupancy, successful polling and encoder failure. Total performance-enabled executions: 9 + 31 + 4 = 44.

A pthread mutex stands in for `k_spinlock`; this checks concurrent host updates/snapshots but not Zephyr IRQ exclusion or timing. Handler/TX scheduling, uptime, stack queries and UART are mocked. Firmware builds check the actual Zephyr APIs. No host test establishes board stack headroom, physical delivery, latency, or 30-minute load stability. See [measurement contract and board procedure](../../../docs/performance-monitoring.md).
