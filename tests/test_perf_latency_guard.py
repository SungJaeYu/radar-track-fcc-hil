"""Check the latency helper with internal assertions enabled and disabled."""

from pathlib import Path
import shutil
import signal
import subprocess

import pytest


@pytest.fixture(scope="module", params=[True, False], ids=["asserts-on", "asserts-off"])
def latency_probe(request, tmp_path_factory):
    root = Path(__file__).resolve().parents[1]
    compiler = shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("A host C compiler is required")
    folder = tmp_path_factory.mktemp("latency-guard")
    includes = []
    if not request.param:
        header = folder / "zephyr/sys/__assert.h"
        header.parent.mkdir(parents=True)
        header.write_text("#define __ASSERT(condition, ...) ((void)0)\n")
        includes.extend(["-I", str(folder)])
    source = folder / "probe.c"
    source.write_text('''
#include <assert.h>
#include <stdlib.h>
#include "diagnostics/perf_monitor.c"

int main(int argc, char **p_argv)
{
    assert(argc == 2);
    const int64_t elapsed_ms = strtoll(p_argv[1], NULL, 10);
    struct perf_latency_stats latency = {0};
    record_latency(&latency, 7);
    record_latency(&latency, elapsed_ms);

    if (elapsed_ms < 0) {
        assert(latency.sample_count == 1);
        assert(latency.total_ms == 7 && latency.max_ms == 7);
        assert(latency.over_budget_count == 0);
        for (unsigned int i = 0; i < PERF_LATENCY_BIN_COUNT; ++i) {
            assert(latency.histogram_counts[i] == (i == 7 ? 1U : 0U));
        }
    } else {
        const uint64_t expected_ms = elapsed_ms > UINT32_MAX ? UINT32_MAX : elapsed_ms;
        assert(latency.sample_count == 2);
        assert(latency.total_ms == 7 + expected_ms);
        assert(latency.max_ms == (expected_ms > 7 ? expected_ms : 7));
        assert(latency.over_budget_count == (expected_ms > 20 ? 1U : 0U));
    }
    return 0;
}
''')
    executable = folder / "probe"
    subprocess.run([
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
        *includes, "-I", str(root / "fcc_app/tests/common/include"),
        "-I", str(root / "fcc_app/tests/test_perf_monitor/include"),
        "-I", str(root / "fcc_app/src"), str(source), "-o", str(executable),
    ], check=True)
    return executable, request.param


@pytest.mark.parametrize("elapsed_ms", [-1, -(2**63), 0, 2**32 - 1, 2**32, 2**63 - 1])
def test_latency_input_boundaries(latency_probe, elapsed_ms):
    executable, assertions_enabled = latency_probe
    result = subprocess.run([str(executable), str(elapsed_ms)], capture_output=True,
                            text=True, timeout=5)
    if elapsed_ms < 0 and assertions_enabled:
        assert result.returncode == -signal.SIGABRT
        assert "Latency must be nonnegative" in result.stderr
    else:
        assert result.returncode == 0, result.stderr
