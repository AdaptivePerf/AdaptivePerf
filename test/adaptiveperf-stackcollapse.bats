setup() {
    bats_require_minimum_version 1.5.0
    load test_helper/bats-support/load.bash
    load test_helper/bats-assert/load.bash
    load assert_funcs.bash
}

@test "[Test 1] Empty input" {
    run -0 /bin/bash -c "echo -n '' | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" ""
}

@test "[Test 2] Only on-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/on_cpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/on_cpu_result_expected.data)"
}

@test "[Test 3] Only off-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/off_cpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/off_cpu_result_expected.data)"
}

@test "[Test 4] Only overall off-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/overall_offcpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/overall_offcpu_result_expected.data)"
}

@test "[Test 5] On-CPU and off-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/on_cpu.data test/adaptiveperf-stackcollapse/off_cpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/on_cpu_result_expected.data test/adaptiveperf-stackcollapse/off_cpu_result_expected.data)"
}

@test "[Test 6] On-CPU and overall off-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/on_cpu.data test/adaptiveperf-stackcollapse/overall_offcpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/on_cpu_result_expected.data test/adaptiveperf-stackcollapse/overall_offcpu_result_expected.data)"
}

@test "[Test 7] Off-CPU and overall off-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/overall_offcpu.data test/adaptiveperf-stackcollapse/off_cpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/overall_offcpu_result_expected.data test/adaptiveperf-stackcollapse/off_cpu_result_expected.data)"
}

@test "[Test 8] On-CPU, off-CPU, and overall off-CPU data" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/off_cpu.data test/adaptiveperf-stackcollapse/overall_offcpu.data test/adaptiveperf-stackcollapse/on_cpu.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/off_cpu_result_expected.data test/adaptiveperf-stackcollapse/overall_offcpu_result_expected.data test/adaptiveperf-stackcollapse/on_cpu_result_expected.data)"
}

@test "[Test 9] Single on-CPU entry" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/on_cpu_single.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/on_cpu_single_result_expected.data)"
}

@test "[Test 10] Single off-CPU entry" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/off_cpu_single.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/off_cpu_single_result_expected.data)"
}

@test "[Test 11] Single overall off-CPU entry" {
    run -0 /bin/bash -c "cat test/adaptiveperf-stackcollapse/overall_offcpu_single.data | ./adaptiveperf-stackcollapse --tid"
    assert_equal "$output" "$(cat test/adaptiveperf-stackcollapse/overall_offcpu_single_result_expected.data)"
}
