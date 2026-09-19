#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0+
"""Bake the EmuCoreX Vulkan shader pack from PCSX2's runtime shader cache.

PCSX2 compiles shaders lazily while a title runs and appends the resulting
SPIR-V to <cache>/vulkan_shaders.idx + .bin. This tool repacks selected entries
into the read-only baked_shaders.idx + .bin pair consumed by VKBakedShaderPack.
Entries keep the exact same source hash key as VKShaderCache, so the runtime can
never bind a binary that does not match the GLSL it would have compiled.

Typical workflow:
  1. Install a build on a device and boot a title (this also compiles the
     utility and warmup pipelines), then play for a while so the cache collects
     the TFX permutations the title actually uses.
  2. Pull <data>/cache/vulkan_shaders.idx and .bin.
  3. Run: python bake_vulkan_shaders.py harvest --cache-dir <dir> --output <dir>
  4. Copy baked_shaders.idx/.bin into
     app/src/main/cpp/pcsx2/bin/resources/shaders/vulkan/ and commit.

The pack format is device independent: it stores SPIR-V only, so one pack works
on every Vulkan device. The shader cache version is checked at runtime and a
stale pack is ignored, never trusted. Multiple caches (for example from an
Adreno and a Turnip device) can be merged into one pack; device-profile specific
shaders have different source hashes and coexist as separate entries.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

PACK_MAGIC = 0x4B425845  # 'EXBK'
PACK_VERSION = 1
SPIRV_MAGIC = 0x07230203

SHADER_CACHE_VERSION_RE = re.compile(r"SHADER_CACHE_VERSION\s*=\s*(\d+)")

CACHE_HEADER = struct.Struct("<IIII16s")
INDEX_ENTRY = struct.Struct("<QQIIII")
PACK_HEADER = struct.Struct("<IIIIQII")
PACK_ENTRY = struct.Struct("<QQIIII")

# Mirrors the vendored shaderc shaderc_shader_kind enum order (vertex, fragment,
# compute, geometry, tess control, tess evaluation), NOT the separate glsl_* values.
STAGE_NAMES = {
    0: "vert",
    1: "frag",
    2: "comp",
    3: "geom",
    4: "tesc",
    5: "tese",
}

INDEX_FILENAME = "vulkan_shaders.idx"
BLOB_FILENAME = "vulkan_shaders.bin"
PACK_INDEX_FILENAME = "baked_shaders.idx"
PACK_BLOB_FILENAME = "baked_shaders.bin"


@dataclass(frozen=True)
class ShaderKey:
    source_hash_low: int
    source_hash_high: int
    source_length: int
    shader_type: int

    @property
    def stage(self) -> str:
        return STAGE_NAMES.get(self.shader_type, f"type{self.shader_type}")


@dataclass
class RuntimeCache:
    file_version: int
    vendor_id: int
    device_id: int
    uuid: bytes
    entries: dict[ShaderKey, bytes]


class BakeError(RuntimeError):
    pass


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / ".git").exists():
            return candidate
    raise BakeError("could not locate the repository root (no .git directory found)")


def read_shader_cache_version(repo_root: Path) -> int:
    version_header = repo_root / "app" / "src" / "main" / "cpp" / "pcsx2" / "pcsx2" / "ShaderCacheVersion.h"
    if not version_header.is_file():
        raise BakeError(f"ShaderCacheVersion.h not found at {version_header}")
    match = SHADER_CACHE_VERSION_RE.search(version_header.read_text(encoding="utf-8"))
    if match is None:
        raise BakeError(f"could not parse SHADER_CACHE_VERSION from {version_header}")
    return int(match.group(1))


def parse_runtime_cache(
    index_path: Path, blob_path: Path, expected_version: int | None, allow_version_mismatch: bool
) -> RuntimeCache:
    index_data = index_path.read_bytes()
    blob_data = blob_path.read_bytes()

    if len(index_data) < 4 + CACHE_HEADER.size:
        raise BakeError(f"'{index_path}' is too small to be a shader cache index")

    (file_version,) = struct.unpack_from("<I", index_data, 0)
    if expected_version is not None and file_version != expected_version and not allow_version_mismatch:
        raise BakeError(
            f"shader cache version {file_version} does not match SHADER_CACHE_VERSION "
            f"{expected_version}; regenerate the cache with the matching core or pass "
            f"--allow-version-mismatch"
        )

    header_length, header_version, vendor_id, device_id, uuid = CACHE_HEADER.unpack_from(index_data, 4)
    if header_length < CACHE_HEADER.size or header_version != 1:
        raise BakeError(f"'{index_path}' has an invalid pipeline cache header")

    if len(blob_data) % 4 != 0:
        raise BakeError(f"'{blob_path}' size is not a multiple of 4 bytes")
    blob_words = len(blob_data) // 4

    entries: dict[ShaderKey, bytes] = {}
    offset = 4 + CACHE_HEADER.size
    duplicate_count = 0
    entry_index = 0
    while offset + INDEX_ENTRY.size <= len(index_data):
        hash_low, hash_high, source_length, shader_type, blob_offset, blob_size = INDEX_ENTRY.unpack_from(
            index_data, offset
        )
        offset += INDEX_ENTRY.size
        entry_index += 1

        if blob_size == 0:
            raise BakeError(f"index entry {entry_index} has a zero-sized blob")
        if blob_offset + blob_size > blob_words:
            raise BakeError(
                f"index entry {entry_index} points past the end of the blob file "
                f"({blob_offset} + {blob_size} > {blob_words})"
            )

        key = ShaderKey(hash_low, hash_high, source_length, shader_type)
        if key in entries:
            duplicate_count += 1
            continue

        start = blob_offset * 4
        blob = blob_data[start : start + blob_size * 4]
        if struct.unpack_from("<I", blob, 0)[0] != SPIRV_MAGIC:
            raise BakeError(f"index entry {entry_index} does not contain valid SPIR-V")
        entries[key] = blob

    trailing = len(index_data) - offset
    if trailing != 0:
        print(f"warning: '{index_path}' has {trailing} trailing bytes, ignoring", file=sys.stderr)
    if duplicate_count:
        print(f"warning: skipped {duplicate_count} duplicate index entries", file=sys.stderr)

    return RuntimeCache(
        file_version=file_version,
        vendor_id=vendor_id,
        device_id=device_id,
        uuid=uuid,
        entries=entries,
    )


def merge_caches(caches: list[tuple[Path, RuntimeCache]]) -> dict[ShaderKey, bytes]:
    versions = {cache.file_version for _, cache in caches}
    if len(versions) != 1:
        raise BakeError(f"cannot merge caches with different shader cache versions: {sorted(versions)}")

    merged: dict[ShaderKey, bytes] = {}
    conflicts = 0
    for _, cache in caches:
        for key, blob in cache.entries.items():
            existing = merged.get(key)
            if existing is None:
                merged[key] = blob
            elif existing != blob:
                conflicts += 1

    if conflicts:
        print(f"warning: {conflicts} entries differed across caches, kept the first", file=sys.stderr)
    return merged


def build_pack_bytes(entries: dict[ShaderKey, bytes], shader_cache_version: int) -> tuple[bytes, bytes, list[dict]]:
    blob = bytearray()
    index = bytearray()
    manifest_entries: list[dict] = []

    for key in sorted(entries, key=lambda k: (k.source_hash_low, k.source_hash_high, k.source_length, k.shader_type)):
        spirv = entries[key]
        blob_offset = len(blob) // 4
        blob_size = len(spirv) // 4
        blob.extend(spirv)
        index.extend(PACK_ENTRY.pack(
            key.source_hash_low,
            key.source_hash_high,
            key.source_length,
            key.shader_type,
            blob_offset,
            blob_size,
        ))
        manifest_entries.append({
            "source_hash_low": f"{key.source_hash_low:016x}",
            "source_hash_high": f"{key.source_hash_high:016x}",
            "source_length": key.source_length,
            "shader_type": key.shader_type,
            "stage": key.stage,
            "blob_words": blob_size,
        })

    header = PACK_HEADER.pack(
        PACK_MAGIC,
        PACK_VERSION,
        shader_cache_version,
        len(entries),
        len(blob),
        0,
        0,
    )
    return bytes(header + index), bytes(blob), manifest_entries


def write_pack(
    entries: dict[ShaderKey, bytes],
    sources: list[dict],
    output_dir: Path,
    shader_cache_version: int,
) -> None:
    index_bytes, blob_bytes, manifest_entries = build_pack_bytes(entries, shader_cache_version)
    output_dir.mkdir(parents=True, exist_ok=True)

    (output_dir / PACK_INDEX_FILENAME).write_bytes(index_bytes)
    (output_dir / PACK_BLOB_FILENAME).write_bytes(blob_bytes)

    stage_counts: dict[str, int] = {}
    for entry in manifest_entries:
        stage_counts[entry["stage"]] = stage_counts.get(entry["stage"], 0) + 1

    manifest = {
        "tool": "bake_vulkan_shaders.py",
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "pack_version": PACK_VERSION,
        "shader_cache_version": shader_cache_version,
        "sources": sources,
        "entry_count": len(manifest_entries),
        "blob_bytes": len(blob_bytes),
        "stage_counts": stage_counts,
        "entries": manifest_entries,
    }
    (output_dir / "baked_shaders.manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )

    print(f"wrote {output_dir / PACK_INDEX_FILENAME} ({len(index_bytes)} bytes)")
    print(f"wrote {output_dir / PACK_BLOB_FILENAME} ({len(blob_bytes)} bytes)")
    print(f"shaders: {len(manifest_entries)} ({', '.join(f'{k}={v}' for k, v in sorted(stage_counts.items()))})")


def verify_pack(pack_dir: Path, expected_version: int | None) -> None:
    index_path = pack_dir / PACK_INDEX_FILENAME
    blob_path = pack_dir / PACK_BLOB_FILENAME
    index_data = index_path.read_bytes()
    blob_data = blob_path.read_bytes()

    if len(index_data) < PACK_HEADER.size:
        raise BakeError(f"'{index_path}' is too small")
    magic, pack_version, shader_cache_version, entry_count, blob_size_bytes, _, _ = PACK_HEADER.unpack_from(index_data, 0)
    if magic != PACK_MAGIC:
        raise BakeError(f"'{index_path}' has an invalid magic")
    if pack_version != PACK_VERSION:
        raise BakeError(f"'{index_path}' has unsupported pack version {pack_version}")
    if expected_version is not None and shader_cache_version != expected_version:
        raise BakeError(
            f"pack shader cache version {shader_cache_version} does not match expected {expected_version}"
        )
    if blob_size_bytes != len(blob_data):
        raise BakeError(f"pack declares {blob_size_bytes} blob bytes, file has {len(blob_data)}")
    if blob_size_bytes % 4 != 0:
        raise BakeError("pack blob size is not a multiple of 4")

    expected_index_size = PACK_HEADER.size + entry_count * PACK_ENTRY.size
    if len(index_data) != expected_index_size:
        raise BakeError(
            f"pack index size {len(index_data)} does not match {entry_count} entries "
            f"({expected_index_size})"
        )

    seen: set[ShaderKey] = set()
    blob_words = blob_size_bytes // 4
    for i in range(entry_count):
        fields = PACK_ENTRY.unpack_from(index_data, PACK_HEADER.size + i * PACK_ENTRY.size)
        key = ShaderKey(fields[0], fields[1], fields[2], fields[3])
        blob_offset, blob_size = fields[4], fields[5]
        if key in seen:
            raise BakeError(f"duplicate entry for {key}")
        seen.add(key)
        if blob_size == 0 or blob_offset + blob_size > blob_words:
            raise BakeError(f"entry {i} has an invalid blob range")
        if struct.unpack_from("<I", blob_data, blob_offset * 4)[0] != SPIRV_MAGIC:
            raise BakeError(f"entry {i} does not contain valid SPIR-V")

    print(f"pack OK: version={pack_version} shader_cache_version={shader_cache_version} "
          f"entries={entry_count} blob_bytes={blob_size_bytes}")


def inspect_cache(cache_dir: Path) -> None:
    index_path = cache_dir / INDEX_FILENAME
    blob_path = cache_dir / BLOB_FILENAME
    cache = parse_runtime_cache(index_path, blob_path, None, True)
    stage_counts: dict[str, int] = {}
    total_words = 0
    for key, blob in cache.entries.items():
        stage_counts[key.stage] = stage_counts.get(key.stage, 0) + 1
        total_words += len(blob) // 4
    print(f"cache version={cache.file_version} entries={len(cache.entries)} blob_words={total_words} "
          f"vendor={cache.vendor_id:#x} device={cache.device_id:#x}")
    for stage, count in sorted(stage_counts.items()):
        print(f"  {stage}: {count}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="mode", required=True)

    harvest = subparsers.add_parser("harvest", help="repack runtime shader caches into a baked pack")
    harvest.add_argument("--cache-dir", type=Path, action="append", required=True,
                         help="directory with vulkan_shaders.idx/.bin (repeatable, merged)")
    harvest.add_argument("--output", type=Path, required=True, help="output directory for baked_shaders.*")
    harvest.add_argument("--shader-cache-version", type=int, default=None,
                         help="expected SHADER_CACHE_VERSION (defaults to the repo's ShaderCacheVersion.h)")
    harvest.add_argument("--allow-version-mismatch", action="store_true")

    verify = subparsers.add_parser("verify", help="validate a baked pack")
    verify.add_argument("--pack-dir", type=Path, required=True)
    verify.add_argument("--shader-cache-version", type=int, default=None)

    inspect = subparsers.add_parser("inspect", help="summarize a runtime shader cache")
    inspect.add_argument("--cache-dir", type=Path, required=True)

    args = parser.parse_args()

    try:
        if args.mode == "harvest":
            expected = args.shader_cache_version
            if expected is None:
                expected = read_shader_cache_version(find_repo_root(Path(__file__).resolve()))

            caches: list[tuple[Path, RuntimeCache]] = []
            for cache_dir in args.cache_dir:
                cache = parse_runtime_cache(
                    cache_dir / INDEX_FILENAME,
                    cache_dir / BLOB_FILENAME,
                    expected,
                    args.allow_version_mismatch,
                )
                caches.append((cache_dir, cache))

            entries = merge_caches(caches)
            sources = [
                {
                    "path": str(cache_dir),
                    "shader_cache_version": cache.file_version,
                    "vendor_id": f"{cache.vendor_id:#x}",
                    "device_id": f"{cache.device_id:#x}",
                    "pipeline_cache_uuid": cache.uuid.hex(),
                    "entry_count": len(cache.entries),
                }
                for cache_dir, cache in caches
            ]
            write_pack(entries, sources, args.output, expected)
        elif args.mode == "verify":
            expected = args.shader_cache_version
            if expected is None:
                try:
                    expected = read_shader_cache_version(find_repo_root(Path(__file__).resolve()))
                except BakeError:
                    expected = None
            verify_pack(args.pack_dir, expected)
        elif args.mode == "inspect":
            inspect_cache(args.cache_dir)
    except (BakeError, OSError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
