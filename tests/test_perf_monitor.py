"""Check performance bench evidence parsing without serial I/O or flashing."""
import importlib.util
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / 'fcc_app/tests/test_perf_monitor/board_check.py'
spec = importlib.util.spec_from_file_location('perf_board_check', SCRIPT)
board_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board_check)


def summary(uptime=5000, omit=None, execution=True):
    # Deliberately fixed text, independent of the firmware's formatting implementation.
    lines = [f'PERF uptime_ms={uptime} measurements=3 without_submit=0 submit_failed=0 invalid_time=0']
    lines += [f'PERF {name}: n=3 total_ms=2 p50_bin=1 p95_bin=1 p99_bin=1 max_ms=1 over20=0'
              for name in ('queue_wait', 'processing', 'rx_to_submit')]
    if execution:
        lines += ['PERF estimator_predict: n=3 min_ns=100 mean_ns=200 max_ns=300',
                  'PERF estimator_correct: n=0 no samples']
    lines += [f'PERF queue {name}: accepted=4 rejected=0 observed_max_used=1' for name in ('rx', 'tx')]
    lines += ['PERF tx: poll_completed=4 encode_failed=0']
    lines += [f'PERF stack {name}: unused=700 min_unused=680' for name in ('rx', 'handler', 'tx')]
    return ''.join('\x1b[0m<inf> radar_perf: ' + line + '\x1b[0m\r\n'
                   for line in lines if omit is None or not line.startswith(omit))


@pytest.mark.parametrize('bytewise', [False, True])
@pytest.mark.parametrize('execution', [False, True])
def test_perf_console_handles_serial_chunks(bytewise, execution):
    parser = board_check.PerfConsole()
    text = summary(execution=execution)
    for chunk in text if bytewise else [text]:
        parser.feed(chunk)
    assert len(parser.snapshots) == 1
    assert parser.snapshots[0]['header']['uptime_ms'] == 5000
    assert parser.snapshots[0]['stack tx']['min_unused'] == 680
    if execution:
        assert parser.snapshots[0]['estimator_predict']['mean_ns'] == 200
        assert parser.snapshots[0]['estimator_correct'] == {'n': 0}


def test_perf_console_discards_incomplete_summary_before_next_header():
    parser = board_check.PerfConsole()
    parser.feed(summary(5000, omit='PERF processing:'))
    assert not parser.snapshots
    parser.feed(summary(10000))
    assert len(parser.snapshots) == 1
    assert parser.snapshots[0]['header']['uptime_ms'] == 10000


def test_perf_console_keeps_stack_query_errors_visible():
    parser = board_check.PerfConsole()
    parser.feed(summary().replace('PERF stack tx: unused=700 min_unused=680',
                                  'PERF stack tx: unavailable=-134'))
    assert parser.snapshots[0]['stack tx'] == {'unavailable': -134}


@pytest.mark.parametrize('message', ['ASSERTION FAIL', '***** MPU FAULT *****', '--- 5 messages dropped ---'])
def test_perf_console_flags_invalid_evidence(message):
    parser = board_check.PerfConsole()
    parser.feed(message + '\n')
    assert parser.faults == [message]


def test_perf_console_counts_boots_and_percentile_rank():
    parser = board_check.PerfConsole()
    parser.feed('FCC ready: USART6 RX/TX initialized\n' * 2)
    assert parser.boots == 2
    assert board_check.percentile([], .5) is None
    assert board_check.percentile(list(range(1, 101)), .5) == 50
    assert board_check.percentile(list(range(1, 101)), .99) == 99


def test_rtt_summary_keeps_host_gap_informational():
    result = board_check.timing_summary([6., 7., 35.], [0., .02, .157], 1., 5)
    assert result['host_max_send_gap_ms'] == pytest.approx(137.)
    assert result['actual_measurement_hz'] == 3
    assert result['host_pacing_is_acceptance_gate'] is False
    assert result['pc_latency_ms'] == {'count': 3, 'p50': 7., 'p95': 35., 'p99': 35., 'max': 35.}
    assert [ok for _, ok in board_check.rtt_budget_checks(result)] == [False, True]
    assert [ok for _, ok in board_check.rtt_budget_checks(result, 35., 35.)] == [True, True]
    assert [ok for _, ok in board_check.rtt_budget_checks(result, 34., 34.)] == [False, False]


def test_empty_rtt_cannot_pass_an_explicit_budget():
    result = board_check.timing_summary([], [], 10., 0)
    assert result['pc_latency_ms']['max'] is None
    assert [ok for _, ok in board_check.rtt_budget_checks(result, 10., 20.)] == [False, False]


def test_default_rtt_budgets_include_boundaries_and_reject_excess():
    def verdict(values):
        result = board_check.timing_summary(values, [0., .137], 1., 0)
        return [ok for _, ok in board_check.rtt_budget_checks(result)]
    assert verdict([20.] * 99 + [100.]) == [True, True]
    assert verdict([20.001] * 99 + [100.]) == [False, True]
    assert verdict([20.] * 99 + [100.001]) == [True, False]
