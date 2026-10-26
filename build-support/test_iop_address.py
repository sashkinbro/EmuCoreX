"""Emit the production IOP address helper with real Oaknut on the host.

Checks exact folding/fallback instructions and independently evaluates their
32-bit arithmetic. Native iop-direct covers actual allocator/memory behavior.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_ee_store_address import function


HARNESS = r'''
#include <oaknut/oaknut.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>
namespace oak = oaknut;
using u32=uint32_t;
using s32=int32_t;
using s64=int64_t;
struct cpuRegistersPack { struct { struct { u32 r[32]; } GPR; } psxRegs; };
cpuRegistersPack layout;
// Production Clang accepts runtime array indices in offsetof; GCC's builtin
// does not. Use an actual instance for the same controlled host-layout offset.
#undef offsetof
#define offsetof(type,member) (reinterpret_cast<const char*>(&layout.member)-reinterpret_cast<const char*>(&layout))
struct Mem { oak::XReg base; s64 offset; };
int _Rs_, _Imm_;
oak::VectorCodeGenerator* oakAsm;
#define pxAssert assert
#define OAK_WARG1 oak::util::W0
#define OAK_WSCRATCH oak::util::W16
oak::WReg oakWRegister(int index) { return oak::WReg{index}; }
void oakLoad32(oak::WReg dst, Mem mem) {
    oakAsm->LDR(dst, mem.base, oak::POffset<14,2>(mem.offset));
}
@ADD@
@ADDRESS@

// Only the instruction classes this production helper can emit are accepted.
bool evaluate(const std::vector<u32>& code, std::array<u32,32>& regs, const std::array<u32,32>& guest) {
    for (u32 word : code) {
        u32 rd=word&31, rn=(word>>5)&31, rm=(word>>16)&31;
        if ((word&0xffe0ffe0u)==0x2a0003e0u) regs[rd]=rm==31 ? 0 : regs[rm];
        else if ((word&0x7f000000u)==0x11000000u || (word&0x7f000000u)==0x51000000u) {
            u32 imm=((word>>10)&4095)<<(((word>>22)&1)*12);
            regs[rd]=(word&0x40000000u) ? regs[rn]-imm : regs[rn]+imm;
        } else if ((word&0xffe0fc00u)==0x0b000000u) regs[rd]=regs[rn]+regs[rm];
        else if ((word&0xffc00000u)==0xb9400000u && rn==27) regs[rd]=guest[(word>>10)&4095];
        else {
            u32 kind=word&0x7f800000u, shift=((word>>21)&3)*16;
            u32 value=((word>>5)&65535)<<shift;
            if (kind==0x52800000u) regs[rd]=value;
            else if (kind==0x12800000u) regs[rd]=~value;
            else if (kind==0x72800000u) regs[rd]=(regs[rd]&~(65535u<<shift))|value;
            else return false;
        }
    }
    return true;
}

int main() {
    unsigned checked=0, failures=0;
    for (int source : {-1,0,1,4,16,19})
    for (int guest_reg : {0,8,31})
    for (int imm : {-32768,-4096,-4095,-1,0,1,4095,4096,32767}) {
        _Rs_=guest_reg; _Imm_=imm;
        std::vector<u32> actual, expected;
        oak::VectorCodeGenerator gen(actual), reference(expected);
        oakAsm=&gen;
        rpsxCalcAddressOperand_emit_oaknut(source);
        if (source>=0 && imm>=-4095 && imm<=4095) {
            if (imm>0) reference.ADD(OAK_WARG1,oakWRegister(source),static_cast<u32>(imm));
            else if (imm<0) reference.SUB(OAK_WARG1,oakWRegister(source),static_cast<u32>(-imm));
            else if (source!=0) reference.MOV(OAK_WARG1,oakWRegister(source));
        } else {
            if (source>=0) reference.MOV(OAK_WARG1,oakWRegister(source));
            else reference.LDR(OAK_WARG1,oak::util::X27,oak::POffset<14,2>(guest_reg*4));
            if (imm) {
                reference.MOV(OAK_WSCRATCH,static_cast<u32>(imm));
                reference.ADD(OAK_WARG1,OAK_WARG1,OAK_WSCRATCH);
            }
        }
        for (u32 initial : {0u,0x7fffffffu,0x80000000u,0xfffffff0u,0xffffffffu,0x12345678u}) {
            std::array<u32,32> regs{}, guest{};
            const u32 value=guest_reg==0 ? 0 : initial;
            guest[guest_reg]=value;
            if (source>=0) regs[source]=value;
            const bool evaluated=evaluate(actual,regs,guest);
            const bool source_preserved=source<=0 || imm < -4095 || imm > 4095 || regs[source]==value;
            const bool same=actual==expected && evaluated && source_preserved && regs[0]==value+static_cast<u32>(imm);
            ++checked;
            if (!same && failures++<5)
                std::printf("FAIL source=%d guest=%d imm=%d initial=%08x words=%zu expected=%zu\n",
                    source,guest_reg,imm,value,actual.size(),expected.size());
        }
    }
    std::printf("IOP production address emitter: %u cases, %u failures\n",checked,failures);
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
    source = (core / 'iop/iR3000Atables-arm64.cpp').read_text(encoding='utf-8')
    helpers = (core / 'OaknutHelpers-arm64.cpp').read_text(encoding='utf-8')
    code = HARNESS.replace('@ADD@', function(helpers, 'void oakAddSignedImm(oak::WReg'))
    code = code.replace('@ADDRESS@', function(source, 'static void rpsxCalcAddressOperand_emit_oaknut('))
    with tempfile.TemporaryDirectory(prefix='iop-address-') as directory:
        cpp = Path(directory) / 'test.cpp'
        binary = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
        cpp.write_text(code, encoding='utf-8')
        environment = os.environ.copy()
        environment['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + environment['PATH']
        subprocess.run([args.compiler, '-std=c++20', '-O2', '-I',
                        str(repo / 'app/src/main/cpp/ARM_ANDROID/third_party/oaknut/include'),
                        str(cpp), '-o', str(binary)], check=True, env=environment)
        return subprocess.run([str(binary)], env=environment).returncode


if __name__ == '__main__':
    raise SystemExit(main())
