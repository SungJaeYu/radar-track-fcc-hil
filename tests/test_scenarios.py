"""통합 시뮬레이터의 CLI, wire 출력, 단일 트랙 제약 검증."""
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def invoke(*args, direct=False):
    command = [sys.executable]
    command += ['pc_sim/run_sim.py'] if direct else ['-m', 'pc_sim.run_sim']
    return subprocess.run(command + list(args), cwd=ROOT, capture_output=True, text=True)


def frames(output):
    return [bytes.fromhex(line.split('frame=', 1)[1])
            for line in output.splitlines() if 'frame=' in line]


@pytest.mark.parametrize('direct', [False, True])
def test_single_target_cli_emits_current_frames(direct):
    result = invoke('--duration', '0.2', '--dt', '0.1', '--noise', '0',
                    '--azimuth-noise', '0', direct=direct)
    assert result.returncode == 0, result.stderr
    emitted = frames(result.stdout)
    assert len(emitted) == 3
    assert emitted[0] == bytes.fromhex("7e 01 01 00 c8 9d")
    for frame, expected in zip(emitted[1:], [(0, 30000, 0), (100, 29985, 0)]):
        assert frame[:3] == bytes.fromhex('7e 0a 02')
        assert len(frame) == 15
        assert struct.unpack('>IHi', frame[3:-2]) == expected
        import binascii
        assert int.from_bytes(frame[-2:], 'big') == binascii.crc_hqx(frame[1:-2], 0xffff)


def test_multitarget_generation_remains_available_without_board():
    result = invoke('--scenario', 'four_targets', '--duration', '0.1')
    assert result.returncode == 0, result.stderr
    assert len(frames(result.stdout)) == 9
    assert frames(result.stdout)[0] == bytes.fromhex("7e 01 01 00 c8 9d")


@pytest.mark.parametrize('extra', [
    ['--scenario', 'two_crossing'], ['--clutter', '1'],
])
def test_serial_rejects_unsupported_multimeasurement_scans(extra):
    result = invoke('--port', 'must-not-be-opened', *extra)
    assert result.returncode == 2
    assert 'single target' in result.stderr


def test_missed_detections_emit_no_measurement():
    result = invoke('--pd', '0', '--duration', '0.2')
    assert result.returncode == 0, result.stderr
    assert frames(result.stdout) == [bytes.fromhex("7e 01 01 00 c8 9d")]


@pytest.mark.parametrize('option,value', [
    ('--dt', '0'), ('--dt', '0.0001'), ('--duration', 'nan'),
    ('--pd', '1.1'), ('--clutter', '-1'), ('--noise', '-1'),
    ('--azimuth-noise', 'inf'), ('--baud', '0'),
])
def test_invalid_scenario_settings(option, value):
    result = invoke(option, value)
    assert result.returncode == 2
    assert 'error:' in result.stderr


def test_uart_scenario_sends_current_frames_and_closes(monkeypatch):
    from unittest.mock import MagicMock
    from pc_sim import run_sim
    serial = MagicMock()
    monkeypatch.setitem(sys.modules, 'serial', serial)
    monkeypatch.setattr(sys, 'argv', ['sim', '--port', 'test', '--duration', '0.2',
                                     '--dt', '0.1', '--noise', '0', '--azimuth-noise', '0'])
    monkeypatch.setattr(run_sim.time, 'sleep', lambda seconds: None)
    run_sim.main()
    connection = serial.Serial.return_value
    sent = [call.args[0] for call in connection.__enter__.return_value.write.call_args_list]
    assert sent == [bytes.fromhex('7e 01 01 00 c8 9d'),
                    bytes.fromhex('7e 0a 02 00 00 00 00 75 30 00 00 00 00 33 7b'),
                    bytes.fromhex('7e 0a 02 00 00 00 64 75 21 00 00 00 00 ef ac')]
    connection.__exit__.assert_called_once()
