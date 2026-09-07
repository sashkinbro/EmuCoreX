package com.sbro.emucorex.ui.settings

import androidx.annotation.StringRes
import com.sbro.emucorex.R

/**
 * Shared model for the per-core opcode-family interpreter fallback screens.
 *
 * IMPORTANT: family bit indices and opcode ids must stay in sync with the
 * native side (see pcsx2/OpcodeFamilies.h). Opcode id layout:
 *   (primary << 11) | sub, where sub is:
 *     SPECIAL  -> funct (6 bits)
 *     REGIMM   -> rt    (5 bits)
 *     COP0/COP1-> rs    (5 bits)
 *     COP1 S/W -> 0x40 | funct
 *     MMI/MMI0..3 -> ((code >> 6) & 0x1F) << 6 | funct
 *     standard -> 0
 */
data class OpcodeEntry(val id: Int, val mnemonic: String)

data class OpcodeFamily(
    val id: String,
    val bit: Int,
    @StringRes val descriptionRes: Int,
    val opcodes: List<OpcodeEntry> = emptyList(),
    val mnemonics: List<String> = emptyList(),
)

object OpcodeFamiliesModel {

    private fun key(primary: Int, sub: Int = 0): Int = (primary shl 11) or (sub and 0x7FF)

    private fun mmiKey(subIndex: Int, funct: Int): Int = key(0x1C, (subIndex shl 6) or funct)

    // ------------------------------------------------------------------ EE
    val EE: List<OpcodeFamily> = listOf(
        OpcodeFamily(
            "SPECIAL", 0, R.string.opcode_fam_ee_special,
            listOf(
                OpcodeEntry(key(0, 0), "SLL"), OpcodeEntry(key(0, 2), "SRL"),
                OpcodeEntry(key(0, 3), "SRA"), OpcodeEntry(key(0, 4), "SLLV"),
                OpcodeEntry(key(0, 6), "SRLV"), OpcodeEntry(key(0, 7), "SRAV"),
                OpcodeEntry(key(0, 8), "JR"), OpcodeEntry(key(0, 9), "JALR"),
                OpcodeEntry(key(0, 10), "MOVZ"), OpcodeEntry(key(0, 11), "MOVN"),
                OpcodeEntry(key(0, 12), "SYSCALL"), OpcodeEntry(key(0, 13), "BREAK"),
                OpcodeEntry(key(0, 15), "SYNC"), OpcodeEntry(key(0, 16), "MFHI"),
                OpcodeEntry(key(0, 17), "MTHI"), OpcodeEntry(key(0, 18), "MFLO"),
                OpcodeEntry(key(0, 19), "MTLO"), OpcodeEntry(key(0, 20), "DSLLV"),
                OpcodeEntry(key(0, 22), "DSRLV"), OpcodeEntry(key(0, 23), "DSRAV"),
                OpcodeEntry(key(0, 24), "MULT"), OpcodeEntry(key(0, 25), "MULTU"),
                OpcodeEntry(key(0, 26), "DIV"), OpcodeEntry(key(0, 27), "DIVU"),
                OpcodeEntry(key(0, 32), "ADD"), OpcodeEntry(key(0, 33), "ADDU"),
                OpcodeEntry(key(0, 34), "SUB"), OpcodeEntry(key(0, 35), "SUBU"),
                OpcodeEntry(key(0, 36), "AND"), OpcodeEntry(key(0, 37), "OR"),
                OpcodeEntry(key(0, 38), "XOR"), OpcodeEntry(key(0, 39), "NOR"),
                OpcodeEntry(key(0, 40), "MFSA"), OpcodeEntry(key(0, 41), "MTSA"),
                OpcodeEntry(key(0, 42), "SLT"), OpcodeEntry(key(0, 43), "SLTU"),
                OpcodeEntry(key(0, 44), "DADD"), OpcodeEntry(key(0, 45), "DADDU"),
                OpcodeEntry(key(0, 46), "DSUB"), OpcodeEntry(key(0, 47), "DSUBU"),
                OpcodeEntry(key(0, 48), "TGE"), OpcodeEntry(key(0, 49), "TGEU"),
                OpcodeEntry(key(0, 50), "TLT"), OpcodeEntry(key(0, 51), "TLTU"),
                OpcodeEntry(key(0, 52), "TEQ"), OpcodeEntry(key(0, 54), "TNE"),
                OpcodeEntry(key(0, 56), "DSLL"), OpcodeEntry(key(0, 58), "DSRL"),
                OpcodeEntry(key(0, 59), "DSRA"), OpcodeEntry(key(0, 60), "DSLL32"),
                OpcodeEntry(key(0, 62), "DSRL32"), OpcodeEntry(key(0, 63), "DSRA32"),
            ),
        ),
        OpcodeFamily(
            "REGIMM", 1, R.string.opcode_fam_regimm,
            listOf(
                OpcodeEntry(key(1, 0), "BLTZ"), OpcodeEntry(key(1, 1), "BGEZ"),
                OpcodeEntry(key(1, 2), "BLTZL"), OpcodeEntry(key(1, 3), "BGEZL"),
                OpcodeEntry(key(1, 8), "TGEI"), OpcodeEntry(key(1, 9), "TGEIU"),
                OpcodeEntry(key(1, 10), "TLTI"), OpcodeEntry(key(1, 11), "TLTIU"),
                OpcodeEntry(key(1, 12), "TEQI"), OpcodeEntry(key(1, 14), "TNEI"),
                OpcodeEntry(key(1, 16), "BLTZAL"), OpcodeEntry(key(1, 17), "BGEZAL"),
                OpcodeEntry(key(1, 18), "BLTZALL"), OpcodeEntry(key(1, 19), "BGEZALL"),
                OpcodeEntry(key(1, 24), "MTSAB"), OpcodeEntry(key(1, 25), "MTSAH"),
            ),
        ),
        OpcodeFamily(
            "JUMP", 2, R.string.opcode_fam_jump,
            listOf(OpcodeEntry(key(2), "J"), OpcodeEntry(key(3), "JAL")),
        ),
        OpcodeFamily(
            "BRANCH", 3, R.string.opcode_fam_branch,
            listOf(
                OpcodeEntry(key(4), "BEQ"), OpcodeEntry(key(5), "BNE"),
                OpcodeEntry(key(6), "BLEZ"), OpcodeEntry(key(7), "BGTZ"),
                OpcodeEntry(key(20), "BEQL"), OpcodeEntry(key(21), "BNEL"),
                OpcodeEntry(key(22), "BLEZL"), OpcodeEntry(key(23), "BGTZL"),
            ),
        ),
        OpcodeFamily(
            "ALU_IMM", 4, R.string.opcode_fam_alu_imm,
            listOf(
                OpcodeEntry(key(8), "ADDI"), OpcodeEntry(key(9), "ADDIU"),
                OpcodeEntry(key(10), "SLTI"), OpcodeEntry(key(11), "SLTIU"),
                OpcodeEntry(key(12), "ANDI"), OpcodeEntry(key(13), "ORI"),
                OpcodeEntry(key(14), "XORI"), OpcodeEntry(key(15), "LUI"),
                OpcodeEntry(key(24), "DADDI"), OpcodeEntry(key(25), "DADDIU"),
            ),
        ),
        OpcodeFamily(
            "LOADSTORE", 5, R.string.opcode_fam_loadstore,
            listOf(
                OpcodeEntry(key(26), "LDL"), OpcodeEntry(key(27), "LDR"),
                OpcodeEntry(key(30), "LQ"), OpcodeEntry(key(31), "SQ"),
                OpcodeEntry(key(32), "LB"), OpcodeEntry(key(33), "LH"),
                OpcodeEntry(key(34), "LWL"), OpcodeEntry(key(35), "LW"),
                OpcodeEntry(key(36), "LBU"), OpcodeEntry(key(37), "LHU"),
                OpcodeEntry(key(38), "LWR"), OpcodeEntry(key(39), "LWU"),
                OpcodeEntry(key(40), "SB"), OpcodeEntry(key(41), "SH"),
                OpcodeEntry(key(42), "SWL"), OpcodeEntry(key(43), "SW"),
                OpcodeEntry(key(44), "SDL"), OpcodeEntry(key(45), "SDR"),
                OpcodeEntry(key(46), "SWR"), OpcodeEntry(key(49), "LWC1"),
                OpcodeEntry(key(55), "LD"), OpcodeEntry(key(57), "SWC1"),
                OpcodeEntry(key(63), "SD"),
            ),
        ),
        OpcodeFamily(
            "COP0", 6, R.string.opcode_fam_cop0,
            listOf(
                OpcodeEntry(key(16, 0), "MFC0"), OpcodeEntry(key(16, 4), "MTC0"),
            ),
        ),
        OpcodeFamily(
            "COP1", 7, R.string.opcode_fam_cop1,
            listOf(
                OpcodeEntry(key(17, 0), "MFC1"), OpcodeEntry(key(17, 2), "CFC1"),
                OpcodeEntry(key(17, 4), "MTC1"), OpcodeEntry(key(17, 6), "CTC1"),
                OpcodeEntry(key(17, 8), "BC1"),
                OpcodeEntry(key(17, 0x40), "ADD.S"), OpcodeEntry(key(17, 0x41), "SUB.S"),
                OpcodeEntry(key(17, 0x42), "MUL.S"), OpcodeEntry(key(17, 0x43), "DIV.S"),
                OpcodeEntry(key(17, 0x44), "SQRT.S"), OpcodeEntry(key(17, 0x45), "ABS.S"),
                OpcodeEntry(key(17, 0x46), "MOV.S"), OpcodeEntry(key(17, 0x47), "NEG.S"),
                OpcodeEntry(key(17, 0x56), "RSQRT.S"), OpcodeEntry(key(17, 0x58), "ADDa.S"),
                OpcodeEntry(key(17, 0x59), "SUBa.S"), OpcodeEntry(key(17, 0x5A), "MULa.S"),
                OpcodeEntry(key(17, 0x5C), "MADD.S"), OpcodeEntry(key(17, 0x5D), "MSUB.S"),
                OpcodeEntry(key(17, 0x5E), "MADDa.S"), OpcodeEntry(key(17, 0x5F), "MSUBa.S"),
                OpcodeEntry(key(17, 0x60), "CVT.S"), OpcodeEntry(key(17, 0x64), "CVT.W"),
                OpcodeEntry(key(17, 0x68), "MAX.S"), OpcodeEntry(key(17, 0x69), "MIN.S"),
                OpcodeEntry(key(17, 0x70), "C.F"), OpcodeEntry(key(17, 0x72), "C.EQ"),
                OpcodeEntry(key(17, 0x74), "C.LT"), OpcodeEntry(key(17, 0x76), "C.LE"),
            ),
        ),
        OpcodeFamily(
            "COP2", 8, R.string.opcode_fam_cop2,
            listOf(
                OpcodeEntry(key(54), "LQC2"), OpcodeEntry(key(62), "SQC2"),
            ),
        ),
        OpcodeFamily(
            "MMI", 9, R.string.opcode_fam_mmi,
            listOf(
                OpcodeEntry(key(0x1C, 0), "MADD"), OpcodeEntry(key(0x1C, 1), "MADDU"),
                OpcodeEntry(key(0x1C, 4), "PLZCW"), OpcodeEntry(key(0x1C, 16), "MFHI1"),
                OpcodeEntry(key(0x1C, 17), "MTHI1"), OpcodeEntry(key(0x1C, 18), "MFLO1"),
                OpcodeEntry(key(0x1C, 19), "MTLO1"), OpcodeEntry(key(0x1C, 24), "MULT1"),
                OpcodeEntry(key(0x1C, 25), "MULTU1"), OpcodeEntry(key(0x1C, 26), "DIV1"),
                OpcodeEntry(key(0x1C, 27), "DIVU1"), OpcodeEntry(key(0x1C, 32), "MADD1"),
                OpcodeEntry(key(0x1C, 33), "MADDU1"), OpcodeEntry(key(0x1C, 48), "PMFHL"),
                OpcodeEntry(key(0x1C, 49), "PMTHL"), OpcodeEntry(key(0x1C, 52), "PSLLH"),
                OpcodeEntry(key(0x1C, 54), "PSRLH"), OpcodeEntry(key(0x1C, 55), "PSRAH"),
                OpcodeEntry(key(0x1C, 60), "PSLLW"), OpcodeEntry(key(0x1C, 62), "PSRLW"),
                OpcodeEntry(key(0x1C, 63), "PSRAW"),
            ),
        ),
        OpcodeFamily(
            "MMI0", 10, R.string.opcode_fam_mmi0,
            listOf(
                OpcodeEntry(mmiKey(0, 0), "PADDW"), OpcodeEntry(mmiKey(0, 1), "PSUBW"),
                OpcodeEntry(mmiKey(0, 2), "PCGTW"), OpcodeEntry(mmiKey(0, 3), "PMAXW"),
                OpcodeEntry(mmiKey(0, 4), "PADDH"), OpcodeEntry(mmiKey(0, 5), "PSUBH"),
                OpcodeEntry(mmiKey(0, 6), "PCGTH"), OpcodeEntry(mmiKey(0, 7), "PMAXH"),
                OpcodeEntry(mmiKey(0, 8), "PADDB"), OpcodeEntry(mmiKey(0, 9), "PSUBB"),
                OpcodeEntry(mmiKey(0, 10), "PCGTB"), OpcodeEntry(mmiKey(0, 16), "PADDSW"),
                OpcodeEntry(mmiKey(0, 17), "PSUBSW"), OpcodeEntry(mmiKey(0, 18), "PEXTLW"),
                OpcodeEntry(mmiKey(0, 19), "PPACW"), OpcodeEntry(mmiKey(0, 20), "PADDSH"),
                OpcodeEntry(mmiKey(0, 21), "PSUBSH"), OpcodeEntry(mmiKey(0, 22), "PEXTLH"),
                OpcodeEntry(mmiKey(0, 23), "PPACH"), OpcodeEntry(mmiKey(0, 24), "PADDSB"),
                OpcodeEntry(mmiKey(0, 25), "PSUBSB"), OpcodeEntry(mmiKey(0, 26), "PEXTLB"),
                OpcodeEntry(mmiKey(0, 27), "PPACB"), OpcodeEntry(mmiKey(0, 30), "PEXT5"),
                OpcodeEntry(mmiKey(0, 31), "PPAC5"),
            ),
        ),
        OpcodeFamily(
            "MMI1", 11, R.string.opcode_fam_mmi1,
            listOf(
                OpcodeEntry(mmiKey(1, 1), "PABSW"), OpcodeEntry(mmiKey(1, 2), "PCEQW"),
                OpcodeEntry(mmiKey(1, 3), "PMINW"), OpcodeEntry(mmiKey(1, 4), "PADSBH"),
                OpcodeEntry(mmiKey(1, 5), "PABSH"), OpcodeEntry(mmiKey(1, 6), "PCEQH"),
                OpcodeEntry(mmiKey(1, 7), "PMINH"), OpcodeEntry(mmiKey(1, 10), "PCEQB"),
                OpcodeEntry(mmiKey(1, 16), "PADDUW"), OpcodeEntry(mmiKey(1, 17), "PSUBUW"),
                OpcodeEntry(mmiKey(1, 18), "PEXTUW"), OpcodeEntry(mmiKey(1, 20), "PADDUH"),
                OpcodeEntry(mmiKey(1, 21), "PSUBUH"), OpcodeEntry(mmiKey(1, 22), "PEXTUH"),
                OpcodeEntry(mmiKey(1, 24), "PADDUB"), OpcodeEntry(mmiKey(1, 25), "PSUBUB"),
                OpcodeEntry(mmiKey(1, 26), "PEXTUB"), OpcodeEntry(mmiKey(1, 27), "QFSRV"),
            ),
        ),
        OpcodeFamily(
            "MMI2", 12, R.string.opcode_fam_mmi2,
            listOf(
                OpcodeEntry(mmiKey(2, 0), "PMADDW"), OpcodeEntry(mmiKey(2, 2), "PSLLVW"),
                OpcodeEntry(mmiKey(2, 3), "PSRLVW"), OpcodeEntry(mmiKey(2, 4), "PMSUBW"),
                OpcodeEntry(mmiKey(2, 8), "PMFHI"), OpcodeEntry(mmiKey(2, 9), "PMFLO"),
                OpcodeEntry(mmiKey(2, 10), "PINTH"), OpcodeEntry(mmiKey(2, 12), "PMULTW"),
                OpcodeEntry(mmiKey(2, 13), "PDIVW"), OpcodeEntry(mmiKey(2, 14), "PCPYLD"),
                OpcodeEntry(mmiKey(2, 16), "PMADDH"), OpcodeEntry(mmiKey(2, 17), "PHMADH"),
                OpcodeEntry(mmiKey(2, 18), "PAND"), OpcodeEntry(mmiKey(2, 19), "PXOR"),
                OpcodeEntry(mmiKey(2, 20), "PMSUBH"), OpcodeEntry(mmiKey(2, 21), "PHMSBH"),
                OpcodeEntry(mmiKey(2, 26), "PEXEH"), OpcodeEntry(mmiKey(2, 27), "PREVH"),
                OpcodeEntry(mmiKey(2, 28), "PMULTH"), OpcodeEntry(mmiKey(2, 29), "PDIVBW"),
                OpcodeEntry(mmiKey(2, 30), "PEXEW"), OpcodeEntry(mmiKey(2, 31), "PROT3W"),
            ),
        ),
        OpcodeFamily(
            "MMI3", 13, R.string.opcode_fam_mmi3,
            listOf(
                OpcodeEntry(mmiKey(3, 0), "PMADDUW"), OpcodeEntry(mmiKey(3, 3), "PSRAVW"),
                OpcodeEntry(mmiKey(3, 8), "PMTHI"), OpcodeEntry(mmiKey(3, 9), "PMTLO"),
                OpcodeEntry(mmiKey(3, 10), "PINTEH"), OpcodeEntry(mmiKey(3, 12), "PMULTUW"),
                OpcodeEntry(mmiKey(3, 13), "PDIVUW"), OpcodeEntry(mmiKey(3, 14), "PCPYUD"),
                OpcodeEntry(mmiKey(3, 18), "POR"), OpcodeEntry(mmiKey(3, 19), "PNOR"),
                OpcodeEntry(mmiKey(3, 26), "PEXCH"), OpcodeEntry(mmiKey(3, 27), "PCPYH"),
                OpcodeEntry(mmiKey(3, 30), "PEXCW"),
            ),
        ),
        OpcodeFamily(
            "MISC", 14, R.string.opcode_fam_misc,
            listOf(
                OpcodeEntry(key(47), "CACHE"), OpcodeEntry(key(51), "PREF"),
            ),
        ),
    )

    // ----------------------------------------------------------------- IOP
    val IOP: List<OpcodeFamily> = listOf(
        OpcodeFamily(
            "SPECIAL", 0, R.string.opcode_fam_iop_special,
            listOf(
                OpcodeEntry(key(0, 0), "SLL"), OpcodeEntry(key(0, 2), "SRL"),
                OpcodeEntry(key(0, 3), "SRA"), OpcodeEntry(key(0, 4), "SLLV"),
                OpcodeEntry(key(0, 6), "SRLV"), OpcodeEntry(key(0, 7), "SRAV"),
                OpcodeEntry(key(0, 8), "JR"), OpcodeEntry(key(0, 9), "JALR"),
                OpcodeEntry(key(0, 12), "SYSCALL"), OpcodeEntry(key(0, 13), "BREAK"),
                OpcodeEntry(key(0, 16), "MFHI"), OpcodeEntry(key(0, 17), "MTHI"),
                OpcodeEntry(key(0, 18), "MFLO"), OpcodeEntry(key(0, 19), "MTLO"),
                OpcodeEntry(key(0, 24), "MULT"), OpcodeEntry(key(0, 25), "MULTU"),
                OpcodeEntry(key(0, 26), "DIV"), OpcodeEntry(key(0, 27), "DIVU"),
                OpcodeEntry(key(0, 32), "ADD"), OpcodeEntry(key(0, 33), "ADDU"),
                OpcodeEntry(key(0, 34), "SUB"), OpcodeEntry(key(0, 35), "SUBU"),
                OpcodeEntry(key(0, 36), "AND"), OpcodeEntry(key(0, 37), "OR"),
                OpcodeEntry(key(0, 38), "XOR"), OpcodeEntry(key(0, 39), "NOR"),
                OpcodeEntry(key(0, 42), "SLT"), OpcodeEntry(key(0, 43), "SLTU"),
            ),
        ),
        OpcodeFamily(
            "REGIMM", 1, R.string.opcode_fam_regimm,
            listOf(
                OpcodeEntry(key(1, 0), "BLTZ"), OpcodeEntry(key(1, 1), "BGEZ"),
                OpcodeEntry(key(1, 16), "BLTZAL"), OpcodeEntry(key(1, 17), "BGEZAL"),
            ),
        ),
        OpcodeFamily(
            "JUMP", 2, R.string.opcode_fam_jump,
            listOf(OpcodeEntry(key(2), "J"), OpcodeEntry(key(3), "JAL")),
        ),
        OpcodeFamily(
            "BRANCH", 3, R.string.opcode_fam_branch,
            listOf(
                OpcodeEntry(key(4), "BEQ"), OpcodeEntry(key(5), "BNE"),
                OpcodeEntry(key(6), "BLEZ"), OpcodeEntry(key(7), "BGTZ"),
            ),
        ),
        OpcodeFamily(
            "ALU_IMM", 4, R.string.opcode_fam_alu_imm,
            listOf(
                OpcodeEntry(key(8), "ADDI"), OpcodeEntry(key(9), "ADDIU"),
                OpcodeEntry(key(10), "SLTI"), OpcodeEntry(key(11), "SLTIU"),
                OpcodeEntry(key(12), "ANDI"), OpcodeEntry(key(13), "ORI"),
                OpcodeEntry(key(14), "XORI"), OpcodeEntry(key(15), "LUI"),
            ),
        ),
        OpcodeFamily(
            "LOADSTORE", 5, R.string.opcode_fam_loadstore,
            listOf(
                OpcodeEntry(key(32), "LB"), OpcodeEntry(key(33), "LH"),
                OpcodeEntry(key(34), "LWL"), OpcodeEntry(key(35), "LW"),
                OpcodeEntry(key(36), "LBU"), OpcodeEntry(key(37), "LHU"),
                OpcodeEntry(key(38), "LWR"), OpcodeEntry(key(40), "SB"),
                OpcodeEntry(key(41), "SH"), OpcodeEntry(key(42), "SWL"),
                OpcodeEntry(key(43), "SW"), OpcodeEntry(key(46), "SWR"),
                OpcodeEntry(key(50), "LWC2"), OpcodeEntry(key(58), "SWC2"),
            ),
        ),
        OpcodeFamily(
            "COP0", 6, R.string.opcode_fam_cop0,
            listOf(
                OpcodeEntry(key(16, 0), "MFC0"), OpcodeEntry(key(16, 2), "CFC0"),
                OpcodeEntry(key(16, 4), "MTC0"), OpcodeEntry(key(16, 6), "CTC0"),
                OpcodeEntry(key(16, 16), "RFE"),
            ),
        ),
        OpcodeFamily(
            "GTE", 7, R.string.opcode_fam_gte,
            listOf(
                OpcodeEntry(key(18, 1), "RTPS"), OpcodeEntry(key(18, 6), "NCLIP"),
                OpcodeEntry(key(18, 12), "OP"), OpcodeEntry(key(18, 16), "DPCS"),
                OpcodeEntry(key(18, 17), "INTPL"), OpcodeEntry(key(18, 18), "MVMVA"),
                OpcodeEntry(key(18, 19), "NCDS"), OpcodeEntry(key(18, 20), "CDP"),
                OpcodeEntry(key(18, 22), "NCDT"), OpcodeEntry(key(18, 25), "NCCS"),
                OpcodeEntry(key(18, 26), "CC"), OpcodeEntry(key(18, 27), "NCS"),
                OpcodeEntry(key(18, 32), "NCT"), OpcodeEntry(key(18, 34), "SQR"),
                OpcodeEntry(key(18, 35), "DCPL"), OpcodeEntry(key(18, 37), "DPCT"),
                OpcodeEntry(key(18, 41), "AVSZ3"), OpcodeEntry(key(18, 42), "AVSZ4"),
                OpcodeEntry(key(18, 48), "RTPT"), OpcodeEntry(key(18, 58), "GPF"),
                OpcodeEntry(key(18, 59), "GPL"), OpcodeEntry(key(18, 60), "NCCT"),
            ),
        ),
    )

    // ------------------------------------------------------------- VU0/VU1
    val VU: List<OpcodeFamily> = listOf(
        OpcodeFamily(
            "UPPER_MATH", 0, R.string.opcode_fam_vu_upper_math,
            mnemonics = listOf("ADD", "SUB", "MUL", "MADD", "MSUB", "MAX", "MINI", "OPMSUB", "ADDi", "MULq", "SUBi", "MADDq"),
        ),
        OpcodeFamily(
            "UPPER_ACC", 1, R.string.opcode_fam_vu_upper_acc,
            mnemonics = listOf("ADDA", "SUBA", "MULA", "MADDA", "MSUBA", "OPMULA", "ADDAq", "MADDAi", "MULAx", "SUBAw"),
        ),
        OpcodeFamily(
            "UPPER_CONVERT", 2, R.string.opcode_fam_vu_upper_convert,
            mnemonics = listOf("ITOF0", "ITOF4", "ITOF12", "ITOF15", "FTOI0", "FTOI4", "FTOI12", "FTOI15", "ABS"),
        ),
        OpcodeFamily(
            "UPPER_MISC", 3, R.string.opcode_fam_vu_upper_misc,
            mnemonics = listOf("CLIP", "NOP"),
        ),
        OpcodeFamily(
            "LOWER_ALU", 4, R.string.opcode_fam_vu_lower_alu,
            mnemonics = listOf("IADD", "ISUB", "IADDI", "IADDIU", "ISUBIU", "IAND", "IOR", "MOVE", "MR32", "MFP", "MTIR", "MFIR", "RINIT", "RGET", "RNEXT", "RXOR", "FCEQ", "FCSET", "FCAND", "FCOR", "FSEQ", "FSSET", "FSAND", "FSOR", "FMEQ", "FMAND", "FMOR", "FCGET"),
        ),
        OpcodeFamily(
            "LOWER_BRANCH", 5, R.string.opcode_fam_vu_lower_branch,
            mnemonics = listOf("B", "BAL", "JR", "JALR", "IBEQ", "IBNE", "IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"),
        ),
        OpcodeFamily(
            "LOWER_LOADSTORE", 6, R.string.opcode_fam_vu_lower_loadstore,
            mnemonics = listOf("LQ", "SQ", "LQI", "SQI", "LQD", "SQD", "ILW", "ISW", "ILWR", "ISWR"),
        ),
        OpcodeFamily(
            "LOWER_DIVSQRT", 7, R.string.opcode_fam_vu_lower_divsqrt,
            mnemonics = listOf("DIV", "SQRT", "RSQRT", "WAITQ"),
        ),
        OpcodeFamily(
            "LOWER_EFU", 8, R.string.opcode_fam_vu_lower_efu,
            mnemonics = listOf("ESADD", "ERSADD", "ELENG", "ERLENG", "ESUM", "ERCPR", "ESQRT", "ERSQRT", "ESIN", "EATAN", "EATANxy", "EATANxz", "EEXP", "WAITP"),
        ),
        OpcodeFamily(
            "LOWER_GIF", 9, R.string.opcode_fam_vu_lower_gif,
            mnemonics = listOf("XGKICK", "XTOP", "XITOP"),
        ),
    )

    fun maskOf(familyIds: Set<String>, families: List<OpcodeFamily>): Long {
        var mask = 0L
        for (family in families) {
            if (family.id in familyIds) mask = mask or (1L shl family.bit)
        }
        return mask
    }

    fun idsCsv(opcodeIds: Set<String>): String {
        if (opcodeIds.isEmpty()) return ""
        return opcodeIds.mapNotNull { it.trim().toLongOrNull() }.joinToString(",") { it.toString() }
    }
}
