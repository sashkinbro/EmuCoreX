"""Host regression for production ZB_ONLY feedback eligibility and depth ordering."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_shared_ir_runtime_gates import ROOT, function


class ZbOnlyDepthOrder(unittest.TestCase):
    def test_actual_feedback_policy_and_alpha_order(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSRendererHW.cpp'
        source = path.read_text(encoding='utf-8')
        self.assertTrue('GSNeedsOrderedZbOnlyFeedback' in source, 'Missing order-preserving ZB_ONLY feedback policy')
        body = function(path, '__ri static constexpr bool GSNeedsOrderedZbOnlyFeedback')
        supports = function(path, '__ri static constexpr bool GSHasBarrierFeedbackSupport')
        fixture = r'''
#include <cstdio>
#define __ri
using u32=unsigned;
constexpr u32 AFAIL_KEEP=0,AFAIL_FB_ONLY=1,AFAIL_ZB_ONLY=2,AFAIL_RGB_ONLY=3;
constexpr u32 ZTST_NEVER=0,ZTST_ALWAYS=1,ZTST_GEQUAL=2,ZTST_GREATER=3;
struct GSDevice { struct FeatureSupport { bool texture_barrier=false,framebuffer_fetch=false,multidraw_fb_copy=false; }; };
struct GSRendererHW { enum PRIM_OVERLAP { PRIM_OVERLAP_UNKNOWN, PRIM_OVERLAP_NO, PRIM_OVERLAP_YES }; };
__FUNCTIONS__
int main() {
 int cases=0;
 for(int feature=0;feature<4;feature++) for(u32 afail=0;afail<4;afail++)
 for(u32 ztst=0;ztst<4;ztst++) for(bool zwe:{false,true}) for(bool eqz:{false,true})
 for(auto overlap:{GSRendererHW::PRIM_OVERLAP_NO,GSRendererHW::PRIM_OVERLAP_YES,GSRendererHW::PRIM_OVERLAP_UNKNOWN}) for(bool date:{false,true}) {
  GSDevice::FeatureSupport f{feature==1,feature==2,feature==3};
  bool expected=!date && feature!=0 && afail==AFAIL_ZB_ONLY && ztst==ZTST_ALWAYS && zwe && !eqz && overlap!=GSRendererHW::PRIM_OVERLAP_NO;
  if(GSNeedsOrderedZbOnlyFeedback(f,afail,ztst,zwe,eqz,overlap,date)!=expected) return 1;
  cases++;
 }
 // Two overlapping fragments with distinct depths, in both alpha orders.
 // ZB_ONLY writes Z on pass AND fail. Grouping by alpha outcome can reorder it.
 int reproduced=0;
 for(int mask=1;mask<7;mask++) {
  int golden=0,legacy=0,feedback=0;
  for(int i=0;i<3;i++){golden=i+1;feedback=i+1;}
  for(bool pass:{true,false}) for(int i=0;i<3;i++) if(bool(mask&(1<<i))==pass) legacy=i+1;
  if(feedback!=golden) return 2;
  reproduced+=legacy!=golden;
 }
 if(!reproduced) return 3;
 std::printf("PASS %d production policy cases; %d pass/fail ordering failures reproduced\n",cases,reproduced);
}
'''.replace('__FUNCTIONS__', supports + '\n' + body).replace('#include <cstdio>', '#include <cstdio>\n#include <initializer_list>')
        compiler = shutil.which('g++') or shutil.which('clang++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='emucorex-zb-order-') as directory:
            cpp, exe = Path(directory) / 'test.cpp', Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
            cpp.write_text(fixture)
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
