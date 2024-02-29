#!/bin/bash
set -e

mkdir /tmp/adaptiveperf.pid.$$

CUR_DIR=$(pwd)
TO_PROFILE="${args[command]}"

cd /tmp/adaptiveperf.pid.$$

trap 'rm -rf /tmp/adaptiveperf.pid.$$' EXIT

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

function check_cores() {
    num_proc=$(nproc)

    if [[ $1 -eq 0 ]]; then
        echo_sub "AdaptivePerf called with -p 0, proceeding..."

        PERF_MASK="0-$((num_proc - 1))"
        PROFILE_MASK=$PERF_MASK
        NUM_PERF_PROC=$num_proc
    else
        if [[ $num_proc -lt 4 ]]; then
            echo_sub "Because there are fewer than 4 logical cores, the value of -p will be ignored unless it is 0."
        fi

        if [[ $num_proc -eq 1 ]]; then
            if [[ $2 == "" && $3 == "" ]]; then
                echo_sub "Running profiling along with post-processing is *NOT* recommended on a machine with only one logical core! You are very likely to get inconsistent results due to profiling threads interfering with the profiled program." 1
                echo_sub "Please delegate post-processing to another machine via TCP/UDP by using the -p flag. If you want to proceed anyway, run AdaptivePerf with -p 0." 1
                exit 1
            else
                echo_sub "1 logical core detected, running post-processing, perf, and the command on core #0 thanks to TCP/UDP delegation (you may still get inconsistent results, but it's less likely due to lighter on-site processing)."
                PERF_MASK="0"
                PROFILE_MASK="0"
                NUM_PERF_PROC=1
            fi
        elif [[ $num_proc -eq 2 ]]; then
            echo_sub "2 logical cores detected, running post-processing and perf on core #0 and the command on core #1."
            PERF_MASK="0"
            PROFILE_MASK="1"
            NUM_PERF_PROC=1
        elif [[ $num_proc -eq 3 ]]; then
            echo_sub "3 logical cores detected, running post-processing and perf on cores #0 and #1 and the command on core #2."
            PERF_MASK="0-1"
            PROFILE_MASK="2"
            NUM_PERF_PROC=2
        elif [[ $1 -gt $((num_proc - 3)) ]]; then
            echo_sub "The value of -p must be less than or equal to the number of logical cores minus 3 (i.e. $((num_proc - 3)))!" 1
            exit 1
        else
            PERF_MASK="2-$((2 + $1 - 1))"
            PROFILE_MASK="$((2 + $1))-$((num_proc - 1))"
            NUM_PERF_PROC=$1
        fi
    fi
}

function check_numa() {
    if ! command -v numactl &> /dev/null; then
        echo_sub "numactl not found! Please install it first before using AdaptivePerf." 1
        exit 1
    fi

    numa_balancing=$(sysctl -n kernel.numa_balancing)
    available_cpu_nodes=( $(numactl -s | sed -nE 's/^cpubind: ([0-9 ]+)/\1/p') )
    available_mem_nodes=( $(numactl -s | sed -nE 's/^membind: ([0-9 ]+)/\1/p') )

    if [[ $numa_balancing -eq 1 && (${#available_cpu_nodes[@]} -gt 1 || ${#available_mem_nodes[@]} -gt 1) ]]; then
        echo_sub "NUMA balancing is enabled and AdaptivePerf is running on more than 1 NUMA node!" 1
        echo_sub "As this will result in broken stacks, AdaptivePerf will not run." 1
        echo_sub "Please disable balancing by running \"sysctl kernel.numa_balancing=0\"." 1
        echo_sub "Alternatively, you can bind AdaptivePerf *both* CPU- and memory-wise to a single NUMA node, e.g. through numactl." 1

        exit 1
    else
        echo_sub "NUMA balancing is disabled or AdaptivePerf is running on a single NUMA node, proceeding."
    fi
}

function prepare_results_dir() {
    printf -v date "%(%Y_%m_%d_%H_%M_%S)T" -1  # Based on https://stackoverflow.com/a/1401495
    PROFILED_FILENAME=$(basename "$TO_PROFILE" | cut -f1 -d ' ')
    RESULT_NAME=${date}_$(hostname)_$PROFILED_FILENAME

    RESULT_DIR=$CUR_DIR/results/$RESULT_NAME
    RESULT_OUT=$CUR_DIR/results/$RESULT_NAME/out

    mkdir -p $RESULT_DIR $RESULT_OUT
}

function perf_record() {
    if [[ $1 == "" ]]; then
        echo_sub "Starting adaptiveperf-server and tracers..."
    else
        echo_sub "Connecting to adaptiveperf-server and starting tracers..."
    fi

    eval "event_args=($6)"
    pipe_triggers=${event_args[@]}
    pipe_triggers=$((pipe_triggers+2))
    pipe_triggers=$((pipe_triggers > NUM_PERF_PROC ? pipe_triggers : NUM_PERF_PROC))

    if [[ $1 == "" ]]; then
        connected=0
        serv_addr="127.0.0.1"
        serv_port=5000
        while [[ connected -eq 0 ]]; do
            adaptiveperf-server -q -m 0 -p $serv_port &
            serv_pid=$!

            connected=1
            while ! 2> /dev/null exec 3<> /dev/tcp/$serv_addr/$serv_port; do
                if ! ps -p $serv_pid &> /dev/null; then
                    set +e
                    wait $serv_pid
                    code=$?
                    set -e

                    if [[ $code -eq 100 ]]; then
                        echo_sub "Port $serv_port is taken for adaptiveperf-server, trying $((serv_port+1))..."
                        serv_port=$((serv_port+1))
                        connected=0
                        break
                    else
                        exit 1
                    fi
                fi
            done
        done
    else
        while IFS=':' read -ra addr_parts; do
            serv_addr=${addr_parts[0]}
            serv_port=${addr_parts[1]}
        done <<< "$1"

        exec 3<> /dev/tcp/$serv_addr/$serv_port
    fi

    echo "start${pipe_triggers} $RESULT_NAME" >&3
    echo $PROFILED_FILENAME >&3
    read -u 3 -ra event_ports

    read -u 3 && [[ $REPLY == "start_profile" ]] && sleep 1 && cd $CUR_DIR && echo_sub "Executing the code..." && start_time=$(date +%s%3N) && taskset -c $PROFILE_MASK $TO_PROFILE 1> $RESULT_OUT/stdout.log 2> $RESULT_OUT/stderr.log && end_time=$(date +%s%3N) && echo_sub "Code execution completed in $(($end_time - $start_time)) ms!" &

    to_profile_pid=$!

    APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT=${event_ports[0]} sudo -E taskset -c $PERF_MASK perf script adaptiveperf-syscall-process " --pid=$to_profile_pid" 1> $RESULT_OUT/perf_syscall_stdout.log 2> $RESULT_OUT/perf_syscall_stderr.log &
    SYSCALLS_PID=$!

    echo ${event_ports[@]:1}

    APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT="${event_ports[@]:1}" sudo -E taskset -c $PERF_MASK perf script adaptiveperf-process " -e task-clock -F $2 --off-cpu $3 --buffer-events $4 --buffer-off-cpu-events $5 --pid=$to_profile_pid" 1> $RESULT_OUT/perf_main_stdout.log 2> $RESULT_OUT/perf_main_stderr.log &
    SAMPLING_PID=$!

    EXTRA_EVENTS=()
    OTHER_PIDS=()

    eval "event_args=($6)"

    for ev in "${event_args[@]}"; do
        if [[ $ev == "" ]]; then
            continue
        fi

        # Based on https://stackoverflow.com/a/918931
        while IFS=',' read -ra event_parts; do
            APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT="${event_ports[@]:1}" sudo -E taskset -c $PERF_MASK perf script adaptiveperf-process " -e ${event_parts[0]}/period=${event_parts[1]}/ --pid=$to_profile_pid" 1> $RESULT_OUT/perf_${event_parts[0]}_stdout.log 2> $RESULT_OUT/perf_${event_parts[0]}_stderr.log &
            OTHER_PIDS=($!)

            echo ${event_parts[0]} ${event_parts[2]} >> $RESULT_OUT/event_dict.data
            EXTRA_EVENTS+=(${event_parts[0]})
        done <<< "$ev"
    done

    wait $to_profile_pid
}

function process_results() {
    read -u 3 -r final
    exec 3>&-

    if [[ $final != "finished" ]]; then
        true
    fi

    cp -rT $RESULT_NAME/ $RESULT_DIR/
}

script_start_time=$(date +%s%3N)

print_notice

echo_main "Checking kernel settings..."
check_kernel_settings

echo_main "Checking CPU specification..."
check_cores ${args[--post-process]} "${args[--tcp]}" "${args[--udp]}"

echo_main "Checking for NUMA..."
check_numa

echo_main "Preparing results directory..."
prepare_results_dir

echo_main "Profiling..."
perf_record "${args[--address]}" ${args[--freq]} ${args[--off-cpu-freq]} ${args[--buffer]} ${args[--off-cpu-buffer]} "${args[--event]}"

echo_main "Processing results..."
process_results

script_end_time=$(date +%s%3N)

echo_main "Done in $(($script_end_time - $script_start_time)) ms in total! You can check the results directory now."
