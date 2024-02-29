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


event_socks = []
next_index = 0


def get_next_event_sock():
    global event_socks, next_index
    sock = event_socks[next_index]
    next_index = (next_index + 1) % len(event_socks)
    return sock


event_sock_dict = defaultdict(lambda: defaultdict(get_next_event_sock))


def trace_begin():
    global event_socks

    ports = list(map(int, os.environ['APERF_SERV_PORT'].split(' ')))

    for p in ports:
        sock = socket.socket()
        sock.connect((os.environ['APERF_SERV_ADDR'], p))
        event_socks.append(sock)


def process_event(param_dict):
    global event_sock_dict

    event_type = param_dict['ev_name']
    comm = param_dict['comm']
    pid = param_dict['sample']['pid']
    tid = param_dict['sample']['tid']
    timestamp = param_dict['sample']['time']
    period = param_dict['sample']['period']
    raw_callchain = param_dict['callchain']

    callchain = []

    for elem in raw_callchain:
        if 'sym' in elem and 'name' in elem['sym']:
            callchain.append(elem['sym']['name'])
        elif 'dso' in elem:
            callchain.append('[' + Path(elem['dso']).name + ']')
        else:
            callchain.append(f'({elem["ip"]:#x})')

    callchain.append(f'{comm}-{pid}/{tid}')

    event_sock_dict[pid][tid].sendall((json.dumps(
        ['<SAMPLE>', re.search(r'^([^/]+)', event_type).group(1),
         str(pid), str(tid),
         timestamp, period, callchain[::-1]]) + '\n').encode('utf-8'))


def trace_end():
    global event_socks

    for sock in event_socks:
        sock.sendall('<STOP>\n'.encode('utf-8'))
        sock.close()
