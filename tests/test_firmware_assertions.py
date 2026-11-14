"""Run invalid internal calls in child processes so assertions cannot kill pytest."""

from pathlib import Path
import shutil
import signal
import subprocess

import pytest


@pytest.fixture(scope="module")
def assertion_probe(tmp_path_factory):
    root = Path(__file__).resolve().parents[1]
    src = root / "fcc_app/src"
    compiler = Path("/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang")
    if not compiler.exists():
        found = shutil.which("cc")
        if not found:
            pytest.skip("A host C compiler is required")
        compiler = Path(found)
    folder = tmp_path_factory.mktemp("firmware-assertions")
    probe = folder / "probe.c"
    probe.write_text('''
#include <math.h>
#include <stddef.h>
#include "common/ring_buffer.h"
#include "protocol/frame_parser.h"
#include "tracking/kalman_cv.h"
#include "tracking/angle_utils.h"
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (argv[1][0] == 'n') wrap_angle_rad(NAN);
    if (argv[1][0] == 'i') wrap_angle_rad(INFINITY);
    if (argv[1][0] == 'm') wrap_angle_rad(-INFINITY);
    if (argv[1][0] == 'r') ring_init(NULL);
    if (argv[1][0] == 'p') {
        struct frame_parser parser;
        parser_init(&parser);
        parser.state = 99;
        parser_feed(&parser, 0);
    }
    if (argv[1][0] == 'k') {
        struct kalman_cv_filter filter;
        kalman_cv_init(&filter, 0, 0, 1, 0, 0, 1, INFINITY, 1);
    }
    return 0;
}
''')
    executable = folder / "probe"
    sdk = Path("/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
    sdk_flags = ["-isysroot", str(sdk)] if sdk.exists() else []
    sources = ["common/ring_buffer.c", "protocol/frame_parser.c",
               "protocol/protocol_frame.c", "common/crc16.c",
               "tracking/kalman_cv.c", "tracking/angle_utils.c"]
    subprocess.run([str(compiler), *sdk_flags, "-std=c11", "-DNDEBUG",
                    "-Wall", "-Wextra", "-Werror", "-I", str(src),
                    "-I", str(root / "fcc_app/tests/common/include"), str(probe),
                    *(str(src / name) for name in sources), "-lm", "-o", str(executable)],
                   check=True)
    return executable


@pytest.mark.parametrize("mode,message", [
    ("ring", "Ring buffer pointer is NULL"),
    ("parser", "Invalid parser state: 99"),
    ("kalman", "Process noise must be finite and nonnegative"),
    ("nan", "Angle value must be finite"),
    ("infinity", "Angle value must be finite"),
    ("minus_infinity", "Angle value must be finite"),
])
def test_internal_contract_asserts_even_with_ndebug(assertion_probe, mode, message):
    result = subprocess.run([str(assertion_probe), mode], capture_output=True, text=True, timeout=5)
    assert result.returncode == -signal.SIGABRT
    assert message in result.stderr
