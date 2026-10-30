"""Single-target GUI settings and wire samples, independent of Qt."""
from dataclasses import dataclass
import math
import random
import struct

from .protocol import build_radar_frame_from_radians


@dataclass(frozen=True)
class Settings:
    range_m: float = 1000.0
    rate_mps: float = -20.0
    azimuth_deg: float = 15.0
    noise_m: float = 5.0
    hz: float = 10.0
    duration_s: float = 30.0
    seed: int = 42

    def validate(self):
        values = (self.range_m, self.rate_mps, self.azimuth_deg,
                  self.noise_m, self.hz, self.duration_s)
        if not all(math.isfinite(v) for v in values):
            raise ValueError('설정값은 유한한 숫자여야 합니다.')
        if not 1 <= self.range_m <= 65535 or self.noise_m < 0:
            raise ValueError('거리는 1..65535 m, 잡음은 0 이상이어야 합니다.')
        if not 1 <= self.hz <= 50 or not 0 < self.duration_s <= 3600:
            raise ValueError('송신 주기는 1..50 Hz, 실행 시간은 0..3600초입니다.')
        if not -180 <= self.azimuth_deg <= 180:
            raise ValueError('방위각은 -180..180도입니다.')


@dataclass(frozen=True)
class Sample:
    timestamp_ms: int
    truth_m: float
    measured_m: int
    azimuth_rad: float
    frame: bytes


def make_sample(settings, timestamp_ms, rng: random.Random):
    truth = settings.range_m + settings.rate_mps * timestamp_ms / 1000
    if not 1 <= truth <= 65535:
        raise ValueError('실제 거리가 1..65535 m 범위를 벗어나 송신을 중지했습니다.')
    frame = build_radar_frame_from_radians(
        timestamp_ms, truth + rng.gauss(0, settings.noise_m),
        math.radians(settings.azimuth_deg))
    _, measured, semicircle = struct.unpack('>IHi', frame[3:-1])
    return Sample(timestamp_ms, truth, measured, semicircle * math.pi / 2**31, frame)
