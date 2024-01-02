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

    source ./adaptiveperf-misc-funcs.sh
}

@test "[Test 1] Mix of on-CPU, off-CPU, and overall off-CPU data" {
    result="$((echo "a;b;c 29120.24" && echo "a;b;[cold]__d 248" && echo "x;y;[cold]__z 99.5#" && echo "a;b;c;d;[cold]__e 58120157#") | convert_from_ns_to_us)"

    assert_equal "$result" "$(echo 'a;b;c 29.12024' && echo 'a;b;[cold]__d 0.248' && echo 'x;y;[cold]__z 0.0995#' && echo 'a;b;c;d;[cold]__e 58120.157#')"
}
