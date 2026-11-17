"""GUI sample generation uses the existing wire contract."""
import math
import random
import struct

import pytest
from pc_sim.gui_model import Settings, make_sample


def test_gui_sample_matches_wire_and_motion():
    settings = Settings(range_noise_std_m=0, azimuth_deg=-90)
    settings.validate()
    sample = make_sample(settings, 200, random.Random(42))
    assert sample.truth_range_m == 996
    assert struct.unpack('>IHi', sample.frame[3:-2]) == (200, 996, -1073741824)
    assert sample.measured_range_m == 996


@pytest.mark.parametrize('settings', [Settings(sample_rate_hz=0), Settings(range_noise_std_m=-1),
    Settings(initial_range_m=float('nan')), Settings(duration_s=0), Settings(azimuth_deg=181)])
def test_gui_rejects_invalid_settings(settings):
    with pytest.raises(ValueError):
        settings.validate()


def test_gui_stops_out_of_range_instead_of_wrapping():
    with pytest.raises(ValueError):
        make_sample(Settings(initial_range_m=1, range_rate_mps=-20), 100, random.Random(42))


def track_report(sequence, input_ms=100, estimate_ms=100, state=2, result=1, valid=True):
    from pc_sim.protocol import decode_track_payload
    return decode_track_payload(struct.pack(
        '>IIIffffBBBB', sequence, input_ms, estimate_ms if valid else 0,
        1000 if valid else 0, 0, -20 if valid else 0, 0, state, result, 3 if valid else 1, 0))


def test_track_monitor_freshness_duplicates_wrap_and_discontinuity():
    from pc_sim import gui_model
    monitor = gui_model.TrackMonitor()
    assert monitor.is_stale(0)
    assert monitor.receive(track_report(0xfffffffe), 10)
    assert not monitor.is_stale(11.999)
    assert monitor.is_stale(12)
    assert not monitor.receive(track_report(0xfffffffe), 12)
    assert monitor.is_stale(12)  # Duplicates must not make old data look fresh.
    assert monitor.receive(track_report(1), 13)
    assert monitor.sequence_gaps == 2  # Missing ffffffff and 0 across uint32 wrap.
    assert monitor.receive(track_report(0), 14)
    assert monitor.discontinuities == 1
    assert len(monitor.reports) == 1
    assert monitor.latest.report_sequence == 0
    monitor.reset()
    assert monitor.latest is None and monitor.sequence_gaps == 0


def test_track_history_uses_estimate_time_and_breaks_on_invalid_report():
    from pc_sim import gui_model
    monitor = gui_model.TrackMonitor()
    samples = [make_sample(Settings(range_noise_std_m=0), stamp, random.Random(42))
               for stamp in (100, 200, 300)]
    monitor.receive(track_report(0, input_ms=200, estimate_ms=100, result=3), 1)
    # An estimate outside this PC run is shown in diagnostics, not paired with truth.
    monitor.receive(track_report(1, input_ms=999, estimate_ms=999), 2)
    monitor.receive(track_report(2, input_ms=300, state=0, result=4, valid=False), 3)
    times, ranges = monitor.range_history(samples)
    assert times[0] == 0.1 and ranges[0] == 1000
    assert 0.999 not in times
    assert math.isnan(ranges[-1])
    assert not monitor.latest.estimate_valid


def test_track_monitor_bounds_history():
    from pc_sim import gui_model
    monitor = gui_model.TrackMonitor()
    for sequence in range(2100):
        monitor.receive(track_report(sequence), sequence / 10)
    assert len(monitor.reports) == 2000
