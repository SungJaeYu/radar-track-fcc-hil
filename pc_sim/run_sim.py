"""
run_sim.py — HIL 시뮬레이터 메인 루프

사용법:
    python run_sim.py --mock                        # 보드 없이 PC 단독 테스트
    python run_sim.py --serial /dev/ttyACM0         # 실제 STM32 연결
    python run_sim.py --mock --scenario four_targets
    python run_sim.py --mock --dt 0.1 --duration 30

루프 구조 (매 dt초):
    1. 표적 위치 전진 (등속)
    2. 센서 모델로 측정값 생성 (노이즈 + 미탐지 + 클러터)
    3. 각 측정값을 MSG_MEAS 프레임으로 인코딩 → transport.send()
    4. transport.recv()로 STM32 트랙 결과 수신 → FrameParser로 파싱
    5. 수신된 MSG_TRACK을 ground truth와 비교해 콘솔 출력
"""

import argparse
import random
import time

from protocol import (
    MSG_MEAS, MSG_TRACK, MSG_CTRL,
    CTRL_START, CTRL_STOP,
    encode_frame, FrameParser,
    MeasPayload, TrackPayload, CtrlPayload,
)
from targets import RadarSensorModel, SCENARIOS
from transport import MockTransport, SerialTransport


def run(transport, scenario_name: str, dt: float, duration: float,
        pd: float, clutter_rate: float, seed: int) -> None:

    rng      = random.Random(seed)
    targets  = SCENARIOS[scenario_name]()
    sensor   = RadarSensorModel(pd=pd, clutter_rate=clutter_rate)
    parser   = FrameParser()
    t_elapsed = 0.0
    step      = 0

    print(f"[sim] 시작: scenario={scenario_name}, dt={dt}s, "
          f"duration={duration}s, pd={pd}, clutter={clutter_rate}")
    print(f"[sim] 표적 {len(targets)}개: "
          + ", ".join(f"ID{t.target_id}" for t in targets))

    # STM32에 시작 신호
    transport.send(encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode()))

    try:
        while t_elapsed < duration:
            tick_start    = time.monotonic()
            timestamp_ms  = int(t_elapsed * 1000)

            # 1. 표적 전진
            for tgt in targets:
                tgt.update(dt)

            # 2. 측정값 생성
            meas_list = sensor.measure(targets, timestamp_ms, rng)

            # 3. 측정값 송신
            for meas in meas_list:
                transport.send(encode_frame(MSG_MEAS, meas.encode()))

            # 4. 트랙 수신 (non-blocking: 짧은 타임아웃)
            raw = transport.recv(n=512)
            for msg_type, payload in parser.feed(raw):
                if msg_type == MSG_TRACK:
                    trk = TrackPayload.decode(payload)
                    _print_track(trk, targets, timestamp_ms)

            # 5. 콘솔 요약
            if step % 10 == 0:
                _print_step(step, timestamp_ms, targets, meas_list)

            step += 1
            t_elapsed += dt

            # dt 주기 맞추기 (실제 serial 모드에서 의미 있음)
            elapsed = time.monotonic() - tick_start
            if elapsed < dt:
                time.sleep(dt - elapsed)

    except KeyboardInterrupt:
        print("\n[sim] 중단 (Ctrl+C)")
    finally:
        transport.send(encode_frame(MSG_CTRL, CtrlPayload(CTRL_STOP).encode()))
        transport.close()
        print("[sim] 종료")


def _print_step(step: int, ts_ms: int, targets, meas_list) -> None:
    print(f"\n--- step {step:4d}  t={ts_ms/1000:.2f}s  "
          f"측정 {len(meas_list)}개 ---")
    for tgt in targets:
        print(f"  GT ID{tgt.target_id}: "
              f"x={tgt.x_m:8.0f}m  y={tgt.y_m:8.0f}m  "
              f"r={tgt.range_m:8.0f}m  az={tgt.azimuth_rad:5.2f}rad")


def _print_track(trk: TrackPayload, targets, ts_ms: int) -> None:
    import math
    # 가장 가까운 ground truth 표적 찾기
    best_err = float("inf")
    for tgt in targets:
        err = math.hypot(trk.x_m - tgt.x_m, trk.y_m - tgt.y_m)
        best_err = min(best_err, err)
    print(f"  [TRACK] ID{trk.track_id}  "
          f"x={trk.x_m:8.0f}m  y={trk.y_m:8.0f}m  "
          f"gt_err={best_err:.1f}m")


def main() -> None:
    ap = argparse.ArgumentParser(description="Radar HIL PC 시뮬레이터")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--mock",   action="store_true", help="인메모리 MockTransport")
    mode.add_argument("--serial", metavar="PORT",      help="시리얼 포트 (예: /dev/ttyACM0)")

    ap.add_argument("--scenario", choices=list(SCENARIOS.keys()),
                    default="two_crossing")
    ap.add_argument("--dt",       type=float, default=0.05,  help="스텝 간격 (초)")
    ap.add_argument("--duration", type=float, default=60.0,  help="총 시뮬레이션 시간 (초)")
    ap.add_argument("--pd",       type=float, default=0.9,   help="탐지확률")
    ap.add_argument("--clutter",  type=float, default=0.5,   help="스캔당 오탐 평균 개수")
    ap.add_argument("--seed",     type=int,   default=42,    help="난수 시드")
    ap.add_argument("--baudrate", type=int,   default=115200)
    args = ap.parse_args()

    if args.mock:
        transport = MockTransport()
    else:
        transport = SerialTransport(args.serial, baudrate=args.baudrate)

    run(transport,
        scenario_name=args.scenario,
        dt=args.dt,
        duration=args.duration,
        pd=args.pd,
        clutter_rate=args.clutter,
        seed=args.seed)


if __name__ == "__main__":
    main()
