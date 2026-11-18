# TX polling / PC timing investigation

This is a 120-second diagnostic run, not the 30-minute acceptance rerun. The previous 163.995ms host-gap failure remains unresolved and its verdict is unchanged.

## Provenance and measurement boundaries

- Base source: `5550bcde96eae7829c60111caecad53d02b8a98b`.
- Temporary firmware: base source plus `temporary-firmware.patch`, built with `perf.conf`. The patch was applied only to `/private/tmp/radar-tx-probe-app`; production source was unchanged.
- `probe_wrapper.py` creates `probe_workflow.py` from the existing board check, selects `build_tx_probe`, and measures PC serial operations, CSV rows/flush, sleep, progress sums, print, and garbage collection. It adds measurement overhead and does not time every Python operation or OS scheduling point.
- `metadata.json` records both image hashes. `build_tx_probe.config` and `build.config` retain exact configurations. Normal firmware was restored; `restored-status.json` contains eight zero counters.
- MCU queue timing starts immediately before enqueue and ends after dequeue. Poll timing surrounds the complete byte loop, includes preemption, and ends when the final `uart_poll_out()` returns. This is not the physical last-bit completion or peer acknowledgment.
- The final quiescent TX summary reconciles all 6,123 transmitted frames. Individual atomic fields are sampled separately while load is active.

## Observed results

| Item | Result |
|---|---:|
| Duration / measurement rate | 120s / 50Hz |
| Measurement sends / matched replies | 6,000 / 6,000 |
| STATUS requests / replies | 121 / 121 |
| TRACK reports including INIT and timeout | 6,002 |
| Poll-loop mean / maximum | 3.048ms / 3.565ms |
| TX enqueue-to-dequeue maximum | 3.085ms |
| RX complete through TX submit maximum | 1ms |
| PC maximum send gap | 45.741ms |
| PC reply latency p50 / p99 / maximum | 5.655ms / 8.412ms / 25.609ms |
| STATUS errors / PC CRC and length errors | 0 |
| Diagnostic check results | 25 / 25 passed |

At 115200 baud, 8N1, a 37-byte response takes about 3.212ms on the wire. The observed polling duration is consistent with this scale; its return can precede completion of the final transmitted bytes. No 164ms event occurred during this short run. This does not exclude a rare firmware or USB/host event in the previous run.

## Confirmed PC work and remaining uncertainty

The existing progress output in `fcc_app/tests/test_perf_monitor/board_check.py` computes `sum(r.input_present for r in reports)` every 60 seconds in the sending loop. It revisits the entire accumulated list. Replaying this exact expression on actual captured reports gave these seven-run median times:

| Reports scanned | Median |
|---|---:|
| 3,000 | 1.874ms |
| 30,000 | 19.071ms |
| 60,000 | 38.107ms |
| 89,971 | 56.901ms |

This confirms a growing blocking operation that exceeds the 20ms send period and matches the old minute-boundary gaps, which grew from about 31ms to 70ms. In the instrumented board run, the first progress sum took 6.323ms wall time and 6.263ms thread CPU time. Timing depends on runtime conditions; the isolated replay and live run are separate measurements.

The old isolated 164ms event occurred around 639.38 seconds, away from the progress output boundary. Its exact cause is **not established** by the progress-sum finding.

During the new load, a requested 1ms sleep took as long as 23.338ms while using only 0.025ms thread CPU time. This demonstrates delayed host-loop resumption, but does not identify an OS cause or account for every observed gap. The largest 45.741ms gap is not fully explained by the instrumented operations. Serial writes stayed below 0.932ms and CSV flushes below 2.741ms. The 50.401ms serial read in the full operation profile occurred after load during normal-image verification, where the port has a 50ms timeout; it is not a load-time stall.

The next targeted change is to increment a received-measurement counter as replies arrive and print that counter, avoiding a full-list scan. Then repeat the original 30-minute test with operation/loop timing retained. The original 100ms host-gap criterion must not be silently relaxed. Current evidence does not justify attributing the 164ms event to byte-at-a-time polling or replacing TX with DMA as its fix.

## Files and replay

`results.json`, `analysis.json`, `pc-operations.json`, `console.log`, and `session/*.csv` retain the observations. `progress_sum_benchmark.py` uses the previous compressed capture to reproduce the isolated sum benchmark. `analyze_probe.py` regenerates the numerical analysis when invoked from the repository root with the original temporary inputs available. The archived wrapper and workflow are diagnostic records, not supported test entry points; they contain local paths and overwrite their fixed output directory if rerun.
