"""test_protocol.py — FrameParser + 코덱 유닛테스트."""

import pytest
from protocol import (
    STX,
    MSG_MEAS, MSG_TRACK, MSG_CTRL,
    CTRL_START, CTRL_STOP, CTRL_RESET,
    MEAS_SIZE, TRACK_SIZE, CTRL_SIZE,
    crc16_ccitt,
    encode_frame,
    FrameParser,
    MeasPayload, TrackPayload, CtrlPayload,
)


def parse_all(data: bytes) -> list:
    return list(FrameParser().feed(data))


# ── 1. CRC16-CCITT ────────────────────────────────────────────────────────────

def test_crc16_known_value():
    # XModem CRC16 표준 검증값
    assert crc16_ccitt(b"123456789") == 0x31C3

def test_crc16_empty():
    assert crc16_ccitt(b"") == 0x0000

def test_crc16_single_byte():
    # 0x00 한 바이트: init=0 이므로 변화 없음
    assert crc16_ccitt(b"\x00") == 0x0000

def test_crc16_differs_by_byte():
    assert crc16_ccitt(b"\x01") != crc16_ccitt(b"\x02")


# ── 2. 페이로드 크기 ──────────────────────────────────────────────────────────

def test_payload_sizes():
    assert MEAS_SIZE  == 20
    assert TRACK_SIZE == 21
    assert CTRL_SIZE  == 1


# ── 3. 인코딩 라운드트립 ──────────────────────────────────────────────────────

def test_roundtrip_meas():
    orig = MeasPayload(timestamp_ms=1000, range_m=500.0,
                       azimuth_rad=0.3, elevation_rad=0.1, doppler_mps=-5.0)
    results = parse_all(encode_frame(MSG_MEAS, orig.encode()))
    assert len(results) == 1
    t, p = results[0]
    assert t == MSG_MEAS
    dec = MeasPayload.decode(p)
    assert dec.timestamp_ms == 1000
    assert abs(dec.range_m       - 500.0) < 1e-3
    assert abs(dec.azimuth_rad   - 0.3  ) < 1e-5
    assert abs(dec.elevation_rad - 0.1  ) < 1e-5
    assert abs(dec.doppler_mps   - (-5.0)) < 1e-5

def test_roundtrip_track():
    orig = TrackPayload(timestamp_ms=2000, track_id=3,
                        x_m=100.0, y_m=200.0, vx_mps=10.0, vy_mps=-5.0)
    results = parse_all(encode_frame(MSG_TRACK, orig.encode()))
    assert len(results) == 1
    t, p = results[0]
    assert t == MSG_TRACK
    dec = TrackPayload.decode(p)
    assert dec.track_id     == 3
    assert dec.timestamp_ms == 2000
    assert abs(dec.x_m  - 100.0) < 1e-3
    assert abs(dec.y_m  - 200.0) < 1e-3
    assert abs(dec.vx_mps - 10.0) < 1e-5

def test_roundtrip_ctrl_start():
    orig = CtrlPayload(cmd=CTRL_START)
    results = parse_all(encode_frame(MSG_CTRL, orig.encode()))
    assert len(results) == 1
    t, p = results[0]
    assert t == MSG_CTRL
    assert CtrlPayload.decode(p).cmd == CTRL_START

def test_roundtrip_ctrl_all_cmds():
    for cmd in (CTRL_START, CTRL_STOP, CTRL_RESET):
        results = parse_all(encode_frame(MSG_CTRL, CtrlPayload(cmd).encode()))
        assert CtrlPayload.decode(results[0][1]).cmd == cmd

def test_roundtrip_zero_payload():
    # LEN=0 엣지케이스
    results = parse_all(encode_frame(MSG_CTRL, b""))
    assert len(results) == 1
    assert results[0][1] == b""


# ── 4. 프레임 구조 검증 ───────────────────────────────────────────────────────

def test_frame_starts_with_stx():
    frame = encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode())
    assert frame[0] == STX

def test_frame_length_field():
    payload = CtrlPayload(CTRL_START).encode()
    frame = encode_frame(MSG_CTRL, payload)
    assert frame[1] == len(payload)

def test_frame_type_field():
    frame = encode_frame(MSG_MEAS, MeasPayload(0, 0.0, 0.0, 0.0, 0.0).encode())
    assert frame[2] == MSG_MEAS

def test_total_frame_size():
    payload = MeasPayload(0, 0.0, 0.0, 0.0, 0.0).encode()
    frame = encode_frame(MSG_MEAS, payload)
    assert len(frame) == 5 + len(payload)  # STX+LEN+TYPE+CRC_H+CRC_L


# ── 5. 가비지 후 자동 resync ──────────────────────────────────────────────────

def test_garbage_before_frame():
    garbage = bytes([0x00, 0xFF, 0xAB, 0x12, 0x34])
    frame = encode_frame(MSG_CTRL, CtrlPayload(CTRL_STOP).encode())
    results = parse_all(garbage + frame)
    assert len(results) == 1
    assert results[0][0] == MSG_CTRL

def test_garbage_between_frames():
    f1 = encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode())
    f2 = encode_frame(MSG_CTRL, CtrlPayload(CTRL_STOP).encode())
    results = parse_all(f1 + bytes([0xDE, 0xAD, 0xBE, 0xEF]) + f2)
    assert len(results) == 2
    assert CtrlPayload.decode(results[0][1]).cmd == CTRL_START
    assert CtrlPayload.decode(results[1][1]).cmd == CTRL_STOP

def test_stx_in_garbage_resyncs():
    # 가비지 안에 STX(0x7E)가 포함돼도 올바른 프레임을 찾아냄
    garbage = bytes([0x00, STX, 0xAB, 0xCD, 0x00])
    frame = encode_frame(MSG_CTRL, CtrlPayload(CTRL_RESET).encode())
    results = parse_all(garbage + frame)
    assert len(results) == 1

def test_all_garbage_no_crash():
    garbage = bytes(range(256)) * 2
    results = parse_all(garbage)
    # 크래시 없이 0개 또는 우연히 맞는 프레임만 반환
    assert isinstance(results, list)


# ── 6. CRC 불일치 거부 ───────────────────────────────────────────────────────

def test_bad_crc_low_byte_rejected():
    frame = bytearray(encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode()))
    frame[-1] ^= 0xFF
    assert parse_all(bytes(frame)) == []

def test_bad_crc_high_byte_rejected():
    frame = bytearray(encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode()))
    frame[-2] ^= 0xFF
    assert parse_all(bytes(frame)) == []

def test_bad_crc_then_good_frame():
    bad = bytearray(encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode()))
    bad[-1] ^= 0xFF
    good = encode_frame(MSG_CTRL, CtrlPayload(CTRL_STOP).encode())
    results = parse_all(bytes(bad) + good)
    assert len(results) == 1
    assert CtrlPayload.decode(results[0][1]).cmd == CTRL_STOP

def test_payload_corruption_rejected():
    frame = bytearray(encode_frame(MSG_MEAS,
                      MeasPayload(1, 100.0, 0.1, 0.0, 0.0).encode()))
    frame[3] ^= 0xFF  # payload 첫 바이트 변조
    assert parse_all(bytes(frame)) == []


# ── 7. split-read 조립 ────────────────────────────────────────────────────────

def test_split_byte_by_byte():
    meas = MeasPayload(timestamp_ms=500, range_m=300.0,
                       azimuth_rad=1.0, elevation_rad=0.0, doppler_mps=0.0)
    frame = encode_frame(MSG_MEAS, meas.encode())
    parser = FrameParser()
    results = []
    for b in frame:
        results.extend(parser.feed(bytes([b])))
    assert len(results) == 1
    assert results[0][0] == MSG_MEAS

def test_split_two_chunks():
    frame = encode_frame(MSG_MEAS,
                         MeasPayload(1, 1.0, 2.0, 3.0, 4.0).encode())
    mid = len(frame) // 2
    parser = FrameParser()
    results = list(parser.feed(frame[:mid])) + list(parser.feed(frame[mid:]))
    assert len(results) == 1

def test_split_one_byte_at_a_time_track():
    orig = TrackPayload(timestamp_ms=9999, track_id=7,
                        x_m=-50.0, y_m=120.0, vx_mps=3.5, vy_mps=-1.2)
    frame = encode_frame(MSG_TRACK, orig.encode())
    parser = FrameParser()
    results = []
    for b in frame:
        results.extend(parser.feed(bytes([b])))
    assert len(results) == 1
    dec = TrackPayload.decode(results[0][1])
    assert dec.track_id == 7
    assert abs(dec.x_m - (-50.0)) < 1e-3


# ── 8. 다중 프레임 연속 수신 ─────────────────────────────────────────────────

def test_multiple_frames_sequential():
    frames = b"".join(
        encode_frame(MSG_MEAS,
                     MeasPayload(i, float(i * 10), 0.0, 0.0, 0.0).encode())
        for i in range(8)
    )
    results = parse_all(frames)
    assert len(results) == 8
    for i, (t, p) in enumerate(results):
        assert t == MSG_MEAS
        assert MeasPayload.decode(p).timestamp_ms == i

def test_mixed_types_sequential():
    meas  = encode_frame(MSG_MEAS,  MeasPayload(1, 10.0, 0.1, 0.0, 2.0).encode())
    track = encode_frame(MSG_TRACK, TrackPayload(2, 5, 3.0, 4.0, 1.0, 0.5).encode())
    ctrl  = encode_frame(MSG_CTRL,  CtrlPayload(CTRL_STOP).encode())
    results = parse_all(meas + track + ctrl)
    assert [t for t, _ in results] == [MSG_MEAS, MSG_TRACK, MSG_CTRL]

def test_stateful_parser_across_calls():
    # 같은 parser 인스턴스에 여러 번 feed
    frame = encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode())
    parser = FrameParser()
    r1 = list(parser.feed(frame))
    r2 = list(parser.feed(frame))
    assert len(r1) == 1
    assert len(r2) == 1


# ── 9. reset() 동작 ──────────────────────────────────────────────────────────

def test_reset_clears_state():
    frame = encode_frame(MSG_CTRL, CtrlPayload(CTRL_START).encode())
    parser = FrameParser()
    # 프레임 절반만 넣고 reset
    list(parser.feed(frame[:3]))
    parser.reset()
    # reset 후 완전한 프레임이 정상 파싱돼야 함
    results = list(parser.feed(frame))
    assert len(results) == 1
