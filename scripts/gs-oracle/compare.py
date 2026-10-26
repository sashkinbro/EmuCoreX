"""Strict GS oracle comparison and benchmark reports (Python standard library)."""
import argparse
import hashlib
import html
import json
import math
import re
from pathlib import Path
import statistics
import struct
import sys
import zlib


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def png(path, width, height, rgb):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    rows = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def load_capture(directory):
    directory = Path(directory)
    value = json.loads((directory / 'capture.json').read_text(encoding='utf-8-sig'))
    if value.get('schema') != 1 or value.get('mode') != 'capture' or value.get('complete') is not True:
        raise ValueError(f'Incomplete or unsupported capture: {directory}')
    points = value.get('checkpoints')
    if not isinstance(points, list) or not points:
        raise ValueError(f'No checkpoints: {directory}')
    seen = set()
    for item in points:
        identity = tuple(item.get(k) for k in ('frame', 'packet', 'name', 'kind'))
        if identity in seen or any(x is None for x in identity) or item['kind'] not in ('rgba', 'fifo', 'depth'):
            raise ValueError('Duplicate or malformed checkpoint')
        seen.add(identity)
        width, height = item.get('width'), item.get('height')
        if type(width) is not int or type(height) is not int or min(width, height) <= 0 or width * height > 16777216:
            raise ValueError('Invalid checkpoint dimensions')
        path = directory / item['file']
        if path.resolve().parent != directory.resolve() or not path.is_file():
            raise ValueError(f'Missing or unsafe checkpoint: {path}')
        data = path.read_bytes()
        if len(data) != width * height * 4:
            raise ValueError(f'Truncated checkpoint: {path}')
        item['_data'] = data
    return value


def compatible(a, b, keys):
    for key in keys:
        if key not in a or key not in b or a[key] != b[key]:
            raise ValueError(f'Incompatible reports: {key}')


def configuration_sha256(configuration):
    return hashlib.sha256(json.dumps(configuration, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode('utf-8')).hexdigest()


def build_configuration(directory):
    """Read configured native compiler provenance without building or running it.

    Cache flags alone miss target_compile_options. Preserve the emitted Ninja
    compile/link settings as well, without source filenames or machine roots.
    """
    directory = Path(directory).resolve()
    cache = {}
    for line in (directory / 'CMakeCache.txt').read_text(encoding='utf-8-sig').splitlines():
        match = re.match(r'^([^#/:][^:]*):[^=]+=(.*)$', line)
        if match:
            cache[match[1]] = match[2]
    ndk = Path(cache.get('CMAKE_ANDROID_NDK') or cache.get('ANDROID_NDK') or '')
    roots = [(str(directory), '${BUILD}'), (cache.get('CMAKE_HOME_DIRECTORY', ''), '${SOURCE}'),
             (str(ndk), '${NDK}')]
    source = Path(cache.get('CMAKE_HOME_DIRECTORY', directory))
    for parent in (source, *source.parents):
        if (parent / '.git').exists():
            roots.append((str(parent), '${PROJECT}'))
            break
    roots = sorted(((root.replace('\\', '/').rstrip('/'), token) for root, token in roots if root),
                   key=lambda item: len(item[0]), reverse=True)

    def normalize(value):
        value = re.sub(r'\\(?!["\\])', '/', value)
        for root, token in roots:
            # Root replacement only; flag order, case and whitespace stay exact.
            value = re.sub(re.escape(root) + r'(?=$|[/\s"\'=])', lambda _: token, value,
                           flags=re.I if re.match(r'^[A-Za-z]:/', root) else 0)
        return value

    missing, compilers = [], {}
    for language in ('C', 'CXX'):
        files = list((directory / 'CMakeFiles').glob(f'*/CMake{language}Compiler.cmake'))
        if len(files) != 1:
            raise ValueError(f'Ambiguous or missing {language} compiler provenance')
        text = files[0].read_text(encoding='utf-8-sig')

        def variable(suffix):
            match = re.search(r'^set\(CMAKE_' + language + suffix + r' "([^"\r\n]*)"\)', text, re.M)
            return match[1] if match else ''

        compiler = Path(variable('_COMPILER'))
        roots.append((str(compiler).replace('\\', '/'), '${' + language + '_COMPILER}'))
        with compiler.open('rb') as stream:
            compiler_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
        compilers[language] = dict(id=variable('_COMPILER_ID'), version=variable('_COMPILER_VERSION'),
            target=cache.get(f'CMAKE_{language}_COMPILER_TARGET', ''),
            frontend_variant=variable('_COMPILER_FRONTEND_VARIANT'),
            sha256=compiler_hash)
        if not compilers[language]['id'] or not compilers[language]['version']:
            missing.append(f'{language} compiler identity')
    roots.sort(key=lambda item: len(item[0]), reverse=True)
    properties = (ndk / 'source.properties').read_text(encoding='utf-8-sig')
    revision = re.search(r'^Pkg.Revision\s*=\s*(\S+)\s*$', properties, re.M)
    if not revision:
        missing.append('NDK revision')
    flags = {key: normalize(value) for key, value in cache.items()
             if re.fullmatch(r'CMAKE_(?:C|CXX|ASM|EXE_LINKER|SHARED_LINKER|MODULE_LINKER|STATIC_LINKER)_FLAGS(?:_[A-Z0-9_]+)?', key)}
    for key in ('CMAKE_C_FLAGS', 'CMAKE_CXX_FLAGS', 'CMAKE_BUILD_TYPE'):
        if key not in cache:
            missing.append(key)
    for language in ('C', 'CXX'):
        if f'CMAKE_{language}_FLAGS_{cache.get("CMAKE_BUILD_TYPE", "").upper()}' not in flags:
            missing.append(f'{language} configuration flags')
    ninja = (directory / 'build.ninja').read_text(encoding='utf-8-sig')
    target_flags = {}
    # GS and common code live in PCSX2/PCSX2_LTO and other linked libraries,
    # not just emucore. Include every native target, while deduplicating equal
    # per-source settings so adding a source alone does not change provenance.
    rule_pattern = (r'((?:C|CXX|ASM)_(?:COMPILER|SHARED_LIBRARY_LINKER|STATIC_LIBRARY_LINKER|EXECUTABLE_LINKER))__'
                    r'([^\s]+)_' + re.escape(cache.get('CMAKE_BUILD_TYPE') or '<missing>') + r'(?=\s|$)')
    for block in re.split(r'(?=^build )', ninja, flags=re.M):
        rule = re.match(r'build .*?: ' + rule_pattern, block)
        if not rule:
            continue
        row = dict(target=rule[2], stage=rule[1])
        row.update({key.lower(): normalize(value) for key, value in
                    re.findall(r'^  (FLAGS|DEFINES|INCLUDES|LINK_FLAGS|LINK_LIBRARIES|ARCH_FLAGS|LANGUAGE_COMPILE_FLAGS) = (.*)$', block, re.M)})
        target_flags[json.dumps(row, sort_keys=True)] = row
    for target in ('emucore', 'emucorex_gs_oracle'):
        if not any(row['target'] == target and row['stage'].endswith('COMPILER') for row in target_flags.values()):
            missing.append(f'{target} emitted compiler settings')
    # Android's --target and --sysroot are part of the rule command, not FLAGS.
    rules = (directory / 'CMakeFiles/rules.ninja').read_text(encoding='utf-8-sig')
    compiler_rules = []
    for block in re.split(r'(?=^rule )', rules, flags=re.M):
        rule = re.match(r'rule ' + rule_pattern, block)
        command = re.search(r'^  command = (.+)$', block, re.M)
        if rule and command:
            compiler_rules.append(dict(target=rule[2], stage=rule[1], command=normalize(command[1])))
    if not compiler_rules:
        missing.append('compiler command rules')
    ir = cache.get('EMUCOREX_ENABLE_EXPERIMENTAL_IR', '').upper()
    if ir not in ('ON', 'OFF', 'TRUE', 'FALSE', '1', '0'):
        missing.append('experimental IR capability')
    abi = cache.get('CMAKE_ANDROID_ARCH_ABI', cache.get('ANDROID_ABI'))
    platform = cache.get('ANDROID_PLATFORM', cache.get('CMAKE_SYSTEM_VERSION'))
    if not abi or not platform:
        missing.append('Android ABI/platform')
    result = dict(schema=2, target_coverage='all-native-targets-v1', complete=not missing, build_type=cache.get('CMAKE_BUILD_TYPE'),
                  experimental_ir=ir in ('ON', 'TRUE', '1') if ir in ('ON', 'OFF', 'TRUE', 'FALSE', '1', '0') else None,
                  abi=abi, platform=platform,
                  ndk=dict(revision=revision[1] if revision else None), compilers=compilers,
                  cache_flags=flags, target_flags=[target_flags[key] for key in sorted(target_flags)],
                  compiler_rules=sorted(compiler_rules, key=lambda row: (row['target'], row['stage'])))
    if missing:
        result['missing'] = missing
    return dict(build_configuration=result, build_configuration_sha256=configuration_sha256(result))


def validated_build_configuration(value):
    configuration = value.get('build_configuration')
    if (not isinstance(configuration, dict) or configuration.get('schema') != 2 or
        configuration.get('target_coverage') != 'all-native-targets-v1' or
        configuration.get('complete') is not True or type(configuration.get('experimental_ir')) is not bool or
        not configuration.get('build_type') or not isinstance(configuration.get('compilers'), dict) or
        not isinstance(configuration.get('ndk'), dict) or not configuration['ndk'].get('revision') or
        not configuration.get('abi') or not configuration.get('platform') or not configuration.get('target_flags') or
        not configuration.get('cache_flags') or not configuration.get('compiler_rules')):
        raise ValueError('Missing or incomplete build configuration; timing baseline refused')
    for language in ('C', 'CXX'):
        compiler = configuration['compilers'].get(language)
        if (not isinstance(compiler, dict) or not compiler.get('id') or not compiler.get('version') or
            not re.fullmatch(r'[0-9a-f]{64}', str(compiler.get('sha256', '')))):
            raise ValueError('Missing compiler build configuration; timing baseline refused')
    if value.get('build_configuration_sha256') != configuration_sha256(configuration):
        raise ValueError('Invalid build configuration fingerprint; timing baseline refused')
    return configuration


def compare_captures(reference, candidate, output, tolerance=0):
    if type(tolerance) is not int or not 0 <= tolerance <= 255:
        raise ValueError('Tolerance must be 0..255')
    a, b = load_capture(reference), load_capture(candidate)
    if a.get('backend') != 'sw':
        raise ValueError('Reference must be Software (sw)')
    compatible(a, b, ('input_sha256', 'settings', 'device'))
    if len(a['checkpoints']) != len(b['checkpoints']):
        raise ValueError('Different checkpoint counts')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for index, (left, right) in enumerate(zip(a['checkpoints'], b['checkpoints'])):
        compatible(left, right, ('frame', 'packet', 'name', 'kind', 'width', 'height'))
        x, y = left['_data'], right['_data']
        item = {k: v for k, v in left.items() if k != '_data'}
        item.update(reference_sha256=hashlib.sha256(x).hexdigest(), candidate_sha256=hashlib.sha256(y).hexdigest())
        changed, outside, maximum, total, first = 0, 0, 0, 0, None
        bounds = [left['width'], left['height'], -1, -1]
        difference = bytearray(len(x) // 4 * 3)
        limit = tolerance if left['kind'] == 'rgba' else 0
        for p in range(len(x) // 4):
            errors = [abs(x[p * 4 + c] - y[p * 4 + c]) for c in range(4)]
            error = max(errors)
            if not error:
                continue
            px, py = p % left['width'], p // left['width']
            changed += 1
            outside += error > limit
            maximum = max(maximum, error)
            total += sum(errors)
            bounds = [min(bounds[0], px), min(bounds[1], py), max(bounds[2], px), max(bounds[3], py)]
            if first is None:
                channel = next(c for c in range(4) if errors[c])
                first = dict(x=px, y=py, channel=channel, reference=x[p*4+channel], candidate=y[p*4+channel])
            # Alpha-only and depth differences must remain visible too.
            difference[p*3:p*3+3] = bytes((min(255, error * 8), min(255, max(errors[:3]) * 8), min(255, errors[3] * 8)))
        item.update(different_pixels=changed, pixels_outside_tolerance=outside, max_byte_error=maximum,
                    mean_byte_error=total / len(x), first_difference=first,
                    bounds=bounds if changed else None, passed=outside == 0)
        if changed:
            filename = f'{index:06d}-diff.png'
            png(output / filename, left['width'], left['height'], difference)
            item['difference_image'] = filename
            for label, data in [('reference', x), ('candidate', y)]:
                image_name = f'{index:06d}-{label}.png'
                png(output / image_name, left['width'], left['height'], b''.join(data[p:p+3] for p in range(0, len(data), 4)))
                item[label + '_image'] = image_name
        results.append(item)
    result = dict(schema=1, reference=str(Path(reference).resolve()), candidate=str(Path(candidate).resolve()),
                  backend=b['backend'], tolerance=tolerance, passed=all(r['passed'] for r in results),
                  failed_checkpoints=sum(not r['passed'] for r in results), checkpoints=results)
    write_json(output / 'comparison.json', result)
    return result


def validate_frame_count(value):
    for key in ('frames', 'frames_per_replay', 'batch'):
        if type(value.get(key)) is not int or value[key] <= 0:
            raise ValueError(f'Invalid frame accounting: {key}')
    if value['frames'] != value['batch'] * value['frames_per_replay']:
        raise ValueError('Frame count does not match replay count and batch')
    if value['mode'] != 'benchmark' and value['batch'] != 1:
        raise ValueError('Capture/profile require one replay')


def benchmark_stats(value):
    if value.get('schema') != 1 or value.get('complete') is not True or value.get('mode') != 'benchmark':
        raise ValueError('Incomplete benchmark')
    validate_frame_count(value)
    samples = value.get('samples_ms', [])
    if not samples or any(type(x) not in (int, float) or not math.isfinite(x) or x <= 0 for x in samples):
        raise ValueError('Invalid benchmark samples')
    if type(value.get('frames')) is not int or value['frames'] <= 0 or value.get('warmup', 0) < 1:
        raise ValueError('Benchmark requires frames and warmup')
    ordered = sorted(samples)
    median = statistics.median(ordered)
    return dict(repeats=len(samples), median_ms=median, p95_ms=ordered[math.ceil(len(ordered)*.95)-1],
                min_ms=ordered[0], max_ms=ordered[-1], median_frame_ms=median / value['frames'],
                replay_frames_per_second=1000 * value['frames'] / median)


def compare_benchmarks(old, new):
    a, b = benchmark_stats(old), benchmark_stats(new)
    if validated_build_configuration(old) != validated_build_configuration(new):
        raise ValueError('Incompatible build configuration; timing baseline refused')
    compatible(old, new, ('backend', 'input_sha256', 'device', 'settings', 'frames', 'frames_per_replay', 'batch', 'warmup', 'driver_sha256'))
    return dict(before=a, after=b, median_change_percent=(b['median_ms']/a['median_ms']-1)*100,
                p95_change_percent=(b['p95_ms']/a['p95_ms']-1)*100)


def profile_stats(value):
    if value.get('schema') != 1 or value.get('complete') is not True or value.get('mode') != 'profile':
        raise ValueError('Incomplete profile')
    points = value.get('profiles', [])
    if not points or len(points) != value.get('frames'):
        raise ValueError('Missing profile frames')
    totals = {}
    seen = set()
    for point in points:
        identity = point['frame']
        if identity in seen:
            raise ValueError('Duplicate profile frame')
        seen.add(identity)
        for key in ('submit_ms', 'wait_ms'):
            if not math.isfinite(point[key]) or point[key] < 0:
                raise ValueError('Invalid profile time')
        for key, count in point['counters'].items():
            if not math.isfinite(count) or count < 0:
                raise ValueError('Invalid profile counter')
            totals[key] = totals.get(key, 0) + count
    hot = sorted(points, key=lambda x: x['submit_ms'] + x['wait_ms'], reverse=True)[:20]
    return dict(totals=totals, slowest_checkpoints=hot)


def validate_builtin_reference(directory):
    value = load_capture(directory)
    color = [p for p in value['checkpoints'] if p['kind'] == 'rgba']
    depth = [p for p in value['checkpoints'] if p['kind'] == 'depth']
    if value['base_cases'] != 44 or len(color) != value['base_cases'] + value['cases'] or len(depth) != value['base_cases'] + value['cases']:
        raise ValueError('Builtin corpus checkpoint count is incorrect')
    for item in color[:12]:
        if (item['width'], item['height']) != (64, 64) or item['_data'][:4] != bytes.fromhex('30201080'):
            raise ValueError(f"Software reference failed independent background anchor: {item['name']}")
    for item in depth[:12]:
        if item['_data'][:4] != bytes.fromhex('01000000'):
            raise ValueError(f"Software reference failed independent depth anchor: {item['name']}")
    # First fixture contains only two opaque rectangles with independently known coverage.
    first = color[0]['_data']
    for x, y, expected in [(4, 6, 'e0a06040'), (43, 41, '7040e0c0'), (60, 57, '30201080')]:
        offset = (y * 64 + x) * 4
        if first[offset:offset+4] != bytes.fromhex(expected):
            raise ValueError(f'Software reference sprite anchor failed at {x},{y}')


def run_report(root, tolerance=0, baseline=None):
    root = Path(root)
    meta = json.loads((root / 'metadata.json').read_text(encoding='utf-8-sig'))
    backends = meta['backends']
    if 'sw' not in backends or len(set(backends)) != len(backends) or len(backends) < 2:
        raise ValueError('Run must include Software and hardware')
    summary = dict(schema=1, metadata=meta, comparisons={}, benchmarks={}, profiles={}, performance_changes={})
    for backend in backends:
        capture_dir, perf_dir = root / backend / 'capture', root / backend / 'benchmark'
        capture = json.loads((capture_dir / 'native-capture.json').read_text())
        perf = json.loads((perf_dir / 'native-benchmark.json').read_text())
        profile = json.loads((root / backend / 'profile' / 'native-profile.json').read_text())
        for value in (capture, perf, profile):
            validate_frame_count(value)
            compatible(value, meta, ('cases', 'seed'))
        compatible(capture, perf, ('schema', 'backend', 'settings', 'frames_per_replay', 'cases', 'seed'))
        compatible(capture, profile, ('schema', 'backend', 'settings', 'frames', 'cases', 'seed'))
        if capture.get('backend') != backend or perf.get('mode') != 'benchmark' or capture.get('mode') != 'capture':
            raise ValueError('Native manifest backend or mode mismatch')
        if meta['input'] == 'builtin':
            corpus = (capture_dir / 'corpus.bin').read_bytes()
            if not corpus:
                raise ValueError('Empty synthetic input')
            input_hash = hashlib.sha256(corpus).hexdigest()
        else:
            input_hash = meta['input_sha256']
            copies = list(root.glob('input.gs*'))
            if len(copies) != 1 or hashlib.sha256(copies[0].read_bytes()).hexdigest() != input_hash:
                raise ValueError('Dump provenance check failed')
        for value in (capture, perf, profile):
            value.update(input_sha256=input_hash, device=meta['device'], core_sha256=meta['core_sha256'])
            for key in ('build_configuration', 'build_configuration_sha256'):
                if key in meta:
                    value[key] = meta[key]
        drivers = [(root / backend / mode / 'driver.txt').read_bytes() for mode in ('capture', 'benchmark', 'profile')]
        if not drivers[0] or any(d != drivers[0] for d in drivers):
            raise ValueError('GPU driver identity changed between passes')
        for value in (capture, perf):
            value['driver_sha256'] = hashlib.sha256(drivers[0]).hexdigest()
        write_json(capture_dir / 'capture.json', capture)
        write_json(perf_dir / 'benchmark.json', perf)
        write_json(root / backend / 'profile' / 'profile.json', profile)
        load_capture(capture_dir)
        if capture['frames'] <= 0 or perf.get('warmup') != meta['warmup'] or len(perf.get('samples_ms', [])) != meta['repeats']:
            raise ValueError('Unexpected frame, warmup or sample count')
        summary['benchmarks'][backend] = benchmark_stats(perf)
        summary['profiles'][backend] = profile_stats(profile)
        if baseline:
            previous = Path(baseline) / backend / 'benchmark' / 'benchmark.json'
            summary['performance_changes'][backend] = compare_benchmarks(json.loads(previous.read_text(encoding='utf-8-sig')), perf)
    if meta['input'] == 'builtin':
        validate_builtin_reference(root / 'sw' / 'capture')
    for backend in backends:
        if backend != 'sw':
            summary['comparisons'][backend] = compare_captures(root / 'sw' / 'capture', root / backend / 'capture', root / ('diff-' + backend), tolerance)
    summary['passed'] = all(c['passed'] for c in summary['comparisons'].values())
    write_json(root / 'report.json', summary)
    lines = ['# GS oracle', '', f"Device: {meta['model']}; Debug; exact tolerance: {tolerance}.", '',
             'Software reference; synchronized replay timing. These timings are not game FPS.', '',
             '| Backend | Correctness | Median replay ms | p95 replay ms | Median frame ms |',
             '|---|---|---:|---:|---:|']
    try:
        validated_build_configuration(meta)
        eligible_configuration = True
    except ValueError:
        eligible_configuration = False
    if eligible_configuration:
        lines[4:4] = [f"Build configuration SHA256: `{meta.get('build_configuration_sha256')}`.", '']
    else:
        lines[4:4] = ['Build configuration provenance is missing, incomplete, or from an older schema; these timings cannot serve as a performance baseline.', '']
    for backend in backends:
        stats = summary['benchmarks'][backend]
        result = summary['comparisons'].get(backend)
        status = 'REFERENCE' if result is None else ('PASS' if result['passed'] else f"{result['failed_checkpoints']} MISMATCHES")
        lines.append(f"| {backend} | {status} | {stats['median_ms']:.3f} | {stats['p95_ms']:.3f} | {stats['median_frame_ms']:.3f} |")
    lines += ['', 'Capture artifacts and native logs are retained under each backend. `diff-*/comparison.json` contains packet/frame coordinates and image links.', '']
    lines += ['Profiling counters are collected in a separate synchronized pass; they do not enter benchmark timings.', '',
              '| Backend | Draws | GPU draw calls | Readbacks | Barriers | Render passes |',
              '|---|---:|---:|---:|---:|---:|']
    for backend, stats in summary['profiles'].items():
        c = stats['totals']
        lines.append(f"| {backend} | {c.get('draws', 0):.0f} | {c.get('draw_calls', 0):.0f} | {c.get('readbacks', 0):.0f} | {c.get('barriers', 0):.0f} | {c.get('render_passes', 0):.0f} |")
    lines.append('')
    for backend, result in summary['performance_changes'].items():
        lines.append(f"{backend}: median time {result['median_change_percent']:+.2f}%; p95 {result['p95_change_percent']:+.2f}% (positive means slower).")
    (root / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    gallery = ['<!doctype html><meta charset="utf-8"><title>GS oracle</title>',
               '<style>body{background:#10151c;color:#e5edf5;font:15px system-ui;margin:32px}a{color:#73c6ff}img{image-rendering:pixelated;max-width:31%;min-width:180px;background:#222}details{padding:14px;border:1px solid #35404f;margin:12px 0}pre{white-space:pre-wrap}summary{cursor:pointer}</style>',
               '<h1>GS oracle</h1><pre>' + html.escape('\n'.join(lines)) + '</pre>']
    for backend, result in summary['comparisons'].items():
        for item in result['checkpoints']:
            if not item['different_pixels']:
                continue
            gallery.append(f"<details><summary>{html.escape(backend)} / {html.escape(item['name'])} / {html.escape(item['kind'])}: {item['different_pixels']} differing pixels</summary>")
            gallery.append('<p>Software reference / candidate / difference ×8 (alpha differences shown in blue)</p>')
            for key in ('reference_image', 'candidate_image', 'difference_image'):
                gallery.append(f'<a href="diff-{backend}/{item[key]}"><img loading="lazy" src="diff-{backend}/{item[key]}"></a>')
            gallery.append('<pre>' + html.escape(json.dumps(item, indent=2)) + '</pre></details>')
    (root / 'report.html').write_text('\n'.join(gallery), encoding='utf-8')
    print('\n'.join(lines))
    return summary


def campaign_report(root):
    root = Path(root).resolve()
    manifest = json.loads((root / 'campaign.json').read_text(encoding='utf-8-sig'))
    runs = manifest.get('runs', [])
    if manifest.get('schema') != 1 or manifest.get('complete') is not True or not runs:
        raise ValueError('Campaign is incomplete or empty')
    expected_backends = manifest['backends']
    if 'sw' not in expected_backends or len(set(expected_backends)) != len(expected_backends) or len(expected_backends) < 2:
        raise ValueError('Invalid planned campaign backends')
    summary = dict(schema=1, complete=True, passed=True, runs=[], errors=[], mismatch_groups=[])
    groups, identities, build_identity = {}, set(), None
    for job in runs:
        identity = job['id']
        if identity in identities:
            raise ValueError('Duplicate campaign run')
        identities.add(identity)
        folder = job.get('report_directory')
        if folder is None:
            summary['errors'].append(dict(id=identity, error=job.get('error', 'Missing run report')))
            continue
        path = (root / folder).resolve()
        if root not in path.parents:
            raise ValueError('Unsafe campaign report path')
        try:
            report = json.loads((path / 'report.json').read_text(encoding='utf-8-sig'))
            meta = report['metadata']
            if report.get('schema') != 1 or type(report.get('passed')) is not bool or not report.get('comparisons'):
                raise ValueError('Incomplete run summary')
            if (meta['serial'] != manifest['serial'] or meta['seed'] != job['seed'] or meta['cases'] != manifest['cases'] or
                set(meta['backends']) != set(expected_backends) or set(report['comparisons']) != set(expected_backends) - {'sw'} or
                meta['input'] != job['input_name'] or meta['input_sha256'] != job['input_sha256']):
                raise ValueError('Campaign run provenance mismatch')
            current_build = (meta['core_sha256'], meta['runner_sha256'], meta['device'],
                             meta.get('build_configuration_sha256'))
            if build_identity is not None and current_build != build_identity:
                raise ValueError('Campaign build or device changed between runs')
            build_identity = current_build
            failed = 0
            for backend, result in report['comparisons'].items():
                if not result.get('checkpoints'):
                    raise ValueError('Missing campaign checkpoints')
                for item in result['checkpoints']:
                    if item['passed']:
                        continue
                    failed += 1
                    first = item['first_difference']
                    signature = (backend, item['name'], item['kind'], item['reference_sha256'], item['candidate_sha256'])
                    group = groups.setdefault(signature, dict(backend=backend, name=item['name'], kind=item['kind'],
                        first_difference=first, different_pixels=item['different_pixels'], occurrences=[]))
                    group['occurrences'].append(dict(run=identity, seed=job['seed'], frame=item['frame'], packet=item['packet'],
                        report=str((Path(folder) / ('diff-' + backend) / 'comparison.json').as_posix())))
            if report['passed'] != (failed == 0):
                raise ValueError('Inconsistent campaign pass status')
            summary['runs'].append(dict(id=identity, seed=job['seed'], passed=report['passed'], failed_checkpoints=failed,
                                        report=str((Path(folder) / 'report.html').as_posix())))
        except (ValueError, KeyError, TypeError, OSError) as error:
            summary['errors'].append(dict(id=identity, error=str(error)))
    summary['complete'] = not summary['errors']
    summary['passed'] = summary['complete'] and all(r['passed'] for r in summary['runs'])
    summary['mismatch_groups'] = list(groups.values())
    write_json(root / 'campaign-report.json', summary)
    lines = ['# GS oracle campaign', '', f"Completed runs: {len(summary['runs'])}/{len(runs)}; errors: {len(summary['errors'])}; distinct exact mismatch groups: {len(groups)}.", '',
             '| Run | Seed | Status | Differing checkpoints |', '|---|---:|---|---:|']
    for run in summary['runs']:
        lines.append(f"| [{run['id']}]({run['report']}) | {run['seed']} | {'PASS' if run['passed'] else 'MISMATCH'} | {run['failed_checkpoints']} |")
    for error in summary['errors']:
        lines += ['', f"ERROR {error['id']}: {error['error']}"]
    lines += ['', '`campaign-report.json` groups identical reference/candidate snapshots and preserves every seed, frame, packet, and source report. A group is a triage aid, not a confirmed independent bug.']
    (root / 'campaign-report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    capture = sub.add_parser('capture')
    capture.add_argument('reference', type=Path)
    capture.add_argument('candidate', type=Path)
    capture.add_argument('output', type=Path)
    capture.add_argument('--tolerance', type=int, default=0)
    perf = sub.add_parser('benchmark')
    perf.add_argument('before', type=Path)
    perf.add_argument('after', type=Path)
    perf.add_argument('output', type=Path)
    report = sub.add_parser('report')
    report.add_argument('root', type=Path)
    report.add_argument('--tolerance', type=int, default=0)
    report.add_argument('--baseline', type=Path)
    campaign = sub.add_parser('campaign')
    campaign.add_argument('root', type=Path)
    configuration = sub.add_parser('build-configuration')
    configuration.add_argument('directory', type=Path)
    configuration.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'build-configuration':
            write_json(args.output, build_configuration(args.directory))
            return 0
        if args.command == 'campaign':
            report = campaign_report(args.root)
            return 2 if not report['complete'] else 0 if report['passed'] else 1
        if args.command == 'report':
            return 0 if run_report(args.root, args.tolerance, args.baseline)['passed'] else 1
        if args.command == 'capture':
            report = compare_captures(args.reference, args.candidate, args.output, args.tolerance)
            print(f"{args.candidate.name}: {'PASS' if report['passed'] else 'MISMATCH'}; {report['failed_checkpoints']}/{len(report['checkpoints'])} checkpoints differ")
            return 0 if report['passed'] else 1
        report = compare_benchmarks(json.loads(args.before.read_text(encoding='utf-8-sig')),
                                    json.loads(args.after.read_text(encoding='utf-8-sig')))
        write_json(args.output, report)
        print(f"Median time change: {report['median_change_percent']:+.2f}% (positive = slower)")
        return 0
    except (ValueError, KeyError, TypeError, OSError) as error:
        print(f'GS oracle ERROR: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
