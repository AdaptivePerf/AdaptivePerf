# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) CERN. See LICENSE for details.

from __future__ import print_function
import os
import sys
import re
import json
import subprocess
import socket
from pathlib import Path
from collections import defaultdict

sys.path.append(os.environ['PERF_EXEC_PATH'] +
                '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

cur_code = [32]

def next_code():
    global cur_code
    res = ''.join(map(chr, cur_code))

    for i in range(len(cur_code)):
        cur_code[i] += 1

        if cur_code[i] <= 126:
            break
        else:
            cur_code[i] = 32

            if i == len(cur_code) - 1:
                cur_code.append(32)

    return res


event_sock = None
tid_dict = {}
cpp_filt = None
cpp_filt_cache = {}
callchain_dict = defaultdict(next_code)


def demangle(name):
    global cpp_filt, cpp_filt_cache

    if name in cpp_filt_cache:
        return cpp_filt_cache[name]

    stdin = cpp_filt.stdin
    stdin.write((name + '\n').encode())
    stdin.flush()

    stdout = cpp_filt.stdout
    result = stdout.readline().decode().strip()
    cpp_filt_cache[name] = result
    return result


def syscall_callback(stack, ret_value):
    global event_sock, cpp_filt

    if int(ret_value) == 0:
        return

    event_sock.sendall((json.dumps([
        '<SYSCALL>', str(ret_value), list(map(
            lambda x: callchain_dict[x['sym']['name']] if 'sym' in x else
            '[' + Path(x['dso']).name + ']' if 'dso' in x else
            f'({x["ip"]:#x})', stack))
        ]) + '\n').encode('utf-8'))


def syscall_tree_callback(syscall_type, comm_name, pid, tid, time,
                          ret_value):
    global event_sock

    event_sock.sendall((json.dumps([
        '<SYSCALL_TREE>',
        syscall_type,
        comm_name,
        str(pid),
        str(tid),
        time,
        str(ret_value)]) + '\n').encode('utf-8'))


def trace_begin():
    global event_sock, cpp_filt

    cpp_filt = subprocess.Popen(['c++filt', '-p'],
                                stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE)

    event_sock = socket.socket()
    event_sock.connect((os.environ['APERF_SERV_ADDR'],
                        int(os.environ['APERF_SERV_PORT'])))


def trace_end():
    global event_sock, cpp_filt, callchain_dict

    event_sock.sendall('<STOP>\n'.encode('utf-8'))
    event_sock.close()

    cpp_filt.terminate()

    reverse_callchain_dict = {v: k for k, v in callchain_dict.items()}

    with open('syscall_callchains.json', mode='w') as f:
        f.write(json.dumps(reverse_callchain_dict) + '\n')


def syscalls__sys_exit_clone3(event_name, context, common_cpu, common_secs,
                              common_nsecs, common_pid, common_comm,
                              common_callchain, __syscall_nr, ret,
                              perf_sample_dict):
    syscall_callback(perf_sample_dict['callchain'], ret)
    syscall_tree_callback('clone3', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)


def syscalls__sys_exit_clone(event_name, context, common_cpu, common_secs,
                             common_nsecs, common_pid, common_comm,
                             common_callchain, __syscall_nr, ret,
                             perf_sample_dict):
    syscall_callback(perf_sample_dict['callchain'], ret)
    syscall_tree_callback('clone', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)


def syscalls__sys_exit_vfork(event_name, context, common_cpu, common_secs,
                             common_nsecs, common_pid, common_comm,
                             common_callchain, __syscall_nr, ret,
                             perf_sample_dict):
    syscall_callback(perf_sample_dict['callchain'], ret)
    syscall_tree_callback('vfork', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)


def syscalls__sys_exit_fork(event_name, context, common_cpu, common_secs,
                            common_nsecs, common_pid, common_comm,
                            common_callchain, __syscall_nr, ret,
                            perf_sample_dict):
    syscall_callback(perf_sample_dict['callchain'], ret)
    syscall_tree_callback('fork', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)


def syscalls__sys_exit_execve(event_name, context, common_cpu, common_secs,
                              common_nsecs, common_pid, common_comm,
                              common_callchain, __syscall_nr, ret,
                              perf_sample_dict):
    syscall_tree_callback('execve', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)


def syscalls__sys_enter_exit(event_name, context, common_cpu, common_secs,
                             common_nsecs, common_pid, common_comm,
                             common_callchain, __syscall_nr, error_code,
                             perf_sample_dict):
    syscall_tree_callback('exit', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], error_code)


def syscalls__sys_enter_exit_group(event_name, context, common_cpu,
                                   common_secs, common_nsecs, common_pid,
                                   common_comm, common_callchain, __syscall_nr,
                                   error_code, perf_sample_dict):
    syscall_tree_callback('exit_group', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], error_code)
