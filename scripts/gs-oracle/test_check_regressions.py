import json
from pathlib import Path
import struct
import tempfile
import unittest

import check_regressions


class GoldenAnchors(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.metadata = dict(input='builtin', seed=1, backends=['sw', 'vk'])
        self.write_metadata()
        for backend in self.metadata['backends']:
            directory = self.root / backend / 'capture'
            directory.mkdir(parents=True)
            points = []
            for index, (name, kind, x, y, channel, expected) in enumerate((
                ('matrix-0194', 'rgba', 0, 0, None, 0x80102030),
                ('matrix-0010', 'rgba', 6, 24, 3, 255),
                ('matrix-0020', 'depth', 13, 15, None, 2),
            )):
                data = bytearray(64 * 64 * 4)
                offset = (y * 64 + x) * 4
                if channel is None:
                    struct.pack_into('<I', data, offset, expected)
                else:
                    data[offset + channel] = expected
                filename = f'{index}.bin'
                (directory / filename).write_bytes(data)
                points.append(dict(name=name, kind=kind, frame=index, packet=index,
                                   width=64, height=64, file=filename))
            manifest = dict(schema=1, mode='capture', complete=True, backend=backend,
                            seed=1, settings='native-exact-v2-interlace-auto',
                            base_cases=44, cases=256, checkpoints=points)
            (directory / 'capture.json').write_text(json.dumps(manifest))

    def write_metadata(self):
        (self.root / 'metadata.json').write_text(json.dumps(self.metadata))

    def test_all_anchors_and_real_failure(self):
        self.assertEqual(check_regressions.check(self.root), (6, []))
        file = self.root / 'vk/capture/0.bin'
        data = bytearray(file.read_bytes())
        data[0] ^= 1
        file.write_bytes(data)
        checks, failures = check_regressions.check(self.root)
        self.assertEqual(checks, 6)
        self.assertEqual(len(failures), 1)

    def test_invalid_backend_sets(self):
        for backends in ([], ['sw'], ['vk', 'gl'], ['sw', 'vk', 'vk'], ['sw', 'bad']):
            with self.subTest(backends=backends):
                self.metadata['backends'] = backends
                self.write_metadata()
                with self.assertRaises(ValueError):
                    check_regressions.check(self.root)

    def test_capture_provenance_and_unsafe_artifacts(self):
        path = self.root / 'vk/capture/capture.json'
        original = json.loads(path.read_text())
        for key, value in (('backend', 'gl'), ('seed', 2), ('complete', False),
                           ('base_cases', 45), ('cases', 194), ('settings', 'other')):
            with self.subTest(key=key):
                path.write_text(json.dumps(dict(original, **{key: value})))
                with self.assertRaises(ValueError):
                    check_regressions.check(self.root)
        for mutation in ('duplicate', 'dimension', 'path'):
            manifest = json.loads(json.dumps(original))
            if mutation == 'duplicate':
                manifest['checkpoints'].append(manifest['checkpoints'][0])
            elif mutation == 'dimension':
                manifest['checkpoints'][0]['width'] = 32
            else:
                manifest['checkpoints'][0]['file'] = '../outside.bin'
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                check_regressions.check(self.root)


if __name__ == '__main__':
    unittest.main()
