# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) 2023 CERN.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
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

teardown() {
    rm -rf test/adaptiveperf-split-ids/processed
}

@test "[Test 1] Both CPU and overall off-CPU empty" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_empty.data test/adaptiveperf-split-ids/overall_offcpu_empty.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed "" 0
}

@test "[Test 2] CPU non-empty, overall off-CPU empty, 1 process only" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_single.data test/adaptiveperf-split-ids/overall_offcpu_empty.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed _no_overall_offcpu.data 1
    assert_file_count test/adaptiveperf-split-ids/processed _with_overall_offcpu.data 1
    assert_file_count test/adaptiveperf-split-ids/processed _sampled_time.data 1

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test2_5_5_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test2_5_5_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test2_5_5_sampled_time_expected.data)"
}

@test "[Test 3] CPU empty, overall off-CPU non-empty, 1 process only" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_empty.data test/adaptiveperf-split-ids/overall_offcpu_single.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed _no_overall_offcpu.data 1
    assert_file_count test/adaptiveperf-split-ids/processed _with_overall_offcpu.data 1
    assert_file_count test/adaptiveperf-split-ids/processed _sampled_time.data 1

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test3_5_5_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test3_5_5_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test3_5_5_sampled_time_expected.data)"
}

@test "[Test 4] Both CPU and overall off-CPU non-empty, 1 process only" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_single.data test/adaptiveperf-split-ids/overall_offcpu_single.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed _no_overall_offcpu.data 1
    assert_file_count test/adaptiveperf-split-ids/processed _with_overall_offcpu.data 1
    assert_file_count test/adaptiveperf-split-ids/processed _sampled_time.data 1

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test4_5_5_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test4_5_5_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/5_5_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test4_5_5_sampled_time_expected.data)"
}

@test "[Test 5] CPU non-empty, overall off-CPU empty, multiple processes" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_multiple.data test/adaptiveperf-split-ids/overall_offcpu_empty.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed _no_overall_offcpu.data 3
    assert_file_count test/adaptiveperf-split-ids/processed _with_overall_offcpu.data 3
    assert_file_count test/adaptiveperf-split-ids/processed _sampled_time.data 3

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_1_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test5_1_1_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_1_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test5_1_1_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_1_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test5_1_1_sampled_time_expected.data)"

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test5_1_5810_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test5_1_5810_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test5_1_5810_sampled_time_expected.data)"

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test5_458571_1290_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test5_458571_1290_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test5_458571_1290_sampled_time_expected.data)"
}

@test "[Test 6] CPU empty, overall off-CPU non-empty, multiple processes" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_empty.data test/adaptiveperf-split-ids/overall_offcpu_multiple.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed _no_overall_offcpu.data 2
    assert_file_count test/adaptiveperf-split-ids/processed _with_overall_offcpu.data 2
    assert_file_count test/adaptiveperf-split-ids/processed _sampled_time.data 2

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test6_1_5810_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test6_1_5810_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test6_1_5810_sampled_time_expected.data)"

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test6_458571_1290_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test6_458571_1290_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test6_458571_1290_sampled_time_expected.data)"
}

@test "[Test 7] Both CPU and overall off-CPU non-empty, multiple processes" {
    ./adaptiveperf-split-ids test/adaptiveperf-split-ids/cpu_multiple.data test/adaptiveperf-split-ids/overall_offcpu_multiple.data

    assert_dir_exists test/adaptiveperf-split-ids/processed
    assert_file_count test/adaptiveperf-split-ids/processed _no_overall_offcpu.data 3
    assert_file_count test/adaptiveperf-split-ids/processed _with_overall_offcpu.data 3
    assert_file_count test/adaptiveperf-split-ids/processed _sampled_time.data 3

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_1_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test7_1_1_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_1_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test7_1_1_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_1_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test7_1_1_sampled_time_expected.data)"

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test7_1_5810_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test7_1_5810_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/1_5810_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test7_1_5810_sampled_time_expected.data)"

    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_no_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test7_458571_1290_no_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_with_overall_offcpu.data)" "$(cat test/adaptiveperf-split-ids/test7_458571_1290_with_overall_offcpu_expected.data)"
    assert_equal "$(cat test/adaptiveperf-split-ids/processed/458571_1290_sampled_time.data)" "$(cat test/adaptiveperf-split-ids/test7_458571_1290_sampled_time_expected.data)"
}
