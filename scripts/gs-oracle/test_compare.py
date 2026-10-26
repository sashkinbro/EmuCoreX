"""Host-side contract tests; no device, third-party modules or emulator required."""
import json
import hashlib
from pathlib import Path
import tempfile
import unittest

import compare


class OracleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def capture(self, name, pixels=b'\x10\x20\x30\x80', **overrides):
        directory = self.root / name
        directory.mkdir()
        (directory / '000.rgba').write_bytes(pixels)
        report = dict(schema=1, backend=name, mode='capture', complete=True,
                      input_sha256='abc', settings='native-exact-v1', device='lenovo',
                      checkpoints=[dict(frame=0, packet=3, name='sprite', kind='rgba',
                                        width=1, height=1, file='000.rgba')])
        report.update(overrides)
        (directory / 'capture.json').write_text(json.dumps(report))
        return directory

    def test_equal(self):
        a, b = self.capture('sw'), self.capture('vk')
        self.assertTrue(compare.compare_captures(a, b, self.root / 'diff')['passed'])

    def test_alpha_difference_and_evidence(self):
        a = self.capture('sw')
        b = self.capture('vk', b'\x10\x20\x30\x81')
        result = compare.compare_captures(a, b, self.root / 'diff')
        self.assertFalse(result['passed'])
        item = result['checkpoints'][0]
        self.assertEqual(item['different_pixels'], 1)
        self.assertEqual(item['first_difference']['channel'], 3)
        self.assertTrue((self.root / 'diff' / item['difference_image']).is_file())

    def test_explicit_tolerance_keeps_exact_difference(self):
        a = self.capture('sw')
        b = self.capture('vk', b'\x11\x20\x30\x80')
        result = compare.compare_captures(a, b, self.root / 'diff', tolerance=1)
        self.assertTrue(result['passed'])
        self.assertEqual(result['checkpoints'][0]['different_pixels'], 1)

    def test_missing_capture_and_dimensions_fail(self):
        a, b = self.capture('sw'), self.capture('vk')
        (b / '000.rgba').unlink()
        with self.assertRaises(ValueError):
            compare.compare_captures(a, b, self.root / 'diff')

    def test_empty_incomplete_and_wrong_input_fail(self):
        a = self.capture('sw')
        for i, override in enumerate([dict(checkpoints=[]), dict(complete=False),
                                      dict(input_sha256='other')]):
            b = self.capture(f'vk{i}', **override)
            with self.assertRaises(ValueError):
                compare.compare_captures(a, b, self.root / f'diff{i}')

    def test_checkpoint_identity_and_size_fail(self):
        a = self.capture('sw')
        for i, change in enumerate([dict(frame=1), dict(width=2), dict(file='../escape')]):
            b = self.capture(f'vk{i}')
            report = json.loads((b / 'capture.json').read_text())
            report['checkpoints'][0].update(change)
            (b / 'capture.json').write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                compare.compare_captures(a, b, self.root / f'diff{i}')

    def test_binary_mismatch_is_never_tolerated(self):
        a = self.capture('sw', b'\0' * 4)
        b = self.capture('vk', b'\1' + b'\0' * 3)
        for d in [a, b]:
            p = json.loads((d / 'capture.json').read_text())
            p['checkpoints'][0]['kind'] = 'fifo'
            (d / 'capture.json').write_text(json.dumps(p))
        self.assertFalse(compare.compare_captures(a, b, self.root / 'diff', tolerance=255)['passed'])

    def test_benchmark_regression_and_incompatibility(self):
        old = dict(schema=1, mode='benchmark', backend='vk', complete=True,
                   input_sha256='abc', device='lenovo', settings='native-exact-v1',
                   frames=4, frames_per_replay=4, batch=1, warmup=1, driver_sha256='driver1', samples_ms=[40, 44, 48])
        new = dict(old, samples_ms=[50, 55, 60])
        result = compare.compare_benchmarks(old, new)
        self.assertAlmostEqual(result['median_change_percent'], 25)
        for change in [dict(device='other'), dict(frames=5), dict(backend='gl'),
                       dict(samples_ms=[]), dict(samples_ms=[float('nan')]),
                       dict(samples_ms=[0]), dict(mode='capture'), dict(complete=False), dict(driver_sha256='driver2'),
                       dict(frames_per_replay=2), dict(batch=0), dict(batch=True)]:
            with self.assertRaises(ValueError):
                compare.compare_benchmarks(old, dict(new, **change))

    def test_profile_rejects_missing_duplicate_and_invalid_data(self):
        point = dict(frame=0, packet=3, name='draw', submit_ms=1, wait_ms=2, counters=dict(draw_calls=4))
        value = dict(schema=1, complete=True, mode='profile', frames=1, profiles=[point])
        self.assertEqual(compare.profile_stats(value)['totals']['draw_calls'], 4)
        for changed in [dict(profiles=[]), dict(frames=2, profiles=[point, point]),
                        dict(profiles=[dict(point, wait_ms=-1)]),
                        dict(profiles=[dict(point, counters=dict(draw_calls=float('nan')))])]:
            with self.assertRaises(ValueError):
                compare.profile_stats(dict(value, **changed))

    def test_complete_report_and_baseline(self):
        root = self.root / 'run'
        root.mkdir()
        dump = b'opaque-test-dump'
        (root / 'input.gs').write_bytes(dump)
        meta = dict(backends=['sw', 'vk'], input='input.gs', input_sha256=hashlib.sha256(dump).hexdigest(),
                    device='lenovo', model='TB710FU', core_sha256='core', runner_sha256='runner', warmup=1, repeats=3, cases=0, seed=1)
        compare.write_json(root / 'metadata.json', meta)
        for backend in meta['backends']:
            base = dict(schema=1, backend=backend, complete=True, settings='native-exact-v2',
                        frames_per_replay=1, cases=0, seed=1, frames=1, batch=1)
            for mode in ['capture', 'benchmark', 'profile']:
                d = root / backend / mode
                d.mkdir(parents=True)
                (d / 'driver.txt').write_text('driver')
                value = dict(base, mode=mode)
                if mode == 'capture':
                    (d / '000.bin').write_bytes(b'\x10\x20\x30\x80')
                    value['checkpoints'] = [dict(frame=0, packet=3, name='display', kind='rgba', width=1, height=1, file='000.bin')]
                elif mode == 'benchmark':
                    value.update(frames=10, batch=10, warmup=1, samples_ms=[10, 11, 12])
                else:
                    value['profiles'] = [dict(frame=0, packet=3, name='display', submit_ms=1, wait_ms=1, counters=dict(draw_calls=2))]
                compare.write_json(d / f'native-{mode}.json', value)
        first = compare.run_report(root)
        self.assertTrue(first['passed'])
        self.assertTrue((root / 'report.html').is_file())
        for mode, changes in [('benchmark', dict(frames=1)), ('capture', dict(frames=2)),
                              ('benchmark', dict(batch=0)), ('benchmark', dict(seed=2))]:
            path = root / 'vk' / mode / f'native-{mode}.json'
            original = json.loads(path.read_text())
            compare.write_json(path, dict(original, **changes))
            with self.assertRaises(ValueError):
                compare.run_report(root)
            compare.write_json(path, original)
        second = compare.run_report(root, baseline=root)
        self.assertEqual(second['performance_changes']['vk']['median_change_percent'], 0)
        (root / 'vk' / 'capture' / '000.bin').write_bytes(b'\xff\x20\x30\x80')
        self.assertFalse(compare.run_report(root)['passed'])
        self.assertIn('<details>', (root / 'report.html').read_text())
        # Campaign aggregation preserves actual mismatches instead of treating
        # an expected failing runner exit as missing evidence or PASS.
        meta['serial'] = 'lenovo'
        compare.write_json(root / 'metadata.json', meta)
        compare.run_report(root)
        campaign_plan = dict(schema=1, complete=True, serial='lenovo', cases=0, backends=['sw', 'vk'],
            runs=[dict(id='dump-0', seed=1, report_directory='run', input_name='input.gs', input_sha256=meta['input_sha256'])])
        compare.write_json(self.root / 'campaign.json', campaign_plan)
        campaign = compare.campaign_report(self.root)
        self.assertTrue(campaign['complete'])
        self.assertFalse(campaign['passed'])
        self.assertEqual(len(campaign['mismatch_groups']), 1)
        self.assertEqual(campaign['mismatch_groups'][0]['occurrences'][0]['frame'], 0)
        for changed in [dict(cases=1), dict(backends=['sw', 'vk', 'gl']),
                        dict(runs=[dict(campaign_plan['runs'][0], input_sha256='wrong')]),
                        dict(runs=[dict(campaign_plan['runs'][0], input_name='another.gs')])]:
            compare.write_json(self.root / 'campaign.json', dict(campaign_plan, **changed))
            rejected = compare.campaign_report(self.root)
            self.assertFalse(rejected['complete'])
            self.assertFalse(rejected['passed'])
        (root / 'vk' / 'profile' / 'driver.txt').write_text('another driver')
        with self.assertRaises(ValueError):
            compare.run_report(root)

    def test_tolerance_cannot_hide_depth(self):
        a = self.capture('sw', b'\0' * 4)
        b = self.capture('vk', b'\1' + b'\0' * 3)
        for d in [a, b]:
            p = json.loads((d / 'capture.json').read_text())
            p['checkpoints'][0]['kind'] = 'depth'
            compare.write_json(d / 'capture.json', p)
        self.assertFalse(compare.compare_captures(a, b, self.root / 'diff', tolerance=255)['passed'])

    def test_campaign_never_turns_missing_runs_into_pass(self):
        root = self.root / 'campaign'
        root.mkdir()
        manifest = dict(schema=1, complete=False, serial='lenovo', cases=0, backends=['sw', 'vk'],
                        runs=[dict(id='seed-1', seed=1, report_directory=None)])
        compare.write_json(root / 'campaign.json', manifest)
        with self.assertRaises(ValueError):
            compare.campaign_report(root)
        manifest['complete'] = True
        compare.write_json(root / 'campaign.json', manifest)
        result = compare.campaign_report(root)
        self.assertFalse(result['complete'])
        self.assertFalse(result['passed'])
        self.assertEqual(len(result['errors']), 1)
        manifest['runs'][0]['report_directory'] = '../escape'
        compare.write_json(root / 'campaign.json', manifest)
        with self.assertRaises(ValueError):
            compare.campaign_report(root)


if __name__ == '__main__':
    unittest.main()
