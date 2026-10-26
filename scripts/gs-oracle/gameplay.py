"""Control an opt-in GameplayProfilingDeviceTest on one explicitly selected device."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import struct
import tempfile
import time
import uuid
import zipfile

import compare


def elf_build_id(data):
    """Read the GNU Build ID from an Android ARM64 ELF, stripped or unstripped."""
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected a little-endian ARM64 ELF core')

    def bounded(offset, size):
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError('Truncated ELF table or note')

    ranges = set()
    phoff, shoff = struct.unpack_from('<QQ', data, 32)
    phsize, phcount, shsize, shcount = struct.unpack_from('<HHHH', data, 54)
    for offset, entry_size, count, minimum, section in ((phoff, phsize, phcount, 56, False),
                                                       (shoff, shsize, shcount, 64, True)):
        if count and entry_size < minimum:
            raise ValueError('Invalid ELF table entry size')
        bounded(offset, entry_size * count)
        for index in range(count):
            entry = offset + entry_size * index
            kind = struct.unpack_from('<I', data, entry + (4 if section else 0))[0]
            if kind == (7 if section else 4): # SHT_NOTE / PT_NOTE
                note_offset = struct.unpack_from('<Q', data, entry + (24 if section else 8))[0]
                note_size = struct.unpack_from('<Q', data, entry + 32)[0]
                bounded(note_offset, note_size)
                ranges.add((note_offset, note_size))
    identities = set()
    for offset, size in ranges:
        end = offset + size
        while offset < end:
            if end - offset < 12:
                raise ValueError('Truncated ELF note header')
            namesize, descsize, kind = struct.unpack_from('<III', data, offset)
            name = offset + 12
            description = name + ((namesize + 3) & ~3)
            following = description + ((descsize + 3) & ~3)
            if following > end:
                raise ValueError('Truncated ELF note contents')
            if data[name:name + namesize].rstrip(b'\0') == b'GNU' and kind == 3 and descsize:
                identities.add(data[description:description + descsize].hex())
            offset = following
    if len(identities) != 1:
        raise ValueError('Missing or conflicting ELF GNU Build ID')
    return identities.pop()


def collect_host_metadata(apk, native_build_directory, serial, run, call):
    """Attach local configuration only after checking installed APK/core identity.

    Git state is observed at pull time; it is not proof of the source revision
    used to build the installed binary. No timing comparison is made here.
    """
    apk, native_build_directory = Path(apk).resolve(), Path(native_build_directory).resolve()
    cache = {}
    for line in (native_build_directory / 'CMakeCache.txt').read_text(encoding='utf-8-sig').splitlines():
        match = re.match(r'^([^#/:][^:]*):[^=]+=(.*)$', line)
        if match:
            cache[match[1]] = match[2]
    library = cache.get('EMUCOREX_NATIVE_LIBRARY_NAME', '')
    if not re.fullmatch(r'[A-Za-z0-9_]+', library) or not cache.get('CMAKE_LIBRARY_OUTPUT_DIRECTORY'):
        raise ValueError('Missing native core library configuration')
    core = Path(cache['CMAKE_LIBRARY_OUTPUT_DIRECTORY']) / f'lib{library}.so'
    core_data = core.read_bytes()
    core_id = elf_build_id(core_data)
    entry = f'lib/arm64-v8a/lib{library}.so'
    with apk.open('rb') as stream:
        apk_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
        stream.seek(0)
        with zipfile.ZipFile(stream) as archive:
            if archive.namelist().count(entry) != 1:
                raise ValueError('APK must contain exactly one matching ARM64 core library')
            packaged_core = archive.read(entry)
    if elf_build_id(packaged_core) != core_id:
        raise ValueError('Local native core Build ID does not match the packaged APK core')
    configuration = compare.build_configuration(native_build_directory)
    compare.validated_build_configuration(configuration)
    package = 'com.sbro.emucorex'
    paths = call('shell', 'pm', 'path', package).decode('utf-8').splitlines()
    bases = [line.removeprefix('package:') for line in paths
             if line.startswith('package:') and line.endswith('/base.apk')]
    if len(bases) != 1 or not re.fullmatch(r'/[A-Za-z0-9_./=+@~-]+', bases[0]):
        raise ValueError('Missing or unsafe installed base APK path')
    installed = call('shell', 'sha256sum', bases[0]).decode('utf-8').strip()
    match = re.fullmatch(r'([0-9a-fA-F]{64})\s+\*?(.+)', installed)
    if not match or match[2] != bases[0] or match[1].lower() != apk_hash:
        raise ValueError('Local APK SHA256 does not match the installed APK')
    repo = Path(__file__).resolve().parents[2]

    def git(*arguments):
        return subprocess.run(['git', '-C', str(repo), *arguments], check=True,
                              capture_output=True).stdout.decode('utf-8').strip()

    return dict(schema=1, serial=serial, run=run, package=package, apk_sha256=apk_hash,
                installed_base_apk=bases[0], core_library=entry, core_build_id=core_id,
                native_core_sha256=hashlib.sha256(core_data).hexdigest(),
                apk_core_sha256=hashlib.sha256(packaged_core).hexdigest(),
                native_build_directory=str(native_build_directory), apk=str(apk),
                commit=git('rev-parse', 'HEAD'), dirty=bool(git('status', '--porcelain')),
                git_state_observed='pull-time source state; not an attestation of binary source revision',
                **configuration)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--run', required=True)
    parser.add_argument('action', choices=['status', 'press', 'release', 'screenshot', 'phase', 'dump', 'save', 'load', 'stop', 'pull'])
    parser.add_argument('--button', type=int)
    parser.add_argument('--duration-ms', type=int, default=150)
    parser.add_argument('--pressure', type=int, default=255)
    parser.add_argument('--value', choices=['navigation', 'gameplay'])
    parser.add_argument('--frames', type=int, default=3)
    parser.add_argument('--slot', type=int)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--apk', type=Path, help='For pull: local APK to verify against installed base.apk')
    parser.add_argument('--native-build-directory', type=Path, help='For pull: matching unstripped native build/configuration')
    args = parser.parse_args()
    if (args.apk is None) != (args.native_build_directory is None):
        parser.error('--apk and --native-build-directory must be supplied together')
    if args.apk is not None and args.action != 'pull':
        parser.error('--apk and --native-build-directory are only supported with pull')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', args.run):
        parser.error('Invalid run ID')
    package = 'com.sbro.emucorex'
    remote = f'files/gameplay-profiles/{args.run}'
    adb = [args.adb, '-s', args.serial]

    def call(*command):
        return subprocess.run(adb + list(command), check=True, capture_output=True).stdout

    def status():
        return json.loads(call('shell', 'run-as', package, 'cat', remote + '/status.json'))

    if args.action == 'status':
        print(json.dumps(status(), indent=2))
        return
    if args.action == 'pull':
        if args.output is None:
            parser.error('pull requires --output (new directory)')
        host_metadata = collect_host_metadata(args.apk, args.native_build_directory, args.serial, args.run, call) if args.apk else None
        args.output.mkdir(parents=True, exist_ok=False)
        # Only this run's test artifacts, not app preferences or user data.
        import tarfile
        archive = call('exec-out', 'run-as', package, 'tar', '-C', remote, '-cf', '-', '.')
        with tarfile.open(fileobj=io.BytesIO(archive)) as source:
            source.extractall(args.output, filter='data')
        if host_metadata is not None:
            # Never overwrite a purported host attestation supplied by the app.
            with (args.output / 'host-metadata.json').open('x', encoding='utf-8') as destination:
                json.dump(host_metadata, destination, indent=2, allow_nan=False)
                destination.write('\n')
        print(args.output.resolve())
        return
    command = dict(id=status()['lastCommand'] + 1, action=args.action)
    if args.action == 'press':
        if args.button not in {19, 20, 21, 22, 96, 97, 99, 100, *range(102, 114), *range(120, 124)}:
            parser.error('Unsupported PS2 pad key')
        if not 1 <= args.duration_ms <= 60000 or not 1 <= args.pressure <= 255:
            parser.error('Invalid duration or pressure')
        command.update(button=args.button, durationMs=args.duration_ms, pressure=args.pressure)
    if args.action == 'phase':
        if args.value is None:
            parser.error('phase requires --value; inspect a screenshot before marking gameplay')
        command['value'] = args.value
    if args.action == 'dump':
        if not 1 <= args.frames <= 120:
            parser.error('frames must be 1..120')
        command['frames'] = args.frames
    if args.action in {'save', 'load'}:
        if args.slot is None or not 0 <= args.slot <= 9:
            parser.error('save/load requires slot 0..9')
        command['slot'] = args.slot
    temporary = '/data/local/tmp/emucorex-gameplay-command-' + uuid.uuid4().hex + '.json'
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / 'command.json'
        path.write_text(json.dumps(command), encoding='utf-8')
        call('push', str(path), temporary)
        try:
            call('shell', 'run-as', package, 'cp', temporary, remote + '/command.tmp')
            call('shell', 'run-as', package, 'mv', remote + '/command.tmp', remote + '/command.json')
        finally:
            call('shell', 'rm', '-f', temporary)
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        current = status()
        if current['state'] == 'failed':
            raise RuntimeError(call('shell', 'run-as', package, 'cat', remote + '/failure.txt').decode())
        if current['lastCommand'] >= command['id']:
            if args.action == 'screenshot' and args.output is not None:
                with args.output.open('xb') as destination:
                    destination.write(call('exec-out', 'run-as', package, 'cat', remote + f'/screen-{command["id"]}.png'))
            print(json.dumps(current, indent=2))
            return
        time.sleep(.2)
    raise TimeoutError('Command not acknowledged; inspect instrumentation log/status before retrying')


if __name__ == '__main__':
    main()
