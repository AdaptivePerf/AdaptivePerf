# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) 2023 CERN.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; only version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

setup() {
    bats_require_minimum_version 1.5.0
    load test_helper/bats-support/load.bash
    load test_helper/bats-assert/load.bash
    load assert_funcs.bash
}

@test "[Test 1] syscalls.data with multiple threads and processes along with some C++-mangled symbols" {
    skip "Symbols etc. are NOT saved in syscalls.data, so this test is very machine-specific: skipping for now"
    result="$(perf script -i test/adaptiveperf-perf-get-callchain/syscalls.data --no-demangle adaptiveperf-perf-get-callchain.py)"
    assert_equal "$result" "$(cat test/adaptiveperf-perf-get-callchain/test1_result_expected.data)"
}
