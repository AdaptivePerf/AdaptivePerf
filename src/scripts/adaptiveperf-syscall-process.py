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
callchain_dict = defaultdict(next_code)
perf_map_paths = set()


def syscall_callback(stack, ret_value):
    global event_sock, cpp_filt, perf_map_paths

    if int(ret_value) == 0:
        return

    def process_callchain_elem(elem):
        if 'dso' in elem and \
           re.search(r'^perf\-\d+\.map$', Path(elem['dso']).name) is not None:
            p = Path(elem['dso'])
            perf_map_paths.add(str(p))
            return f'({elem["ip"]:#x};{p.name})'
        elif 'sym' in elem and 'name' in elem['sym']:
            return callchain_dict[elem['sym']['name']]
        elif 'dso' in elem:
            p = Path(elem['dso'])
            return '[' + p.name + ']'
        else:
            return f'({x["ip"]:#x})'

    event_sock.sendall((json.dumps([
        '<SYSCALL>', str(ret_value), list(map(process_callchain_elem, stack))
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
    global event_sock

    event_sock = socket.socket()
    event_sock.connect((os.environ['APERF_SERV_ADDR'],
                        int(os.environ['APERF_SERV_PORT'])))


def trace_end():
    global event_sock, callchain_dict, perf_map_paths

    event_sock.sendall('<STOP>\n'.encode('utf-8'))
    event_sock.close()

    reverse_callchain_dict = {v: k for k, v in callchain_dict.items()}

    with open('syscall_callchains.json', mode='w') as f:
        f.write(json.dumps(reverse_callchain_dict) + '\n')

    with open('perf_map_paths_syscalls.data', mode='w') as f:
        f.write('\n'.join(perf_map_paths) + '\n')


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
