import json
from pathlib import Path
import struct
import tempfile
import unittest

import triage


class TriageTests(unittest.TestCase):
    def test_alpha_only_and_signed_byte_deltas(self):
        result = triage.differences(bytes([1, 2, 3, 128]), bytes([1, 2, 3, 127]), 1, 'rgba')
        self.assertEqual(result['changed_channels'], ['A'])
        self.assertEqual(result['channels'][3]['signed_delta_top'], [dict(value=-1, count=1)])
        self.assertEqual(result['different_pixels'], 1)

    def test_depth_is_numeric_u32_without_wrap(self):
        result = triage.differences(struct.pack('<II', 0xffffffff, 0x0000ffff),
                                   struct.pack('<II', 0, 0x00010000), 2, 'depth')
        self.assertEqual(result['depth_u32']['min_delta'], -4294967295)
        self.assertEqual(result['depth_u32']['max_delta'], 1)
        self.assertEqual(result['depth_u32']['max_absolute_delta'], 4294967295)

    def test_matrix_selectors_match_generator(self):
        self.assertEqual(triage.selectors('matrix-0198', 44, 1)['frame_psm'], 2)
        self.assertEqual(triage.selectors('matrix-0198', 44, 1)['depth_psm'], 1)
        self.assertEqual(triage.selectors('matrix-0007', 44, 1)['atst'], 7)
        self.assertEqual(triage.selectors('matrix-0129', 44, 1)['colclamp'], 1)

    def test_inventory_covers_every_failure_and_hardware_agreement(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            point = dict(frame=0, packet=0, name='sprites', kind='rgba', width=1, height=1, file='0.bin')
            for backend, raw in [('sw', b'\0\0\0\0'), ('vk', b'\0\0\0\1'), ('gl', b'\0\0\0\1')]:
                path = root / backend / 'capture'
                path.mkdir(parents=True)
                (path / 'capture.json').write_text(json.dumps(dict(schema=1, complete=True, mode='capture',
                    backend=backend, settings='test', input_sha256='abc', device='test', checkpoints=[point])))
                (path / '0.bin').write_bytes(raw)
                if backend != 'sw':
                    diff = root / f'diff-{backend}'
                    diff.mkdir()
                    (diff / 'comparison.json').write_text(json.dumps(dict(tolerance=0, failed_checkpoints=1,
                                                                         checkpoints=[dict(passed=False)])))
            result = triage.inventory(root)
            self.assertEqual(result['coverage']['failure_records'], 2)
            self.assertEqual(result['coverage']['backend_failures'], dict(vk=1, gl=1))
            self.assertEqual(result['hardware_agreement'], {'both-hardware-equal-SW-different': 1})
            (root / 'vk/capture/0.bin').write_bytes(b'\0\0\0\0')
            with self.assertRaisesRegex(ValueError, 'coverage'):
                triage.inventory(root)

    def test_truncated_raw_rejected(self):
        with self.assertRaises(ValueError):
            triage.differences(b'123', b'1234', 1, 'depth')


if __name__ == '__main__':
    unittest.main()
