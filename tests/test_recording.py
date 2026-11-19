"""Recording and evaluation use sensor-aligned values, never GUI history limits."""
import csv
import json
import math
import random
from dataclasses import replace

import pytest
from pc_sim.gui_model import Settings, make_sample
from pc_sim.protocol import TrackReport, TrackState, TrackReportResult, TrackReportFlag


def report(sequence, stamp, distance, angle=0):
    return TrackReport(sequence, stamp, stamp, distance, angle, 0, 0,
                       TrackState.CONFIRMED, TrackReportResult.UPDATED,
                       TrackReportFlag.INPUT_PRESENT | TrackReportFlag.ESTIMATE_VALID)


def test_record_and_evaluate_alignment_wrap_duplicates_and_invalid(tmp_path):
    from pc_sim.recording import SessionRecorder, evaluate_session
    settings = Settings(initial_range_m=1000, range_rate_mps=0, azimuth_deg=180, range_noise_std_m=0)
    path = tmp_path / 'run'
    recorder = SessionRecorder(path, settings, 'board')
    for stamp in (0, 100, 200):
        sample = make_sample(settings, stamp, random.Random(42))
        recorder.record_sample(replace(sample, measured_range_m=1003), stamp / 1000 + 1)
    recorder.record_report(report(1, 100, 1001, -math.pi + 0.01), 1.15)
    # Rejected input points to the same old estimate and must not bias RMSE.
    recorder.record_report(replace(report(2, 100, 1001, -math.pi + 0.01),
                                   input_timestamp_ms=99, result=TrackReportResult.TIME_REJECTED), 1.16)
    recorder.record_report(report(3, 200, 999, math.pi - 0.01), 1.25)
    recorder.record_report(replace(report(4, 0, 0), state=TrackState.EMPTY,
                                   result=TrackReportResult.RESET, flags=TrackReportFlag(0)), 2.5)
    recorder.record_wire('rx', b'\x7e\x20', 1.1)
    recorder.record_status({'crc_errors': 1}, 1.5)
    recorder.close()
    result = evaluate_session(path, warmup_ms=0)
    assert result['matched_estimates'] == 2
    assert result['range_measurement_rmse_m'] == 3
    assert result['range_estimate_rmse_m'] == 1
    assert result['azimuth_estimate_rmse_rad'] == pytest.approx(0.01)
    assert result['pc_roundtrip_ms']['max'] == pytest.approx(50)
    assert result['invalid_estimates'] == 1
    assert result['duplicate_estimate_times'] == 1
    assert json.loads((path / 'metadata.json').read_text())['closed'] is True
    with (path / 'wire.csv').open() as stream:
        assert list(csv.DictReader(stream))[0]['hex'] == '7e20'


def test_evaluation_excludes_warmup_and_unmatched_reports(tmp_path):
    from pc_sim.recording import SessionRecorder, evaluate_session
    settings = Settings(range_noise_std_m=0)
    with SessionRecorder(tmp_path / 'run', settings, 'board') as recorder:
        recorder.record_sample(make_sample(settings, 0, random.Random(0)), 1)
        recorder.record_report(replace(report(0, 0, 1000), result=TrackReportResult.STARTED), 1.1)
        recorder.record_report(report(1, 100, 998), 1.2)
    result = evaluate_session(tmp_path / 'run')
    assert result['matched_estimates'] == 0
    assert result['range_estimate_rmse_m'] is None
    assert result['unmatched_estimates'] == 1


def test_recorder_no_overwrite_and_preview_not_board_evidence(tmp_path):
    from pc_sim.recording import SessionRecorder, evaluate_session
    path = tmp_path / 'run'
    with SessionRecorder(path, Settings(), 'preview'):
        pass
    with pytest.raises(FileExistsError):
        SessionRecorder(path, Settings(), 'board')
    with pytest.raises(ValueError, match='board'):
        evaluate_session(path)


def test_warmup_restarts_after_track_reacquisition(tmp_path):
    from pc_sim.recording import SessionRecorder, evaluate_session
    settings = Settings(range_noise_std_m=0)
    with SessionRecorder(tmp_path / 'run', settings, 'board') as recorder:
        for sequence, stamp in enumerate((0, 2000, 5500, 7500)):
            sample = make_sample(settings, stamp, random.Random(0))
            recorder.record_sample(sample, stamp / 1000)
            result = TrackReportResult.STARTED if stamp in (0, 5500) else TrackReportResult.UPDATED
            recorder.record_report(replace(report(sequence, stamp, sample.truth_range_m), result=result), stamp / 1000 + .01)
    result = evaluate_session(tmp_path / 'run')
    assert result['warmup_excluded'] == 2
    assert result['matched_estimates'] == 2
    assert result['range_estimate_rmse_m'] == 0


def test_evaluation_cli_rejects_invalid_warmup_before_opening_port(tmp_path, monkeypatch):
    from pc_sim import evaluate
    def unexpected_open(*args, **kwargs):
        pytest.fail('Invalid arguments must not open UART')
    monkeypatch.setattr(evaluate.serial, 'Serial', unexpected_open)
    with pytest.raises(ValueError, match='nonnegative'):
        evaluate.run_scenario('unused', 'normal', tmp_path, warmup_ms=-1)
    assert not list(tmp_path.iterdir())


@pytest.mark.parametrize('stamp,included', [(1999, False), (2000, True), (2001, True)])
def test_warmup_exact_boundary(tmp_path, stamp, included):
    from pc_sim.recording import SessionRecorder, evaluate_session
    settings = Settings(range_noise_std_m=0)
    with SessionRecorder(tmp_path / 'run', settings, 'board') as recorder:
        for sequence, time_ms in enumerate((0, stamp)):
            sample = make_sample(settings, time_ms, random.Random(0))
            recorder.record_sample(sample, time_ms / 1000)
            recorder.record_report(replace(report(sequence, time_ms, sample.truth_range_m),
                result=TrackReportResult.STARTED if sequence == 0 else TrackReportResult.UPDATED),
                time_ms / 1000 + .01)
    assert evaluate_session(tmp_path / 'run')['matched_estimates'] == int(included)


def test_response_coverage_rejections_and_max_error_are_not_hidden_by_rmse(tmp_path):
    from pc_sim.recording import SessionRecorder, evaluate_session
    settings = Settings(initial_range_m=1000, range_rate_mps=0, range_noise_std_m=0, azimuth_deg=0)
    with SessionRecorder(tmp_path / 'run', settings, 'board') as recorder:
        for stamp in (0, 100, 200, 300):
            recorder.record_sample(make_sample(settings, stamp, random.Random(0)), stamp / 1000)
        recorder.record_report(report(0, 0, 1003), .01)
        recorder.record_report(report(1, 100, 996), .11)
        # A rejected measurement repeats the previous estimate, never a new accuracy sample.
        recorder.record_report(replace(report(2, 200, 996), estimate_timestamp_ms=100,
                                      result=TrackReportResult.GATE_REJECTED), .21)
        recorder.record_report(report(3, 100, 996), .22)  # Duplicate input response.
        recorder.record_report(report(4, 999, 1000), 1)  # Foreign response.
    result = evaluate_session(tmp_path / 'run', warmup_ms=0)
    assert result['missing_input_reports'] == 1
    assert result['duplicate_input_reports'] == result['unmatched_input_reports'] == 1
    assert result['gate_rejected'] == 1 and result['gate_rejection_fraction'] == .25
    assert result['estimate_coverage'] == .5
    assert result['range_estimate_rmse_m'] == pytest.approx(math.sqrt(12.5))
    assert result['range_estimate_max_abs_error_m'] == 4


@pytest.mark.parametrize('warmup', [-1, 0.1, math.nan, math.inf])
def test_report_rejects_invalid_warmup(tmp_path, warmup):
    from pc_sim.recording import SessionRecorder, evaluate_session
    with SessionRecorder(tmp_path / 'run', Settings(), 'board'):
        pass
    with pytest.raises(ValueError):
        evaluate_session(tmp_path / 'run', warmup)
