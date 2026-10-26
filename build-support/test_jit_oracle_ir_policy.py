"""Exercise the production oracle dispatcher with inexpensive suite stubs."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from test_shared_ir_runtime_gates import ROOT, function


class OracleIrPolicy(unittest.TestCase):
    def test_dispatch_refuses_unavailable_ir_and_preserves_direct_suites(self):
        directory = ROOT / "app/src/main/cpp/ARM_ANDROID/src/debug"
        dispatch = function(directory / "jit_oracle.cpp", 'extern "C" __attribute__((visibility("default"))) int EmuCoreXRunJitOracle(')
        policy = directory / "jit_oracle_ir_policy.h"
        required = re.findall(r'"([a-z0-9-]+)"', policy.read_text())
        allowed = ["classifier", "ee-direct", "ee-direct-stores", "iop-direct", "vu-direct",
                   "ee-branch-immediates", "ir-immediates", "vu-pq", "vu-itof-codegen"]
        names = set(re.findall(r"\b([A-Z]\w*)\(", dispatch)) - {
            "EmuCoreXRunJitOracle", "OracleSuiteRequiresExperimentalIr", "Run"}
        stubs = "\n".join(
            f'template<class... T> void {name}(T... args) {{ selected = "{name}"; arguments = sizeof...(args); (Record(args), ...); }}'
            for name in names)
        fixture = r'''
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "__POLICY__"
int failures, checks, arguments;
const char* selected = nullptr;
const char* direct_suite = nullptr;
void Record(const char* value) { direct_suite = value; }
template<class T> void Record(T) {}
void Check(bool, const char*) {}
namespace Threading::UserspaceSemaphoreOracle { void Run(void (*)(bool, const char*)) { selected = "semaphore"; } }
__STUBS__
__DISPATCH__
int main(int argc, char** argv) {
    const int result = EmuCoreXRunJitOracle(argv[1]);
    if (std::atoi(argv[2])) return result != 2 || selected != nullptr;
    if (result != 0 || !selected) return 3;
    if (!std::strcmp(argv[1], "ee-direct") || !std::strcmp(argv[1], "ee-direct-stores"))
        return std::strcmp(selected, "EETests") || arguments != 21 || !direct_suite || std::strcmp(direct_suite, argv[1]);
    if (!std::strcmp(argv[1], "iop-direct")) return std::strcmp(selected, "IOPTests") || arguments != 5;
    if (!std::strcmp(argv[1], "vu-direct")) return std::strcmp(selected, "VUTests") || arguments != 10;
    return 0;
}
'''.replace("__POLICY__", policy.as_posix()).replace("__STUBS__", stubs).replace("__DISPATCH__", dispatch)
        compiler = shutil.which("g++") or shutil.which("clang++") or "C:/msys64/ucrt64/bin/g++.exe"
        env = dict(os.environ)
        env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
        with tempfile.TemporaryDirectory(prefix="emucorex-oracle-policy-") as temp:
            source = Path(temp) / "policy.cpp"
            source.write_text(fixture, encoding="utf-8")
            for research in (False, True):
                executable = Path(temp) / ("policy.exe" if os.name == "nt" else "policy")
                subprocess.run([compiler, "-std=c++17", "-D__ANDROID__", "-DEMUCOREX_ENABLE_NATIVE_SELF_TESTS", f"-DEMUCOREX_ENABLE_EXPERIMENTAL_IR={int(research)}", str(source), "-o", str(executable)], check=True, env=env, capture_output=True)
                for suite in required + allowed:
                    with self.subTest(research=research, suite=suite):
                        unavailable = not research and suite in required
                        result = subprocess.run([str(executable), suite, str(int(unavailable))], env=env, capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                        if unavailable:
                            self.assertIn("UNAVAILABLE", result.stderr)
                            self.assertNotIn("RESULT", result.stdout)
        print(f"Verified {2 * (len(required) + len(allowed))} production dispatch cases")


if __name__ == "__main__":
    unittest.main()
