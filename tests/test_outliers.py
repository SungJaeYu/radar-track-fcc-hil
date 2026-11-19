"""Spikes change measurements, not truth; missing data must never look like recovery."""
from dataclasses import asdict, replace
import json
import math

import pytest

from pc_sim import spike_sweep
from pc_sim.evaluate import scenario_settings, scenario_samples, run_scenario
from pc_sim.outliers import SpikeSpec, inject_spike, analyze_spike


def fixture_rows(spec=SpikeSpec(start_index=5), count=15):
    samples, reports = [], []
    for index in range(count):
        stamp = index * 100
        samples.append({'timestamp_ms': stamp, 'truth_range_m': 1000, 'truth_azimuth_rad': 0})
        reports.append({'input_timestamp_ms': stamp, 'estimate_timestamp_ms': stamp,
                        'range_m': 1000, 'azimuth_rad': 0, 'flags': 3, 'state': 2, 'result': 1})
    return samples, reports


@pytest.mark.parametrize('axis,offset', [('range', 100), ('range', -999), ('range', 64535),
                                       ('angle', 10), ('angle', -10), ('angle', 180)])
@pytest.mark.parametrize('length', [1, 5])
def test_injection_preserves_truth_timestamps_and_clean_samples(axis, offset, length):
    original = scenario_samples(scenario_settings('spike'), 120)
    spec = SpikeSpec(length=length, range_offset_m=offset if axis == 'range' else 0,
                     azimuth_offset_deg=offset if axis == 'angle' else 0)
    result = inject_spike(original, spec)
    for i, (before, after) in enumerate(zip(original, result)):
        assert (before.truth_range_m, before.timestamp_ms) == (after.truth_range_m, after.timestamp_ms)
        if 40 <= i < 40 + length:
            assert before.frame != after.frame
            if axis == 'range':
                assert after.measured_range_m == 1000 + offset
            else:
                expected = (offset + 180) % 360 - 180
                assert math.degrees(after.azimuth_rad) == pytest.approx(expected, abs=1e-6)
        else:
            assert before == after


def test_control_case_is_byte_identical():
    samples = scenario_samples(scenario_settings('spike'), 120)
    assert inject_spike(samples, SpikeSpec()) == samples


@pytest.mark.parametrize('kwargs', [
    {'start_index': 2}, {'start_index': 1.5}, {'start_index': 19},
    {'length': 0}, {'length': -1}, {'length': 1.5}, {'length': 78},
    {'recovery_hold': 0}, {'recovery_hold': 1.5}, {'recovery_range_m': -1},
    {'recovery_azimuth_deg': -1}, {'recovery_azimuth_deg': 180.01},
    {'range_offset_m': math.nan}, {'range_offset_m': math.inf},
    {'azimuth_offset_deg': -180.01}, {'azimuth_offset_deg': 180.01},
    {'azimuth_offset_deg': math.nan}, {'azimuth_offset_deg': math.inf},
    {'recovery_range_m': math.inf}, {'recovery_azimuth_deg': math.nan},
])
def test_invalid_spec_rejected_before_side_effects(tmp_path, monkeypatch, kwargs):
    from pc_sim import evaluate
    monkeypatch.setattr(evaluate.serial, 'Serial', lambda *a, **k: pytest.fail('UART opened'))
    with pytest.raises(ValueError):
        run_scenario('unused', 'spike', tmp_path, spike=replace(SpikeSpec(), **kwargs))
    assert not list(tmp_path.iterdir())


@pytest.mark.parametrize('offset', [-1000, 64536])
def test_out_of_wire_range_spike_is_not_clipped(tmp_path, monkeypatch, offset):
    from pc_sim import evaluate
    monkeypatch.setattr(evaluate.serial, 'Serial', lambda *a, **k: pytest.fail('UART opened'))
    with pytest.raises(ValueError):
        run_scenario('unused', 'spike', tmp_path, spike=SpikeSpec(range_offset_m=offset))
    assert not list(tmp_path.iterdir())


def test_plan_has_control_both_signs_single_burst_and_wire_extremes():
    plan = spike_sweep.make_plan()
    assert len(plan) == 49 and plan[0]['axis'] == 'control'
    assert {p['spike']['length'] for p in plan} == {1, 5}
    assert {p['spike']['range_offset_m'] for p in plan if p['axis'] == 'range'} == set(spike_sweep.RANGE_OFFSETS)
    assert {p['spike']['azimuth_offset_deg'] for p in plan if p['axis'] == 'azimuth'} == set(spike_sweep.ANGLE_OFFSETS)


@pytest.mark.parametrize('changes', [{'lengths': []}, {'range_offsets': []}, {'angle_offsets': []},
    {'count': 43}, {'start_index': 39, 'hz': 50}, {'range_offsets': [-1000]}])
def test_plan_preflight_rejects_bad_cases(changes):
    with pytest.raises(ValueError):
        spike_sweep.make_plan(**changes)


def test_recovery_is_sustained_and_uses_sensor_time_not_warmup():
    spec = SpikeSpec(start_index=5, recovery_hold=3)
    samples, reports = fixture_rows(spec)
    reports[5]['range_m'] = 1080
    reports[6]['range_m'] = 1010
    reports[7]['range_m'] = 1004
    result = analyze_spike(samples, reports, asdict(spec))
    assert result['pre_spike_settled'] and result['recovered']
    assert result['peak_range_error_m'] == 80
    assert result['clean_samples_to_recovery'] == 3
    assert result['recovery_delay_sensor_ms'] == 200
    assert result['first_recovered_sensor_ms'] == 800
    assert result['confirmation_sensor_ms'] == 1000


@pytest.mark.parametrize('value,recovered', [(1001.999, True), (1002, True), (1002.001, False)])
def test_recovery_error_tolerance_boundary(value, recovered):
    spec = SpikeSpec(start_index=5)
    samples, reports = fixture_rows(spec)
    for report in reports[6:]:
        report['range_m'] = value
    result = analyze_spike(samples, reports, asdict(spec))
    assert result['recovered'] == recovered
    if not recovered:
        assert result['recovery_delay_sensor_ms'] is None


@pytest.mark.parametrize('failure', ['missing', 'duplicate', 'invalid', 'old_time', 'coasting', 'rejected', 'nan'])
def test_bad_sample_breaks_recovery_streak(failure):
    spec = SpikeSpec(start_index=5, recovery_hold=3)
    samples, reports = fixture_rows(spec, count=12)
    if failure == 'missing':
        del reports[7]
    elif failure == 'duplicate':
        reports.append(dict(reports[7]))
    elif failure == 'invalid':
        reports[7]['flags'] = 1
    elif failure == 'old_time':
        reports[7]['estimate_timestamp_ms'] = 600
    elif failure == 'coasting':
        reports[7]['state'] = 3
    elif failure == 'rejected':
        reports[7]['result'] = 2
    elif failure == 'nan':
        reports[7]['range_m'] = math.nan
    result = analyze_spike(samples, reports, asdict(spec))
    assert result['first_recovered_sensor_ms'] == 800
    assert result['confirmation_sensor_ms'] == 1000


def test_reacquisition_start_does_not_hide_recovery_delay():
    spec = SpikeSpec(start_index=5, length=5)
    samples, reports = fixture_rows(spec, count=20)
    for i in range(5, 10):
        reports[i].update(result=2, state=3)
    reports[9].update(flags=1, state=4)
    reports[10].update(flags=1, state=0, result=4)
    reports[11].update(state=1, result=0)
    reports[12].update(state=1)
    result = analyze_spike(samples, reports, asdict(spec))
    assert result['injected_gate_rejections'] == 5
    assert result['unavailable_estimates_during_or_after_spike'] == 2
    assert result['first_recovered_sensor_ms'] == 1300
    assert result['clean_samples_to_recovery'] == 4


def test_precondition_is_not_assumed_from_later_recovery():
    samples, reports = fixture_rows()
    reports[3]['state'] = 1
    result = analyze_spike(samples, reports, asdict(SpikeSpec(start_index=5)))
    assert not result['pre_spike_settled'] and result['recovered']


def test_angular_error_is_wrapped_before_recovery_decision():
    samples, reports = fixture_rows()
    for sample in samples:
        sample['truth_azimuth_rad'] = math.radians(179.95)
    for report in reports:
        report['azimuth_rad'] = math.radians(-179.95)
    result = analyze_spike(samples, reports, asdict(SpikeSpec(start_index=5)))
    assert result['pre_spike_settled'] and result['recovered']
    assert result['peak_azimuth_error_deg'] == pytest.approx(.1)


def test_insufficient_tail_does_not_pass_recovery():
    samples, reports = fixture_rows(count=8)
    with pytest.raises(ValueError, match='Incomplete'):
        analyze_spike(samples, reports, asdict(SpikeSpec(start_index=5)))


def test_dry_run_has_no_files_or_uart(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(spike_sweep, 'run_suite', lambda *a, **k: pytest.fail('Suite executed'))
    spike_sweep.main(['--dry-run', '--output', str(tmp_path)])
    assert json.loads(capsys.readouterr().out)['run_count'] == 49
    assert not list(tmp_path.iterdir())


def test_suite_preserves_recovery_failures_and_continues(tmp_path, monkeypatch):
    plan = spike_sweep.make_plan(range_offsets=[100], angle_offsets=[10], lengths=[1])
    def mock_run(port, scenario, output, count, hz, spike):
        folder = output / str(len(list(output.iterdir())))
        folder.mkdir()
        result = dict.fromkeys(('missing_input_reports', 'duplicate_input_reports', 'unmatched_input_reports',
                                'sequence_gaps', 'sequence_discontinuities', 'time_rejected'), 0)
        result.update(recording_closed=True, spike={'pre_spike_settled': True,
                      'recovered': spike.range_offset_m == 0, 'first_spike_result': 1})
        return folder, result
    monkeypatch.setattr(spike_sweep, 'run_scenario', mock_run)
    path, document = spike_sweep.run_suite('mock', tmp_path, plan)
    assert document['complete'] and document['failed_runs'] == 1 and document['passed_runs'] == 2
    assert not document['runs'][1]['result']['spike']['recovered']
    assert (path / 'spikes.json').is_file()


def test_suite_io_error_saves_partial_manifest(tmp_path, monkeypatch):
    def fail(*a, **k):
        raise OSError('Disconnected')
    monkeypatch.setattr(spike_sweep, 'run_scenario', fail)
    with pytest.raises(OSError):
        spike_sweep.run_suite('mock', tmp_path, spike_sweep.make_plan())
    folder, = tmp_path.iterdir()
    document = json.loads((folder / 'spikes.json').read_text())
    assert not document['complete'] and document['failed_runs'] == 1
    assert len(document['runs']) == 1


def test_suite_detects_wrong_first_gate_result(tmp_path, monkeypatch):
    plan = spike_sweep.make_plan(range_offsets=[500], angle_offsets=[45], lengths=[1])
    def mock_run(port, scenario, output, count, hz, spike):
        folder = output / str(len(list(output.iterdir())))
        folder.mkdir()
        result = dict.fromkeys(('missing_input_reports', 'duplicate_input_reports', 'unmatched_input_reports',
                                'sequence_gaps', 'sequence_discontinuities', 'time_rejected'), 0)
        result.update(recording_closed=True, spike={'pre_spike_settled': True,
                      'recovered': True, 'first_spike_result': 1})
        return folder, result
    monkeypatch.setattr(spike_sweep, 'run_scenario', mock_run)
    _, document = spike_sweep.run_suite('mock', tmp_path, plan)
    assert document['passed_runs'] == 1 and document['failed_runs'] == 2
    assert not document['runs'][1]['first_gate_ok']
