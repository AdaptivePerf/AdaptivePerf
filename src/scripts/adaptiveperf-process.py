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


event_streams = []
next_index = 0
callchain_dict = defaultdict(next_code)
overall_event_type = None
perf_map_paths = set()


def get_next_event_stream():
    global event_streams, next_index
    stream = event_streams[next_index]
    next_index = (next_index + 1) % len(event_streams)
    return stream


event_stream_dict = defaultdict(lambda: defaultdict(get_next_event_stream))


def write(stream, msg):
    if isinstance(stream, socket.socket):
        stream.sendall((msg + '\n').encode('utf-8'))
    else:
        stream.write((msg + '\n').encode('utf-8'))
        stream.flush()


def trace_begin():
    global event_streams

    serv_connect = os.environ['APERF_SERV_CONNECT'].split(' ')
    instrs = serv_connect[1:]

    for i in instrs:
        parts = i.split('_')
        if serv_connect[0] == 'tcp':
            stream = socket.socket()
            stream.connect((parts[0], int(parts[1])))
            event_streams.append(stream)
        elif serv_connect[0] == 'pipe':
            stream = os.fdopen(int(parts[1]), 'wb')
            stream.write('connect'.encode('ascii'))
            stream.flush()
            event_streams.append(stream)


def process_event(param_dict):
    global event_stream_dict, overall_event_type, perf_map_paths

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
        if 'dso' in elem and \
           re.search(r'^perf\-\d+\.map$', Path(elem['dso']).name) is not None:
            p = Path(elem['dso'])
            perf_map_paths.add(str(p))
            callchain.append(f'({elem["ip"]:#x};{p.name})')
        elif 'sym' in elem and 'name' in elem['sym']:
            callchain.append(callchain_dict[elem['sym']['name']])
        elif 'dso' in elem:
            p = Path(elem['dso'])
            callchain.append(f'({elem["ip"]:#x};{p.name})')
        else:
            callchain.append(f'({elem["ip"]:#x})')

    callchain.append(f'{comm}-{pid}/{tid}')

    write(event_stream_dict[pid][tid], json.dumps(
        ['<SAMPLE>', parsed_event_type,
         str(pid), str(tid),
         timestamp, period, callchain[::-1]]))


def trace_end():
    global event_streams, callchain_dict, overall_event_type, perf_map_paths

    for stream in event_streams:
        write(stream, '<STOP>')
        stream.close()

    if overall_event_type is not None:
        reverse_callchain_dict = {v: k for k, v in callchain_dict.items()}

        with open(f'{overall_event_type}_callchains.json', mode='w') as f:
            f.write(json.dumps(reverse_callchain_dict) + '\n')

        with open(f'perf_map_paths_{overall_event_type}.data', mode='w') as f:
            f.write('\n'.join(perf_map_paths) + '\n')
