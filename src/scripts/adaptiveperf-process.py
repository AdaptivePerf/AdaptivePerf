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


event_sock = None


def trace_begin():
    global event_sock

    event_sock = socket.socket()
    event_sock.connect((os.environ['APERF_SERV_ADDR'],
                        int(os.environ['APERF_SERV_PORT'])))

    sock = socket.socket(family=socket.AF_UNIX, type=socket.SOCK_DGRAM)
    sock.sendto(b'\x00', 'start.sock')
    sock.close()


def process_event(param_dict):
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

    event_sock.sendall((json.dumps(
        ['<SAMPLE>', re.search(r'^([^/]+)', event_type).group(1), pid, tid,
         timestamp, period, callchain[::-1]]) + '\n').encode('utf-8'))


def trace_end():
    event_sock.sendall('<STOP>\n'.encode('utf-8'))
    event_sock.close()
