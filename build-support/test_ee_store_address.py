"""Host instruction regression for the production legacy EE store address emitter.

Requires a C++20 host compiler; does not execute ARM code or access a device.
The allocator/memory seams are controlled here; native EE oracle coverage is
still required for full allocator integration and memory-handler behavior.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <oaknut/oaknut.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace oak = oaknut;
using u32 = uint32_t;
using s32 = int32_t;
constexpr int EE_HOST_RCX=1, EE_HOST_RDX=2, X86TYPE_GPR=1, MODE_READ=1;
int _Rs_, _Imm_, cached_base;
bool constant_base;
std::vector<int> freed;
oak::VectorCodeGenerator* oakAsm;
#define pxAssert assert
#define GPR_IS_CONST1(reg) (constant_base)
oak::WReg oakWRegister(int reg) { return oak::WReg{reg}; }
oak::XReg oakXRegister(int reg) { return oak::XReg{reg}; }
void recBeginOaknutEmit() {}
void recEndOaknutEmit() {}
int _checkX86reg(int, int, int) { return cached_base; }
void _freeX86reg(int reg) {
    freed.push_back(reg);
    if (cached_base == reg) cached_base=-1;
}
// These are the only controlled production seams. Constant, uncached and
// cached source routes emit distinct real ARM64 instructions.
void recMoveGPRtoOakW(oak::WReg dst, int) {
    if (constant_base) oakAsm->MOV(dst, 0x12345678u);
    else if (cached_base >= 0) oakAsm->MOV(dst, oakWRegister(cached_base));
    else oakAsm->LDR(dst, oak::util::X27);
}
@ADD@
@STORE@
@PREPARE@

int main() {
    int checked=0, failures=0;
    for (int source : {-1,1,2,4,19})
    for (int rs : {0,8})
    for (bool constant : {false,true})
    for (int imm : {-32768,-4096,-4095,-1,0,1,4095,4096,32767})
    for (bool align : {false,true})
    for (int value : {-1,1,2,19}) {
        _Rs_=rs; _Imm_=imm; cached_base=source; constant_base=constant;
        std::vector<u32> actual, expected;
        oak::VectorCodeGenerator gen(actual), reference(expected);
        oakAsm=&gen; freed.clear();
        int result=recPrepareStoreAddressToECX_emit_oaknut(align,value);
        const std::vector<int> expected_freed = value == 1 ?
            std::vector<int>{2,1} : std::vector<int>{1};
        bool same=freed==expected_freed && result==(value==1 ? 2 : value);
        // Mirror only the call boundary's allocator effects, then independently
        // construct the required optimal sequence with real Oaknut operations.
        int surviving_base=source;
        for (int reg : expected_freed) if (surviving_base==reg) surviving_base=-1;
        oakAsm=&reference;
        if (value==1) reference.MOV(oak::util::X2,oak::util::X1);
        if (rs==0) reference.MOV(oak::util::W1,static_cast<u32>(imm));
        else if (!constant && surviving_base>=0 && imm>=-4095 && imm<=4095) {
            if (imm>0) reference.ADD(oak::util::W1,oakWRegister(surviving_base),static_cast<u32>(imm));
            else if (imm<0) reference.SUB(oak::util::W1,oakWRegister(surviving_base),static_cast<u32>(-imm));
            else if (surviving_base!=1) reference.MOV(oak::util::W1,oakWRegister(surviving_base));
        } else {
            cached_base=surviving_base;
            recMoveGPRtoOakW(oak::util::W1,rs);
            oakAddSignedImm(oak::util::W1,oak::util::W1,imm,oak::util::W4);
        }
        if (align) reference.AND(oak::util::W1,oak::util::W1,oak::BitImm32(~0x0fu));
        same &= actual==expected;
        ++checked;
        if (!same && failures++<5)
            std::printf("FAIL source=%d rs=%d const=%d imm=%d align=%d value=%d actual=%zu expected=%zu\n",
                source,rs,constant,imm,align,value,actual.size(),expected.size());
    }
    std::printf("EE store address production emitter: %d cases, %d failures\n",checked,failures);
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=shutil.which('g++') or shutil.which('clang++'))
    args = parser.parse_args()
    if not args.compiler:
        parser.error('pass --compiler with a C++20 host compiler')
    repo = Path(__file__).resolve().parents[1]
    core = repo / 'app/src/main/cpp/pcsx2/pcsx2/arm64'
    stores = (core / 'ee/iR5900LoadStore-arm64.cpp').read_text(encoding='utf-8')
    helpers = (core / 'OaknutHelpers-arm64.cpp').read_text(encoding='utf-8')
    code = HARNESS.replace('@ADD@', function(helpers, 'void oakAddSignedImm(oak::WReg'))
    code = code.replace('@STORE@', function(stores, 'static void recStoreAddressToECX_emit_oaknut('))
    code = code.replace('@PREPARE@', function(stores, 'static int recPrepareStoreAddressToECX_emit_oaknut('))
    with tempfile.TemporaryDirectory(prefix='ee-store-address-') as directory:
        temporary = Path(directory)
        cpp = temporary / 'test.cpp'
        binary = temporary / ('test.exe' if os.name == 'nt' else 'test')
        cpp.write_text(code, encoding='utf-8')
        environment = os.environ.copy()
        environment['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + environment['PATH']
        subprocess.run([args.compiler, '-std=c++20', '-O2', '-I',
                        str(repo / 'app/src/main/cpp/ARM_ANDROID/third_party/oaknut/include'),
                        str(cpp), '-o', str(binary)], check=True, env=environment)
        return subprocess.run([str(binary)], env=environment).returncode


if __name__ == '__main__':
    raise SystemExit(main())
