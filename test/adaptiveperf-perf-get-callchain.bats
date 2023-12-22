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
