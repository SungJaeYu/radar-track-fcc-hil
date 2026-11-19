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


@pytest.mark.parametrize('field,low,high', [
    ('initial_range_m', 1, 65535), ('sample_rate_hz', 1, 50), ('azimuth_deg', -180, 180),
])
def test_settings_closed_bounds_and_adjacent_floats(field, low, high):
    from dataclasses import replace
    for value in (low, math.nextafter(low, math.inf), (low + high) / 2,
                  math.nextafter(high, -math.inf), high):
        replace(Settings(), **{field: value}).validate()
    for value in (math.nextafter(low, -math.inf), math.nextafter(high, math.inf)):
        with pytest.raises(ValueError):
            replace(Settings(), **{field: value}).validate()


@pytest.mark.parametrize('field', ['initial_range_m', 'range_rate_mps', 'azimuth_deg',
    'range_noise_std_m', 'azimuth_noise_std_deg', 'sample_rate_hz', 'duration_s'])
@pytest.mark.parametrize('value', [math.nan, math.inf, -math.inf])
def test_settings_nonfinite_input_rejected(field, value):
    from dataclasses import replace
    with pytest.raises(ValueError):
        replace(Settings(), **{field: value}).validate()


@pytest.mark.parametrize('field', ['range_noise_std_m', 'azimuth_noise_std_deg'])
def test_noise_lower_boundary(field):
    from dataclasses import replace
    for value in (0, math.nextafter(0, math.inf), 30):
        replace(Settings(), **{field: value}).validate()
    with pytest.raises(ValueError):
        replace(Settings(), **{field: math.nextafter(0, -math.inf)}).validate()


@pytest.mark.parametrize('value,valid', [
    (-1, False), (0, False), (math.nextafter(0, math.inf), True),
    (math.nextafter(3600, 0), True), (3600, True), (math.nextafter(3600, math.inf), False),
])
def test_duration_open_lower_and_closed_upper_bound(value, valid):
    settings = Settings(duration_s=value)
    if valid:
        settings.validate()
    else:
        with pytest.raises(ValueError):
            settings.validate()


@pytest.mark.parametrize('distance', [1, 2, 65534, 65535])
def test_sample_wire_range_endpoints(distance):
    sample = make_sample(Settings(initial_range_m=distance, range_rate_mps=0,
                                  range_noise_std_m=0), 0, random.Random(0))
    assert sample.measured_range_m == distance


@pytest.mark.parametrize('delta,gaps,discontinuities', [
    (1, 0, 0), (0x7fffffff, 0x7ffffffe, 0), (0x80000000, 0, 1), (0x80000001, 0, 1),
])
def test_sequence_half_range_boundary(delta, gaps, discontinuities):
    from pc_sim.gui_model import TrackMonitor
    monitor = TrackMonitor()
    monitor.receive(track_report(0), 1)
    monitor.receive(track_report(delta), 2)
    assert (monitor.sequence_gaps, monitor.discontinuities) == (gaps, discontinuities)
