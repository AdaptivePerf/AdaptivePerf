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


event_stream = None
tid_dict = {}
callchain_dict = defaultdict(next_code)
perf_map_paths = set()

def write(msg):
    global event_stream

    if isinstance(event_stream, socket.socket):
        event_stream.sendall((msg + '\n').encode('utf-8'))
    else:
        event_stream.write((msg + '\n').encode('utf-8'))
        event_stream.flush()


def syscall_callback(stack, ret_value):
    global perf_map_paths

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
            return f'({elem["ip"]:#x};{p.name})'
        else:
            return f'({elem["ip"]:#x})'

    write(json.dumps([
        '<SYSCALL>', str(ret_value), list(map(process_callchain_elem, stack))
    ]))


def syscall_tree_callback(syscall_type, comm_name, pid, tid, time,
                          ret_value):
    write(json.dumps([
        '<SYSCALL_TREE>',
        syscall_type,
        comm_name,
        str(pid),
        str(tid),
        time,
        str(ret_value)]))


def trace_begin():
    global event_stream

    serv_connect = os.environ['APERF_SERV_CONNECT'].split(' ')
    parts = serv_connect[1].split('_')

    if serv_connect[0] == 'tcp':
        event_stream = socket.socket()
        event_stream.connect((parts[0],
                              int(parts[1])))
    elif serv_connect[0] == 'pipe':
        event_stream = os.fdopen(int(parts[1]), 'wb')
        event_stream.write('connect'.encode('ascii'))
        event_stream.flush()


def trace_end():
    global event_stream, callchain_dict, perf_map_paths

    write('<STOP>')
    event_stream.close()

    reverse_callchain_dict = {v: k for k, v in callchain_dict.items()}

    with open('syscall_callchains.json', mode='w') as f:
        f.write(json.dumps(reverse_callchain_dict) + '\n')

    with open('perf_map_paths_syscalls.data', mode='w') as f:
        f.write('\n'.join(perf_map_paths) + '\n')


def sched__sched_process_fork(event_name, context, common_cpu,
	                          common_secs, common_nsecs, common_pid,
                              common_comm, common_callchain, parent_comm,
                              parent_pid, child_comm, child_pid,
		                      perf_sample_dict):
    syscall_callback(perf_sample_dict['callchain'], child_pid)
    syscall_tree_callback('new_proc', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'], perf_sample_dict['sample']['time'],
                          child_pid)


def sched__sched_process_exit(event_name, context, common_cpu,
	                          common_secs, common_nsecs, common_pid,
                              common_comm, common_callchain, comm, pid,
                              prio, perf_sample_dict):
    syscall_tree_callback('exit', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], 0)


def syscalls__sys_exit_execve(event_name, context, common_cpu, common_secs,
                              common_nsecs, common_pid, common_comm,
                              common_callchain, __syscall_nr, ret,
                              perf_sample_dict):
    syscall_tree_callback('execve', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)

def syscalls__sys_exit_execveat(event_name, context, common_cpu, common_secs,
                                common_nsecs, common_pid, common_comm,
                                common_callchain, __syscall_nr, ret,
                                perf_sample_dict):
    syscall_tree_callback('execve', common_comm, perf_sample_dict['sample']['pid'],
                          perf_sample_dict['sample']['tid'],
                          perf_sample_dict['sample']['time'], ret)
