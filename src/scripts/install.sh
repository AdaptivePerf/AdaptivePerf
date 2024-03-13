#!/bin/bash
set -e

if [[ ! -n "$PERF_EXEC_PATH" ]]; then
    if [[ -d /libexec/perf-core/scripts/python ]]; then
	PERF_EXEC_PATH=/libexec/perf-core/scripts/python
    elif [[ -d /usr/libexec/perf-core/scripts/python ]]; then
	PERF_EXEC_PATH=/usr/libexec/perf-core/scripts/python
    elif [[ -d /usr/local/libexec/perf-core/scripts/python ]]; then
	PERF_EXEC_PATH=/usr/local/libexec/perf-core/scripts/python
    else
	echo "perf with Python support is either not installed or installed in a custom directory!"
	echo ""
	echo "Please install it or set the PERF_EXEC_PATH environment variable to where Python"
	echo "scripts for perf are stored (usually your perf setup prefix + libexec/perf-core/scripts/python)."
	exit 1
    fi
fi

if [[ $1 == "uninstall" ]]; then
    rm $PERF_EXEC_PATH/bin/adaptiveperf-*
    rm $PERF_EXEC_PATH/adaptiveperf-*.py
else
    cp adaptiveperf-syscall-process-report adaptiveperf-syscall-process-record $PERF_EXEC_PATH/bin
    cp adaptiveperf-process-report adaptiveperf-process-record $PERF_EXEC_PATH/bin
    cp adaptiveperf-syscall-process.py adaptiveperf-process.py $PERF_EXEC_PATH/
fi
