#!/bin/bash

# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) CERN. See LICENSE for details.

set +e
mkdir /tmp/adaptiveperf.pid.$$

CUR_DIR=$(pwd)
TO_PROFILE="${args[command]}"

cd /tmp/adaptiveperf.pid.$$

function cleanup() {
    pkill -P $$
    rm -rf /tmp/adaptiveperf.pid.$$
}

trap 'cleanup' EXIT

function print_notice() {
    echo "AdaptivePerf: comprehensive profiling tool based on Linux perf"
    echo "Copyright (C) CERN."
    echo ""
    echo "This program is free software; you can redistribute it and/or"
    echo "modify it under the terms of the GNU General Public License"
    echo "as published by the Free Software Foundation; only version 2."
    echo ""
    echo "This program is distributed in the hope that it will be useful,"
    echo "but WITHOUT ANY WARRANTY; without even the implied warranty of"
    echo "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"
    echo "GNU General Public License for more details."
    echo ""
    echo "You should have received a copy of the GNU General Public License"
    echo "along with this program; if not, write to the Free Software"
    echo "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,"
    echo "MA 02110-1301, USA."
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
    if [[ $2 -eq 1 ]]; then
        echo -e "\033[1;31m==> $1\033[0m"
    else
        echo -e "\033[1;32m==> $1\033[0m"
    fi
}

function handle_unknown_error() {
    echo_main "An unknown error has occurred! If the issue persists, please contact the AdaptivePerf developers, citing \"line $1\"." 1
    exit 2
}

ENABLE_ERR_TRAP="trap 'handle_unknown_error \$LINENO' ERR"
DISABLE_ERR_TRAP="trap - ERR"

function check_system_config() {
    if ! command -v ss &> /dev/null; then
        echo_sub "ss not found! Please install it before using AdaptivePerf." 1
        exit 1
    fi

    if ! command -v c++filt &> /dev/null; then
        echo_sub "c++filt not found! C++ names will not be demangled in perf symbol maps (used e.g. for JIT-ed codes)."
    fi

    if ! grep -qs '/sys/kernel/debug ' /proc/mounts; then
        echo_sub "/sys/kernel/debug is not mounted, mounting..."
        if ! sudo mount -t debugfs none /sys/kernel/debug; then
            echo_sub "Could not mount /sys/kernel/debug! Exiting." 1
            exit 2
        fi
    fi

    if ! paranoid=$(sysctl -n kernel.perf_event_paranoid); then
        echo_sub "Could not run \"sysctl -n kernel.perf_event_paranoid\"! Exiting." 1
        exit 2
    fi

    if ! max_stack=$(sysctl -n kernel.perf_event_max_stack); then
        echo_sub "Could not run \"sysctl -n kernel.perf_event_max_stack\"! Exiting." 1
        exit 2
    fi

    if [[ $paranoid -ne -1 ]]; then
        echo_sub "kernel.perf_event_paranoid is not -1! Please run \"sysctl kernel.perf_event_paranoid=-1\" before profiling." 1
        exit 1
    fi

    echo_sub "Note that stacks with more than $max_stack entries/entry *WILL* be broken in your results! To avoid that, run \"sysctl kernel.perf_event_max_stack=<larger value>\"."
    echo_sub "Remember that max stack values larger than 1024 are currently *NOT* supported for off-CPU stacks (they will be capped at 1024 entries)."
}

function check_cores() {
    num_proc=$(nproc)
    POST_PROCESSING_PARAM=$1

    if [[ $1 -eq 0 ]]; then
        echo_sub "AdaptivePerf called with -p 0, proceeding..."

        PERF_MASK="0-$((num_proc - 1))"
        PROFILE_MASK=$PERF_MASK
        NUM_PERF_PROC=$num_proc
        POST_PROCESSING_PARAM=$num_proc
    else
        if [[ $num_proc -lt 4 ]]; then
            echo_sub "Because there are fewer than 4 logical cores, the value of -p will be ignored for the profiled program unless it is 0."
        fi

        if [[ $num_proc -eq 1 ]]; then
            if [[ $2 == "" && $3 == "" ]]; then
                echo_sub "Running profiling along with post-processing is *NOT* recommended on a machine with only one logical core! You are very likely to get inconsistent results due to profiling threads interfering with the profiled program." 1
                echo_sub "Please delegate post-processing to another machine via TCP/UDP by using the -a flag. If you want to proceed anyway, run AdaptivePerf with -p 0." 1
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
        echo_sub "numactl not found! No NUMA checks will be performed."
        echo_sub "If you do not have NUMA, you can safely ignore this message."
        echo_sub "Otherwise, it is *STRONGLY* recommended to install numactl so that AdaptivePerf can perform the necessary checks."
    else
        if ! numa_balancing=$(sysctl -n kernel.numa_balancing); then
            echo_sub "Could not run \"sysctl -n kernel.numa_balancing\"! Exiting." 1
            exit 2
        fi

        if ! available_cpu_nodes=( $(numactl -s | sed -nE 's/^cpubind: ([0-9 ]+)/\1/p') ); then
            echo_sub "Could not run \"numactl -s\" to determine CPU NUMA nodes! Exiting." 1
            exit 2
        fi

        if ! available_mem_nodes=( $(numactl -s | sed -nE 's/^membind: ([0-9 ]+)/\1/p') ); then
            echo_sub "Could not run \"numactl -s\" to determine memory NUMA nodes! Exiting." 1
            exit 2
        fi

        if [[ $numa_balancing -eq 1 && (${#available_cpu_nodes[@]} -gt 1 || ${#available_mem_nodes[@]} -gt 1) ]]; then
            echo_sub "NUMA balancing is enabled and AdaptivePerf is running on more than 1 NUMA node!" 1
            echo_sub "As this will result in broken stacks, AdaptivePerf will not run." 1
            echo_sub "Please disable balancing by running \"sysctl kernel.numa_balancing=0\"." 1
            echo_sub "Alternatively, you can bind AdaptivePerf *both* CPU- and memory-wise to a single NUMA node, e.g. through numactl." 1

            exit 1
        else
            echo_sub "NUMA balancing is disabled or AdaptivePerf is running on a single NUMA node, proceeding."
        fi
    fi
}

function prepare_results_dir() {
    printf -v date "%(%Y_%m_%d_%H_%M_%S)T" -1  # Based on https://stackoverflow.com/a/1401495
    PROFILED_FILENAME=$(basename "$TO_PROFILE" | cut -f1 -d ' ')
    RESULT_NAME=${date}_$(hostname)__$PROFILED_FILENAME

    if [[ $1 == "" ]]; then
        RESULT_DIR=$CUR_DIR/results/$RESULT_NAME
        RESULT_OUT=$CUR_DIR/results/$RESULT_NAME/out
        RESULT_PROCESSED=$CUR_DIR/results/$RESULT_NAME/processed
    else
        RESULT_DIR=/tmp/adaptiveperf.pid.$$/results/$RESULT_NAME
        RESULT_OUT=/tmp/adaptiveperf.pid.$$/results/$RESULT_NAME/out
        RESULT_PROCESSED=/tmp/adaptiveperf.pid.$$/results/$RESULT_NAME/processed
    fi

    if ! mkdir -p $RESULT_DIR $RESULT_OUT $RESULT_PROCESSED; then
        echo_sub "Could not create $RESULT_DIR and/or $RESULT_OUT! Exiting." 1
        exit 2
    fi
}

function perf_record() {
    if [[ $1 == "" ]]; then
        echo_sub "Starting adaptiveperf-server and tracers..."
    else
        echo_sub "Connecting to adaptiveperf-server and starting tracers..."
    fi

    echo_sub "If AdaptivePerf hangs here, checking the logs in the path below *BEFORE* exiting may provide hints why this happens."
    echo_sub $RESULT_OUT

    eval "event_args=($6)"
    pipe_triggers=${#event_args[@]}
    pipe_triggers=$(((pipe_triggers+1)*POST_PROCESSING_PARAM+1))
    pipe_triggers=$((pipe_triggers > NUM_PERF_PROC ? pipe_triggers : NUM_PERF_PROC))

    if [[ $1 == "" ]]; then
        connected=0
        serv_addr="127.0.0.1"
        serv_port=5000
        serv_buf_size=$8

        while [[ $connected -eq 0 ]]; do
            while ss -tulpn | grep LISTEN | grep ":${serv_port}" &> /dev/null; do
                serv_port=$((serv_port+1))
            done

            adaptiveperf-server -q -m 0 -p $serv_port -b $serv_buf_size &
            serv_pid=$!

            connected=1
            while ! 2> /dev/null exec 3<> /dev/tcp/$serv_addr/$serv_port; do
                if ! ps -p $serv_pid &> /dev/null; then
                    eval $DISABLE_ERR_TRAP
                    wait $serv_pid
                    code=$?
                    eval $ENABLE_ERR_TRAP

                    if [[ $code -eq 100 ]]; then
                        serv_port=$((serv_port+1))
                        connected=0
                        break
                    else
                        echo_sub "adaptiveperf-server has finished with non-zero exit code $code. Exiting." 1
                        exit 2
                    fi
                fi
            done
        done
    else
        IFS=':' read -ra addr_parts <<< "$1"  # Based on https://stackoverflow.com/a/918931
        serv_addr=${addr_parts[0]}
        serv_port=${addr_parts[1]}

        if ! exec 3<> /dev/tcp/$serv_addr/$serv_port; then
            echo_sub "Could not connect to $serv_addr:$serv_port! Please check your address and try again." 1
            exit 2
        fi
    fi

    if ! echo "start${pipe_triggers} $RESULT_NAME" >&3; then
        echo_sub "I/O error has occurred in the communication with adaptiveperf-server (start)! Exiting." 1
        exit 2
    fi

    if ! echo $PROFILED_FILENAME >&3; then
        echo_sub "I/O error has occurred in the communication with adaptiveperf-server (filename)! Exiting." 1
        exit 2
    fi

    if ! read -u 3 -ra event_ports; then
        echo_sub "I/O error has occurred in the communication with adaptiveperf-server (event_ports)! Exiting." 1
        exit 2
    fi

    if [[ ${event_ports[0]} == error* ]]; then
        echo_sub "adaptiveperf-server has encountered an error! Exiting." 1
        exit 2
    fi

    sleep_time=$7

    read -u 3 && [[ $REPLY == "start_profile" ]] && echo_sub "All tracers have signalled their readiness, starting the code in $sleep_time second(s)..." && sleep $sleep_time && cd $CUR_DIR && echo_sub "Executing the code..." && start_time=$(date +%s%3N) && taskset -c $PROFILE_MASK $TO_PROFILE 1> $RESULT_OUT/stdout.log 2> $RESULT_OUT/stderr.log && end_time=$(date +%s%3N) && echo_sub "Code execution completed in $(($end_time - $start_time)) ms!" &

    to_profile_pid=$!

    alternative_perf=$9

    if [[ $alternative_perf != "" && ! -n "$PERF_PYTHON_PATH" ]]; then
        perf_path=$(which perf)
        if [[ -d /libexec/perf-core/scripts/python && $perf_path == /bin/* ]]; then
            PERF_PYTHON_PATH=/libexec/perf-core/scripts/python
        elif [[ -d /usr/libexec/perf-core/scripts/python && $perf_path == /usr/bin/* ]]; then
            PERF_PYTHON_PATH=/usr/libexec/perf-core/scripts/python
        elif [[ -d /usr/local/libexec/perf-core/scripts/python && $perf_path == /usr/local/bin/* ]]; then
            PERF_PYTHON_PATH=/usr/local/libexec/perf-core/scripts/python
        else
            echo_sub "perf with Python support is either not installed or installed in a custom directory!" 1
            echo_sub "" 1
            echo_sub "Please install it or set the PERF_PYTHON_PATH environment variable to where Python" 1
            echo_sub "scripts for perf are stored (usually your perf setup prefix + libexec/perf-core/scripts/python)." 1
            echo_sub "" 1
            echo_sub "Alternatively, run AdaptivePerf without the -l flag." 1
            exit 1
        fi
    fi

    if [[ $alternative_perf != "" ]]; then
        if ! (sudo taskset -c $PERF_MASK $PERF_PYTHON_PATH/bin/adaptiveperf-syscall-process-record "-o - --pid=$to_profile_pid" | APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT=${event_ports[0]} PERF_EXEC_PATH=$PERF_PYTHON_PATH/../.. taskset -c $PERF_MASK $PERF_PYTHON_PATH/bin/adaptiveperf-syscall-process-report) 1> $RESULT_OUT/perf_syscall_stdout.log 2> $RESULT_OUT/perf_syscall_stderr.log; then
            cp $RESULT_OUT/perf_syscall_*.log $CUR_DIR
            echo_sub "Syscall profiling has failed! The logs (perf_syscall...) have been copied to the current directory." 1
            kill $to_profile_pid
        fi &
        SYSCALLS_PID=$!
    else
        if ! APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT=${event_ports[0]} sudo -E taskset -c $PERF_MASK perf script adaptiveperf-syscall-process " --pid=$to_profile_pid" 1> $RESULT_OUT/perf_syscall_stdout.log 2> $RESULT_OUT/perf_syscall_stderr.log; then
            cp $RESULT_OUT/perf_syscall_*.log $CUR_DIR
            echo_sub "Syscall profiling has failed! The logs (perf_syscall...) have been copied to the current directory." 1
            kill $to_profile_pid
        fi &
        SYSCALLS_PID=$!
    fi

    if [[ $alternative_perf != "" ]]; then
        if ! (sudo taskset -c $PERF_MASK $PERF_PYTHON_PATH/bin/adaptiveperf-process-record "-e task-clock -F $2 --off-cpu $3 --buffer-events $4 --buffer-off-cpu-events $5 -o - --pid=$to_profile_pid" | APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT="${event_ports[@]:1:$POST_PROCESSING_PARAM}" PERF_EXEC_PATH=$PERF_PYTHON_PATH/../.. taskset -c $PERF_MASK $PERF_PYTHON_PATH/bin/adaptiveperf-process-report) 1> $RESULT_OUT/perf_main_stdout.log 2> $RESULT_OUT/perf_main_stderr.log; then
            cp $RESULT_OUT/perf_main_*.log $CUR_DIR
            echo_sub "Wall time profiling has failed! The logs (perf_main...) have been copied to the current directory." 1
            kill $to_profile_pid
        fi &
        SAMPLING_PID=$!
    else
        if ! APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT="${event_ports[@]:1:$POST_PROCESSING_PARAM}" sudo -E taskset -c $PERF_MASK perf script adaptiveperf-process " -e task-clock -F $2 --off-cpu $3 --buffer-events $4 --buffer-off-cpu-events $5 --pid=$to_profile_pid" 1> $RESULT_OUT/perf_main_stdout.log 2> $RESULT_OUT/perf_main_stderr.log; then
            cp $RESULT_OUT/perf_main_*.log $CUR_DIR
            echo_sub "Wall time profiling has failed! The logs (perf_main...) have been copied to the current directory." 1
            kill $to_profile_pid
        fi &
        SAMPLING_PID=$!
    fi

    EXTRA_EVENTS=()
    OTHER_PIDS=()

    eval "event_args=($6)"
    start_index=$((POST_PROCESSING_PARAM+1))

    for ev in "${event_args[@]}"; do
        if [[ $ev == "" ]]; then
            continue
        fi

        IFS=',' read -ra event_parts <<< "$ev"  # Based on https://stackoverflow.com/a/918931

        if [[ $alternative_perf != "" ]]; then
            if ! (sudo taskset -c $PERF_MASK $PERF_PYTHON_PATH/bin/adaptiveperf-process-record "-e ${event_parts[0]}/period=${event_parts[1]}/ --pid=$to_profile_pid -o -" | APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT="${event_ports[@]:$start_index:$POST_PROCESSING_PARAM}" PERF_EXEC_PATH=$PERF_PYTHON_PATH/../.. taskset -c $PERF_MASK $PERF_PYTHON_PATH/bin/adaptiveperf-process-report) 1> $RESULT_OUT/perf_${event_parts[0]}_stdout.log 2> $RESULT_OUT/perf_${event_parts[0]}_stderr.log; then
                cp $RESULT_OUT/perf_${event_parts[0]}_*.log $CUR_DIR
                echo_sub "${event_parts[0]} profiling has failed! The logs (perf_${event_parts[0]}...) have been copied to the current directory." 1
                kill $to_profile_pid
            fi &
            OTHER_PIDS+=($!)
        else
            if ! APERF_SERV_ADDR=$serv_addr APERF_SERV_PORT="${event_ports[@]:$start_index:$POST_PROCESSING_PARAM}" sudo -E taskset -c $PERF_MASK perf script adaptiveperf-process " -e ${event_parts[0]}/period=${event_parts[1]}/ --pid=$to_profile_pid" 1> $RESULT_OUT/perf_${event_parts[0]}_stdout.log 2> $RESULT_OUT/perf_${event_parts[0]}_stderr.log; then
                cp $RESULT_OUT/perf_${event_parts[0]}_*.log $CUR_DIR
                echo_sub "${event_parts[0]} profiling has failed! The logs (perf_${event_parts[0]}...) have been copied to the current directory." 1
                kill $to_profile_pid
            fi &
            OTHER_PIDS+=($!)
        fi

        echo ${event_parts[0]} ${event_parts[2]} >> $RESULT_OUT/event_dict.data
        EXTRA_EVENTS+=(${event_parts[0]})

        start_index=$((start_index+POST_PROCESSING_PARAM))
    done

    eval $DISABLE_ERR_TRAP

    wait $to_profile_pid
    code=$?

    if [[ $code -ne 0 ]]; then
        wait $SYSCALLS_PID
        wait $SAMPLING_PID
        for i in ${OTHER_PIDS[@]}; do
            wait $i
        done

        echo_sub "Profiled program or its wrapper has finished with non-zero exit code $code. This is expected if you have seen \"Profiling has failed\" errors. Exiting." 1
        exit 2
    fi

    eval $ENABLE_ERR_TRAP
}

function process_results() {
    eval $DISABLE_ERR_TRAP

    wait $SYSCALLS_PID
    syscalls_code=$?

    wait $SAMPLING_PID
    sampling_code=$?

    other_codes=()
    for i in ${OTHER_PIDS[@]}; do
        wait $i
        other_codes+=($?)
    done

    eval $ENABLE_ERR_TRAP

    error=false

    if [[ $syscalls_code -ne 0 ]]; then
        echo_sub "The syscall profiler has returned non-zero exit code $syscalls_code." 1
        error=true
    fi

    if [[ $sampling_code -ne 0 ]]; then
        echo_sub "The on-CPU/off-CPU profiler has returned non-zero exit code $sampling_code." 1
        error=true
    fi

    index=0
    for i in ${other_codes[@]}; do
        if [[ $i -ne 0 ]]; then
            echo_sub "The custom event profiler with ID $index has returned non-zero exit code $i." 1
            error=true
        fi
        index=$((index+1))
    done

    if [[ $error == true ]]; then
        echo_sub "One or more profilers have encountered an error. Exiting." 1
        exit 2
    fi

    if ! read -u 3 -r msg; then
        echo_sub "I/O error has occurred in the communication with adaptiveperf-server (first_after_profile)! Exiting." 1
        exit 2
    fi

    if [[ $msg != "out_files" ]]; then
        echo_sub "adaptiveperf-server has not indicated its successful completion! Exiting." 1
        exit 2
    fi

    if ! compgen -G "perf_map_paths_*.data" > /dev/null; then
        echo_sub "perf_map_paths_\*.data files have not been produced! Exiting." 1
        exit 2
    fi

    perf_map_paths=$(cat perf_map_paths_*.data | sort | uniq)
    rm -f perf_map_paths_*.data

    for perf_map_path in "$perf_map_paths"; do
        if [[ -f $perf_map_path ]]; then
            if command -v c++filt &> /dev/null; then
                cat $perf_map_path | c++filt > $(basename $perf_map_path)
            else
                cp $perf_map_path .
            fi
        fi
    done

    if [[ $1 != "" ]]; then
        for filename in *.map *.json; do
            len=$(wc -c < "$filename")

            if ! echo "$len p $filename" >&3; then
                echo_sub "I/O error has occurred in the communication with adaptiveperf-server (processed_file)! Exiting." 1
                exit 2
            fi

            if ! read -u 3 -r msg; then
                echo_sub "I/O error has occurred in the communication with adaptiveperf-server (processed_file_confirm)! Exiting." 1
                exit 2
            fi

            if [[ $msg != "ok" ]]; then
                echo_sub "Could not transmit processed file details to adaptiveperf-server! Exiting." 1
                exit 2
            fi
        done

        for file in $RESULT_OUT/*; do
            filename=$(basename $file)
            len=$(wc -c < "$file")

            if ! echo "$len o $filename" >&3; then
                echo_sub "I/O error has occurred in the communication with adaptiveperf-server (out_file)! Exiting." 1
                exit 2
            fi

            if ! read -u 3 -r msg; then
                echo_sub "I/O error has occurred in the communication with adaptiveperf-server (out_file_confirm)! Exiting." 1
                exit 2
            fi

            if [[ $msg != "ok" ]]; then
                echo_sub "Could not transmit out file details to adaptiveperf-server! Exiting." 1
                exit 2
            fi
        done
    fi

    if ! echo "<STOP>" >&3; then
        echo_sub "I/O error has occurred in the communication with adaptiveperf-server (file_stop)! Exiting." 1
        exit 2
    fi

    if [[ $1 != "" ]]; then
        for file in *.map *.json; do
            if ! cat $file >&3; then
                echo_sub "I/O error has occurred in the communication with adaptiveperf-server (processed_file_send)! Exiting." 1
                exit 2
            fi
        done

        for file in $RESULT_OUT/*; do
            if ! cat $file >&3; then
                echo_sub "I/O error has occurred in the communication with adaptiveperf-server (out_file_send)! Exiting." 1
                exit 2
            fi
        done
    fi

    if ! read -u 3 -r final; then
        echo_sub "I/O error has occurred in the communication with adaptiveperf-server (final)! Exiting." 1
        exit 2
    fi

    if ! exec 3>&-; then
        echo_sub "Could not close the socket connection with adaptiveperf-server! Exiting." 1
        exit 2
    fi

    if [[ $final != "finished" ]]; then
        echo_sub "adaptiveperf-server has not indicated its successful completion! Exiting." 1
        exit 2
    fi

    if [[ $1 == "" ]]; then
        if ! cp -rT $RESULT_NAME/ $RESULT_DIR/; then
            echo_sub "Could not copy the results to the final directory! Exiting." 1
            exit 2
        fi

        if compgen -G "*.map" > /dev/null; then
            if ! cp *.map $RESULT_PROCESSED; then
                echo_sub "Could not copy the perf symbol maps to the final directory! Exiting." 1
                exit 2
            fi
        fi

        if compgen -G "*.json" > /dev/null; then
            if ! cp *.json $RESULT_PROCESSED; then
                echo_sub "Could not copy the callchain dictionaries to the final directory! Exiting." 1
                exit 2
            fi
        fi
    fi
}

eval $ENABLE_ERR_TRAP
script_start_time=$(date +%s%3N)

print_notice

echo_main "Checking system configuration..."
check_system_config

echo_main "Checking CPU specification..."
check_cores ${args[--post-process]} "${args[--tcp]}" "${args[--udp]}"

echo_main "Checking for NUMA..."
check_numa

echo_main "Preparing results directory..."
prepare_results_dir "${args[--address]}"

echo_main "Profiling..."
perf_record "${args[--address]}" ${args[--freq]} ${args[--off-cpu-freq]} ${args[--buffer]} ${args[--off-cpu-buffer]} "${args[--event]}" ${args[--warmup]} ${args[--server-buffer]} "${args[--alternative]}"

echo_main "Processing results..."
process_results "${args[--address]}"

script_end_time=$(date +%s%3N)

echo_main "Done in $(($script_end_time - $script_start_time)) ms in total! You can check the results directory now."
