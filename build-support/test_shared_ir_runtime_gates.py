"""Run the production IR entry gates on the host, without Android or a full build.

Properties and oracle overrides deliberately request IR, including in Debug.
Only a separate experimental research build may honor those requests.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ARM64 = ROOT / "app/src/main/cpp/PCSX2/pcsx2/arm64"


def function(path, signature):
    source = path.read_text(encoding="utf-8")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class SharedIrRuntimeGates(unittest.TestCase):
    def test_production_gates_ignore_properties_and_overrides(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if not compiler and Path("C:/msys64/ucrt64/bin/g++.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/g++.exe"
        self.assertIsNotNone(compiler, "A host C++ compiler is required")
        fixture = r'''
#include <array>
#include <atomic>
#include <cstdlib>
using u32 = unsigned;
constexpr int PROP_VALUE_MAX = 92, ANDROID_LOG_INFO = 4;
int requested_property;
int __system_property_get(const char*, char* value) {
    value[0] = requested_property ? '1' : '0'; value[1] = 0; return 1;
}
int __android_log_print(int, const char*, const char*, ...) { return 0; }
int s_oracleEeIrModeOverride = -1, s_iopIrOverride = -1;
std::atomic<int> s_vuSharedIrOverride[2] = {-1, -1};
__GATES__
int main(int argc, char** argv) {
    requested_property = std::atoi(argv[1]);
    const int override_value = std::atoi(argv[2]);
    s_oracleEeIrModeOverride = s_iopIrOverride = override_value;
    for (auto& value : s_vuSharedIrOverride) value.store(override_value);
    const bool expected = std::atoi(argv[3]) != 0;
    return EeIrEnabled() != expected || IopIrEnabled() != expected ||
        mVUSharedIrEnabled(0) != expected || mVUSharedIrEnabled(1) != expected ||
        mVUSharedIrEnabled(2) || mVUSharedIrEnabled(0xffffffffu);
}
'''.replace("__GATES__", "\n".join([
            function(ARM64 / "ee/iR5900-arm64.cpp", "static bool EeIrEnabled()"),
            function(ARM64 / "iop/iR3000A-arm64.cpp", "static bool IopIrEnabled()"),
            function(ARM64 / "vu/microVU-arm64.cpp", "bool mVUSharedIrEnabled(u32 index)"),
        ]))
        variants = {
            "release": (["__ANDROID__"], False),
            "debug": (["__ANDROID__", "EMUCOREX_ENABLE_NATIVE_SELF_TESTS"], False),
            "debug-explicit-off": (["__ANDROID__", "EMUCOREX_ENABLE_NATIVE_SELF_TESTS", "EMUCOREX_ENABLE_EXPERIMENTAL_IR=0"], False),
            "research": (["__ANDROID__", "EMUCOREX_ENABLE_NATIVE_SELF_TESTS", "EMUCOREX_ENABLE_EXPERIMENTAL_IR=1"], True),
            "research-without-self-tests": (["__ANDROID__", "EMUCOREX_ENABLE_EXPERIMENTAL_IR=1"], False),
            "non-android": (["EMUCOREX_ENABLE_NATIVE_SELF_TESTS", "EMUCOREX_ENABLE_EXPERIMENTAL_IR=1"], False),
        }
        env = dict(os.environ)
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        cases = 0
        with tempfile.TemporaryDirectory(prefix="emucorex-ir-gates-") as temp:
            source = Path(temp) / "gates.cpp"
            source.write_text(fixture, encoding="utf-8")
            for name, (defines, research) in variants.items():
                executable = Path(temp) / (name + (".exe" if os.name == "nt" else ""))
                subprocess.run([compiler, "-std=c++17", *["-D" + d for d in defines], str(source), "-o", str(executable)], check=True, env=env, capture_output=True)
                for prop in (0, 1):
                    for override in (-1, 0, 1):
                        expected = research and (bool(prop) if override == -1 else override == 1)
                        with self.subTest(build=name, property=prop, override=override):
                            result = subprocess.run([str(executable), str(prop), str(override), str(int(expected))], env=env)
                            self.assertEqual(result.returncode, 0, "Production EE/IOP/VU gates selected an unexpected path")
                            cases += 1
        print(f"Verified {cases} build/property/override combinations across EE, IOP, VU0, VU1 and invalid VU indices")


if __name__ == "__main__":
    unittest.main()
