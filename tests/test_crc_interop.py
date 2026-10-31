"""Cross-check Python frames against the actual firmware parser on the host."""
import binascii
import ctypes
from pathlib import Path
import shutil
import subprocess

import pytest
from pc_sim.protocol import crc16, encode_frame, build_init_frame, build_radar_frame


def test_crc_standard_check_vector():
    assert crc16(b'123456789') == 0x29b1


@pytest.mark.parametrize('payload', [b'\0', bytes(range(32)), b'\x7e\xff\x00'])
def test_crc_matches_independent_reference(payload):
    frame = encode_frame(2, payload)
    assert len(frame) == len(payload) + 5
    assert int.from_bytes(frame[-2:], 'big') == binascii.crc_hqx(frame[1:-2], 0xffff)


@pytest.fixture(scope='module')
def parser_lib(tmp_path_factory):
    compiler = Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang')
    if not compiler.exists():
        found = shutil.which('cc')
        if not found:
            pytest.skip('A host C compiler is required for firmware parser interoperability checks')
        compiler = Path(found)
    folder = tmp_path_factory.mktemp('crc-parser')
    wrapper = folder / 'wrapper.c'
    wrapper.write_text('''
#include "frame_parser.c"
static struct frame_parser parser;
void reset_parser(void) { parser_init(&parser); }
int feed_byte(unsigned char b) { return parser_feed(&parser, b); }
int frame_type(void) { return parser.type; }
int payload_size(void) { return parser.payload_length; }
int payload_byte(int i) { return parser.payload[i]; }
int crc_check_vector(void) {
    const unsigned char data[] = "123456789";
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < sizeof(data) - 1; ++i) crc = crc16_update(crc, data[i]);
    return crc;
}
''')
    library = folder / 'parser.so'
    src = Path(__file__).resolve().parents[1] / 'fcc_app/src'
    sdk = Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk')
    sdk_flags = ['-isysroot', str(sdk)] if sdk.exists() else []
    subprocess.run([str(compiler), *sdk_flags, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-shared', '-fPIC', '-I', str(src), str(wrapper), '-o', str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.reset_parser.restype = None
    lib.feed_byte.argtypes = [ctypes.c_ubyte]
    lib.payload_byte.argtypes = [ctypes.c_int]
    return lib


def feed(lib, frame):
    return [lib.feed_byte(byte) for byte in frame]


def test_firmware_crc_standard_check_vector(parser_lib):
    assert parser_lib.crc_check_vector() == 0x29b1


@pytest.mark.parametrize('frame', [build_init_frame(), build_radar_frame(100, 1000, -1073741824),
                                  encode_frame(5, b'r'), encode_frame(3, bytes(range(32)))])
def test_python_frames_accepted_by_firmware(parser_lib, frame):
    parser_lib.reset_parser()
    assert feed(parser_lib, frame) == [0] * (len(frame) - 1) + [1]
    assert parser_lib.frame_type() == frame[2]
    assert parser_lib.payload_size() == frame[1]
    assert bytes(parser_lib.payload_byte(i) for i in range(frame[1])) == frame[3:-2]


@pytest.mark.parametrize('offset', [2, 3, -2, -1])
def test_corrupted_frame_rejected_then_next_frame_accepted(parser_lib, offset):
    parser_lib.reset_parser()
    frame = bytearray(build_radar_frame(100, 1000, -1073741824))
    frame[offset] ^= 1
    assert feed(parser_lib, frame)[-1] == 2  # PARSER_CRC_ERROR
    assert feed(parser_lib, build_init_frame())[-1] == 1


@pytest.mark.parametrize('length', [0, 33, 255])
def test_invalid_length_rejected_then_next_frame_accepted(parser_lib, length):
    parser_lib.reset_parser()
    assert feed(parser_lib, bytes([0x7e, length])) == [0, 3]  # PARSER_LENGTH_ERROR
    assert feed(parser_lib, build_init_frame())[-1] == 1


def test_crc_low_byte_required_for_completion(parser_lib):
    parser_lib.reset_parser()
    frame = build_init_frame()
    assert feed(parser_lib, frame[:-1]) == [0] * (len(frame) - 1)
    assert feed(parser_lib, frame[-1:]) == [1]


def test_plausible_corrupted_length_does_not_report_ready(parser_lib):
    parser_lib.reset_parser()
    frame = bytearray(build_radar_frame(100, 1000, -1073741824))
    frame[1] = 9
    results = feed(parser_lib, frame)
    assert 1 not in results
    assert 2 in results
    assert feed(parser_lib, build_init_frame())[-1] == 1
