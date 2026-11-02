#!/usr/bin/env python3
"""Audit that one selected Android core compiles only its complete GS fork.

This is a source/compile-database audit, not a build. It deliberately checks
non-GS translation units too, since they can include GS headers directly.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from functools import lru_cache
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[7]
CANONICAL_GS = REPO_ROOT / "app/src/main/cpp/PCSX2/pcsx2/GS"
VARIANTS = REPO_ROOT / "app/src/main/cpp/GSVariants"
PCSX2_ROOT = REPO_ROOT / "app/src/main/cpp/PCSX2"
PCSX2_CORE = PCSX2_ROOT / "pcsx2"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".metal"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".inc"}
SCAN_SUFFIXES = SOURCE_SUFFIXES | HEADER_SUFFIXES
GS_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*(["<])((?:pcsx2/)?GS(?:/[^\r\n">]+|\.h))([">])([^\r\n]*)$', re.MULTILINE)
MALFORMED_GS_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*<(?:GS/|GS\.h)[^>\r\n]*$', re.MULTILINE)
RESOURCE_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*["<](bin/resources/[^">]+)[">]', re.MULTILINE)


def fail(message: str) -> None:
    print(f"GS isolation FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def include_has_trailing_tokens(suffix: str) -> bool:
    tail = suffix.strip()
    return bool(tail and not tail.startswith(("//", "/*")))


def check_include_lexer() -> None:
    """Mutation check: joined directives must not pass as one good include."""
    for joined in (
        '#include <GS/GS.h>#include <GS/GSLzma.h>\n',
        '#include "GS/GS.h"#include "GS/GSLzma.h"\n',
    ):
        match = GS_INCLUDE_RE.search(joined)
        if not match or not include_has_trailing_tokens(match.group(4)):
            fail("include lexer mutation check failed to reject a joined #include directive")
    valid = GS_INCLUDE_RE.search('#include <GS/GS.h> // valid trailing comment\n')
    if not valid or include_has_trailing_tokens(valid.group(4)):
        fail("include lexer mutation check rejected a valid trailing comment")
    if not MALFORMED_GS_INCLUDE_RE.search('#include <GS/GS.h\n'):
        fail("include lexer mutation check failed to reject a missing closing delimiter")


def family_root(family: str) -> Path:
    return VARIANTS / family.title() / "GS"


def variant_files(root: Path) -> set[str]:
    if not root.is_dir():
        fail(f"selected GS source root does not exist: {root}")
    return {
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES | HEADER_SUFFIXES
    }


def check_fork_completeness(root: Path) -> int:
    canonical = variant_files(CANONICAL_GS)
    selected = variant_files(root)
    missing = sorted(canonical - selected)
    extra = sorted(selected - canonical)
    if missing:
        fail("selected branch is missing canonical GS inputs: " + ", ".join(missing[:20]))
    # Extra branch files are allowed for branch-local code, but must not be
    # symlinks/hardlinks back into the other tree.
    linked = [p for p in root.rglob("*") if p.is_symlink()]
    if linked:
        fail("GS source roots must be physical copies; symlinks found: " + ", ".join(map(str, linked[:10])))
    return len(selected)


def check_variant_facade(root: Path) -> None:
    facade = root.parent / "GS.h"
    canonical_facade = PCSX2_CORE / "GS.h"
    opposite_family = "Adreno" if root.parent.name == "Mali" else "Mali"
    opposite_facade = VARIANTS / opposite_family / "GS.h"
    if not facade.is_file() or facade.is_symlink():
        fail(f"selected family has no physical GS facade header: {facade}")
    if not canonical_facade.is_file():
        fail(f"canonical upstream GS facade is missing: {canonical_facade}")
    for other in (canonical_facade, opposite_facade):
        if other.exists() and os.path.samefile(facade, other):
            fail(f"selected GS facade aliases another path instead of being a physical copy: {facade} == {other}")
    host = root.parent / "Host.cpp"
    canonical_host = PCSX2_CORE / "Host.cpp"
    opposite_host = VARIANTS / opposite_family / "Host.cpp"
    if not host.is_file() or host.is_symlink() or not canonical_host.is_file():
        fail(f"selected family Host.cpp overlay or canonical source is missing: {host}")
    if os.path.samefile(host, canonical_host) or (opposite_host.exists() and os.path.samefile(host, opposite_host)):
        fail(f"selected Host.cpp must be a physical family overlay: {host}")
    overlay = host.read_bytes()
    overlay = overlay.replace(b"#include <GS.h>", b'#include "GS.h"')
    overlay = overlay.replace(b"#include <GS/Renderers/HW/GSTextureReplacements.h>", b'#include "GS/Renderers/HW/GSTextureReplacements.h"')
    if overlay != canonical_host.read_bytes():
        fail(f"selected Host.cpp differs from pinned upstream beyond the GS include spelling: {host}")
    # The outer GS.cpp implements GS register semantics and therefore belongs
    # to the vendor fork just as the GS/ subtree does.
    branch_gs_cpp = root.parent / "GS.cpp"
    canonical_gs_cpp = PCSX2_CORE / "GS.cpp"
    opposite_gs_cpp = VARIANTS / opposite_family / "GS.cpp"
    if not branch_gs_cpp.is_file() or branch_gs_cpp.is_symlink() or not canonical_gs_cpp.is_file():
        fail(f"selected family GS.cpp or canonical source is missing: {branch_gs_cpp}")
    if branch_gs_cpp.read_bytes() != canonical_gs_cpp.read_bytes():
        fail(f"selected family GS.cpp is not an exact physical source copy: {branch_gs_cpp}")
    if os.path.samefile(branch_gs_cpp, canonical_gs_cpp) or (opposite_gs_cpp.exists() and os.path.samefile(branch_gs_cpp, opposite_gs_cpp)):
        fail(f"selected GS.cpp must be a physical family copy: {branch_gs_cpp}")


def check_include_resolution(root: Path) -> int:
    """Every literal GS/... include in shipped PCSX2/Android C++ must exist in this fork."""
    include_files = [p for p in PCSX2_CORE.rglob("*") if p.is_file() and p.suffix.lower() in SCAN_SUFFIXES]
    include_files += [p for p in root.parent.rglob("*") if p.is_file() and p.suffix.lower() in SCAN_SUFFIXES]
    include_files += [p for p in (REPO_ROOT / "app/src/main/cpp/ARM_ANDROID/src").rglob("*") if p.is_file() and p.suffix.lower() in SCAN_SUFFIXES]
    missing: list[str] = []
    missing_resource_includes: list[str] = []
    count = 0
    for source in include_files:
        # The canonical upstream GS tree is an uncompiled reference now; all
        # active branch includes are checked from the selected physical fork.
        if source.is_relative_to(PCSX2_CORE / "GS") or source in (
            PCSX2_CORE / "GS.h", PCSX2_CORE / "Host.cpp", PCSX2_CORE / "Achievements.cpp",
        ):
            continue
        try:
            contents = source.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if MALFORMED_GS_INCLUDE_RE.search(contents):
            missing.append(f"{source.relative_to(REPO_ROOT)} has a malformed GS include directive")
        for opener, include, closer, trailing in GS_INCLUDE_RE.findall(contents):
            count += 1
            if (opener, closer) not in (("<", ">"), ('"', '"')):
                missing.append(f"{source.relative_to(REPO_ROOT)} has mismatched include delimiters for {include}")
            if include_has_trailing_tokens(trailing):
                missing.append(f"{source.relative_to(REPO_ROOT)} has trailing tokens after GS include {include}; directives may have been merged")
            external_core_source = False
            try:
                source.relative_to(PCSX2_CORE / "GS")
            except ValueError:
                try:
                    source.relative_to(PCSX2_CORE)
                    external_core_source = source != PCSX2_CORE / "GS.h"
                except ValueError:
                    external_core_source = source.is_relative_to(REPO_ROOT / "app/src/main/cpp/ARM_ANDROID/src")
            if external_core_source and (opener != "<" or closer != ">"):
                missing.append(f"{source.relative_to(REPO_ROOT)} must use angle form for selected GS include {include}")
            if include == "GS.h":
                target = root.parent / "GS.h"
            elif include.startswith("GS/"):
                target = root.parent / include
            else:
                missing.append(f"{source.relative_to(REPO_ROOT)} uses non-variant include prefix {include}")
                continue
            if not target.is_file():
                missing.append(f"{source.relative_to(REPO_ROOT)} -> {include}")
        for include in RESOURCE_INCLUDE_RE.findall(contents):
            if not (root.parent / include).is_file():
                missing_resource_includes.append(f"{source.relative_to(REPO_ROOT)} -> {include}")
    if missing:
        fail("selected fork cannot satisfy GS includes (would fall through to canonical): " + "; ".join(missing[:20]))
    if missing_resource_includes:
        fail("selected fork cannot satisfy compile-time GS resource includes: " + "; ".join(missing_resource_includes[:20]))
    return count


def check_resources(family: str) -> int:
    canonical = PCSX2_ROOT / "bin/resources/shaders"
    selected = PCSX2_ROOT / f"bin/resources/gs_variants/{family.lower()}/shaders"
    if not canonical.is_dir() or not selected.is_dir():
        fail(f"missing branch shader resources: {selected}")
    expected = {p.relative_to(canonical).as_posix() for p in canonical.rglob("*") if p.is_file()}
    actual = {p.relative_to(selected).as_posix() for p in selected.rglob("*") if p.is_file()}
    missing = sorted(expected - actual)
    if missing:
        fail(f"{family} shader tree is incomplete: " + ", ".join(missing[:20]))
    if any(p.is_symlink() for p in selected.rglob("*")):
        fail(f"{family} shader resources must be physical files, not links")
    # Runtime and in-tree shader fixtures must pass through GSDevice's
    # family-rooted loader. Direct host-resource reads could silently make a
    # branch oracle test the canonical shader directory instead.
    bypasses: list[str] = []
    for source in family_root(family).rglob("*"):
        if not source.is_file() or source.suffix.lower() not in SCAN_SUFFIXES:
            continue
        contents = source.read_text(encoding="utf-8", errors="replace")
        if "HostReadResourceFile" in contents:
            bypasses.append(f"{source.relative_to(REPO_ROOT)} calls HostReadResourceFile")
        if re.search(r'(?:ReadFileToString|ReadBinaryFile|HostReadResourceFile)\s*\([^;\n]*"(?:resources/)?shaders/(?:vulkan|opengl|dx11|common)/', contents):
            bypasses.append(f"{source.relative_to(REPO_ROOT)} contains a canonical shader resource path")
    if bypasses:
        fail(f"{family} shader resource bypass: " + "; ".join(bypasses[:20]))
    family_token = family.lower()
    shader_loader = (family_root(family) / "Renderers/Common/GSDevice.cpp").read_text(encoding="utf-8")
    baked_loader = (family_root(family) / "Renderers/Vulkan/VKBakedShaderPack.cpp").read_text(encoding="utf-8")
    if f'gs_variants/{family_token}/' not in shader_loader:
        fail(f"{family} GSDevice shader loader does not use its private asset root")
    if f'gs_variants/{family_token}/shaders/vulkan/baked_shaders.idx' not in baked_loader:
        fail(f"{family} baked shader loader does not use its private asset root")
    if f'gs_{family_token}_baked_shaders.idx' not in baked_loader:
        fail(f"{family} baked shader fallback cache is not namespaced")
    return len(actual)


def command_tokens(entry: dict) -> list[str]:
    if isinstance(entry.get("arguments"), list):
        return [str(token) for token in entry["arguments"]]
    return shlex.split(str(entry.get("command", "")), posix=(sys.platform != "win32"))


@lru_cache(maxsize=None)
def resolved_dependency(raw_path: str, build_root: str) -> Path:
    """Resolve one Ninja dependency spelling once per audit invocation."""
    candidate = Path(raw_path.replace("\\", os.sep))
    return (candidate if candidate.is_absolute() else Path(build_root) / candidate).resolve()


def check_compile_database(root: Path, compile_database: Path) -> tuple[int, int, int]:
    try:
        entries = json.loads(compile_database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read compile database {compile_database}: {error}")
    variant = root.parent.resolve()
    opposite_family = "Adreno" if root.parent.name == "Mali" else "Mali"
    opposite_variant = (VARIANTS / opposite_family).resolve()
    canonical = CANONICAL_GS.resolve()
    gs_units = 0
    outer_gs_units = 0
    relevant_units = 0
    android_units = 0
    errors: list[str] = []
    for entry in entries:
        directory = Path(entry.get("directory", ".")).resolve()
        source = Path(entry.get("file", ""))
        if not source.is_absolute():
            source = (directory / source).resolve()
        tokens = command_tokens(entry)
        source_is_pcsx2_core = source.is_relative_to(PCSX2_CORE.resolve())
        source_is_android = source.is_relative_to((REPO_ROOT / "app/src/main/cpp/ARM_ANDROID/src").resolve())
        source_is_variant = source.is_relative_to(variant)
        source_is_opposite_variant = source.is_relative_to(opposite_variant)
        if source_is_opposite_variant:
            errors.append(f"opposite-family GS object is still compiled: {source}")
        if source == (PCSX2_CORE / "Host.cpp").resolve():
            errors.append("canonical Host.cpp is compiled instead of the selected family overlay")
        if source == (PCSX2_CORE / "GS.cpp").resolve():
            errors.append("canonical outer GS.cpp is compiled instead of the selected family copy")
        if not (source_is_pcsx2_core or source_is_android or source_is_variant or source_is_opposite_variant):
            continue
        relevant_units += 1
        android_units += int(source_is_android)
        if source_is_variant:
            gs_units += 1
            outer_gs_units += int(source == (variant / "GS.cpp").resolve())
        else:
            try:
                source.relative_to(canonical)
            except ValueError:
                pass
            else:
                errors.append(f"canonical GS object is still compiled: {source}")
        include_args: list[str] = []
        index = 0
        while index < len(tokens):
            token = tokens[index]
            if token in ("-I", "-isystem") and index + 1 < len(tokens):
                index += 1
                include_args.append(tokens[index])
            elif token.startswith("-I") and len(token) > 2:
                include_args.append(token[2:])
            index += 1
        resolved_includes = [(directory / p).resolve() if not Path(p).is_absolute() else Path(p).resolve() for p in include_args]
        variant_positions = [i for i, p in enumerate(resolved_includes) if p == variant]
        canonical_roots = {PCSX2_ROOT.resolve(), (PCSX2_ROOT / "pcsx2").resolve()}
        canonical_positions = [i for i, p in enumerate(resolved_includes) if p in canonical_roots]
        needs_variant_include = source_is_variant or source_is_pcsx2_core
        if source_is_android:
            try:
                needs_variant_include = bool(GS_INCLUDE_RE.search(source.read_text(encoding="utf-8", errors="replace")))
            except OSError:
                needs_variant_include = False
        if needs_variant_include:
            if not variant_positions:
                errors.append(f"variant include root is absent for GS-dependent source {source}")
            if variant_positions and canonical_positions and min(variant_positions) > min(canonical_positions):
                errors.append(f"variant include root is behind canonical headers for {source}")
    if not relevant_units:
        fail(f"no PCSX2 core/Android core compile commands found in {compile_database}")
    if not android_units:
        errors.append("no Android emucore translation units found in compile database; direct GS include precedence was not checked")
    if not gs_units:
        errors.append("no GS translation units were compiled from the selected variant root")
    if not outer_gs_units:
        errors.append("selected family outer GS.cpp is absent from the compile database")
    if errors:
        fail("; ".join(errors[:30]))
    return relevant_units, gs_units, android_units


def check_ninja_dependencies(root: Path, build_directory: Path, ninja_executable: str) -> tuple[int, int]:
    if not build_directory.is_dir():
        fail(f"Ninja build directory does not exist: {build_directory}")
    targets_result = subprocess.run(
        [ninja_executable, "-C", str(build_directory), "-t", "targets", "all"],
        check=False, capture_output=True, text=True,
    )
    if targets_result.returncode != 0:
        fail(f"ninja -t targets all failed in {build_directory}: {targets_result.stderr.strip()}")
    live_core_objects: set[str] = set()
    for line in targets_result.stdout.splitlines():
        if ": " not in line:
            continue
        target = line.rsplit(": ", 1)[0]
        normalized_target = target.replace("\\", "/").lower()
        is_core_object = any(marker in normalized_target for marker in (
            "/cmakefiles/pcsx2.dir/", "cmakefiles/pcsx2.dir/",
            "/cmakefiles/pcsx2_lto.dir/", "cmakefiles/pcsx2_lto.dir/",
            "/cmakefiles/emucore.dir/", "cmakefiles/emucore.dir/",
        ))
        if is_core_object and normalized_target.endswith((".o", ".obj")):
            live_core_objects.add(normalized_target)
    if not live_core_objects:
        fail(f"ninja -t targets all found no live PCSX2/emucore object outputs in {build_directory}")

    result = subprocess.run(
        [ninja_executable, "-C", str(build_directory), "-t", "deps"],
        check=False, capture_output=True, text=True,
    )
    if result.returncode != 0:
        fail(f"ninja -t deps failed in {build_directory}: {result.stderr.strip()}")
    sections: dict[str, str] = {}
    current_target: str | None = None
    current_lines: list[str] = []
    header = re.compile(r"^(.+): #deps \d+(?:,.*)?$")
    for line in result.stdout.splitlines():
        match = header.match(line)
        if match:
            if current_target is not None:
                sections[current_target] = "\n".join(current_lines)
            current_target = match.group(1).replace("\\", "/").lower()
            current_lines = []
        elif current_target is not None:
            current_lines.append(line)
    if current_target is not None:
        sections[current_target] = "\n".join(current_lines)
    live_sections = {target: sections[target] for target in live_core_objects if target in sections}
    if not live_sections:
        fail("Ninja's dependency database has no entries for current PCSX2/emucore object outputs")
    missing_entries = live_core_objects - live_sections.keys()
    selected_path = root.parent.resolve()
    opposite_family = "Adreno" if root.parent.name == "Mali" else "Mali"
    opposite_path = (VARIANTS / opposite_family).resolve()
    canonical_gs = CANONICAL_GS.resolve()
    canonical_facade = (PCSX2_CORE / "GS.h").resolve()
    canonical_host = (PCSX2_CORE / "Host.cpp").resolve()
    canonical_outer_gs = (PCSX2_CORE / "GS.cpp").resolve()
    build_root = build_directory.resolve()
    errors: list[str] = []
    if missing_entries:
        errors.append(f"{len(missing_entries)} live PCSX2/emucore objects have no recorded dependency entry")
    selected_count = 0
    forbidden: set[Path] = set()
    for section in live_sections.values():
        for raw_line in section.splitlines():
            raw = raw_line.strip()
            if not raw or raw.startswith("#"):
                continue
            # ninja -t deps emits dependency paths indented, usually absolute.
            # Resolve every path so ../ spellings cannot evade the check.
            dependency = resolved_dependency(raw, str(build_root))
            if dependency.is_relative_to(selected_path):
                selected_count += 1
            if dependency.is_relative_to(opposite_path):
                forbidden.add(dependency)
            if dependency.is_relative_to(canonical_gs) or dependency in (canonical_facade, canonical_host, canonical_outer_gs):
                forbidden.add(dependency)
    if forbidden:
        errors.append("Ninja dependencies contain forbidden canonical/opposite GS paths: " + ", ".join(str(path) for path in sorted(forbidden)[:10]))
    if selected_count == 0:
        errors.append("Ninja dependency database contains no selected-family GS dependencies")
    if errors:
        fail("; ".join(errors))
    return len(live_sections), selected_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--family", required=True, choices=("MALI", "ADRENO"))
    parser.add_argument("--compile-commands", type=Path)
    parser.add_argument("--ninja-build-directory", type=Path, help="optional Ninja build directory; reads Ninja's actual .ninja_deps database")
    parser.add_argument("--ninja-executable", default="ninja", help="Ninja executable used with --ninja-build-directory")
    parser.add_argument("--source-only", action="store_true", help="skip compile-database checks")
    args = parser.parse_args()
    check_include_lexer()
    root = family_root(args.family)
    copied = check_fork_completeness(root)
    check_variant_facade(root)
    includes = check_include_resolution(root)
    resources = check_resources(args.family)
    if args.compile_commands:
        if not args.ninja_build_directory:
            fail("compile-database mode also requires --ninja-build-directory so physical header dependencies are proven")
        core_units, gs_units, android_units = check_compile_database(root, args.compile_commands.resolve())
        dep_entries, selected_deps = check_ninja_dependencies(root, args.ninja_build_directory.resolve(), args.ninja_executable)
        detail = f" ninja_dep_entries={dep_entries} selected_ninja_dependencies={selected_deps}"
        print(f"GS isolation PASS family={args.family} fork_files={copied} gs_includes={includes} shader_files={resources} core_units={core_units} android_core_units={android_units} variant_gs_units={gs_units}{detail}")
    elif args.source_only:
        print(f"GS source isolation PASS family={args.family} fork_files={copied} gs_includes={includes} shader_files={resources}")
    else:
        fail("provide --compile-commands or --source-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
