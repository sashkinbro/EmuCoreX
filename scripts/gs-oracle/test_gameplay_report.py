"""Host-only validation of gameplay evidence and button interval reconstruction."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

import gameplay_report as subject


SNAPSHOT = '50.000\n100.000\nFPS: 50 [P] | VPS: 50\nGPU:Adreno | 50% (10ms)\nEE: 60% (12ms)\nGS: 70% (14ms)\nVU: 20% (4ms)'


class GameplayReportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.meta = dict(game='/game.iso', bios='/bios', renderer=14, core='test', model='Lenovo', fingerprint='os',
                         settings=dict(Renderer='14', upscale_multiplier='1', accurate_blending_unit='1', HWDownloadMode='0'))
        self.commands = [dict(id=0, action='phase', value='gameplay', elapsedMs=1000),
                         dict(id=1, action='press', button=96, pressure=255, durationMs=1000, elapsedMs=1100),
                         dict(id=2, action='stop', elapsedMs=5000)]
        self.samples = [dict(elapsedMs=t, sequence=s, phase=p, fresh=f, snapshot=SNAPSHOT)
                        for t, s, p, f in [(500, 1, 'navigation', True), (1200, 2, 'gameplay', True),
                                           (1700, 2, 'gameplay', True), (2200, 3, 'gameplay', True),
                                           (2700, 4, 'settling', True)]]
        (self.root / 'phase-0-gameplay.png').write_bytes(b'\x89PNG\r\n\x1a\n' + b'\0\0\0\rIHDR' + (8).to_bytes(4, 'big') * 2)

    def write(self):
        for name, value in [('status.json', dict(state='complete', lastCommand=self.commands[-1]['id'], elapsedMs=6000)), ('metadata.json', self.meta)]:
            (self.root / name).write_text(json.dumps(value))
        for name, values in [('commands.jsonl', self.commands), ('metrics.jsonl', self.samples)]:
            (self.root / name).write_text(''.join(json.dumps(x) + '\n' for x in values))

    def test_cached_navigation_settling_and_expired_button_excluded(self):
        self.write()
        result = subject.report(self.root, 96)
        self.assertEqual(result['observed_gameplay']['samples'], 2)
        self.assertEqual(result['active_input']['samples'], 1)
        self.assertEqual(result['fresh_flag_disagreements'], 1)
        self.assertEqual(result['phases']['gameplay'], dict(fresh=2, stale=1))
        self.assertEqual(result['active_input']['gpu_ms']['median'], 10)
        subject.write_report(self.root, result)
        self.assertTrue((self.root / 'report.md').is_file())

    def test_repress_replaces_expiry_and_release_load_clear(self):
        self.commands = self.commands[:-1] + [dict(id=2, action='press', button=96, durationMs=100, elapsedMs=1300),
                                              dict(id=3, action='release', elapsedMs=2300),
                                              dict(id=4, action='press', button=96, durationMs=5000, elapsedMs=2400),
                                              dict(id=5, action='load', elapsedMs=2800),
                                              dict(id=6, action='stop', elapsedMs=5000)]
        self.samples += [dict(self.samples[-1], elapsedMs=3000, sequence=5, phase='gameplay')]
        self.write()
        result = subject.report(self.root, 96)
        self.assertEqual(result['active_input']['samples'], 1)
        self.assertEqual(result['observed_gameplay']['samples'], 2)

    def test_diagnostics_override_incorrect_gameplay_label(self):
        for action in ('screenshot', 'dump', 'save', 'load'):
            with self.subTest(action=action):
                self.commands.insert(2, dict(id=2, action=action, elapsedMs=2000))
                self.commands[-1]['id'] = 3
                self.write()
                self.assertEqual(subject.report(self.root)['observed_gameplay']['samples'], 1)
                self.commands.pop(2)

    def test_malformed_metrics_rejected(self):
        original = copy.deepcopy(self.samples)
        for change in (dict(elapsedMs=100), dict(sequence=0), dict(snapshot='NaN\n100'), dict(snapshot='50\n100\nVPS: 50'), dict(phase='unknown')):
            with self.subTest(change=change):
                self.samples = copy.deepcopy(original)
                self.samples[1].update(change)
                self.write()
                with self.assertRaises(ValueError):
                    subject.report(self.root)

    def test_missing_confirmation_metadata_and_incomplete_status_rejected(self):
        self.write()
        screenshot = (self.root / 'phase-0-gameplay.png').read_bytes()
        (self.root / 'phase-0-gameplay.png').unlink()
        with self.assertRaises(OSError):
            subject.report(self.root)
        (self.root / 'phase-0-gameplay.png').write_bytes(screenshot)
        del self.meta['settings']['Renderer']
        self.write()
        with self.assertRaises(ValueError):
            subject.report(self.root)
        self.meta['settings']['Renderer'] = '14'
        self.write()
        (self.root / 'status.json').write_text('{"state":"failed"}')
        with self.assertRaises(ValueError):
            subject.report(self.root)

    def test_release_and_load_end_active_interval(self):
        for action in ('release', 'load'):
            with self.subTest(action=action):
                self.commands = [dict(id=0, action='phase', value='gameplay', elapsedMs=1000),
                                 dict(id=1, action='press', button=96, durationMs=60000, elapsedMs=1100),
                                 dict(id=2, action=action, elapsedMs=2000),
                                 dict(id=3, action='phase', value='gameplay', elapsedMs=2100),
                                 dict(id=4, action='stop', elapsedMs=5000)]
                (self.root / 'phase-3-gameplay.png').write_bytes((self.root / 'phase-0-gameplay.png').read_bytes())
                self.write()
                result = subject.report(self.root, 96)
                self.assertEqual(result['observed_gameplay']['samples'], 2)
                self.assertEqual(result['active_input']['samples'], 1)

    def test_require_unlimited_checks_metadata_and_every_fresh_gameplay_target(self):
        original = copy.deepcopy(self.samples)
        for unlimited, target in [(None, 'Max'), (False, 'Max'), (True, '100%'), (True, None)]:
            with self.subTest(unlimited=unlimited, target=target):
                self.samples = copy.deepcopy(original)
                self.meta.pop('unlimited', None)
                if unlimited is not None:
                    self.meta['unlimited'] = unlimited
                for sample in self.samples:
                    if target is not None:
                        sample['snapshot'] += '\nSpeed: 100% | Target: ' + target
                self.write()
                with self.assertRaises(ValueError):
                    subject.report(self.root, require_unlimited=True)
        self.meta.update(unlimited=True, gpuTimingRequested=False)
        self.samples = copy.deepcopy(original)
        for sample in self.samples:
            sample['snapshot'] += '\nSpeed: 100% | Target: Max'
        self.write()
        result = subject.report(self.root, require_unlimited=True)
        self.assertEqual(result['limiter']['mode'], 'unlimited')
        self.assertTrue(result['uncapped_benchmark_eligible'])
        self.meta['gpuTimingRequested'] = True
        self.write()
        self.assertFalse(subject.report(self.root, require_unlimited=True)['uncapped_benchmark_eligible'])
        self.samples[3]['snapshot'] = self.samples[3]['snapshot'].replace('Target: Max', 'Target: 100%')
        self.write()
        with self.assertRaises(ValueError):
            subject.report(self.root, require_unlimited=True)

    def test_old_profile_is_readable_but_not_uncapped_benchmark(self):
        self.write()
        result = subject.report(self.root)
        self.assertEqual(result['limiter']['mode'], 'unknown')
        self.assertEqual(result['gpu_timing']['requested'], 'missing')
        self.assertFalse(result['uncapped_benchmark_eligible'])
        for sample in self.samples:
            sample['snapshot'] += '\nSpeed: 100% | Target: 100%'
        self.write()
        self.assertEqual(subject.report(self.root)['limiter']['mode'], 'capped')


if __name__ == '__main__':
    unittest.main()
