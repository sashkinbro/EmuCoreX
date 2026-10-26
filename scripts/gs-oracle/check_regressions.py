"""Check independent raw-layout, alpha-write and depth-order GS anchors.

Requires the unchanged 44-base-scene builtin corpus, seed 1, Cases >= 195.
This complements the full strict comparison; passing does not waive its failures.
"""
import argparse
import json
from pathlib import Path
import struct
import compare


def check(run):
    metadata = json.loads((run / 'metadata.json').read_text(encoding='utf-8-sig'))
    if metadata.get('input') != 'builtin' or metadata.get('seed') != 1:
        raise ValueError('Anchors require builtin input and seed 1')
    backends = metadata.get('backends', [])
    if (not isinstance(backends, list) or len(backends) < 2 or 'sw' not in backends or
            len(set(backends)) != len(backends) or not set(backends) <= {'sw', 'vk', 'gl'}):
        raise ValueError('Require Software and at least one unique hardware backend')
    failures, checked = [], 0
    for backend in backends:
        directory = run / backend / 'capture'
        capture = compare.load_capture(directory)
        if (capture.get('backend') != backend or capture.get('seed') != 1 or
                capture.get('settings') != 'native-exact-v2-interlace-auto' or
                capture.get('base_cases') != 44 or capture.get('cases', 0) < 195):
            raise ValueError('Incomplete capture or incompatible builtin corpus')
        points = {(p['name'], p['kind']): p for p in capture['checkpoints']}
        if len(points) != len(capture['checkpoints']):
            raise ValueError('Ambiguous scene/kind anchors')
        # CT32->CT16 writes all have y>=1. GS columnTable16 halfwords 0,1
        # map to (0,0),(8,0); neither is touched, so the old raw word survives.
        # FB_ONLY writes failing A=255 too. ZB_ONLY+ALWAYS writes Z in original
        # primitive order: n=2 overwrites n=1 regardless of their alpha outcome.
        for name, kind, x, y, channel, expected in (
            ('matrix-0194', 'rgba', 0, 0, None, 0x80102030),
            ('matrix-0010', 'rgba', 6, 24, 3, 255),
            ('matrix-0020', 'depth', 13, 15, None, 2),
        ):
            point = points[name, kind]
            if point['width'] != 64 or point['height'] != 64:
                raise ValueError('Anchors require 64x64 builtin scenes')
            data = point['_data']
            offset = (y * point['width'] + x) * 4
            actual = data[offset + channel] if channel is not None else struct.unpack_from('<I', data, offset)[0]
            checked += 1
            if actual != expected:
                failures.append(f'{backend}/{name}/{kind} ({x},{y}): 0x{actual:x}, expected 0x{expected:x}')
    return checked, failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    args = parser.parse_args()
    checked, failures = check(args.run)
    for failure in failures:
        print('FAIL ' + failure)
    print(f'GS golden anchors: {checked} checks, {len(failures)} failures')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
