"""Noise plans, input boundaries and serial lifecycle; no fabricated HIL results."""
import json
import math
import random
import struct
from dataclasses import replace

import pytest

from pc_sim import evaluate, noise_sweep
from pc_sim.gui_model import Settings, make_sample
from pc_sim.protocol import encode_frame


@pytest.mark.parametrize('kwargs', [
    {'scenario': 'unknown'}, {'count': 2}, {'count': 0}, {'count': -1},
    {'count': 3.5}, {'count': True}, {'hz': 0}, {'hz': .999}, {'hz': 50.001},
    {'hz': math.nan}, {'hz': math.inf}, {'hz': -math.inf},
    {'warmup_ms': -1}, {'warmup_ms': math.nan}, {'warmup_ms': math.inf},
    {'warmup_ms': 0.5}, {'warmup_ms': 11901},
    {'range_noise_std_m': -0.001}, {'range_noise_std_m': math.inf},
    {'range_noise_std_m': math.nan}, {'azimuth_noise_std_deg': -0.001},
    {'azimuth_noise_std_deg': math.nan}, {'azimuth_noise_std_deg': math.inf},
    {'seed': 1.5}, {'seed': None},
    {'scenario': 'dropout', 'count': 89}, {'scenario': 'dropout', 'hz': 4.999},
    {'count': 10000},  # Generated trajectory goes out of wire range.
])
def test_invalid_run_is_rejected_before_uart_or_files(tmp_path, monkeypatch, kwargs):
    monkeypatch.setattr(evaluate.serial, 'Serial', lambda *a, **k: pytest.fail('UART opened'))
    options = {'scenario': 'noisy', **kwargs}
    with pytest.raises(ValueError):
        evaluate.run_scenario('unused', output=tmp_path, **options)
    assert not list(tmp_path.iterdir())


@pytest.mark.parametrize('hz,count,warmup', [(1, 3, 2000), (50, 3, 40), (10, 120, 11900)])
def test_run_accepts_exact_rate_count_and_warmup_boundaries(hz, count, warmup):
    settings = evaluate.scenario_settings('normal', count, hz, warmup)
    samples = evaluate.scenario_samples(settings, count)
    assert len(samples) == count
    assert samples[-1].timestamp_ms == warmup


@pytest.mark.parametrize('hz,count', [(5, 45), (10, 90), (50, 450)])
def test_dropout_accepts_exact_minimum_duration(hz, count):
    evaluate.scenario_settings('dropout', count, hz)


def test_default_plan_covers_each_axis_and_seed_without_repeating_baseline():
    plan = noise_sweep.make_plan()
    assert len(plan) == 100
    assert {(p['range_noise_std_m'], p['azimuth_noise_std_deg']) for p in plan} == {
        (0, 0), (2, 0), (5, 0), (8, 0), (15, 0), (30, 0),
        (0, .1), (0, .5), (0, 1), (0, 3)}
    for pair in {(p['range_noise_std_m'], p['azimuth_noise_std_deg']) for p in plan}:
        assert {p['seed'] for p in plan if (p['range_noise_std_m'], p['azimuth_noise_std_deg']) == pair} == set(range(10))


@pytest.mark.parametrize('kwargs', [
    {'range_levels': []}, {'angle_levels': []}, {'seeds': []},
    {'range_levels': [-1]}, {'range_levels': [math.inf]},
    {'angle_levels': [math.nan]}, {'angle_levels': [-1]}, {'seeds': [1.5]},
    {'range_levels': [1e308]},
])
def test_invalid_sweep_plan(kwargs):
    with pytest.raises(ValueError):
        noise_sweep.make_plan(**kwargs)


def test_plan_deduplicates_repeated_levels_and_seeds():
    assert len(noise_sweep.make_plan([0, 0, 2], [0, 0], [42, 42])) == 2


def test_legacy_seed_sequence_and_new_angular_noise():
    original = evaluate.scenario_settings('noisy')
    rng = random.Random(42)
    expected_ranges = [round(1000 - .5 * i + rng.gauss(0, 8)) for i in range(120)]
    assert [s.measured_range_m for s in evaluate.scenario_samples(original, 120)] == expected_ranges
    angular = replace(original, range_noise_std_m=0, azimuth_noise_std_deg=1)
    first = evaluate.scenario_samples(angular, 120)
    assert first == evaluate.scenario_samples(angular, 120)
    assert first != evaluate.scenario_samples(replace(angular, seed=43), 120)
    assert len({s.azimuth_rad for s in first}) > 100
    # Truth stays fixed; angular noise affects only the transmitted measurement.
    assert angular.azimuth_deg == 15


def test_angle_noise_wraps_across_pi_without_clipping():
    class FixedNoise:
        def gauss(self, mean, std):
            return std
    sample = make_sample(Settings(azimuth_deg=179, range_noise_std_m=0,
                                  azimuth_noise_std_deg=3), 0, FixedNoise())
    assert sample.azimuth_rad == pytest.approx(math.radians(-178), abs=2e-9)


def fake_uart(monkeypatch, short_write=False):
    clock = [0.0]
    class UART:
        def __init__(self, *args, **kwargs):
            self.pending = bytearray()
            self.sequence = 0
            self.closed = False
            self.writes = []
        def __enter__(self):
            return self
        def __exit__(self, *args):
            self.closed = True
        @property
        def in_waiting(self):
            return len(self.pending)
        def write(self, frame):
            self.writes.append(frame)
            if short_write:
                return len(frame) - 1
            kind = frame[2]
            if kind in (1, 5):
                payload = struct.pack('>IIIffffBBBB', self.sequence, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0)
            elif kind == 2:
                stamp, distance, angle = struct.unpack('>IHi', frame[3:-2])
                payload = struct.pack('>IIIffffBBBB', self.sequence, stamp, stamp,
                                      distance, angle * math.pi / 2**31, -5, 0, 2, 1, 3, 0)
            elif kind == 3:
                self.pending.extend(encode_frame(6, bytes(32)))
                return len(frame)
            self.sequence += 1
            self.pending.extend(encode_frame(7, payload))
            return len(frame)
        def read(self, n):
            clock[0] += .01
            raw = bytes(self.pending[:n])
            del self.pending[:n]
            return raw
    uart = UART()
    monkeypatch.setattr(evaluate.serial, 'Serial', lambda *a, **k: uart)
    monkeypatch.setattr(evaluate.time, 'monotonic', lambda: clock[0])
    return uart


def test_run_writes_seed_noise_metadata_and_evaluates_matching_responses(tmp_path, monkeypatch):
    uart = fake_uart(monkeypatch)
    path, result = evaluate.run_scenario('mock', 'noisy', tmp_path, count=3, hz=50, warmup_ms=0,
                                        range_noise_std_m=2, azimuth_noise_std_deg=.5, seed=7)
    settings = json.loads((path / 'metadata.json').read_text())['settings']
    assert settings['seed'] == 7 and settings['azimuth_noise_std_deg'] == .5
    assert settings['range_noise_std_m'] == 2
    assert result['matched_input_reports'] == result['matched_estimates'] == 3
    assert result['missing_input_reports'] == result['duplicate_input_reports'] == 0
    assert uart.closed


def test_short_write_closes_uart_and_preserves_partial_session(tmp_path, monkeypatch):
    uart = fake_uart(monkeypatch, short_write=True)
    with pytest.raises(OSError, match='Short UART write'):
        evaluate.run_scenario('mock', 'normal', tmp_path, count=3, warmup_ms=0)
    assert uart.closed
    folder, = tmp_path.iterdir()
    assert not (folder / 'evaluation.json').exists()
    assert json.loads((folder / 'metadata.json').read_text())['closed']


def test_dropout_transmits_both_interval_boundaries_correctly(tmp_path, monkeypatch):
    uart = fake_uart(monkeypatch)
    _, result = evaluate.run_scenario('mock', 'dropout', tmp_path, count=90, warmup_ms=0)
    stamps = [struct.unpack('>I', f[3:7])[0] for f in uart.writes if f[2] == 2]
    assert 3900 in stamps and 4000 not in stamps and 5400 not in stamps and 5500 in stamps
    assert result['sent_samples'] == 75


def test_sweep_records_failure_without_polluting_success_summary(tmp_path, monkeypatch):
    plan = noise_sweep.make_plan([0], [0], [0, 1])
    def fake_run(port, output, **case):
        folder = output / str(case['seed'])
        folder.mkdir()
        result = dict.fromkeys(('missing_input_reports', 'duplicate_input_reports',
                               'unmatched_input_reports', 'sequence_gaps',
                               'sequence_discontinuities', 'time_rejected'), 0)
        result.update(recording_closed=True, range_estimate_rmse_m=2 if case['seed'] == 0 else 100)
        result['missing_input_reports'] = case['seed']
        return folder, result
    monkeypatch.setattr(noise_sweep, 'run_scenario', fake_run)
    path, document = noise_sweep.run_sweep('mock', tmp_path, plan)
    row, = document['summary']
    assert document['complete'] and row['failed_runs'] == row['recorded_runs'] == 1
    assert row['metrics']['range_estimate_rmse_m'] == {'count': 1, 'mean': 2, 'min': 2, 'max': 2}
    assert json.loads((path / 'sweep.json').read_text())['runs'][1]['result']['range_estimate_rmse_m'] == 100


def test_sweep_aborts_io_failure_but_keeps_manifest(tmp_path, monkeypatch):
    def fail(*args, **kwargs):
        raise OSError('Disconnected')
    monkeypatch.setattr(noise_sweep, 'run_scenario', fail)
    with pytest.raises(OSError):
        noise_sweep.run_sweep('mock', tmp_path, noise_sweep.make_plan([0], [0], [0, 1]))
    folder, = tmp_path.iterdir()
    document = json.loads((folder / 'sweep.json').read_text())
    assert not document['complete'] and len(document['runs']) == 1
    assert document['runs'][0]['error'] == 'Disconnected'


def test_dry_run_has_no_uart_or_file_side_effects(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(evaluate.serial, 'Serial', lambda *a, **k: pytest.fail('UART opened'))
    noise_sweep.main(['--dry-run', '--output', str(tmp_path), '--seeds', '42'])
    assert json.loads(capsys.readouterr().out)['run_count'] == 10
    assert not list(tmp_path.iterdir())


def test_spike_uart_records_actual_injection_and_offline_recovery(tmp_path, monkeypatch):
    from pc_sim.outliers import SpikeSpec
    from pc_sim.recording import evaluate_session
    uart = fake_uart(monkeypatch)
    spec = SpikeSpec(start_index=5, length=2, range_offset_m=500)
    path, result = evaluate.run_scenario('mock', 'spike', tmp_path, count=15, warmup_ms=0, spike=spec)
    distances = [struct.unpack('>IHi', f[3:-2])[1] for f in uart.writes if f[2] == 2]
    assert distances == [1000] * 5 + [1500] * 2 + [1000] * 8
    assert json.loads((path / 'metadata.json').read_text())['spike']['range_offset_m'] == 500
    assert result['spike']['pre_spike_settled'] and result['spike']['recovered']
    assert result['spike']['recovery_delay_sensor_ms'] == 0
    assert result['spike']['peak_range_error_m'] == 500  # Mock echoes, not a Kalman result.
    assert result['spike'] == evaluate_session(path, warmup_ms=0)['spike']
    assert uart.closed
