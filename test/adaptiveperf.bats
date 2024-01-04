setup() {
    bats_require_minimum_version 1.5.0
    load test_helper/bats-support/load.bash
    load test_helper/bats-assert/load.bash
    load assert_funcs.bash

    paranoid=$(sysctl -n kernel.perf_event_paranoid)
    max_stack=$(sysctl -n kernel.perf_event_max_stack)
    numa_balancing=$(sysctl -n kernel.numa_balancing)

    assert [ $paranoid -eq -1 ]
    assert [ $max_stack -eq 1024 ]
    assert [ $numa_balancing -eq 0 ]
}

teardown() {
    rm -rf results a.out perf_txt.data syscalls_txt.data
}

function asserts_after_adaptiveperf_run() {
    assert_dir_exists results
    assert_dir_count results 1

    run -0 mv results/* results/test
    
    assert_file_exists results/test/perf.data
    assert_file_exists results/test/syscalls.data
    assert_file_exists results/test/overall_offcpu_collapsed.data
    assert_file_exists results/test/new_proc_callchains.data
    assert_file_exists results/test/out/stdout.log
    assert_file_exists results/test/out/stderr.log
    assert_file_count results/test/processed _no_overall_offcpu.data $1
    assert_file_count results/test/processed _with_overall_offcpu.data $1
    assert_file_count results/test/processed _sampled_time.data $1
    assert_file_count results/test/processed _walltime.svg $1
    assert_file_count results/test/processed _walltime_chart.svg $1

    assert_file_non_empty results/test/new_proc_callchains.data
    
    run -0 /bin/bash -c "perf script -i results/test/perf.data > perf_txt.data"
    run -0 /bin/bash -c "perf script -i results/test/syscalls.data > syscalls_txt.data"

    assert_file_non_empty perf_txt.data
    assert_file_non_empty syscalls_txt.data

    assert_all_files_non_empty results/test/processed _no_overall_offcpu.data
    assert_all_files_non_empty results/test/processed _with_overall_offcpu.data
    assert_all_files_non_empty results/test/processed _sampled_time.data
    assert_all_files_non_empty results/test/processed _walltime.svg
    assert_all_files_non_empty results/test/processed _walltime_chart.svg

    if [[ $2 -ne 0 ]]; then
        assert_equal "$(cat perf_txt.data | sed -nE 's/.*\s+([0-9\.]+)\s+task-clock.*/\1/p' | sort | uniq)" "$2"
    fi

    assert_equal "$((grep 'Starting script' <<< \"$4\") | wc -l)" "$3"

    assert_file_count_prefix /tmp adaptiveperf.pid 0
}

@test "[Test 1] Profiling a single-threaded program with default settings" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/single_threaded.cpp
    run -0 ./adaptiveperf ./a.out

    asserts_after_adaptiveperf_run 2 100000000 $(nproc) "$output"
}

@test "[Test 2] Profiling a multi-threaded program with default settings" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/multi_threaded.cpp
    run -0 ./adaptiveperf ./a.out

    asserts_after_adaptiveperf_run 18 100000000 $(nproc) "$output"
}

@test "[Test 3] Profiling a single-threaded program with sampling rate 25 Hz" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/single_threaded.cpp
    run -0 ./adaptiveperf -F 25 ./a.out

    asserts_after_adaptiveperf_run 2 40000000 $(nproc) "$output"
}

@test "[Test 4] Profiling a multi-threaded program with sampling rate 25 Hz" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/multi_threaded.cpp
    run -0 ./adaptiveperf -F 25 ./a.out

    asserts_after_adaptiveperf_run 18 40000000 $(nproc) "$output"
}

@test "[Test 5] Profiling a single-threaded program with 16 post-processing threads" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/single_threaded.cpp
    run -0 ./adaptiveperf -t 16 ./a.out

    asserts_after_adaptiveperf_run 2 100000000 16 "$output"
}

@test "[Test 6] Profiling a multi-threaded program with 16 post-processing threads" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/multi_threaded.cpp
    run -0 ./adaptiveperf -t 16 ./a.out

    asserts_after_adaptiveperf_run 18 100000000 16 "$output"
}

@test "[Test 7] Profiling a single-threaded program with sampling rate 40 Hz and 11 post-processing threads" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/single_threaded.cpp
    run -0 ./adaptiveperf -t 11 -F 40 ./a.out

    asserts_after_adaptiveperf_run 2 25000000 11 "$output"
}

@test "[Test 8] Profiling a multi-threaded program with sampling rate 40 Hz and 11 post-processing threads" {
    g++ -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer test/adaptiveperf/multi_threaded.cpp
    run -0 ./adaptiveperf -t 11 -F 40 ./a.out

    asserts_after_adaptiveperf_run 18 25000000 11 "$output"
}

@test "[Test 9] Profiling a very short-living program with default settings" {
    run -0 ./adaptiveperf ls

    asserts_after_adaptiveperf_run 1 0 $(nproc) "$output"
}

@test "[Test 10] Profiling a very short-living program with default settings and kernel.perf_event_paranoid not equal to -1" {
    PATH=$(pwd)/test/adaptiveperf/mock_test10:$PATH run ! ./adaptiveperf ls
}

@test "[Test 11] Profiling a very short-living program with default settings and kernel.perf_event_max_stack set to 213" {
    PATH=$(pwd)/test/adaptiveperf/mock_test11:$PATH run -0 ./adaptiveperf ls

    assert_line --partial "Note that stacks with more than 213 entries/entry"
    asserts_after_adaptiveperf_run 1 0 $(nproc) "$output"
}

@test "[Test 12] Profiling a very short-living program with default settings and NUMA balancing disabled" {
    PATH=$(pwd)/test/adaptiveperf/mock_test12:$PATH run -0 ./adaptiveperf ls

    assert_line --partial "NUMA balancing is disabled"
    asserts_after_adaptiveperf_run 1 0 $(nproc) "$output"
}

@test "[Test 13] Profiling a very short-living program with default settings and NUMA balancing enabled" {
    PATH=$(pwd)/test/adaptiveperf/mock_test13:$PATH run -0 ./adaptiveperf ls

    assert_line --partial "NUMA balancing is enabled"
    asserts_after_adaptiveperf_run 1 0 $(nproc) "$output"
}

@test "[Test 14] Profiling a very short-living program with default settings and NUMA balancing enabled with no available NUMA nodes" {
    PATH=$(pwd)/test/adaptiveperf/mock_test14:$PATH run ! ./adaptiveperf ls

    assert_line --partial "NUMA balancing is enabled"
    assert_line --partial "No more NUMA nodes available for profiling"
}
