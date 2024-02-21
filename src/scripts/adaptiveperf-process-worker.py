import sys
import json
import fileinput
import subprocess


def process(output, output_time_ordered, offcpu_regions,
            event_type, timestamp, period, callchain_parts):
    if event_type == 'offcpu-time':
        if len(callchain_parts) > 0:
            callchain_parts[-1] = '[cold]_' + callchain_parts[-1]
        else:
            callchain_parts.append('[cold]_(just thread/process)')

        offcpu_regions.append((timestamp / 1000000000, period))

    cur_elem = output
    elem_dict = {}

    for p in callchain_parts:
        if p not in elem_dict:
            elem = {
                'name': p,
                'value': 0,
                'children': []
            }
            
            cur_elem['children'].append(elem)
            elem_dict[p] = elem
        else:
            elem = elem_dict[p]
            
        elem['value'] += period
        cur_elem = elem

    output_time_ordered.append([timestamp, callchain_parts, period])


def main(pid_tid, extra_event_name=None):
    output = {
        'name': 'all',
        'value': 0,
        'children': []
    }

    output_time_ordered = {
        'name': 'all',
        'value': 0,
        'children': []
    }

    to_output_time_ordered = []
    offcpu_regions = []
    total_period = 0

    pid_tid_name = pid_tid.replace('/', '_')

    for line in fileinput.input(files=[]):
        event_type, timestamp, period, callchain_parts = json.loads(line)

        if event_type in ['task-clock', 'offcpu-time']:
            period /= 1000
        
        process(output, to_output_time_ordered, offcpu_regions,
                event_type, timestamp, period, callchain_parts)
        total_period += period

    print(f'{pid_tid} starts')
    output['value'] = total_period
    output_time_ordered['value'] = total_period

    to_output_time_ordered.sort(key=lambda x: x[0])

    for _, callchain_parts, period in to_output_time_ordered:
        cur_elem = output_time_ordered

        for i in range(len(callchain_parts)):
            p = callchain_parts[i]
            children = cur_elem['children']
            
            if len(children) == 0 or children[-1]['name'] != p or \
               (i == len(callchain_parts) - 1 and
                len(children[-1]['children']) > 0) or \
                (i < len(callchain_parts) - 1 and
                 len(children[-1]['children']) == 0):
                elem = {
                    'name': p,
                    'value': 0,
                    'children': []
                }
                children.append(elem)
            else:
                elem = children[-1]

            elem['value'] += period
            cur_elem = elem
            
    if extra_event_name is None:
        json_name = f'{pid_tid_name}_walltime.data'
        json_chart_name = f'{pid_tid_name}_walltime_chart.data'
    else:
        json_name = f'{pid_tid_name}_{extra_event_name}.data'
        json_chart_name = f'{pid_tid_name}_{extra_event_name}_chart.data'

    with open(json_name, mode='w') as f:
        f.write(json.dumps(output))

    with open(json_chart_name, mode='w') as f:
        f.write(json.dumps(output_time_ordered))

    if extra_event_name is None:
        with open(f'{pid_tid_name}_sampled_time.data', mode='w') as f:
            f.write(f'{total_period}\n')

        with open('{pid_tid_name}_offcpu.data', mode='w') as f:
            for timestamp, period in offcpu_regions:
                f.write(f"{pid_tid.replace('/', ' ')} {timestamp} {period}\n")

    print(f'{pid_tid} stops')

if __name__ == '__main__':
    if len(sys.argv) == 2:
        main(sys.argv[1])
    elif len(sys.argv) == 3:
        main(sys.argv[1], sys.argv[2])
