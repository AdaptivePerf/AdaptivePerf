# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) CERN. See LICENSE for details.

# This script uses the perf-script Python API.
# See the man page for perf-script-python for learning how the API works.

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

cur_code_sym = [32]  # In ASCII

def next_code(cur_code):
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
frontend_stream = None
tid_dict = {}
symbol_dict = defaultdict(lambda: next_code(cur_code_sym))
dso_dict = defaultdict(set)
perf_map_paths = set()


def write(stream, msg):
    if isinstance(stream, socket.socket):
        stream.sendall((msg + '\n').encode('utf-8'))
    else:
        if 'b' in stream.mode:
            stream.write((msg + '\n').encode('utf-8'))
        else:
            stream.write(msg + '\n')

        stream.flush()


def syscall_callback(stack, ret_value):
    global perf_map_paths, dso_dict

    if int(ret_value) == 0:
        return

    # Callchain symbol names are attempted to be obtained here. In case of
    # failure, an instruction address is put instead, along with
    # the name of an executable/library if available.
    #
    # If obtained, symbol names are compressed to save memory.
    # The dictionary mapping compressed names to full ones
    # is saved at the end of profiling to syscall_callchains.json
    # (see reverse_callchain_dict in trace_end()).
    #
    # Also, if a symbol name is detected to come from a perf symbol
    # map (i.e. the name of an executable/library is in form of
    # perf-<number>.map), the path to the map is saved so that AdaptivePerf
    # can copy it to the profiling results directory later.
    def process_callchain_elem(elem):
        sym_result = [f'[{elem["ip"]:#x}]', '']
        off_result = hex(elem['ip'])

        if 'dso' in elem:
            p = Path(elem['dso'])
            if re.search(r'^perf\-\d+\.map$', p.name) is not None:
                perf_map_paths.add(str(p))
                sym_result[1] = p.name
            else:
                dso_dict[elem['dso']].add(hex(elem['dso_off']))
                sym_result[0] = f'[{elem["dso"]}]'
                sym_result[1] = elem['dso']
                off_result = hex(elem['dso_off'])

        if 'sym' in elem and 'name' in elem['sym']:
            sym_result[0] = elem['sym']['name']

        return symbol_dict[tuple(sym_result)], off_result

    callchain = list(map(process_callchain_elem, stack))[::-1]

    write(event_stream, json.dumps({
        'type': 'syscall',
        'ret_value': str(ret_value),
        'callchain': callchain
    }))


def syscall_tree_callback(syscall_type, comm_name, pid, tid, time,
                          ret_value):
    write(event_stream, json.dumps({
        'type': 'syscall_meta',
        'subtype': syscall_type,
        'comm': comm_name,
        'pid': str(pid),
        'tid': str(tid),
        'time': time,
        'ret_value': str(ret_value)
    }))


def trace_begin():
    global event_stream, frontend_stream

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

    frontend_connect = os.environ['APERF_CONNECT'].split(' ')
    instrs = frontend_connect[1:]
    parts = instrs[0].split('_')

    if frontend_connect[0] == 'pipe':
        stream = os.fdopen(int(parts[1]), 'w')
        stream.write('connect')
        stream.flush()
        frontend_stream = stream


def trace_end():
    global event_stream, callchain_dict, perf_map_paths

    write(event_stream, '<STOP>')
    event_stream.close()

    reverse_symbol_dict = {v: k for k, v in symbol_dict.items()}

    with open('syscall_callchains.json', mode='w') as f:
        f.write(json.dumps(reverse_symbol_dict) + '\n')

    write(frontend_stream, json.dumps({
        'type': 'sources',
        'data': {k: list(v) for k, v in dso_dict.items()}
    }))

    write(frontend_stream, json.dumps({
        'type': 'symbol_maps',
        'data': list(perf_map_paths)
    }))

    write(frontend_stream, '<STOP>')
    frontend_stream.close()


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
