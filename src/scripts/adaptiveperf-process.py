# AdaptivePerf: comprehensive profiling tool based on Linux perf
# Copyright (C) CERN. See LICENSE for details.

# This script uses the perf-script Python API.
# See the man page for perf-script-python for learning how the API works.

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


event_streams = []
next_index = 0
symbol_dict = defaultdict(lambda: next_code(cur_code_sym))
dso_dict = defaultdict(set)
overall_event_type = None
perf_map_paths = set()


def get_next_event_stream():
    global event_streams, next_index
    stream = event_streams[next_index]
    next_index = (next_index + 1) % len(event_streams)
    return stream


event_stream_dict = defaultdict(lambda: defaultdict(get_next_event_stream))
frontend_stream = None


def write(stream, msg):
    if isinstance(stream, socket.socket):
        stream.sendall((msg + '\n').encode('utf-8'))
    else:
        if 'b' in stream.mode:
            stream.write((msg + '\n').encode('utf-8'))
        else:
            stream.write(msg + '\n')

        stream.flush()


def trace_begin():
    global event_streams, frontend_stream

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

    frontend_connect = os.environ['APERF_CONNECT'].split(' ')
    instrs = frontend_connect[1:]
    parts = instrs[0].split('_')

    if frontend_connect[0] == 'pipe':
        stream = os.fdopen(int(parts[1]), 'w')
        stream.write('connect')
        stream.flush()
        frontend_stream = stream


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

    # Callchain symbol names are attempted to be obtained here. In case of
    # failure, an instruction address is put instead, along with
    # the name of an executable/library if available.
    #
    # If obtained, symbol names are compressed to save memory.
    # The dictionary mapping compressed names to full ones
    # is saved at the end of profiling to <event type>_callchains.json
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

    callchain = list(map(process_callchain_elem, raw_callchain))[::-1]

    write(event_stream_dict[pid][tid], json.dumps({
        'type': 'sample',
        'event_type': parsed_event_type,
        'pid': str(pid),
        'tid': str(tid),
        'time': timestamp,
        'period': period,
        'callchain': callchain
    }))


def trace_end():
    global event_streams, callchain_dict, overall_event_type, perf_map_paths

    for stream in event_streams:
        write(stream, '<STOP>')
        stream.close()

    if overall_event_type is not None:
        reverse_symbol_dict = {v: k for k, v in symbol_dict.items()}

        with open(f'{overall_event_type}_callchains.json', mode='w') as f:
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
