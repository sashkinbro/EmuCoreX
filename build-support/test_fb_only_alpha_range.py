"""Exercise the production alpha-range decision with framebuffer-write semantics."""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSRendererHW.cpp').read_text()
block = re.search(r'\tconst int afail_type = m_cached_ctx.TEST.GetAFAIL\(m_cached_ctx.FRAME.PSM\);\n.*?\n\tconst bool needs_ad', source, re.S).group(0)
block = block[:block.rfind('\n\tconst bool needs_ad')]
program = r'''
#include <cstdio>
enum { AFAIL_KEEP, AFAIL_FB_ONLY, AFAIL_ZB_ONLY, AFAIL_RGB_ONLY };
struct Context {
  struct { bool ATE; int ATST=0, AREF=128, fail; int GetAFAIL(int) const { return fail; } } TEST;
  struct { int PSM=0; } FRAME;
};
struct Renderer {
  Context m_cached_ctx;
  bool blend, source_alpha; int corrections=0;
  bool NeedsBlending() const { return blend; }
  bool IsUsingAsInBlend() const { return source_alpha; }
  void CorrectATEAlphaMinMax(int, int) { ++corrections; }
  void Check() {
''' + block + r'''
  }
};
int main() {
  int cases=0, failures=0;
  for(int ate=0; ate<2; ++ate) for(int fail=0; fail<4; ++fail)
  for(int blend=0; blend<2; ++blend) for(int as=0; as<2; ++as) {
    Renderer r{}; r.m_cached_ctx.TEST.ATE=ate; r.m_cached_ctx.TEST.fail=fail;
    r.blend=blend; r.source_alpha=as; r.Check();
    // FB_ONLY writes the alpha of failing fragments too (including 255).
    // RGB_ONLY retains its existing blend-source-alpha restriction.
    const bool expected=ate && fail!=AFAIL_FB_ONLY &&
      (fail!=AFAIL_RGB_ONLY || !blend || !as);
    ++cases;
    if(bool(r.corrections)!=expected) { ++failures;
      std::printf("FAIL ATE=%d AFAIL=%d blend=%d As=%d\n",ate,fail,blend,as); }
  }
  std::printf("alpha-range decisions: %d cases, %d failures\n",cases,failures);
  return failures?1:0;
}
'''
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default=shutil.which('g++') or shutil.which('clang++'))
args = parser.parse_args()
if not args.compiler:
    parser.error('pass --compiler with a C++17 host compiler')
compiler = args.compiler
environment = os.environ.copy()
environment['PATH'] = str(Path(compiler).resolve().parent) + os.pathsep + environment['PATH']
with tempfile.TemporaryDirectory(prefix='emucorex-alpha-range-') as tmp:
    cpp, exe = Path(tmp) / 'test.cpp', Path(tmp) / ('test.exe' if os.name == 'nt' else 'test')
    cpp.write_text(program)
    subprocess.run([compiler, '-std=c++17', '-O2', str(cpp), '-o', str(exe)], check=True, env=environment)
    subprocess.run([str(exe)], check=True, env=environment)
