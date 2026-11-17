"""Single-target samples and board report display policy, independent of Qt."""
from collections import deque
from dataclasses import dataclass
import math
import random
import struct

from .protocol import TrackReport, build_measurement_frame_from_radians


@dataclass(frozen=True)
class Settings:
    initial_range_m: float = 1000.0
    range_rate_mps: float = -20.0
    azimuth_deg: float = 15.0
    range_noise_std_m: float = 5.0
    sample_rate_hz: float = 10.0
    duration_s: float = 30.0
    seed: int = 42

    def validate(self):
        values = (self.initial_range_m, self.range_rate_mps, self.azimuth_deg,
                  self.range_noise_std_m, self.sample_rate_hz, self.duration_s)
        if not all(math.isfinite(v) for v in values):
            raise ValueError('설정값은 유한한 숫자여야 합니다.')
        if not 1 <= self.initial_range_m <= 65535 or self.range_noise_std_m < 0:
            raise ValueError('거리는 1..65535 m, 잡음은 0 이상이어야 합니다.')
        if not 1 <= self.sample_rate_hz <= 50 or not 0 < self.duration_s <= 3600:
            raise ValueError('송신 주기는 1..50 Hz, 실행 시간은 0..3600초입니다.')
        if not -180 <= self.azimuth_deg <= 180:
            raise ValueError('방위각은 -180..180도입니다.')


@dataclass(frozen=True)
class Sample:
    timestamp_ms: int
    truth_range_m: float
    measured_range_m: int
    azimuth_rad: float
    frame: bytes


def make_sample(settings, timestamp_ms, rng: random.Random):
    truth = settings.initial_range_m + settings.range_rate_mps * timestamp_ms / 1000
    if not 1 <= truth <= 65535:
        raise ValueError('실제 거리가 1..65535 m 범위를 벗어나 송신을 중지했습니다.')
    frame = build_measurement_frame_from_radians(
        timestamp_ms, truth + rng.gauss(0, settings.range_noise_std_m),
        math.radians(settings.azimuth_deg))
    _, measured, semicircle = struct.unpack('>IHi', frame[3:-2])
    return Sample(timestamp_ms, truth, measured, semicircle * math.pi / 2**31, frame)


class TrackMonitor:
    """Bounded report history and PC reception freshness, not board track lifetime."""

    STALE_AFTER_S = 2.0

    def __init__(self):
        self.reset()

    def reset(self):
        """Start a new PC display session without sending a firmware command."""
        self.reports = deque(maxlen=2000)
        self.latest = None
        self.received_at = None
        self.sequence_gaps = 0
        self.discontinuities = 0

    def receive(self, report: TrackReport, received_at: float) -> bool:
        """Ignore duplicates; count forward gaps modulo uint32.

        A backwards/ambiguous jump may mean reboot or reordered input. Start a
        new history segment without claiming a reset cause or a huge loss count.
        """
        if self.latest is not None:
            delta = (report.report_sequence - self.latest.report_sequence) & 0xffffffff
            if delta == 0:
                return False
            if delta < 0x80000000:
                self.sequence_gaps += delta - 1
            else:
                self.discontinuities += 1
                self.reports.clear()
        self.latest = report
        self.received_at = received_at
        self.reports.append(report)
        return True

    def is_stale(self, now: float) -> bool:
        return self.received_at is None or now - self.received_at >= self.STALE_AFTER_S

    def range_history(self, samples):
        """Align estimates with retained PC sensor timestamps, never receive time.

        Invalid or unmatched reports break the curve instead of plotting zero or
        attaching a report to the most recently generated measurement.
        """
        timestamps = {sample.timestamp_ms for sample in samples}
        times, ranges = [], []
        for report in self.reports:
            if report.estimate_valid and report.estimate_timestamp_ms in timestamps:
                times.append(report.estimate_timestamp_ms / 1000)
                ranges.append(report.range_m)
            elif times:
                times.append(times[-1])
                ranges.append(math.nan)
        return times, ranges
