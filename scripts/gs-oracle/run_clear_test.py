"""Compile the production OpenGL CommitClear body and validate it on one Android device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', required=True, type=Path)
    parser.add_argument('--adb', required=True, type=Path)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    production = repo / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/OpenGL/GSDeviceOGL.cpp'
    source = production.read_text(encoding='utf-8')
    start = source.index('void GSDeviceOGL::CommitClear(')
    end = source.index('std::unique_ptr<GSDownloadTexture> GSDeviceOGL::CreateDownloadTexture', start)
    body = source[start:end]
    (output / 'CommitClear.inc').write_text(body, encoding='utf-8')
    host = 'windows-x86_64' if sys.platform == 'win32' else 'darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64'
    toolchain = args.ndk / 'toolchains/llvm/prebuilt' / host
    compiler = toolchain / ('bin/clang++.exe' if sys.platform == 'win32' else 'bin/clang++')
    binary = output / 'clear-test'
    command = [compiler, '--target=aarch64-linux-android29', f'--sysroot={toolchain / "sysroot"}',
               '-std=c++20', '-O2', '-static-libstdc++', '-I', output,
               Path(__file__).parent / 'native/clear_usage_test.cpp', '-lEGL', '-lGLESv3', '-o', binary]
    with (output / 'build.log').open('w') as log:
        run(command, stdout=log, stderr=subprocess.STDOUT)
    remote = '/data/local/tmp/emucorex-clear-test-' + uuid.uuid4().hex[:12]
    adb = [args.adb, '-s', args.serial]
    run(adb + ['shell', 'mkdir', '-p', remote])
    run(adb + ['push', binary, remote + '/clear-test'])
    run(adb + ['shell', 'chmod', '755', remote + '/clear-test'])
    result = subprocess.run([str(x) for x in adb + ['shell', remote + '/clear-test']],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / 'device.log').write_text(result.stdout, encoding='utf-8')
    metadata = dict(serial=args.serial, remote=remote, exit_code=result.returncode,
                    production_body_sha256=hashlib.sha256(body.encode()).hexdigest(),
                    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode != 0 or '18/18 production clear cases passed' not in result.stdout:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
