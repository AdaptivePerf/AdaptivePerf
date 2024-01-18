#!/bin/bash
set -e

touch /tmp/adaptiveperf.pid.$$
trap 'rm -f /tmp/adaptiveperf.pid.$$' EXIT

TO_PROFILE="${args[command]}"

# From https://stackoverflow.com/a/246128
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source $SCRIPT_DIR/adaptiveperf-misc-funcs.sh

function print_notice() {
    echo "AdaptivePerf: comprehensive profiling tool based on Linux perf"
    echo "Copyright (C) CERN."
    echo ""
}

function echo_sub() {
    if [[ $2 -eq 1 ]]; then
        echo -e "\033[0;31m-> $1\033[0m"
    else
        echo -e "\033[0;34m-> $1\033[0m"
    fi
}

function echo_main() {
    echo -e "\033[1;32m==> $1\033[0m"
}

function check_kernel_settings() {
    if ! grep -qs '/sys/kernel/debug ' /proc/mounts; then
        echo_sub "/sys/kernel/debug is not mounted, mounting..."
        sudo mount -t debugfs none /sys/kernel/debug
    fi

    paranoid=$(sysctl -n kernel.perf_event_paranoid)
    max_stack=$(sysctl -n kernel.perf_event_max_stack)

    if [[ $paranoid -ne -1 ]]; then
        echo_sub "kernel.perf_event_paranoid is not -1! Please run \"sysctl kernel.perf_event_paranoid=-1\" before profiling." 1
        exit 1
    fi

    echo_sub "Note that stacks with more than $max_stack entries/entry *WILL* be broken in your results! To avoid that, run \"sysctl kernel.perf_event_max_stack=<larger value>\"."
    echo_sub "Remember that max stack values larger than 1024 are currently *NOT* supported for off-CPU stacks (they will be capped at 1024 entries)."
}

function check_numa() {
    if ! command -v numactl &> /dev/null; then
        echo_sub "numactl not found! Please install it first before using adaptiveperf." 1
        exit 1
    fi

    numa_balancing=$(sysctl -n kernel.numa_balancing)

    if [[ $numa_balancing -eq 1 ]]; then
        echo_sub "NUMA balancing is enabled, the code will be run on a single NUMA node only to avoid broken stacks."
        echo_sub "If it's not desired, run \"sysctl kernel.numa_balancing=0\" to disable NUMA balancing."

        available_nodes=( $(numactl -H | sed -nE 's/node ([0-9]+) size:.*/\1/p') )
        running_perf_instances=$(( $(ls /tmp/adaptiveperf.pid.* | wc -l) - 1))  # Do not count itself

        if [[ $running_perf_instances -ge ${#available_nodes[@]} ]]; then
            echo_sub "No more NUMA nodes available for profiling! Wait until at least one adaptiveperf instance finishes executing." 1
            exit 1
        fi

        NODE_NUM=${available_nodes[$running_perf_instances]}

        echo_sub "$((${#available_nodes[@]} - $running_perf_instances)) NUMA node(s) available for profiling, picking node number $NODE_NUM."
    else
        echo_sub "NUMA balancing is disabled, the code will be run on all NUMA nodes."
        echo_sub "If it's not desired, run \"sysctl kernel.numa_balancing=1\" to enable NUMA balancing."

        NODE_NUM=-1
    fi
}

function prepare_results_dir() {
    printf -v date "%(%Y_%m_%d_%H_%M_%S)T" -1  # Based on https://stackoverflow.com/a/1401495
    RESULT_STORAGE=${date}_$(hostname)_$(basename "$TO_PROFILE" | cut -f1 -d ' ')
    mkdir -p results/$RESULT_STORAGE

    echo $RESULT_STORAGE
}

function perf_record() {
    echo_sub "Starting tracers..."

    mkdir results/$RESULT_STORAGE/out
    result_out=$(pwd)/results/$RESULT_STORAGE/out

    mkfifo $result_out/waitpipe

    if [[ $NODE_NUM -eq -1 ]]; then
        cat $result_out/waitpipe > /dev/null && $TO_PROFILE 1> $result_out/stdout.log 2> $result_out/stderr.log &
    else
        cat $result_out/waitpipe > /dev/null && numactl --cpunodebind $NODE_NUM --membind $NODE_NUM $TO_PROFILE 1> $result_out/stdout.log 2> $result_out/stderr.log &
    fi

    to_profile_pid=$!
    syscall_list="syscalls:sys_exit_execve,syscalls:sys_exit_clone,syscalls:sys_exit_fork,syscalls:sys_exit_vfork,syscalls:sys_enter_exit,syscalls:sys_enter_exit_group"

    if [[ $(sudo perf list | grep syscalls:sys_exit_clone3 | wc -l) -gt 0 ]]; then
        syscall_list+=",syscalls:sys_exit_clone3"
    fi

    sudo perf record --call-graph fp -e $syscall_list -o $result_out/syscalls.data --pid=$to_profile_pid -k CLOCK_MONOTONIC &
    syscalls_pid=$!

    sudo perf record --call-graph fp -e task-clock -F $1 -o $result_out/perf.data --off-cpu --pid=$to_profile_pid -k CLOCK_MONOTONIC &
    sampling_pid=$!

    EXTRA_EVENTS=()
    other_pids=()
    eval "event_args=($2)"
    for ev in "${event_args[@]}"; do
        if [[ $ev == "" ]]; then
            continue
        fi

        # Based on https://stackoverflow.com/a/918931
        while IFS=',' read -ra event_parts; do
            sudo perf record --call-graph fp -e ${event_parts[0]}/period=${event_parts[1]}/ -o $result_out/extra_${event_parts[0]}.data --pid=$to_profile_pid -k CLOCK_MONOTONIC &
            other_pids+=($!)

            echo ${event_parts[0]} ${event_parts[2]} >> $result_out/event_dict.data
            EXTRA_EVENTS+=(${event_parts[0]})
        done <<< "$ev"
    done

    echo_sub "Waiting 10 seconds for the tracers to warm up..."
    sleep 10

    echo_sub "Executing the code..."

    start_time=$(date +%s%3N)

    echo 1 > $result_out/waitpipe
    wait $to_profile_pid

    end_time=$(date +%s%3N)
    runtime_length=$(($end_time - $start_time))

    wait $syscalls_pid &> /dev/null
    wait $sampling_pid &> /dev/null

    for pid in ${other_pids[@]}; do
        wait $pid &> /dev/null
    done

    rm $result_out/waitpipe

    sudo chown $(whoami) $result_out/*.data

    echo_sub "Code execution completed in $runtime_length ms!"
}

function clean_up_and_report() {
    cd results/$RESULT_STORAGE
    mv out/*.data .

    jobs=$1

    if [[ $jobs == "max" ]]; then
        jobs=$(nproc)
    fi

    max_stack=$(sysctl -n kernel.perf_event_max_stack)

    perf script -i perf.data -F comm,tid,pid,time,event,ip,sym,dso,period --ns --no-demangle --max-stack=$max_stack | c++filt -p | adaptiveperf-split-report $jobs script
    perf script -i syscalls.data --no-demangle --max-stack=$max_stack $SCRIPT_DIR/adaptiveperf-perf-get-callchain.py > new_proc_callchains.data

    for event in ${EXTRA_EVENTS[@]}; do
        i="extra_$event.data"
        perf script -i $i -F comm,tid,pid,time,event,ip,sym,dso,period --ns --no-demangle --max-stack=$max_stack | c++filt -p | adaptiveperf-split-report $jobs script_extra_$event
    done

    PIDS=()
    echo_sub "Running wall time post-processing..."
    for i in $(seq 0 $((jobs-1))); do
        script_i="script${i}.data"
        echo_sub "Starting $script_i..."
        (cat $script_i | stackcollapse-perf.pl --tid | convert_from_ns_to_us > ${script_i:0:-5}_collapsed.data) && (cat $script_i | sed -nE 's/^\S*\s*([0-9]+)\/([0-9]+)\s+([0-9\.]+):\s+([0-9]+)\s+offcpu-time.*/\1 \2 \3 \4/p' > offcpu${i}.data) && rm $script_i && echo_sub "$script_i done!" &
        PIDS+=($!)
    done

    for pid in ${PIDS[@]}; do
        wait $pid &> /dev/null
    done

    for i in offcpu*.data; do
        cat $i >> offcpu.data
        rm $i
    done

    adaptiveperf-merge $(ls script*_collapsed.data | sort -V) > script_collapsed.data.tmp
    rm script*_collapsed.data
    mv script_collapsed.data.tmp script_collapsed.data

    for i in ${EXTRA_EVENTS[@]}; do
        PIDS=()
        echo_sub "Running $i post-processing..."
        for script_i in script_extra_${i}*.data; do
            echo_sub "Starting $script_i..."
            (cat $script_i | stackcollapse-perf.pl --tid > ${script_i:0:-5}_collapsed.data) && rm $script_i && echo_sub "$script_i done!" &
            PIDS+=($!)
        done

        for pid in ${PIDS[@]}; do
            wait $pid &> /dev/null
        done

        adaptiveperf-merge $(ls script_extra_${i}*_collapsed.data | sort -V) > script_extra_${i}_collapsed.data.tmp
        rm script_extra_${i}*_collapsed.data
        mv script_extra_${i}_collapsed.data.tmp script_extra_${i}_collapsed.data
    done
}

function split_reports() {
    cd ../..

    if compgen -G "results/$RESULT_STORAGE/script_extra*_collapsed.data" > /dev/null; then
        adaptiveperf-split-ids results/$RESULT_STORAGE/script_collapsed.data results/$RESULT_STORAGE/overall_offcpu_collapsed.data results/$RESULT_STORAGE/script_extra*_collapsed.data
    else
        adaptiveperf-split-ids results/$RESULT_STORAGE/script_collapsed.data results/$RESULT_STORAGE/overall_offcpu_collapsed.data
    fi
}

function remove_leftovers_and_make_flame_graphs() {
    rm results/$RESULT_STORAGE/script*.data
    cd results/$RESULT_STORAGE/processed

    if compgen -G "*_no_overall_offcpu.data" > /dev/null; then
        for i in *_no_overall_offcpu.data; do
            flamegraph.pl --title="Wall time" --countname=us "$i" > "${i:0:-23}_walltime.svg"
        done
    fi

    if compgen -G "*_with_overall_offcpu.data" > /dev/null; then
        for i in *_with_overall_offcpu.data; do
            flamegraph.pl --title="Wall time (time-ordered)" --countname=us --flamechart "$i" > "${i:0:-25}_walltime_chart.svg"
        done
    fi

    if [[ ${#EXTRA_EVENTS[@]} -gt 0 ]]; then
        for ev in ${EXTRA_EVENTS[@]}; do
            for i in *_extra_${ev}.data; do
                flamegraph.pl --title="$ev" --colors=green --countname="occurrence(s)" "$i" > "${i:0:-5}.svg"
                flamegraph.pl --title="$ev (time-ordered)" --colors=green --countname="occurrence(s)" --flamechart "$i" > "${i:0:-5}_chart.svg"
            done
        done
    fi
}

print_notice

echo_main "Checking kernel settings..."
check_kernel_settings

echo_main "Checking for NUMA..."
check_numa

echo_main "Preparing results directory..."
prepare_results_dir

echo_main "Profiling..."
perf_record ${args[--freq]} "${args[--event]}"

echo_main "Cleaning up and creating collapsed reports..."
clean_up_and_report ${args[--threads]}

echo_main "Splitting reports into processes/threads..."
split_reports

echo_main "Removing leftover files and producing flame graphs..."
remove_leftovers_and_make_flame_graphs

echo_main "All finished! You can check the results directory now."
