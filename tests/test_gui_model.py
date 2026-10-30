"""GUI sample generation uses the existing wire contract."""
import random
import struct

import pytest
from pc_sim.gui_model import Settings, make_sample


def test_gui_sample_matches_wire_and_motion():
    settings = Settings(noise_m=0, azimuth_deg=-90)
    settings.validate()
    sample = make_sample(settings, 200, random.Random(42))
    assert sample.truth_m == 996
    assert struct.unpack('>IHi', sample.frame[3:-1]) == (200, 996, -1073741824)
    assert sample.measured_m == 996


@pytest.mark.parametrize('settings', [Settings(hz=0), Settings(noise_m=-1),
    Settings(range_m=float('nan')), Settings(duration_s=0), Settings(azimuth_deg=181)])
def test_gui_rejects_invalid_settings(settings):
    with pytest.raises(ValueError):
        settings.validate()


def test_gui_stops_out_of_range_instead_of_wrapping():
    with pytest.raises(ValueError):
        make_sample(Settings(range_m=1, rate_mps=-20), 100, random.Random(42))
