"""
targets.py — 등속 표적 운동 모델 + 레이더 센서 모델

표적 좌표계: 직교 (x=동, y=북), 단위 m / m/s
레이더 출력: 극좌표 (range_m, azimuth_rad, elevation_rad, doppler_mps)
"""

import math
import random
from dataclasses import dataclass, field
from typing import List, Optional

from protocol import MeasPayload


# ── 표적 운동 모델 (등속, CV) ──────────────────────────────────────────────────

@dataclass
class Target:
    """단일 표적. 매 스텝 dt초마다 update()로 위치를 전진시킨다."""
    target_id: int
    x_m:       float   # 초기 x (동)
    y_m:       float   # 초기 y (북)
    vx_mps:    float   # x 속도
    vy_mps:    float   # y 속도

    def update(self, dt: float) -> None:
        self.x_m += self.vx_mps * dt
        self.y_m += self.vy_mps * dt

    @property
    def range_m(self) -> float:
        return math.hypot(self.x_m, self.y_m)

    @property
    def azimuth_rad(self) -> float:
        """레이더 정면(y축)에서 시계방향 양수 (atan2(x, y))."""
        return math.atan2(self.x_m, self.y_m)

    @property
    def elevation_rad(self) -> float:
        """수평면 기준 앙각. 표적이 동고도(z=0)이면 0."""
        return 0.0

    def radial_velocity(self) -> float:
        """시선 방향 속도 (양수=멀어짐, 도플러)."""
        r = self.range_m
        if r < 1e-6:
            return 0.0
        return (self.x_m * self.vx_mps + self.y_m * self.vy_mps) / r


# ── 레이더 센서 모델 ────────────────────────────────────────────────────────────

@dataclass
class RadarSensorModel:
    """
    실제 표적 위치를 받아 노이즈·미탐지·오탐이 섞인 측정값 리스트를 반환한다.

    파라미터:
        sigma_range_m      range 측정 표준편차 (m)
        sigma_azimuth_rad  방위각 측정 표준편차 (rad)
        sigma_doppler_mps  도플러 측정 표준편차 (m/s)
        pd                 탐지확률 (0.0 – 1.0)
        clutter_rate       스캔당 오탐 평균 개수 (Poisson)
        max_range_m        이 거리 초과 표적은 탐지 안 됨
    """
    sigma_range_m:     float = 10.0
    sigma_azimuth_rad: float = 0.01
    sigma_doppler_mps: float = 0.5
    pd:                float = 0.9
    clutter_rate:      float = 0.5
    max_range_m:       float = 30_000.0

    def measure(self,
                targets: List[Target],
                timestamp_ms: int,
                rng: random.Random = random.Random()
                ) -> list:
        """
        (MeasPayload 데이터 딕셔너리) 리스트 반환.
        순서는 랜덤 셔플 (데이터 연관 알고리즘이 순서에 의존 못하도록).
        """
        measurements = []

        for tgt in targets:
            if tgt.range_m > self.max_range_m:
                continue
            if rng.random() > self.pd:
                continue  # 미탐지

            r   = tgt.range_m        + rng.gauss(0, self.sigma_range_m)
            az  = tgt.azimuth_rad    + rng.gauss(0, self.sigma_azimuth_rad)
            el  = tgt.elevation_rad  # elevation 노이즈는 생략 (2D 시나리오)
            dop = tgt.radial_velocity() + rng.gauss(0, self.sigma_doppler_mps)

            measurements.append(MeasPayload(
                timestamp_ms=timestamp_ms,
                range_m=max(0.0, r),
                azimuth_rad=az,
                elevation_rad=el,
                doppler_mps=dop,
            ))

        # 오탐 (클러터) 주입: Poisson 분포 개수
        n_clutter = _poisson(self.clutter_rate, rng)
        for _ in range(n_clutter):
            r   = rng.uniform(0, self.max_range_m)
            az  = rng.uniform(-math.pi, math.pi)
            dop = rng.gauss(0, self.sigma_doppler_mps * 3)
            measurements.append(MeasPayload(
                timestamp_ms=timestamp_ms,
                range_m=r,
                azimuth_rad=az,
                elevation_rad=0.0,
                doppler_mps=dop,
            ))

        rng.shuffle(measurements)
        return measurements


def _poisson(lam: float, rng: random.Random) -> int:
    """Knuth 알고리즘으로 Poisson(lambda) 샘플."""
    if lam <= 0:
        return 0
    L = math.exp(-lam)
    k, p = 0, 1.0
    while p > L:
        k += 1
        p *= rng.random()
    return k - 1


# ── 사전 정의 시나리오 ──────────────────────────────────────────────────────────

def scenario_two_crossing() -> List[Target]:
    """두 표적이 정면에서 교차하는 시나리오."""
    return [
        Target(target_id=1, x_m=-5000, y_m=20000, vx_mps= 50, vy_mps=-80),
        Target(target_id=2, x_m= 5000, y_m=20000, vx_mps=-50, vy_mps=-80),
    ]

def scenario_single_approach() -> List[Target]:
    """단일 표적이 레이더 정면으로 접근."""
    return [
        Target(target_id=1, x_m=0, y_m=30000, vx_mps=0, vy_mps=-150),
    ]

def scenario_four_targets() -> List[Target]:
    """4개 표적 동시 추적 부하 테스트."""
    return [
        Target(target_id=1, x_m=-8000, y_m=25000, vx_mps= 40, vy_mps=-60),
        Target(target_id=2, x_m= 8000, y_m=25000, vx_mps=-40, vy_mps=-60),
        Target(target_id=3, x_m=    0, y_m=15000, vx_mps= 20, vy_mps=-100),
        Target(target_id=4, x_m= 3000, y_m=10000, vx_mps=-30, vy_mps=-120),
    ]

SCENARIOS = {
    "two_crossing":    scenario_two_crossing,
    "single_approach": scenario_single_approach,
    "four_targets":    scenario_four_targets,
}
