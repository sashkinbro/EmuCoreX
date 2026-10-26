"""Host-side contract tests; no device, third-party modules or emulator required."""
import json
import hashlib
from pathlib import Path
import tempfile
import unittest
import copy

import compare


class OracleTests(unittest.TestCase):
    def build_provenance(self):
        config = dict(schema=2, target_coverage='all-native-targets-v1', complete=True, build_type='Debug', experimental_ir=False,
                      abi='arm64-v8a', platform='android-29', ndk=dict(revision='29.0.14206865'),
                      compilers={language: dict(id='Clang', version='21.0.0', sha256='a' * 64)
                                 for language in ('C', 'CXX')},
                      cache_flags={'CMAKE_C_FLAGS_DEBUG': '-O2 -g1', 'CMAKE_CXX_FLAGS_DEBUG': '-O2 -g1'},
                      compiler_rules=[dict(target='emucore', stage='CXX_COMPILER', command='${CXX_COMPILER} $FLAGS')],
                      target_flags=[dict(target='emucore', flags='-O2 -g1 -fPIC')])
        digest = hashlib.sha256(json.dumps(config, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        return dict(build_configuration=config, build_configuration_sha256=digest)

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
        old.update(self.build_provenance())
        new = dict(old, samples_ms=[50, 55, 60])
        result = compare.compare_benchmarks(old, new)
        self.assertAlmostEqual(result['median_change_percent'], 25)
        for change in [dict(device='other'), dict(frames=5), dict(backend='gl'),
                       dict(samples_ms=[]), dict(samples_ms=[float('nan')]),
                       dict(samples_ms=[0]), dict(mode='capture'), dict(complete=False), dict(driver_sha256='driver2'),
                       dict(frames_per_replay=2), dict(batch=0), dict(batch=True)]:
            with self.assertRaises(ValueError):
                compare.compare_benchmarks(old, dict(new, **change))

    def test_benchmark_rejects_missing_and_changed_build_configuration(self):
        old = dict(schema=1, mode='benchmark', backend='vk', complete=True,
                   input_sha256='abc', device='lenovo', settings='native-exact-v1',
                   frames=4, frames_per_replay=4, batch=1, warmup=1,
                   driver_sha256='driver1', samples_ms=[40, 44, 48], **self.build_provenance())
        legacy = {k: v for k, v in old.items() if not k.startswith('build_configuration')}
        for before, after in ((legacy, old), (old, legacy), (legacy, legacy)):
            with self.assertRaisesRegex(ValueError, 'build configuration'):
                compare.compare_benchmarks(before, after)
        for change in (dict(build_type='Release'), dict(experimental_ir=True),
                       dict(cache_flags={'CMAKE_CXX_FLAGS_DEBUG': '-g1'})):
            changed = copy.deepcopy(old)
            changed['build_configuration'].update(change)
            config = changed['build_configuration']
            changed['build_configuration_sha256'] = hashlib.sha256(
                json.dumps(config, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            with self.assertRaisesRegex(ValueError, 'build configuration'):
                compare.compare_benchmarks(old, changed)
        # A changed code binary is expected when evaluating an optimization.
        self.assertEqual(compare.compare_benchmarks(dict(old, core_sha256='old'),
                         dict(old, core_sha256='new'))['median_change_percent'], 0)
        for change in (dict(build_configuration_sha256='bad'),
                       dict(build_configuration=dict(old['build_configuration'], complete=False)),
                       dict(build_configuration={})):
            with self.assertRaisesRegex(ValueError, 'build configuration'):
                compare.compare_benchmarks(old, dict(old, **change))

    def configured_build(self, name):
        root = self.root / name
        build, source, ndk = root / 'build', root / 'src', root / 'ndk'
        cmake = build / 'CMakeFiles' / '3.22.1'
        cmake.mkdir(parents=True)
        source.mkdir()
        ndk.mkdir()
        (ndk / 'source.properties').write_text('Pkg.Revision = 29.0.14206865\n')
        cache = dict(CMAKE_BUILD_TYPE='Debug', CMAKE_HOME_DIRECTORY=source.as_posix(),
                     CMAKE_ANDROID_NDK=ndk.as_posix(), ANDROID_ABI='arm64-v8a', ANDROID_PLATFORM='android-29',
                     EMUCOREX_ENABLE_EXPERIMENTAL_IR='OFF', CMAKE_C_FLAGS='', CMAKE_CXX_FLAGS='',
                     CMAKE_C_FLAGS_DEBUG='-O2 -g1', CMAKE_CXX_FLAGS_DEBUG=f'-O2 -g1 -I{source.as_posix()}/include')
        (build / 'CMakeCache.txt').write_text('\n'.join(f'{key}:STRING={value}' for key, value in cache.items()))
        for language in ('C', 'CXX'):
            compiler = ndk / (language.lower() + '-compiler')
            compiler.write_bytes(b'same compiler content for relocated build')
            (cmake / f'CMake{language}Compiler.cmake').write_text(
                f'set(CMAKE_{language}_COMPILER "{compiler.as_posix()}")\n'
                f'set(CMAKE_{language}_COMPILER_ID "Clang")\n'
                f'set(CMAKE_{language}_COMPILER_VERSION "21.0.0")\n')
        (build / 'build.ninja').write_text('\n'.join(
            f'build CMakeFiles/{target}.dir/changed-source.cpp.o: CXX_COMPILER__{target}_Debug source.cpp\n'
            f'  FLAGS = -O2 -g1 -include {build.as_posix()}/pch.hxx\n'
            f'  INCLUDES = -I{source.as_posix()}/include\n'
            for target in ('emucore', 'emucorex_gs_oracle')))
        (build / 'CMakeFiles/rules.ninja').write_text('\n'.join(
            f'rule CXX_COMPILER__{target}_Debug\n'
            f'  command = {(ndk / "cxx-compiler").as_posix()} --target=aarch64-linux-android29 $FLAGS -c $in\n'
            for target in ('emucore', 'emucorex_gs_oracle')))
        return build

    def test_configuration_records_compiler_flags_and_is_relocation_stable(self):
        left = self.configured_build('first location')
        right = self.configured_build('relocated')
        a, b = compare.build_configuration(left), compare.build_configuration(right)
        self.assertEqual(a, b)
        self.assertEqual(compare.validated_build_configuration(a)['experimental_ir'], False)
        self.assertEqual(a['build_configuration']['compilers']['CXX']['version'], '21.0.0')
        self.assertNotIn(str(self.root), json.dumps(a))
        ninja = right / 'build.ninja'
        ninja.write_text(ninja.read_text().replace('changed-source.cpp', 'optimized-source.cpp'))
        self.assertEqual(a, compare.build_configuration(right))
        for path, before, after in (
            (right / 'CMakeFiles/3.22.1/CMakeCXXCompiler.cmake', '21.0.0', '22.0.0'),
            (right.parent / 'ndk/source.properties', '29.0.14206865', '30.0.0'),
            (right / 'CMakeFiles/rules.ninja', 'android29', 'android30'),
            (right / 'CMakeCache.txt', '-O2 -g1', '-g1'),
        ):
            original_content = path.read_text()
            path.write_text(original_content.replace(before, after))
            self.assertNotEqual(a['build_configuration_sha256'], compare.build_configuration(right)['build_configuration_sha256'])
            path.write_text(original_content)
        original = ninja.read_text()
        ninja.write_text(original.replace('-O2', '-O0'))
        self.assertNotEqual(a['build_configuration_sha256'], compare.build_configuration(right)['build_configuration_sha256'])
        ninja.write_text(original)
        cache = right / 'CMakeCache.txt'
        cache.write_text(cache.read_text().replace('EMUCOREX_ENABLE_EXPERIMENTAL_IR:STRING=OFF', ''))
        unknown = compare.build_configuration(right)
        self.assertFalse(unknown['build_configuration']['complete'])
        self.assertIsNone(unknown['build_configuration']['experimental_ir'])
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            compare.validated_build_configuration(unknown)

    def test_transitive_native_targets_and_per_source_flags_are_fingerprinted(self):
        build = self.configured_build('transitive')
        ninja = build / 'build.ninja'
        rules = build / 'CMakeFiles/rules.ninja'
        for target, source in (('PCSX2', 'GSState.cpp'), ('PCSX2_LTO', 'GSRendererHW.cpp'),
                               ('common', 'Threading.cpp'), ('zlib', 'inflate.c')):
            language = 'C' if target == 'zlib' else 'CXX'
            with ninja.open('a') as stream:
                stream.write(f'\nbuild CMakeFiles/{target}.dir/{source}.o: {language}_COMPILER__{target}_Debug {source}\n'
                             '  FLAGS = -O0 -g1\n')
            with rules.open('a') as stream:
                stream.write(f'\nrule {language}_COMPILER__{target}_Debug\n'
                             '  command = compiler --target=aarch64-linux-android29 $FLAGS -c $in\n')
        before = compare.build_configuration(build)
        self.assertTrue(before['build_configuration']['complete'])
        original = ninja.read_text()
        # Only a GSState per-source compiler setting changes, not global flags.
        ninja.write_text(original.replace('GSState.cpp\n  FLAGS = -O0', 'GSState.cpp\n  FLAGS = -O3'))
        after = compare.build_configuration(build)
        self.assertNotEqual(before['build_configuration_sha256'], after['build_configuration_sha256'])
        ninja.write_text(original)
        targets = {row['target'] for row in before['build_configuration']['target_flags']}
        self.assertTrue({'PCSX2', 'PCSX2_LTO', 'common', 'zlib'} <= targets)
        # Adding another source with an already represented configuration does
        # not make ordinary code changes look like compiler-setting changes.
        with ninja.open('a') as stream:
            stream.write('\nbuild CMakeFiles/PCSX2.dir/NewGSCode.cpp.o: CXX_COMPILER__PCSX2_Debug NewGSCode.cpp\n'
                         '  FLAGS = -O0 -g1\n')
        self.assertEqual(before, compare.build_configuration(build))

    def test_old_target_coverage_schema_cannot_be_a_timing_baseline(self):
        provenance = self.build_provenance()
        provenance['build_configuration']['schema'] = 1
        provenance['build_configuration_sha256'] = compare.configuration_sha256(provenance['build_configuration'])
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            compare.validated_build_configuration(provenance)

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
        meta.update(self.build_provenance())
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
        enriched = json.loads((root / 'vk/profile/profile.json').read_text())
        self.assertEqual(enriched['build_configuration_sha256'], meta['build_configuration_sha256'])
        old_coverage = copy.deepcopy(meta)
        old_coverage['build_configuration']['schema'] = 1
        old_coverage['build_configuration_sha256'] = compare.configuration_sha256(old_coverage['build_configuration'])
        compare.write_json(root / 'metadata.json', old_coverage)
        self.assertTrue(compare.run_report(root)['passed'])
        self.assertIn('older schema', (root / 'report.md').read_text())
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            compare.run_report(root, baseline=root)
        # Historical evidence remains valid for correctness, but its absent
        # compiler provenance must never be backfilled from another build.
        legacy_meta = {k: v for k, v in meta.items() if not k.startswith('build_configuration')}
        compare.write_json(root / 'metadata.json', legacy_meta)
        self.assertTrue(compare.run_report(root)['passed'])
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            compare.run_report(root, baseline=root)
        compare.write_json(root / 'metadata.json', meta)
        compare.run_report(root)
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
