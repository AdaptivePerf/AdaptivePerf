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

teardown() {
    rm -f script*.data
}

@test "[Test 1] Empty perf-script output" {
    echo -n "" | ./adaptiveperf-split-report 2

    assert_file_exists script0.data
    assert_char_count script0.data 0

    assert_file_exists script1.data
    assert_char_count script1.data 0
}

@test "[Test 2] perf-script output too small to be divided into 100 files" {
    cat test/adaptiveperf-split-report/too_small_perf_script.data | ./adaptiveperf-split-report 100

    assert_file_exists script0.data
    assert_line_count script0.data 97

    assert_file_exists script1.data
    assert_line_count script1.data 9

    assert_file_exists script2.data
    assert_line_count script2.data 103

    file_list="script0.data script1.data script2.data"
    
    for i in {3..99}; do
        assert_file_exists script${i}.data
        assert_char_count script${i}.data 0
        file_list+=" script${i}.data"
    done

    assert_equal "$(cat $file_list)" "$(cat test/adaptiveperf-split-report/too_small_perf_script.data)"
}

@test "[Test 3] perf-script output to be divided into 7 files (equal block counts per part)" {
    cat test/adaptiveperf-split-report/perf_script.data | ./adaptiveperf-split-report 7

    assert_file_exists script0.data
    assert_line_count script0.data 66781

    assert_file_exists script1.data
    assert_line_count script1.data 66885

    assert_file_exists script2.data
    assert_line_count script2.data 66885

    assert_file_exists script3.data
    assert_line_count script3.data 66885

    assert_file_exists script4.data
    assert_line_count script4.data 66885

    assert_file_exists script5.data
    assert_line_count script5.data 66885

    assert_file_exists script6.data
    assert_line_count script6.data 66693

    assert_equal "$(cat script0.data script1.data script2.data script3.data script4.data script5.data script6.data)" "$(cat test/adaptiveperf-split-report/perf_script.data)"
}

@test "[Test 4] perf-script output to be divided into 5 files (inequal block counts per part)" {
    cat test/adaptiveperf-split-report/perf_script.data | ./adaptiveperf-split-report 5

    assert_file_exists script0.data
    assert_line_count script0.data 93556

    assert_file_exists script1.data
    assert_line_count script1.data 93660

    assert_file_exists script2.data
    assert_line_count script2.data 93660

    assert_file_exists script3.data
    assert_line_count script3.data 93660

    assert_file_exists script4.data
    assert_line_count script4.data 93363

    assert_equal "$(cat script0.data script1.data script2.data script3.data script4.data)" "$(cat test/adaptiveperf-split-report/perf_script.data)"
}
