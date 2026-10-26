"""Summarize an operator-confirmed gameplay profile; no third-party dependencies."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def read_lines(path):
    return [json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]


def integer(value):
    return type(value) is int and value >= 0


def number(value):
    result = float(value)
    require(math.isfinite(result) and result >= 0, 'Invalid metric value')
    return result


def parse_snapshot(snapshot):
    require(isinstance(snapshot, str), 'Invalid snapshot')
    lines = snapshot.splitlines()
    require(len(lines) >= 2, 'Malformed native snapshot')
    result = dict(reported_fps=number(lines[0]), speed_percent=number(lines[1]))
    vps = re.search(r'\bVPS:\s*([\d.]+)', snapshot)
    require(vps is not None, 'Missing VPS in native overlay')
    result['vps'] = number(vps[1])
    for unit in ('EE', 'GS', 'VU', 'GPU'):
        line = next((line for line in lines if line.startswith(unit + ':')), None)
        require(line is not None, 'Missing ' + unit + ' overlay')
        match = re.search(r'\(([\d.]+)\s*ms\)', line)
        require(match is not None or (unit == 'GPU' and 'N/A' in line), 'Malformed ' + unit + ' overlay')
        result[unit.lower() + '_ms'] = number(match[1]) if match else None
    return result


def stats(points):
    result = dict(samples=len(points))
    result['first_sample_ms'] = points[0]['elapsedMs'] if points else None
    result['last_sample_ms'] = points[-1]['elapsedMs'] if points else None
    for key in ('speed_percent', 'reported_fps', 'vps', 'ee_ms', 'gs_ms', 'vu_ms', 'gpu_ms'):
        values = sorted(point[key] for point in points if point[key] is not None)
        result[key] = (dict(samples=len(values), median=statistics.median(values),
                            p95=values[math.ceil(.95 * len(values)) - 1]) if values else None)
    return result


def report(directory, active_button=None, require_unlimited=False):
    directory = Path(directory)
    status, meta = read_json(directory / 'status.json'), read_json(directory / 'metadata.json')
    require(isinstance(status, dict) and isinstance(meta, dict), 'Invalid capture metadata/status')
    require(active_button is None or integer(active_button), 'Invalid active button')
    require(status.get('state') == 'complete', 'Capture did not complete')
    for key in ('unlimited', 'gpuTimingRequested'):
        require(key not in meta or type(meta[key]) is bool, 'Invalid metadata: ' + key)
    if require_unlimited:
        require(meta.get('unlimited') is True, 'Unlimited metadata must explicitly be true')
    for key in ('game', 'bios', 'core', 'model', 'fingerprint'):
        require(isinstance(meta.get(key), str) and bool(meta[key].strip()), 'Missing metadata: ' + key)
    require(type(meta.get('renderer')) is int and meta['renderer'] in (12, 14), 'Invalid renderer')
    require(isinstance(meta.get('settings'), dict) and str(meta['settings'].get('Renderer')) == str(meta['renderer']), 'Invalid settings metadata')
    require(all(key in meta['settings'] for key in ('upscale_multiplier', 'accurate_blending_unit', 'HWDownloadMode')), 'Incomplete settings')
    require(number(meta['settings']['upscale_multiplier']) > 0, 'Invalid upscale multiplier')
    for key in ('accurate_blending_unit', 'HWDownloadMode'):
        require(str(meta['settings'][key]).isdigit(), 'Invalid settings: ' + key)
    commands, samples = read_lines(directory / 'commands.jsonl'), read_lines(directory / 'metrics.jsonl')
    require(bool(commands) and bool(samples), 'Missing commands or samples')
    previous_id, previous_time = -1, -1
    proof = []
    actions = {'press', 'release', 'screenshot', 'phase', 'dump', 'save', 'load', 'stop'}
    for command in commands:
        require(isinstance(command, dict), 'Invalid command record')
        require(integer(command.get('id')) and command['id'] > previous_id, 'Command IDs must increase')
        require(integer(command.get('elapsedMs')) and command['elapsedMs'] >= previous_time, 'Command times must not decrease')
        previous_id, previous_time = command['id'], command['elapsedMs']
        action = command.get('action')
        require(action in actions, 'Unknown command')
        if action == 'press':
            require(integer(command.get('button')) and type(command.get('durationMs')) is int and 1 <= command['durationMs'] <= 60000
                    and type(command.get('pressure', 255)) is int and 1 <= command.get('pressure', 255) <= 255, 'Invalid button command')
        if action == 'phase':
            require(command.get('value') in ('navigation', 'gameplay'), 'Invalid phase command')
            if command['value'] == 'gameplay':
                filename = f"phase-{command['id']}-gameplay.png"
                data = (directory / filename).read_bytes()
                require(len(data) >= 24 and data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR'
                        and int.from_bytes(data[16:20], 'big') > 0 and int.from_bytes(data[20:24], 'big') > 0, 'Invalid gameplay screenshot')
                proof.append(filename)
    require(bool(proof), 'No operator gameplay confirmation screenshot')
    require(status.get('lastCommand') == previous_id, 'Command log does not match completed status')
    held, command_index, command_phase = {}, 0, 'navigation'
    last_time, last_sequence = -1, -1
    phases, gameplay, active, fresh_flag_disagreements = {}, [], [], 0
    targets = dict(unlimited=0, capped=0, unknown=0)
    for sample in samples:
        require(isinstance(sample, dict), 'Invalid sample record')
        now, sequence = sample.get('elapsedMs'), sample.get('sequence')
        require(integer(now) and now > last_time, 'Sample times must strictly increase')
        require(integer(sequence) and sequence >= last_sequence, 'Native sample sequence decreased')
        snapshot = sample.get('snapshot')
        require(isinstance(snapshot, str), 'Invalid snapshot')
        fresh = sequence > last_sequence and bool(snapshot)
        last_time, last_sequence = now, sequence
        require(sample.get('phase') in ('navigation', 'gameplay', 'settling'), 'Invalid sample phase')
        if 'fresh' in sample:
            require(type(sample['fresh']) is bool, 'Invalid freshness field')
            fresh_flag_disagreements += sample['fresh'] != fresh
        while command_index < len(commands) and commands[command_index]['elapsedMs'] <= now:
            command = commands[command_index]
            action = command['action']
            if action == 'press':
                held[command['button']] = command['elapsedMs'] + command['durationMs']
            elif action in ('release', 'load', 'stop'):
                held.clear()
            if action in ('screenshot', 'dump', 'save', 'load', 'stop'):
                command_phase = 'navigation'
            elif action == 'phase':
                command_phase = command['value']
            command_index += 1
        phase = sample['phase']
        # Both the operator command history and measured label must agree.
        if phase == 'gameplay' and command_phase != 'gameplay':
            phase = 'navigation'
        counts = phases.setdefault(phase, dict(fresh=0, stale=0))
        counts['fresh' if fresh else 'stale'] += 1
        if snapshot:
            parsed = parse_snapshot(snapshot)
            parsed['elapsedMs'] = now
            if fresh and phase == 'gameplay':
                match = re.search(r'\bTarget:\s*(Max|\d+(?:\.\d+)?%)(?=\s|$)', snapshot)
                target = 'unknown' if match is None else 'unlimited' if match[1] == 'Max' else 'capped'
                targets[target] += 1
                if require_unlimited:
                    require(target == 'unlimited', 'Fresh gameplay sample does not report Target: Max')
            if fresh and phase == 'gameplay' and parsed['reported_fps'] > 0:
                gameplay.append(parsed)
                if active_button is not None and held.get(active_button, -1) > now:
                    active.append(parsed)
    require(gameplay, 'No fresh confirmed gameplay samples')
    require(integer(status.get('elapsedMs')) and status['elapsedMs'] >= last_time, 'Completed status predates samples')
    mode = ('unlimited' if meta.get('unlimited') is True and targets['unlimited'] > 0 and not targets['capped'] and not targets['unknown']
            else 'capped' if (meta.get('unlimited') is False or targets['capped'] > 0) and not targets['unlimited']
            else 'unknown')
    value = dict(schema=1, metadata=meta, complete=True, active_button=active_button,
                 limiter=dict(mode=mode, requested=meta.get('unlimited', 'missing'), fresh_gameplay_targets=targets),
                 gpu_timing=dict(requested=meta.get('gpuTimingRequested', 'missing'),
                                 gameplay_samples_with_timing=sum(p['gpu_ms'] is not None for p in gameplay)),
                 uncapped_benchmark_eligible=mode == 'unlimited' and meta.get('gpuTimingRequested') is False,
                 screenshot_evidence=proof, phases=phases, fresh_flag_disagreements=fresh_flag_disagreements,
                 observed_gameplay=stats(gameplay), active_input=stats(active) if active_button is not None else None,
                 interpretation='Observed instrumented gameplay profile, not a causal before/after benchmark. Uncapped eligibility requires explicit unlimited metadata, Target: Max in every fresh gameplay sample, and GPU timing explicitly disabled; it does not establish comparable binary/configuration, state, workload, or temperature. Gameplay is operator-confirmed with screenshot evidence; labels do not establish visual correctness. Active input means commanded button pressure, not proof of driving or movement.')
    return value


def write_report(directory, value):
    directory = Path(directory)
    (directory / 'report.json').write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    lines = ['# Gameplay profile', '', value['interpretation'], '',
             f"Renderer: {value['metadata']['renderer']}; device: {value['metadata']['model']}.", '',
             f"Limiter: **{value['limiter']['mode']}**; unlimited requested: {value['limiter']['requested']}; GPU timing requested: {value['gpu_timing']['requested']}; uncapped benchmark eligible: {value['uncapped_benchmark_eligible']}.", '',
             '| Window | Fresh samples | Speed median / p95 % | FPS median / p95 | VPS median / p95 |',
             '|---|---:|---:|---:|---:|']
    for label, window in [('Confirmed gameplay', value['observed_gameplay']),
                          (f"Active button {value['active_button']}", value['active_input'])]:
        if window is None:
            continue
        def cell(key):
            x = window[key]
            return f"{x['median']:.3f} / {x['p95']:.3f}" if x else 'N/A'
        lines.append(f"| {label} | {window['samples']} | {cell('speed_percent')} | {cell('reported_fps')} | {cell('vps')} |")
    lines += ['', '| Window | EE median / p95 ms | GS median / p95 ms | VU median / p95 ms | GPU median / p95 ms |', '|---|---:|---:|---:|---:|']
    for label, window in [('Confirmed gameplay', value['observed_gameplay']), ('Active input', value['active_input'])]:
        if window is not None:
            cells = ['N/A' if window[key] is None else f"{window[key]['median']:.3f} / {window[key]['p95']:.3f}" for key in ('ee_ms', 'gs_ms', 'vu_ms', 'gpu_ms')]
            lines.append('| ' + label + ' | ' + ' | '.join(cells) + ' |')
    lines += ['', 'Fresh/stale samples by phase: ' + json.dumps(value['phases']) + '.', '',
              'Screenshot evidence: ' + ', '.join(value['screenshot_evidence']) + '.', '']
    (directory / 'report.md').write_text('\n'.join(lines), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--active-button', type=int)
    parser.add_argument('--require-unlimited', action='store_true', help='Require explicit unlimited metadata and Target: Max in every fresh gameplay sample')
    args = parser.parse_args()
    try:
        value = report(args.directory, args.active_button, args.require_unlimited)
        write_report(args.directory, value)
        print(json.dumps({key: value[key] for key in ('limiter', 'gpu_timing', 'uncapped_benchmark_eligible', 'observed_gameplay', 'active_input')}, indent=2))
        return 0
    except (ValueError, TypeError, KeyError, OSError) as error:
        print('Gameplay report ERROR: ' + str(error))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
