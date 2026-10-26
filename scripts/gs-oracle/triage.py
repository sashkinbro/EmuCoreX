"""Inventory every strict GS checkpoint mismatch without choosing a correct backend."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct

import compare


def top(counter, count=12):
    return [dict(value=value, count=number) for value, number in counter.most_common(count)]


def selectors(name, base_cases, seed):
    if not name.startswith('matrix-'):
        return dict(type='named', name=name)
    variant = int(name[7:])
    family = (variant // 32) % 8
    blend = variant % 81
    result = dict(type='matrix', variant=variant, family=family,
                  family_name=['alpha-test', 'depth-test-mask', 'destination-alpha-fba',
                               'framebuffer-mask', 'colclamp-pabe', 'scissor',
                               'frame-depth-layout', 'fba'][family],
                  blend_enabled=bool(variant % 2),
                  alpha=dict(a=blend % 3, b=(blend // 3) % 3, c=(blend // 9) % 3,
                             d=(blend // 27) % 3, fix=[0, 64, 128, 255][variant % 4]))
    if family == 0:
        result.update(atst=variant % 8, aref=128, afail=(variant // 8) % 4)
    elif family == 1:
        result.update(ztst=variant % 4, zmsk=(variant // 4) % 2)
    elif family == 2:
        result.update(datm=variant % 2, fba=(variant // 2) % 2)
    elif family == 3:
        random = (seed ^ ((variant + base_cases) * 0x9e3779b9)) & 0xffffffff
        random ^= (random << 13) & 0xffffffff
        random ^= random >> 17
        random ^= (random << 5) & 0xffffffff
        result['fbmsk'] = random & 0xffffffff
    elif family == 4:
        result.update(colclamp=variant % 2, pabe=(variant // 2) % 2)
    elif family == 6:
        result.update(frame_psm=[0, 1, 2, 10][variant % 4],
                      depth_psm=[0, 1, 2, 10][(variant // 4) % 4])
    elif family == 7:
        result['fba'] = variant % 2
    return result


def differences(reference, candidate, width, kind):
    if len(reference) != len(candidate) or len(reference) % 4:
        raise ValueError('Mismatched or truncated raw checkpoint')
    channels = [Counter() for _ in range(4)]
    pairs = [Counter() for _ in range(4)]
    masks, pixel_errors, depths, depth_pairs = Counter(), Counter(), Counter(), Counter()
    bounds = [width, len(reference) // 4 // width, -1, -1]
    examples = []
    for p in range(len(reference) // 4):
        a, b = reference[p * 4:p * 4 + 4], candidate[p * 4:p * 4 + 4]
        if a == b:
            continue
        x, y = p % width, p // width
        bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
        mask = ''.join('RGBA'[c] for c in range(4) if a[c] != b[c]) if kind != 'depth' else ''.join(str(c) for c in range(4) if a[c] != b[c])
        masks[mask] += 1
        pixel_errors[max(abs(a[c] - b[c]) for c in range(4))] += 1
        for c in range(4):
            if a[c] != b[c]:
                channels[c][b[c] - a[c]] += 1
                pairs[c][f'{a[c]}->{b[c]}'] += 1
        if kind == 'depth':
            aa, bb = struct.unpack('<I', a)[0], struct.unpack('<I', b)[0]
            depths[bb - aa] += 1
            depth_pairs[f'{aa:#010x}->{bb:#010x}'] += 1
        if len(examples) < 8:
            examples.append(dict(x=x, y=y, reference=list(a), candidate=list(b)))
    result = dict(different_pixels=sum(masks.values()), changed_channels=[('RGBA'[c] if kind != 'depth' else f'byte{c}') for c in range(4) if channels[c]],
                  channel_masks=top(masks), bounds=bounds if masks else None,
                  max_byte_error=max(pixel_errors, default=0), pixel_max_byte_errors=top(pixel_errors),
                  channels=[dict(channel='RGBA'[c] if kind != 'depth' else f'byte{c}',
                                 different_bytes=sum(channels[c].values()),
                                 min_signed_delta=min(channels[c], default=0),
                                 max_signed_delta=max(channels[c], default=0),
                                 within_absolute_delta={str(limit): sum(n for delta, n in channels[c].items() if abs(delta) <= limit) for limit in (1, 2, 3)},
                                 signed_delta_top=top(channels[c]), value_pairs_top=top(pairs[c])) for c in range(4)],
                  first_examples=examples)
    if depths:
        result['depth_u32'] = dict(encoding='little-endian uint32 raw VRAM; candidate minus SW, without modular wrap',
            min_delta=min(depths), max_delta=max(depths), max_absolute_delta=max(abs(v) for v in depths),
            signed_delta_top=top(depths, 24), value_pairs_top=top(depth_pairs, 24))
    return result


def manifest(directory):
    result = json.loads((directory / 'capture.json').read_text(encoding='utf-8-sig'))
    if result.get('schema') != 1 or result.get('complete') is not True or result.get('mode') != 'capture':
        raise ValueError('Incomplete capture')
    seen = set()
    for point in result['checkpoints']:
        key = tuple(point[k] for k in ('frame', 'packet', 'name', 'kind'))
        if key in seen:
            raise ValueError('Duplicate checkpoint')
        seen.add(key)
    return result


def read_point(directory, point):
    path = directory / point['file']
    if path.resolve().parent != directory.resolve() or point['kind'] not in ('rgba', 'depth', 'fifo'):
        raise ValueError('Unsafe or unsupported checkpoint')
    data = path.read_bytes()
    if min(point['width'], point['height']) <= 0 or len(data) != point['width'] * point['height'] * 4:
        raise ValueError('Truncated checkpoint')
    return data


def inventory(root):
    root = Path(root)
    directories = {backend: root / backend / 'capture' for backend in ('sw', 'vk', 'gl')}
    captures = {backend: manifest(directory) for backend, directory in directories.items()}
    sw = captures['sw']
    for hardware in ('vk', 'gl'):
        compare.compatible(sw, captures[hardware], ('settings', 'input_sha256', 'device'))
        if len(sw['checkpoints']) != len(captures[hardware]['checkpoints']):
            raise ValueError('Checkpoint count differs')
    failures, groups = [], defaultdict(lambda: dict(checkpoints=0, pixels=0, maximum_byte_error=0))
    counts, kinds = Counter(), Counter()
    selector_groups = defaultdict(lambda: dict(checkpoints=0, pixels=0, maximum_byte_error=0))
    agreement = Counter()
    for index, point in enumerate(sw['checkpoints']):
        data = {'sw': read_point(directories['sw'], point)}
        for backend in ('vk', 'gl'):
            other = captures[backend]['checkpoints'][index]
            compare.compatible(point, other, ('frame', 'packet', 'name', 'kind', 'width', 'height'))
            data[backend] = read_point(directories[backend], other)
        failing = [backend for backend in ('vk', 'gl') if data[backend] != data['sw']]
        if not failing:
            continue
        agreement['both-hardware-equal-SW-different' if data['vk'] == data['gl'] else
                  'VK-only' if failing == ['vk'] else 'GL-only' if failing == ['gl'] else 'all-three-different'] += 1
        selection = selectors(point['name'], sw.get('base_cases', 44), sw.get('seed', 1))
        for backend in failing:
            stats = differences(data['sw'], data[backend], point['width'], point['kind'])
            item = dict(index=index, backend=backend, checkpoint=point, scene=selection,
                        hardware_pair_identical=data['vk'] == data['gl'],
                        reference_sha256=hashlib.sha256(data['sw']).hexdigest(),
                        candidate_sha256=hashlib.sha256(data[backend]).hexdigest(), **stats)
            failures.append(item)
            counts[backend] += 1
            kinds[f'{backend}/{point["kind"]}'] += 1
            label = selection.get('family_name', selection['name'] if 'name' in selection else '')
            key = (backend, label, point['kind'], '+'.join(stats['changed_channels']))
            group = groups[key]
            group['checkpoints'] += 1
            group['pixels'] += stats['different_pixels']
            group['maximum_byte_error'] = max(group['maximum_byte_error'], stats['max_byte_error'])
            selector_key = (backend, json.dumps(selection, sort_keys=True), point['kind'], '+'.join(stats['changed_channels']))
            detail = selector_groups[selector_key]
            detail['checkpoints'] += 1
            detail['pixels'] += stats['different_pixels']
            detail['maximum_byte_error'] = max(detail['maximum_byte_error'], stats['max_byte_error'])
    # Cross-check all strict failures against the existing independent comparison.
    expected = {}
    for backend in ('vk', 'gl'):
        path = root / f'diff-{backend}' / 'comparison.json'
        if path.exists():
            comparison = json.loads(path.read_text(encoding='utf-8-sig'))
            actual = {f['index'] for f in failures if f['backend'] == backend}
            declared = {i for i, cp in enumerate(comparison['checkpoints']) if not cp['passed']}
            if comparison['tolerance'] != 0 or actual != declared or len(actual) != comparison['failed_checkpoints']:
                raise ValueError('Inventory differs from strict comparison coverage')
            expected[backend] = comparison['failed_checkpoints']
    return dict(schema=1, run=str(root.resolve()), strict_tolerance=0,
                interpretation='Differences are observations, not an assertion that software or hardware is correct.',
                coverage=dict(checkpoints_per_backend=len(sw['checkpoints']), backend_failures=dict(counts),
                              verified_comparison_failures=expected, failure_records=len(failures)),
                hardware_agreement=dict(agreement), failures_by_kind=dict(kinds),
                groups=[dict(backend=k[0], scene_group=k[1], kind=k[2], changed_channels=k[3], **v)
                        for k, v in sorted(groups.items(), key=lambda item: (-item[1]['checkpoints'], item[0]))],
                selector_groups=[dict(backend=k[0], selectors=json.loads(k[1]), kind=k[2], changed_channels=k[3], **v)
                                 for k, v in sorted(selector_groups.items())],
                failures=failures)


def markdown(result):
    lines = ['# Strict GS mismatch inventory', '', result['interpretation'], '',
             f'Coverage: `{json.dumps(result["coverage"], sort_keys=True)}`', '',
             f'Hardware agreement: `{json.dumps(result["hardware_agreement"], sort_keys=True)}`', '',
             '| Backend | Scene group | Kind | Changed channels | Checkpoints | Pixels | Max byte error |',
             '|---|---|---|---|---:|---:|---:|']
    lines += [f'| {g["backend"]} | {g["scene_group"]} | {g["kind"]} | {g["changed_channels"]} | {g["checkpoints"]} | {g["pixels"]} | {g["maximum_byte_error"]} |' for g in result['groups']]
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path, help='JSON path; default RUN/triage.json')
    args = parser.parse_args()
    result = inventory(args.run)
    output = args.output or args.run / 'triage.json'
    compare.write_json(output, result)
    output.with_suffix('.md').write_text(markdown(result), encoding='utf-8')
    print(json.dumps(dict(coverage=result['coverage'], hardware_agreement=result['hardware_agreement'], output=str(output)), sort_keys=True))


if __name__ == '__main__':
    main()
