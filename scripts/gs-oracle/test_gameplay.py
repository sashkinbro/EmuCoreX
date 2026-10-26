"""Host tests for verified gameplay provenance; no connected device required."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import tarfile
import unittest
from unittest import mock
import zipfile

import compare
import gameplay


def elf(build_id):
    note = struct.pack('<III', 4, len(build_id), 3) + b'GNU\0' + build_id
    note += b'\0' * (-len(note) % 4)
    ident = b'\x7fELF\x02\x01\x01' + b'\0' * 9
    header = struct.pack('<16sHHIQQQIHHHHHH', ident, 3, 183, 1, 0, 64, 0, 0, 64, 56, 1, 64, 0, 0)
    return header + struct.pack('<IIQQQQQQ', 4, 0, 120, 0, 0, len(note), len(note), 4) + note


class GameplayProvenanceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / 'native'
        self.build.mkdir()
        self.core = self.build / 'libemucore_4k.so'
        self.identity = bytes(range(20))
        self.core.write_bytes(elf(self.identity) + b'local debug symbols')
        (self.build / 'CMakeCache.txt').write_text(
            f'CMAKE_LIBRARY_OUTPUT_DIRECTORY:PATH={self.build.as_posix()}\n'
            'EMUCOREX_NATIVE_LIBRARY_NAME:STRING=emucore_4k\n')
        self.apk = self.root / 'app.apk'
        self.write_apk(self.identity)
        config = dict(schema=2, target_coverage='all-native-targets-v1', complete=True,
                      build_type='Debug', experimental_ir=False, abi='arm64-v8a', platform='android-29',
                      compilers={lang: dict(id='Clang', version='21', sha256='a' * 64) for lang in ('C', 'CXX')},
                      ndk=dict(revision='29.0.14206865'), cache_flags={'CMAKE_CXX_FLAGS_DEBUG': '-O2 -g1'},
                      target_flags=[dict(target='PCSX2', flags='-O2 -g1')], compiler_rules=[dict(command='clang $FLAGS')])
        self.configuration = dict(build_configuration=config, build_configuration_sha256=compare.configuration_sha256(config))
        self.base = '/data/app/~~abc/com.sbro.emucorex-xyz/base.apk'

    def write_apk(self, identity):
        with zipfile.ZipFile(self.apk, 'w') as archive:
            archive.writestr('lib/arm64-v8a/libemucore_4k.so', elf(identity))

    def collect(self, installed_hash=None):
        installed_hash = installed_hash or hashlib.sha256(self.apk.read_bytes()).hexdigest()
        call = mock.Mock(side_effect=[f'package:{self.base}\n'.encode(),
                                      f'{installed_hash}  {self.base}\n'.encode()])
        git = mock.Mock(side_effect=[subprocess.CompletedProcess([], 0, b'123abc\n'),
                                     subprocess.CompletedProcess([], 0, b' M changed.cpp\n')])
        with mock.patch.object(compare, 'build_configuration', return_value=self.configuration), \
             mock.patch.object(gameplay.subprocess, 'run', git):
            result = gameplay.collect_host_metadata(self.apk, self.build, 'lenovo', 'run-1', call)
        return result, call

    def test_provenance_options_must_be_paired_before_adb(self):
        stderr = io.StringIO()
        with mock.patch.object(sys, 'argv', ['gameplay.py', '--adb', 'adb', '--serial', 'lenovo',
                               '--run', 'run-1', 'pull', '--output', str(self.root / 'out'), '--apk', str(self.apk)]), \
             mock.patch.object(gameplay.subprocess, 'run') as commands, contextlib.redirect_stderr(stderr):
            with self.assertRaises(SystemExit):
                gameplay.main()
        self.assertIn('--apk and --native-build-directory must be supplied together', stderr.getvalue())
        commands.assert_not_called()

    def test_matching_installed_apk_and_packaged_core_attach_configuration(self):
        result, call = self.collect()
        self.assertEqual(result['apk_sha256'], hashlib.sha256(self.apk.read_bytes()).hexdigest())
        self.assertEqual(result['core_build_id'], self.identity.hex())
        self.assertNotEqual(result['native_core_sha256'], result['apk_core_sha256'])
        self.assertEqual(result['build_configuration_sha256'], self.configuration['build_configuration_sha256'])
        self.assertTrue(result['dirty'])
        self.assertEqual(result['commit'], '123abc')
        self.assertEqual(result['serial'], 'lenovo')
        self.assertEqual(call.call_args_list, [mock.call('shell', 'pm', 'path', 'com.sbro.emucorex'),
                                              mock.call('shell', 'sha256sum', self.base)])

    def test_mismatches_and_incomplete_configuration_refuse_provenance(self):
        with self.assertRaisesRegex(ValueError, 'installed APK'):
            self.collect('0' * 64)
        self.write_apk(bytes(reversed(self.identity)))
        with self.assertRaisesRegex(ValueError, 'Build ID'):
            self.collect()
        self.write_apk(self.identity)
        self.configuration['build_configuration']['complete'] = False
        with self.assertRaisesRegex(ValueError, 'build configuration'):
            self.collect()

    def test_elf_notes_reject_malformed_and_missing_identity(self):
        self.assertEqual(gameplay.elf_build_id(elf(self.identity)), self.identity.hex())
        for data in (b'not ELF', elf(self.identity)[:130], elf(b''),
                     elf(self.identity).replace(b'GNU\0', b'BAD\0')):
            with self.assertRaises(ValueError):
                gameplay.elf_build_id(data)

    def test_invalid_installed_base_path_is_not_executed(self):
        with mock.patch.object(compare, 'build_configuration', return_value=self.configuration):
            call = mock.Mock(return_value=b'package:/data/app/evil;command/base.apk\n')
            with self.assertRaisesRegex(ValueError, 'base APK path'):
                gameplay.collect_host_metadata(self.apk, self.build, 'lenovo', 'run-1', call)
            self.assertEqual(call.call_count, 1)

    def test_pull_writes_host_metadata_only_after_identity_verification(self):
        archive = io.BytesIO()
        with tarfile.open(fileobj=archive, mode='w') as target:
            content = b'{"state":"complete"}'
            info = tarfile.TarInfo('status.json')
            info.size = len(content)
            target.addfile(info, io.BytesIO(content))
        installed_hash = hashlib.sha256(self.apk.read_bytes()).hexdigest()

        def execute(command, **kwargs):
            if command[0] == 'git':
                output = b'123abc\n' if command[-1] == 'HEAD' else b''
            elif command[3:6] == ['shell', 'pm', 'path']:
                output = f'package:{self.base}\n'.encode()
            elif command[3:5] == ['shell', 'sha256sum']:
                output = f'{installed_hash}  {self.base}\n'.encode()
            elif command[3] == 'exec-out':
                output = archive.getvalue()
            else:
                self.fail(f'Unexpected command: {command}')
            return subprocess.CompletedProcess(command, 0, output)

        base_args = ['gameplay.py', '--adb', 'adb', '--serial', 'lenovo', '--run', 'run-1', 'pull']
        output = self.root / 'verified'
        with mock.patch.object(compare, 'build_configuration', return_value=self.configuration), \
             mock.patch.object(gameplay.subprocess, 'run', side_effect=execute), \
             mock.patch.object(sys, 'argv', base_args + ['--output', str(output), '--apk', str(self.apk),
                                                      '--native-build-directory', str(self.build)]), \
             contextlib.redirect_stdout(io.StringIO()):
            gameplay.main()
        self.assertTrue((output / 'status.json').is_file())
        self.assertEqual(json.loads((output / 'host-metadata.json').read_text())['core_build_id'], self.identity.hex())
        installed_hash = '0' * 64
        rejected = self.root / 'rejected'
        with mock.patch.object(compare, 'build_configuration', return_value=self.configuration), \
             mock.patch.object(gameplay.subprocess, 'run', side_effect=execute), \
             mock.patch.object(sys, 'argv', base_args + ['--output', str(rejected), '--apk', str(self.apk),
                                                      '--native-build-directory', str(self.build)]):
            with self.assertRaisesRegex(ValueError, 'installed APK'):
                gameplay.main()
        self.assertFalse(rejected.exists())
        # The original pull remains available and does not invent provenance.
        plain = self.root / 'plain'
        with mock.patch.object(gameplay.subprocess, 'run', side_effect=execute), \
             mock.patch.object(sys, 'argv', base_args + ['--output', str(plain)]), \
             contextlib.redirect_stdout(io.StringIO()):
            gameplay.main()
        self.assertTrue((plain / 'status.json').is_file())
        self.assertFalse((plain / 'host-metadata.json').exists())

    def test_provenance_options_are_pull_only(self):
        with mock.patch.object(sys, 'argv', ['gameplay.py', '--adb', 'adb', '--serial', 'lenovo',
                               '--run', 'run-1', 'status', '--apk', str(self.apk),
                               '--native-build-directory', str(self.build)]), \
             mock.patch.object(gameplay.subprocess, 'run') as commands, contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                gameplay.main()
        commands.assert_not_called()


if __name__ == '__main__':
    unittest.main()
