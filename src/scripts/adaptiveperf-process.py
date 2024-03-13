# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) CERN. See LICENSE for details.

import os
import sys
import subprocess
import json
import re
import socket
from pathlib import Path
from collections import defaultdict

sys.path.append(os.environ['PERF_EXEC_PATH'] +
                '/scripts/python/Perf-Trace-Util/lib/Perf/Trace')

from perf_trace_context import *
from Core import *

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


event_socks = []
next_index = 0
cpp_filt = None
cpp_filt_cache = {}
callchain_dict = defaultdict(next_code)
overall_event_type = None


def get_next_event_sock():
    global event_socks, next_index
    sock = event_socks[next_index]
    next_index = (next_index + 1) % len(event_socks)
    return sock


event_sock_dict = defaultdict(lambda: defaultdict(get_next_event_sock))


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


def trace_begin():
    global event_socks, cpp_filt

    cpp_filt = subprocess.Popen(['c++filt', '-p'],
                                stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE)

    ports = list(map(int, os.environ['APERF_SERV_PORT'].split(' ')))

    for p in ports:
        sock = socket.socket()
        sock.connect((os.environ['APERF_SERV_ADDR'], p))
        event_socks.append(sock)


def process_event(param_dict):
    global event_sock_dict, overall_event_type

    event_type = param_dict['ev_name']
    comm = param_dict['comm']
    pid = param_dict['sample']['pid']
    tid = param_dict['sample']['tid']
    timestamp = param_dict['sample']['time']
    period = param_dict['sample']['period']
    raw_callchain = param_dict['callchain']

    parsed_event_type = re.search(r'^([^/]+)', event_type).group(1)

    if overall_event_type is None:
        if parsed_event_type in ['task-clock', 'offcpu-time']:
            overall_event_type = 'walltime'
        else:
            overall_event_type = parsed_event_type

    callchain = []

    for elem in raw_callchain:
        if 'sym' in elem and 'name' in elem['sym']:
            callchain.append(callchain_dict[elem['sym']['name']])
        elif 'dso' in elem:
            callchain.append('[' + Path(elem['dso']).name + ']')
        else:
            callchain.append(f'({elem["ip"]:#x})')

    callchain.append(f'{comm}-{pid}/{tid}')

    event_sock_dict[pid][tid].sendall((json.dumps(
        ['<SAMPLE>', parsed_event_type,
         str(pid), str(tid),
         timestamp, period, callchain[::-1]]) + '\n').encode('utf-8'))


def trace_end():
    global event_socks, cpp_filt, callchain_dict, overall_event_type

    for sock in event_socks:
        sock.sendall('<STOP>\n'.encode('utf-8'))
        sock.close()

    cpp_filt.terminate()

    if overall_event_type is not None:
        reverse_callchain_dict = {v: k for k, v in callchain_dict.items()}

        with open(f'{overall_event_type}_callchains.json', mode='w') as f:
            f.write(json.dumps(reverse_callchain_dict) + '\n')
